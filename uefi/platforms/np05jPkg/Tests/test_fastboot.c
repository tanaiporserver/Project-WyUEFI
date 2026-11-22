/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Host unit tests for Project SunUEFI Fastboot 0.4 core ported to nubia NP05J.
**/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Drivers/Np05jUsbSerialDxe/Np05jFastbootCore.h"

#define ESP_SIZE   (128U * 1024U)
#define BOOT_SIZE  (64U * 1024U)
#define BLK_SIZE   4096U

typedef struct {
  UINT8  Data[4096];
  UINTN  Size;
} MOCK_PACKET;

typedef struct {
  MOCK_PACKET  Pkts[128];
  UINTN        Count;
  UINT8        Esp[ESP_SIZE];
  UINT8        BootA[BOOT_SIZE];
  UINT32       PrepareCount;
  UINT32       WriteCount;
  UINT32       ReadCount;
  UINT32       FlushCount;
  BOOLEAN      CorruptNextReadback;
  CHAR8        LastNav[32];
  BOOLEAN      StorageEnabled;
} MOCK_ENV;

static void
MockClearPkts (
  MOCK_ENV *Env
  )
{
  Env->Count = 0U;
}

static EFI_STATUS
MockSend (
  VOID       *Context,
  CONST VOID *Data,
  UINTN       Bytes
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;

  assert (Env->Count < ARRAY_SIZE (Env->Pkts));
  assert (Bytes <= sizeof (Env->Pkts[0].Data));
  if (Bytes > 0U) {
    memcpy (Env->Pkts[Env->Count].Data, Data, Bytes);
  }
  Env->Pkts[Env->Count].Size = Bytes;
  Env->Count++;
  return EFI_SUCCESS;
}

static void
ExpectPktStr (
  CONST MOCK_ENV *Env,
  UINTN           Idx,
  CONST CHAR8    *Expected
  )
{
  UINTN Len = strlen (Expected);

  assert (Idx < Env->Count);
  assert (Env->Pkts[Idx].Size == Len);
  assert (memcmp (Env->Pkts[Idx].Data, Expected, Len) == 0);
}

static EFI_STATUS
MockStorageReady (
  VOID *Context
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;
  return Env->StorageEnabled ? EFI_SUCCESS : EFI_NOT_READY;
}

static EFI_STATUS
MockStorageInfo (
  VOID                    *Context,
  CONST CHAR8             *ExactName,
  NP05J_FB_PARTITION_INFO *Info
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;

  if (!Env->StorageEnabled) {
    return EFI_NOT_READY;
  }

  if (strcmp (ExactName, "sunuefi_esp") == 0) {
    memset (Info, 0, sizeof (*Info));
    strcpy (Info->Name, "sunuefi_esp");
    Info->Bytes     = ESP_SIZE;
    Info->BlockSize = BLK_SIZE;
    Info->ReadOnly  = FALSE;
    Info->Token     = (VOID *)(UINTN)1U;
    return EFI_SUCCESS;
  }

  if (strcmp (ExactName, "boot_a") == 0) {
    memset (Info, 0, sizeof (*Info));
    strcpy (Info->Name, "boot_a");
    Info->Bytes     = BOOT_SIZE;
    Info->BlockSize = BLK_SIZE;
    Info->ReadOnly  = TRUE;
    Info->Token     = (VOID *)(UINTN)2U;
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

static EFI_STATUS
MockReadBlocks (
  VOID                          *Context,
  CONST NP05J_FB_PARTITION_INFO *Info,
  UINT64                         Lba,
  UINTN                          Bytes,
  VOID                          *Buffer
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;
  UINT8    *Src;
  UINT64    MaxBytes;

  if (Info->Token == (VOID *)(UINTN)1U) {
    Src      = Env->Esp;
    MaxBytes = ESP_SIZE;
  } else if (Info->Token == (VOID *)(UINTN)2U) {
    Src      = Env->BootA;
    MaxBytes = BOOT_SIZE;
  } else {
    return EFI_INVALID_PARAMETER;
  }

  if (((Bytes % BLK_SIZE) != 0U) || ((Lba * BLK_SIZE + Bytes) > MaxBytes)) {
    return EFI_INVALID_PARAMETER;
  }

  memcpy (Buffer, Src + (Lba * BLK_SIZE), Bytes);
  Env->ReadCount++;

  if (Env->CorruptNextReadback && ((Env->ReadCount % 2U) == 1U || Env->WriteCount > 0U)) {
    ((UINT8 *)Buffer)[0] ^= 0x5AU;
    Env->CorruptNextReadback = FALSE;
  }

  return EFI_SUCCESS;
}

static EFI_STATUS
MockPrepareWrite (
  VOID                          *Context,
  CONST NP05J_FB_PARTITION_INFO *Info
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;

  if (Info->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  Env->PrepareCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS
MockWriteBlocks (
  VOID                          *Context,
  CONST NP05J_FB_PARTITION_INFO *Info,
  UINT64                         Lba,
  UINTN                          Bytes,
  CONST VOID                    *Buffer
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;

  if ((Info->Token != (VOID *)(UINTN)1U) || Info->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  if (((Bytes % BLK_SIZE) != 0U) || ((Lba * BLK_SIZE + Bytes) > ESP_SIZE)) {
    return EFI_INVALID_PARAMETER;
  }

  memcpy (Env->Esp + (Lba * BLK_SIZE), Buffer, Bytes);
  Env->WriteCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS
MockFlush (
  VOID                          *Context,
  CONST NP05J_FB_PARTITION_INFO *Info
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;
  (VOID)Info;
  Env->FlushCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS
MockQuery (
  VOID        *Context,
  CONST CHAR8 *Name,
  CHAR8        Value[60]
  )
{
  (VOID)Context;
  if (strcmp (Name, "usb-speed") == 0) {
    snprintf (Value, 60U, "SS");
    return EFI_SUCCESS;
  }
  return EFI_UNSUPPORTED;
}

static EFI_STATUS
MockDiagnostic (
  VOID           *Context,
  NP05J_FASTBOOT *State,
  CONST CHAR8    *Command
  )
{
  MOCK_ENV *Env = (MOCK_ENV *)Context;

  if (strcmp (Command, "oem status") == 0) {
    MockSend (Env, "INFOstatus-ok", 13U);
    return MockSend (Env, "OKAY", 4U);
  }
  if (strcmp (Command, "oem ramlog") == 0) {
    CONST CHAR8 *Log = "ramlog-line\n";
    assert (Np05jFastbootStageCopy (State, Log, strlen (Log)) == EFI_SUCCESS);
    return MockSend (Env, "OKAY", 4U);
  }
  if (strcmp (Command, "oem screenshot") == 0) {
    CONST CHAR8 *Bmp = "BMmock";
    assert (Np05jFastbootStageCopy (State, Bmp, strlen (Bmp)) == EFI_SUCCESS);
    return MockSend (Env, "OKAY", 4U);
  }
  if ((strcmp (Command, "oem setup") == 0) ||
      (strcmp (Command, "oem shell") == 0) ||
      (strcmp (Command, "oem simpleinit") == 0) ||
      (strcmp (Command, "oem boot-stable") == 0))
  {
    snprintf (Env->LastNav, sizeof (Env->LastNav), "%s", Command);
    return MockSend (Env, "OKAY", 4U);
  }

  return EFI_UNSUPPORTED;
}

static void
PutLe16 (
  UINT8 *P,
  UINT16 V
  )
{
  P[0] = (UINT8)V;
  P[1] = (UINT8)(V >> 8);
}

static void
PutLe32 (
  UINT8 *P,
  UINT32 V
  )
{
  P[0] = (UINT8)V;
  P[1] = (UINT8)(V >> 8);
  P[2] = (UINT8)(V >> 16);
  P[3] = (UINT8)(V >> 24);
}

static EFI_STATUS
SendCmd (
  NP05J_FASTBOOT *Fb,
  CONST CHAR8    *Cmd
  )
{
  return Np05jFastbootPacket (Fb, Cmd, strlen (Cmd));
}

static void
RunFlashToCompletion (
  NP05J_FASTBOOT *Fb
  )
{
  UINTN Guard = 0U;
  while (Np05jFastbootFlashPending (Fb)) {
    assert (++Guard < 256U);
    assert (Np05jFastbootFlashStep (Fb) == EFI_SUCCESS);
  }
}

int
main (
  void
  )
{
  MOCK_ENV          Env;
  NP05J_FASTBOOT    Fb;
  NP05J_FB_STORAGE  Storage;

  memset (&Env, 0, sizeof (Env));
  Env.StorageEnabled = TRUE;

  for (UINTN I = 0U; I < ESP_SIZE; I++) {
    Env.Esp[I] = (UINT8)((I * 7U + 3U) & 0xFFU);
  }
  for (UINTN I = 0U; I < BOOT_SIZE; I++) {
    Env.BootA[I] = (UINT8)((I * 13U + 5U) & 0xFFU);
  }

  Storage = (NP05J_FB_STORAGE){
    .Context       = &Env,
    .Ready         = MockStorageReady,
    .Info          = MockStorageInfo,
    .ReadBlocks    = MockReadBlocks,
    .PrepareWrite  = MockPrepareWrite,
    .WriteBlocks   = MockWriteBlocks,
    .Flush         = MockFlush,
    .MaxFetchBytes = 2048U
  };

  /* Adjust MaxFetchBytes to block-aligned (storage requirement: MaxFetchBytes % 4096 == 0) */
  assert (Np05jFastbootInit (&Fb, &Env, MockSend, NULL) == EFI_SUCCESS);
  Fb.Query      = MockQuery;
  Fb.Diagnostic = MockDiagnostic;
  assert (Np05jFastbootSetStorage (&Fb, &Storage) == EFI_INVALID_PARAMETER);
  Storage.MaxFetchBytes = 4096U;
  assert (Np05jFastbootSetStorage (&Fb, &Storage) == EFI_SUCCESS);

  /* 1. getvar tests */
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:version") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY0.4");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:product") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAYnp05j-sunuefi");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:serialno") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAYSunUEFI-np05j");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:version-bootloader") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY4.1-SunUEFI-v5");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:is-userspace") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAYno");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:storage-policy") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAYsunuefi-partition-writes");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:max-download-size") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY0x04000000");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:max-fetch-size") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY0x00001000");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:partition-size:sunuefi_esp") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY0x0000000000020000");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:partition-size:boot_a") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY0x0000000000010000");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:partition-size:missing") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILunknown partition");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:download-size") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY00000000");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:SunUEFI:ram-boot") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAYdisabled");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:usb-speed") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAYSS");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:unknown") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILunknown variable");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:all") == EFI_SUCCESS);
  assert (Env.Count == 7U);
  ExpectPktStr (&Env, 6U, "OKAY");

  /* 2. download + multi-chunk + ZLP + sha256("abc") + upload + discard */
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "download:123") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILdownload length must be eight hex digits");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "download:04000001") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILdownload exceeds RAM limit");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "download:00000003") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "DATA00000003");
  assert (Np05jFastbootPacket (&Fb, "ab", 2U) == EFI_SUCCESS);
  assert (Np05jFastbootPacket (&Fb, "", 0U) == EFI_SUCCESS);
  assert (Np05jFastbootPacket (&Fb, "c", 1U) == EFI_SUCCESS);
  ExpectPktStr (&Env, 1U, "OKAY");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:download-size") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY00000003");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem sha256") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "INFOba7816bf8f01cfea414140de5dae2223");
  ExpectPktStr (&Env, 1U, "INFOb00361a396177a9cb410ff61f20015ad");
  ExpectPktStr (&Env, 2U, "OKAY");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "upload") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "DATA00000003");
  ExpectPktStr (&Env, 1U, "abc");
  ExpectPktStr (&Env, 2U, "OKAY");

  /* Download overflow rejection */
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "download:00000004") == EFI_SUCCESS);
  assert (Np05jFastbootPacket (&Fb, "12345", 5U) == EFI_SUCCESS);
  ExpectPktStr (&Env, 1U, "FAILdownload overflow");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem discard") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY");
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "upload") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILno staged RAM payload");

  /* 3. oem help, diagnostics, navigation & rejected commands */
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem help") == EFI_SUCCESS);
  assert (Env.Count > 10U);
  ExpectPktStr (&Env, Env.Count - 1U, "OKAY");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem status") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "INFOstatus-ok");
  ExpectPktStr (&Env, 1U, "OKAY");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem ramlog") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY");
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "upload") == EFI_SUCCESS);
  ExpectPktStr (&Env, 1U, "ramlog-line\n");
  ExpectPktStr (&Env, 2U, "OKAY");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem screenshot") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY");
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "upload") == EFI_SUCCESS);
  ExpectPktStr (&Env, 1U, "BMmock");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem setup") == EFI_SUCCESS);
  assert (strcmp (Env.LastNav, "oem setup") == 0);
  assert (SendCmd (&Fb, "oem shell") == EFI_SUCCESS);
  assert (strcmp (Env.LastNav, "oem shell") == 0);
  assert (SendCmd (&Fb, "oem simpleinit") == EFI_SUCCESS);
  assert (strcmp (Env.LastNav, "oem simpleinit") == 0);
  assert (SendCmd (&Fb, "oem boot-stable") == EFI_SUCCESS);
  assert (strcmp (Env.LastNav, "oem boot-stable") == 0);

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem log") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILlog service unavailable");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "flashing unlock") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILcommand disabled by RAM-only policy");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "set_active a") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILcommand disabled by RAM-only policy");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "boot") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILRAM boot handoff not implemented in debug v1");

  /* 4. fetch aligned + unaligned + bounds */
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "fetch:boot_a:0x0:0x1000") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "DATA00001000");
  assert (Env.Pkts[1].Size == 4096U);
  assert (memcmp (Env.Pkts[1].Data, Env.BootA, 4096U) == 0);
  ExpectPktStr (&Env, 2U, "OKAY");

  /* Cross-block unaligned fetch: offset 4090, len 200 */
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "fetch:boot_a:0xffa:0xc8") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "DATA000000c8");
  assert (Env.Pkts[1].Size == 200U);
  assert (memcmp (Env.Pkts[1].Data, Env.BootA + 0xFFA, 200U) == 0);
  ExpectPktStr (&Env, 2U, "OKAY");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "fetch:boot_a:0x10000:0x10") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILfetch outside partition");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "fetch:boot_a:0x0:0x2000") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILinvalid fetch range");

  /* 5. Raw flash with unaligned tail RMW + read-only rejection */
  {
    UINT8  Payload[4873];
    UINT8  OriginalTail[BLK_SIZE - 777U];
    for (UINTN I = 0U; I < sizeof (Payload); I++) {
      Payload[I] = (UINT8)(0xA5U ^ (I & 0xFFU));
    }
    memcpy (OriginalTail, Env.Esp + BLK_SIZE + 777U, sizeof (OriginalTail));

    assert (SendCmd (&Fb, "download:00001309") == EFI_SUCCESS);
    assert (Np05jFastbootPacket (&Fb, Payload, sizeof (Payload)) == EFI_SUCCESS);

    MockClearPkts (&Env);
    assert (SendCmd (&Fb, "flash:boot_a") == EFI_SUCCESS);
    ExpectPktStr (&Env, 0U, "FAILpartition is read-only");

    MockClearPkts (&Env);
    assert (SendCmd (&Fb, "flash:sunuefi_esp") == EFI_SUCCESS);
    ExpectPktStr (&Env, 0U, "INFOpreparing partition write; keep USB connected");
    RunFlashToCompletion (&Fb);
    ExpectPktStr (&Env, 1U, "OKAY");

    assert (memcmp (Env.Esp, Payload, sizeof (Payload)) == 0);
    assert (memcmp (Env.Esp + BLK_SIZE + 777U, OriginalTail, sizeof (OriginalTail)) == 0);
  }

  /* 6. Android sparse flash (RAW + FILL + DONT_CARE hole + CRC32) and invalid sparse */
  {
    /*
     * 4 sparse blocks * 4096 = 16384 bytes:
     *   Block 0: 0xCAC1 RAW (4096 bytes of 0x51)
     *   Block 1: 0xCAC2 FILL (4096 bytes of 0xCAFEBABE)
     *   Block 2: 0xCAC3 DONT_CARE (preserve pre-existing marker)
     *   Block 3: 0xCAC4 CRC32 (0 blocks expanded)
     */
    UINT8  Sparse[28U + (12U + 4096U) + (12U + 4U) + 12U + (12U + 4U)];
    UINTN  At = 0U;
    UINT8  HoleBackup[BLK_SIZE];
    UINT32 PrevPrepares;

    for (UINTN I = 0U; I < BLK_SIZE; I++) {
      Env.Esp[2U * BLK_SIZE + I] = (UINT8)(0x3CU + (I & 0x0FU));
    }
    memcpy (HoleBackup, Env.Esp + 2U * BLK_SIZE, BLK_SIZE);

    memset (Sparse, 0, sizeof (Sparse));
    PutLe32 (Sparse + 0, 0xED26FF3AU);
    PutLe16 (Sparse + 4, 1U);
    PutLe16 (Sparse + 6, 0U);
    PutLe16 (Sparse + 8, 28U);
    PutLe16 (Sparse + 10, 12U);
    PutLe32 (Sparse + 12, BLK_SIZE);
    PutLe32 (Sparse + 16, 3U);
    PutLe32 (Sparse + 20, 4U);
    At = 28U;

    /* Chunk 0: RAW 1 block */
    PutLe16 (Sparse + At, 0xCAC1U);
    PutLe32 (Sparse + At + 4, 1U);
    PutLe32 (Sparse + At + 8, 12U + BLK_SIZE);
    memset (Sparse + At + 12, 0x51, BLK_SIZE);
    At += 12U + BLK_SIZE;

    /* Chunk 1: FILL 1 block */
    PutLe16 (Sparse + At, 0xCAC2U);
    PutLe32 (Sparse + At + 4, 1U);
    PutLe32 (Sparse + At + 8, 16U);
    PutLe32 (Sparse + At + 12, 0xCAFEBABEU);
    At += 16U;

    /* Chunk 2: DONT_CARE 1 block */
    PutLe16 (Sparse + At, 0xCAC3U);
    PutLe32 (Sparse + At + 4, 1U);
    PutLe32 (Sparse + At + 8, 12U);
    At += 12U;

    /* Chunk 3: CRC32 0 blocks */
    PutLe16 (Sparse + At, 0xCAC4U);
    PutLe32 (Sparse + At + 4, 0U);
    PutLe32 (Sparse + At + 8, 16U);
    PutLe32 (Sparse + At + 12, 0x12345678U);
    At += 16U;
    assert (At == sizeof (Sparse));

    /* First corrupt chunk type -> must fail BEFORE PrepareWrite */
    Sparse[28U + 12U + BLK_SIZE] = 0xFFU;
    PrevPrepares = Env.PrepareCount;
    assert (SendCmd (&Fb, "download:00001054") == EFI_SUCCESS);
    assert (Np05jFastbootPacket (&Fb, Sparse, sizeof (Sparse)) == EFI_SUCCESS);
    MockClearPkts (&Env);
    assert (SendCmd (&Fb, "flash:sunuefi_esp") == EFI_SUCCESS);
    RunFlashToCompletion (&Fb);
    assert (Env.PrepareCount == PrevPrepares);
    ExpectPktStr (&Env, 1U, "FAILinvalid or unsupported sparse image");

    /* Fix chunk type and flash valid sparse */
    Sparse[28U + 12U + BLK_SIZE] = 0xC2U;
    assert (SendCmd (&Fb, "download:00001054") == EFI_SUCCESS);
    assert (Np05jFastbootPacket (&Fb, Sparse, sizeof (Sparse)) == EFI_SUCCESS);
    MockClearPkts (&Env);
    assert (SendCmd (&Fb, "flash:sunuefi_esp") == EFI_SUCCESS);
    RunFlashToCompletion (&Fb);
    ExpectPktStr (&Env, 1U, "OKAY");

    for (UINTN I = 0U; I < BLK_SIZE; I++) {
      assert (Env.Esp[I] == 0x51U);
    }
    for (UINTN I = 0U; I < BLK_SIZE; I += 4U) {
      assert (Env.Esp[BLK_SIZE + I] == 0xBEU);
      assert (Env.Esp[BLK_SIZE + I + 1U] == 0xBAU);
      assert (Env.Esp[BLK_SIZE + I + 2U] == 0xFEU);
      assert (Env.Esp[BLK_SIZE + I + 3U] == 0xCAU);
    }
    assert (memcmp (Env.Esp + 2U * BLK_SIZE, HoleBackup, BLK_SIZE) == 0);
  }

  /* 7. erase sunuefi_esp & reject boot_a */
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "erase:boot_a") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILpartition is read-only");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "erase:sunuefi_esp") == EFI_SUCCESS);
  RunFlashToCompletion (&Fb);
  ExpectPktStr (&Env, 1U, "OKAY");
  for (UINTN I = 0U; I < ESP_SIZE; I++) {
    assert (Env.Esp[I] == 0U);
  }

  /* 8. Readback mismatch fault injection */
  {
    UINT8   Data[BLK_SIZE];
    UINT32  PrevFlush;
    memset (Data, 0x77, sizeof (Data));
    assert (SendCmd (&Fb, "download:00001000") == EFI_SUCCESS);
    assert (Np05jFastbootPacket (&Fb, Data, sizeof (Data)) == EFI_SUCCESS);

    PrevFlush               = Env.FlushCount;
    Env.CorruptNextReadback = TRUE;
    MockClearPkts (&Env);
    assert (SendCmd (&Fb, "flash:sunuefi_esp") == EFI_SUCCESS);
    RunFlashToCompletion (&Fb);
    assert (Env.FlushCount == PrevFlush + 1U);
    ExpectPktStr (&Env, 1U, "FAILpartition write, sync or readback failed");
  }

  /* 9. DiagnosticsOnly lock */
  assert (Np05jFastbootRestrictToDiagnostics (&Fb, TRUE) == EFI_SUCCESS);
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "getvar:max-download-size") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY0x00000000");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "download:00000010") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILonly diagnostics available after boot failure");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "oem status") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "INFOstatus-ok");
  ExpectPktStr (&Env, 1U, "OKAY");

  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "continue") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY");
  assert (Fb.ExitRequested == TRUE);

  assert (Np05jFastbootRestrictToDiagnostics (&Fb, FALSE) == EFI_SUCCESS);
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "reboot") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "FAILresource cleanup incomplete; read logs and hold power");

  /* 10. Normal reboot/continue flags */
  assert (Np05jFastbootInit (&Fb, &Env, MockSend, NULL) == EFI_SUCCESS);
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "reboot") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY");
  assert (Fb.RebootRequested == TRUE);

  assert (Np05jFastbootInit (&Fb, &Env, MockSend, NULL) == EFI_SUCCESS);
  MockClearPkts (&Env);
  assert (SendCmd (&Fb, "continue") == EFI_SUCCESS);
  ExpectPktStr (&Env, 0U, "OKAY");
  assert (Fb.ExitRequested == TRUE);

  printf ("test_fastboot: ALL 10 TEST GROUPS PASSED\n");
  return 0;
}
