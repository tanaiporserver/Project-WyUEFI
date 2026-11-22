/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Portable SunUEFI Fastboot 0.4 command layer, Android sparse validator/flasher,
  and FIPS 180-4 SHA-256 implementation for nubia NP05J.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/core/usb/PianoFastboot.c and PianoFastbootFlash.inc.
**/

#include "Np05jFastbootCore.h"

#define FLASH_PROGRESS_BYTES  0x1000000U
#define FLASH_SCAN_RECORDS    128U

#define SPARSE_HEADER_MAGIC   0xED26FF3AU
#define SPARSE_CHUNK_RAW      0xCAC1U
#define SPARSE_CHUNK_FILL     0xCAC2U
#define SPARSE_CHUNK_DONT_CARE 0xCAC3U
#define SPARSE_CHUNK_CRC32    0xCAC4U

#define ROR32(X, N) (((X) >> (N)) | ((X) << (32U - (N))))

struct _NP05J_FB_FLASH {
  NP05J_FB_PARTITION_INFO  Info;
  UINT8                    Expected[NP05J_FASTBOOT_MAX_BLOCK_IO_BYTES];
  UINT8                    Check[NP05J_FASTBOOT_MAX_BLOCK_IO_BYTES];
  UINT64                   Offset;
  UINT64                   Remaining;
  UINT64                   Written;
  UINT64                   Reported;
  UINT64                   SparseBytes;
  UINT64                   StartedUs;
  NP05J_FB_FLASH_TIMING    Timing;
  UINTN                    Source;
  UINTN                    At;
  UINT32                   Chunks;
  UINT32                   Block;
  UINT16                   Header;
  BOOLEAN                  Sparse;
  BOOLEAN                  Filling;
  BOOLEAN                  Prepared;
  enum {
    FlashValidate = 0,
    FlashPrepare,
    FlashData,
    FlashFlush
  }                        Phase;
  EFI_STATUS               Status;
};

typedef struct {
  UINT32  State[8];
  UINT64  BitCount;
  UINT8   Buf[64];
  UINTN   BufLen;
} NP05J_FB_SHA256_CTX;

STATIC CONST UINT32 mSha256K[64] = {
  0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
  0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
  0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
  0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
  0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
  0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
  0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
  0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

STATIC
VOID
Sha256Transform (
  IN OUT NP05J_FB_SHA256_CTX  *Ctx,
  IN     CONST UINT8          Block[64]
  )
{
  UINT32  W[64];
  UINT32  A;
  UINT32  B;
  UINT32  C;
  UINT32  D;
  UINT32  E;
  UINT32  F;
  UINT32  G;
  UINT32  H;

  for (UINTN I = 0U; I < 16U; I++) {
    W[I] = ((UINT32)Block[I * 4U] << 24) |
           ((UINT32)Block[I * 4U + 1U] << 16) |
           ((UINT32)Block[I * 4U + 2U] << 8) |
           ((UINT32)Block[I * 4U + 3U]);
  }

  for (UINTN I = 16U; I < 64U; I++) {
    UINT32  S0 = ROR32 (W[I - 15U], 7U) ^ ROR32 (W[I - 15U], 18U) ^ (W[I - 15U] >> 3U);
    UINT32  S1 = ROR32 (W[I - 2U], 17U) ^ ROR32 (W[I - 2U], 19U) ^ (W[I - 2U] >> 10U);
    W[I] = W[I - 16U] + S0 + W[I - 7U] + S1;
  }

  A = Ctx->State[0];
  B = Ctx->State[1];
  C = Ctx->State[2];
  D = Ctx->State[3];
  E = Ctx->State[4];
  F = Ctx->State[5];
  G = Ctx->State[6];
  H = Ctx->State[7];

  for (UINTN I = 0U; I < 64U; I++) {
    UINT32  S1    = ROR32 (E, 6U) ^ ROR32 (E, 11U) ^ ROR32 (E, 25U);
    UINT32  Ch    = (E & F) ^ ((~E) & G);
    UINT32  Temp1 = H + S1 + Ch + mSha256K[I] + W[I];
    UINT32  S0    = ROR32 (A, 2U) ^ ROR32 (A, 13U) ^ ROR32 (A, 22U);
    UINT32  Maj   = (A & B) ^ (A & C) ^ (B & C);
    UINT32  Temp2 = S0 + Maj;

    H = G;
    G = F;
    F = E;
    E = D + Temp1;
    D = C;
    C = B;
    B = A;
    A = Temp1 + Temp2;
  }

  Ctx->State[0] += A;
  Ctx->State[1] += B;
  Ctx->State[2] += C;
  Ctx->State[3] += D;
  Ctx->State[4] += E;
  Ctx->State[5] += F;
  Ctx->State[6] += G;
  Ctx->State[7] += H;
}

STATIC
VOID
Sha256Update (
  IN OUT NP05J_FB_SHA256_CTX  *Ctx,
  IN     CONST VOID           *Data,
  IN     UINTN                Len
  )
{
  CONST UINT8  *Ptr = (CONST UINT8 *)Data;

  while (Len > 0U) {
    UINTN  Take = 64U - Ctx->BufLen;
    if (Take > Len) {
      Take = Len;
    }

    CopyMem (Ctx->Buf + Ctx->BufLen, Ptr, Take);
    Ctx->BufLen   += Take;
    Ctx->BitCount += (UINT64)Take * 8ULL;
    Ptr           += Take;
    Len           -= Take;

    if (Ctx->BufLen == 64U) {
      Sha256Transform (Ctx, Ctx->Buf);
      Ctx->BufLen = 0U;
    }
  }
}

VOID
Np05jFastbootSha256 (
  IN  CONST VOID  *Data,
  IN  UINTN       Len,
  OUT UINT8       Digest[32]
  )
{
  NP05J_FB_SHA256_CTX  Ctx;
  UINT64               TotalBits;
  UINT8                Pad;
  UINT8                Zero;
  UINT8                BeLen[8];

  Ctx.State[0] = 0x6a09e667U;
  Ctx.State[1] = 0xbb67ae85U;
  Ctx.State[2] = 0x3c6ef372U;
  Ctx.State[3] = 0xa54ff53aU;
  Ctx.State[4] = 0x510e527fU;
  Ctx.State[5] = 0x9b05688cU;
  Ctx.State[6] = 0x1f83d9abU;
  Ctx.State[7] = 0x5be0cd19U;
  Ctx.BitCount = 0ULL;
  Ctx.BufLen   = 0U;

  if ((Data != NULL) && (Len > 0U)) {
    Sha256Update (&Ctx, Data, Len);
  }

  TotalBits = Ctx.BitCount;
  Pad       = 0x80U;
  Zero      = 0x00U;
  Sha256Update (&Ctx, &Pad, 1U);
  while (Ctx.BufLen != 56U) {
    Sha256Update (&Ctx, &Zero, 1U);
  }

  for (UINTN I = 0U; I < 8U; I++) {
    BeLen[I] = (UINT8)(TotalBits >> (56U - I * 8U));
  }

  CopyMem (Ctx.Buf + 56U, BeLen, 8U);
  Sha256Transform (&Ctx, Ctx.Buf);

  for (UINTN I = 0U; I < 8U; I++) {
    Digest[I * 4U]      = (UINT8)(Ctx.State[I] >> 24);
    Digest[I * 4U + 1U] = (UINT8)(Ctx.State[I] >> 16);
    Digest[I * 4U + 2U] = (UINT8)(Ctx.State[I] >> 8);
    Digest[I * 4U + 3U] = (UINT8)(Ctx.State[I]);
  }
}

STATIC
BOOLEAN
StorageReady (
  IN CONST NP05J_FASTBOOT  *S
  )
{
  return (!S->DiagnosticsOnly &&
          (S->Storage.Ready != NULL) &&
          (S->Storage.Info != NULL) &&
          (S->Storage.ReadBlocks != NULL) &&
          (S->Storage.Ready (S->Storage.Context) == EFI_SUCCESS));
}

STATIC
BOOLEAN
StorageWriteConfigured (
  IN CONST NP05J_FASTBOOT  *S
  )
{
  return ((S->Storage.PrepareWrite != NULL) &&
          (S->Storage.WriteBlocks != NULL) &&
          (S->Storage.Flush != NULL));
}

BOOLEAN
Np05jFastbootStorageWritable (
  IN NP05J_FASTBOOT  *State
  )
{
  return ((State != NULL) && StorageWriteConfigured (State) && StorageReady (State));
}

STATIC
EFI_STATUS
Reply (
  IN NP05J_FASTBOOT  *S,
  IN CONST CHAR8     *Text
  )
{
  return S->Send (S->Context, Text, AsciiStrLen (Text));
}

STATIC
BOOLEAN
Equal (
  IN CONST CHAR8  *A,
  IN CONST CHAR8  *B
  )
{
  return (AsciiStrCmp (A, B) == 0);
}

STATIC
VOID
Hex32 (
  IN  UINT32  Value,
  OUT CHAR8   *Out
  )
{
  STATIC CONST CHAR8  Digits[] = "0123456789abcdef";

  for (UINTN I = 0U; I < 8U; I++) {
    Out[I] = Digits[(Value >> ((7U - I) * 4U)) & 15U];
  }

  Out[8] = '\0';
}

STATIC
VOID
Hex64 (
  IN  UINT64  Value,
  OUT CHAR8   *Out
  )
{
  STATIC CONST CHAR8  Digits[] = "0123456789abcdef";

  for (UINTN I = 0U; I < 16U; I++) {
    Out[I] = Digits[(Value >> ((15U - I) * 4U)) & 15U];
  }

  Out[16] = '\0';
}

STATIC
BOOLEAN
PartitionName (
  IN CONST CHAR8  *Name
  )
{
  UINTN  N = 0U;

  for (; Name[N] != '\0'; N++) {
    CHAR8  C = Name[N];
    if ((N >= NP05J_FASTBOOT_PARTITION_NAME) ||
        !(((C >= 'a') && (C <= 'z')) ||
          ((C >= 'A') && (C <= 'Z')) ||
          ((C >= '0') && (C <= '9')) ||
          (C == '_') || (C == '-') || (C == '.')))
    {
      return FALSE;
    }
  }

  return (N != 0U);
}

STATIC
EFI_STATUS
PartitionInfo (
  IN  NP05J_FASTBOOT           *S,
  IN  CONST CHAR8              *Name,
  OUT NP05J_FB_PARTITION_INFO  *Info
  )
{
  EFI_STATUS  Status;

  if (!PartitionName (Name)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Info, sizeof (*Info));
  Status = S->Storage.Info (S->Storage.Context, Name, Info);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if ((Info->Name[NP05J_FASTBOOT_PARTITION_NAME] != '\0') ||
      (AsciiStrCmp (Name, Info->Name) != 0) ||
      (Info->Token == NULL) ||
      (Info->Bytes == 0ULL) ||
      (Info->BlockSize < 512U) ||
      (Info->BlockSize > 4096U) ||
      ((Info->BlockSize & (Info->BlockSize - 1U)) != 0U) ||
      ((Info->Bytes % Info->BlockSize) != 0ULL))
  {
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

STATIC
UINT16
FlashLe16 (
  IN CONST UINT8  *P
  )
{
  return (UINT16)(P[0] | ((UINT16)P[1] << 8));
}

STATIC
UINT32
FlashLe32 (
  IN CONST UINT8  *P
  )
{
  return (UINT32)FlashLe16 (P) | ((UINT32)FlashLe16 (P + 2) << 16);
}

STATIC
BOOLEAN
SparseImage (
  IN CONST UINT8  *Data,
  IN UINTN        Bytes
  )
{
  return ((Bytes >= 4U) && (FlashLe32 (Data) == SPARSE_HEADER_MAGIC));
}

STATIC
BOOLEAN
SparseHeaderValid (
  IN CONST UINT8                    *Data,
  IN UINTN                          Bytes,
  IN CONST NP05J_FB_PARTITION_INFO  *Info
  )
{
  UINTN   Header;
  UINT32  Block;
  UINT64  Total;

  if ((Bytes < 28U) || (FlashLe16 (Data + 4) != 1U)) {
    return FALSE;
  }

  Header = FlashLe16 (Data + 8);
  Block  = FlashLe32 (Data + 12);
  Total  = (UINT64)FlashLe32 (Data + 16) * Block;

  return ((Header >= 28U) &&
          (Header <= Bytes) &&
          (FlashLe16 (Data + 10) >= 12U) &&
          (Block != 0U) &&
          ((Block % Info->BlockSize) == 0U) &&
          (Total != 0ULL) &&
          (Total <= Info->Bytes));
}

STATIC
BOOLEAN
ValidateSparseStep (
  IN     CONST NP05J_FASTBOOT  *S,
  IN OUT NP05J_FB_FLASH        *F
  )
{
  CONST UINT8  *Data = S->Download;
  UINTN        Bytes = S->Received;

  for (UINTN I = 0U; (I < FLASH_SCAN_RECORDS) && (F->Chunks != 0U); I++) {
    CONST UINT8  *Chunk;
    UINT32       Length;
    UINT64       Expanded;
    UINTN        Payload;

    if ((F->At > Bytes) || (F->Header > (Bytes - F->At))) {
      return FALSE;
    }

    Chunk    = Data + F->At;
    Length   = FlashLe32 (Chunk + 8);
    Expanded = (UINT64)FlashLe32 (Chunk + 4) * F->Block;
    if ((Length < F->Header) || (Length > (Bytes - F->At)) || (Expanded > (F->SparseBytes - F->Offset))) {
      return FALSE;
    }

    Payload = Length - F->Header;
    switch (FlashLe16 (Chunk)) {
      case SPARSE_CHUNK_RAW:
        if ((Expanded == 0ULL) || (Expanded != Payload)) {
          return FALSE;
        }
        break;

      case SPARSE_CHUNK_FILL:
        if ((Expanded == 0ULL) || (Payload != 4U)) {
          return FALSE;
        }
        break;

      case SPARSE_CHUNK_DONT_CARE:
        if ((Expanded == 0ULL) || (Payload != 0U)) {
          return FALSE;
        }
        break;

      case SPARSE_CHUNK_CRC32:
        if ((Expanded != 0ULL) || (Payload != 4U)) {
          return FALSE;
        }
        break;

      default:
        return FALSE;
    }

    F->Offset += Expanded;
    F->At     += Length;
    F->Chunks--;
  }

  if (F->Chunks != 0U) {
    return TRUE;
  }

  if ((F->At != Bytes) || (F->Offset != F->SparseBytes)) {
    return FALSE;
  }

  F->At     = FlashLe16 (Data + 8);
  F->Offset = 0ULL;
  F->Chunks = FlashLe32 (Data + 20);
  F->Phase  = FlashPrepare;
  return TRUE;
}

BOOLEAN
Np05jFastbootFlashPending (
  IN CONST NP05J_FASTBOOT  *State
  )
{
  return ((State != NULL) && (State->Flash != NULL));
}

STATIC
VOID
FlashFree (
  IN OUT NP05J_FASTBOOT  *S
  )
{
  if (S->Flash != NULL) {
    ZeroMem (S->Flash, sizeof (*S->Flash));
    FreePool (S->Flash);
    S->Flash = NULL;
  }
}

STATIC
VOID
CancelFlash (
  IN OUT NP05J_FASTBOOT  *S
  )
{
  if (S->Flash == NULL) {
    return;
  }

  S->FlashExecuting = TRUE;
  if (S->Flash->Prepared && (S->Storage.Flush != NULL)) {
    S->Storage.Flush (S->Storage.Context, &S->Flash->Info);
  }

  FlashFree (S);
  S->FlashExecuting       = FALSE;
  S->FlashCancelRequested = FALSE;
}

STATIC
UINT64
FlashNow (
  IN CONST NP05J_FASTBOOT  *S
  )
{
  return (S->NowUs != NULL) ? S->NowUs (S->Context) : 0ULL;
}

STATIC
UINT64
FlashElapsed (
  IN CONST NP05J_FASTBOOT  *S,
  IN UINT64                Start
  )
{
  UINT64  End = FlashNow (S);

  return (End >= Start) ? (End - Start) : 0ULL;
}

STATIC
EFI_STATUS
FlashFinish (
  IN OUT NP05J_FASTBOOT  *S,
  IN     CONST CHAR8     *Message
  )
{
  NP05J_FB_FLASH  *F = S->Flash;

  if ((S->NowUs != NULL) && (S->ReportFlash != NULL)) {
    F->Timing.Bytes   = F->Written;
    F->Timing.TotalUs = FlashElapsed (S, F->StartedUs);
    S->ReportFlash (F->Info.Name, &F->Timing, Message);
  }

  FlashFree (S);
  S->FlashExecuting = FALSE;
  return Reply (S, Message);
}

STATIC
EFI_STATUS
FlashProgress (
  IN OUT NP05J_FASTBOOT  *S,
  IN     UINT64          Written
  )
{
  CHAR8   Text[64];
  CHAR8   Digits[20];
  UINTN   Count = 0U;
  UINTN   At;
  UINT64  MiB = Written >> 20;

  CopyMem (Text, "INFOverified ", sizeof ("INFOverified ") - 1U);
  At = sizeof ("INFOverified ") - 1U;

  do {
    Digits[Count++] = (CHAR8)('0' + (MiB % 10ULL));
    MiB /= 10ULL;
  } while (MiB != 0ULL);

  while (Count > 0U) {
    Text[At++] = Digits[--Count];
  }

  CopyMem (Text + At, " MiB written", sizeof (" MiB written"));
  return Reply (S, Text);
}

STATIC
EFI_STATUS
FlashWriteStep (
  IN OUT NP05J_FASTBOOT  *S,
  IN OUT NP05J_FB_FLASH  *F
  )
{
  UINTN       Bytes = (UINTN)MIN ((UINT64)sizeof (F->Expected), F->Remaining);
  UINTN       Block = F->Info.BlockSize;
  UINTN       Whole = Bytes - (Bytes % Block);
  UINT64      Lba   = F->Offset / Block;
  EFI_STATUS  Status;
  UINT64      Start;

  if (Whole != 0U) {
    Bytes = Whole;
  } else {
    Whole = Block;
    Start = FlashNow (S);
    Status = S->Storage.ReadBlocks (S->Storage.Context, &F->Info, Lba, Whole, F->Expected);
    F->Timing.ReadUs += FlashElapsed (S, Start);
    if ((Status != EFI_SUCCESS) || S->FlashCancelRequested) {
      return Status;
    }
  }

  if (!F->Filling) {
    CopyMem (F->Expected, S->Download + F->Source, Bytes);
  }

  Start = FlashNow (S);
  Status = S->Storage.WriteBlocks (S->Storage.Context, &F->Info, Lba, Whole, F->Expected);
  F->Timing.WriteUs += FlashElapsed (S, Start);
  if ((Status != EFI_SUCCESS) || S->FlashCancelRequested) {
    return Status;
  }

  Start = FlashNow (S);
  Status = S->Storage.ReadBlocks (S->Storage.Context, &F->Info, Lba, Whole, F->Check);
  F->Timing.ReadUs += FlashElapsed (S, Start);
  if ((Status != EFI_SUCCESS) || S->FlashCancelRequested) {
    return Status;
  }

  if (CompareMem (F->Expected, F->Check, Whole) != 0) {
    return EFI_CRC_ERROR;
  }

  F->Remaining -= Bytes;
  F->Offset    += Bytes;
  F->Written   += Bytes;
  if (!F->Filling) {
    F->Source += Bytes;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
NextSparseRange (
  IN     CONST NP05J_FASTBOOT  *S,
  IN OUT NP05J_FB_FLASH        *F
  )
{
  for (UINTN I = 0U; (I < FLASH_SCAN_RECORDS) && (F->Chunks != 0U); I++) {
    CONST UINT8  *Chunk   = S->Download + F->At;
    UINT16       Type     = FlashLe16 (Chunk);
    UINT64       Expanded = (UINT64)FlashLe32 (Chunk + 4) * F->Block;

    F->At += FlashLe32 (Chunk + 8);
    F->Chunks--;

    if ((Type == SPARSE_CHUNK_RAW) || (Type == SPARSE_CHUNK_FILL)) {
      F->Source    = (UINTN)(Chunk - S->Download) + F->Header;
      F->Remaining = Expanded;
      F->Filling   = (Type == SPARSE_CHUNK_FILL);
      if (F->Filling) {
        UINTN  Bytes = (UINTN)MIN (Expanded, (UINT64)sizeof (F->Expected));
        CopyMem (F->Expected, Chunk + F->Header, 4U);
        for (UINTN N = 4U; N < Bytes; N *= 2U) {
          CopyMem (F->Expected + N, F->Expected, MIN (N, Bytes - N));
        }
      }

      break;
    }

    F->Offset += Expanded;
  }
}

EFI_STATUS
Np05jFastbootFlashStep (
  IN OUT NP05J_FASTBOOT  *State
  )
{
  NP05J_FB_FLASH  *F;

  if ((State == NULL) || (State->Flash == NULL)) {
    return EFI_NOT_READY;
  }

  if (State->FlashExecuting) {
    return EFI_ALREADY_STARTED;
  }

  State->FlashExecuting = TRUE;
  F                     = State->Flash;

  if (F->Phase == FlashValidate) {
    if (!ValidateSparseStep (State, F)) {
      return FlashFinish (State, "FAILinvalid or unsupported sparse image");
    }
  } else if (F->Phase == FlashPrepare) {
    UINT64  Start = FlashNow (State);
    F->Status           = State->Storage.PrepareWrite (State->Storage.Context, &F->Info);
    F->Timing.PrepareUs += FlashElapsed (State, Start);
    F->Prepared         = (F->Status == EFI_SUCCESS);
    if (!F->Prepared && !State->FlashCancelRequested) {
      return FlashFinish (State, "FAILcannot prepare partition; restart and retry");
    }

    F->Phase = FlashData;
  } else if (F->Phase == FlashFlush) {
    UINT64      Start = FlashNow (State);
    EFI_STATUS  Flush = State->Storage.Flush (State->Storage.Context, &F->Info);
    F->Timing.FlushUs += FlashElapsed (State, Start);
    F->Prepared        = FALSE;
    if (F->Status == EFI_SUCCESS) {
      F->Status = Flush;
    }

    if (!State->FlashCancelRequested) {
      return FlashFinish (
               State,
               (F->Status == EFI_SUCCESS) ? "OKAY" : "FAILpartition write, sync or readback failed"
               );
    }
  } else if (F->Remaining != 0ULL) {
    F->Status = FlashWriteStep (State, F);
    if (F->Status != EFI_SUCCESS) {
      F->Phase = FlashFlush;
    } else if (!State->FlashCancelRequested && ((F->Written - F->Reported) >= FLASH_PROGRESS_BYTES)) {
      EFI_STATUS  ReplyStatus;
      F->Reported = F->Written;
      ReplyStatus = FlashProgress (State, F->Written);
      if (ReplyStatus != EFI_SUCCESS) {
        State->FlashExecuting = FALSE;
        CancelFlash (State);
        return ReplyStatus;
      }
    }
  } else if (F->Sparse && (F->Chunks != 0U)) {
    NextSparseRange (State, F);
  } else {
    F->Phase = FlashFlush;
  }

  State->FlashExecuting = FALSE;
  if (State->FlashCancelRequested) {
    Np05jFastbootReset (State);
    return EFI_ABORTED;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FlashStart (
  IN OUT NP05J_FASTBOOT  *S,
  IN     CONST CHAR8     *Name,
  IN     BOOLEAN         Erase
  )
{
  NP05J_FB_PARTITION_INFO  Info;
  NP05J_FB_FLASH           *F;
  EFI_STATUS               Status;
  BOOLEAN                  Sparse;

  if (!Np05jFastbootStorageWritable (S)) {
    return Reply (S, "FAILstorage write backend not ready");
  }

  Status = PartitionInfo (S, Name, &Info);
  if (Status != EFI_SUCCESS) {
    return Reply (S, (Status == EFI_NOT_FOUND) ? "FAILunknown partition" : "FAILinvalid partition information");
  }

  if (Info.ReadOnly) {
    return Reply (S, "FAILpartition is read-only");
  }

  if (!Erase && (!S->Complete || (S->Download == NULL) || (S->Received == 0U))) {
    return Reply (S, "FAILno complete RAM payload");
  }

  Sparse = !Erase && SparseImage (S->Download, S->Received);
  if (Sparse) {
    if (!SparseHeaderValid (S->Download, S->Received, &Info)) {
      return Reply (S, "FAILinvalid or unsupported sparse image");
    }
  } else if (!Erase && (S->Received > Info.Bytes)) {
    return Reply (S, "FAILimage exceeds partition size");
  }

  F = (NP05J_FB_FLASH *)AllocateZeroPool (sizeof (*F));
  if (F == NULL) {
    return Reply (S, "FAILnot enough RAM");
  }

  F->Info      = Info;
  F->Sparse    = Sparse;
  F->Phase     = FlashPrepare;
  F->StartedUs = FlashNow (S);

  if (Sparse) {
    F->At          = FlashLe16 (S->Download + 8);
    F->Header      = FlashLe16 (S->Download + 10);
    F->Block       = FlashLe32 (S->Download + 12);
    F->Chunks      = FlashLe32 (S->Download + 20);
    F->SparseBytes = (UINT64)FlashLe32 (S->Download + 16) * F->Block;
    F->Phase       = FlashValidate;
  } else {
    F->Remaining = Erase ? Info.Bytes : S->Received;
    F->Filling   = Erase;
  }

  S->Flash                = F;
  S->FlashCancelRequested = FALSE;
  Status                  = Reply (S, "INFOpreparing partition write; keep USB connected");
  if (Status != EFI_SUCCESS) {
    CancelFlash (S);
  }

  return Status;
}

STATIC
EFI_STATUS
Help (
  IN OUT NP05J_FASTBOOT  *S
  )
{
  STATIC CONST CHAR8  *Diagnostics[] = {
    "INFOUEFI Fastboot commands:",
    "INFOgetvar NAME - query device; NAME=all lists basics",
    "INFOoem help - show commands available now",
    "INFOoem status - show saved startup and device status",
    "INFOoem ramlog - stage firmware log",
    "INFOoem screenshot - stage screen as BMP",
    "INFOget_staged FILE - save staged log or screenshot",
    "INFOreboot / continue - save settings and restart"
  };
  STATIC CONST CHAR8  *Normal[] = {
    "INFOstage FILE - download a RAM payload (max 64 MiB)",
    "INFOoem sha256 - hash the downloaded payload",
    "INFOoem discard - discard staged RAM data"
  };
  STATIC CONST CHAR8  *Navigation[] = {
    "INFOoem setup / shell / simpleinit - open firmware UI",
    "INFOoem boot-stable - start the configured OS boot order"
  };
  EFI_STATUS  Status;

  for (UINTN I = 0U; I < ARRAY_SIZE (Diagnostics); I++) {
    Status = Reply (S, Diagnostics[I]);
    if (Status != EFI_SUCCESS) {
      return Status;
    }
  }

  if (S->DiagnosticsRebootBlocked) {
    Status = Reply (S, "INFOrestart unavailable; read logs, then hold power");
    if (Status != EFI_SUCCESS) {
      return Status;
    }
  }

  if (S->DiagnosticsOnly) {
    return Reply (S, "OKAY");
  }

  for (UINTN I = 0U; I < ARRAY_SIZE (Normal); I++) {
    Status = Reply (S, Normal[I]);
    if (Status != EFI_SUCCESS) {
      return Status;
    }
  }

  if (StorageReady (S)) {
    Status = Reply (S, "INFOfetch PARTITION FILE - read partition to file");
    if (Status != EFI_SUCCESS) {
      return Status;
    }

    if (StorageWriteConfigured (S)) {
      Status = Reply (S, "INFOflash PARTITION FILE - write raw or sparse image");
      if (Status != EFI_SUCCESS) {
        return Status;
      }

      Status = Reply (S, "INFOerase PARTITION - zero the named partition");
      if (Status != EFI_SUCCESS) {
        return Status;
      }
    }
  }

  if (S->Diagnostic != NULL) {
    for (UINTN I = 0U; I < ARRAY_SIZE (Navigation); I++) {
      Status = Reply (S, Navigation[I]);
      if (Status != EFI_SUCCESS) {
        return Status;
      }
    }
  }

  return Reply (S, "OKAY");
}

STATIC
UINT32
FetchLimit (
  IN CONST NP05J_FASTBOOT  *S
  )
{
  return (S->Storage.MaxFetchBytes != 0U) ? S->Storage.MaxFetchBytes : NP05J_FASTBOOT_DEFAULT_FETCH_BYTES;
}

STATIC
EFI_STATUS
GetVar (
  IN OUT NP05J_FASTBOOT  *S,
  IN     CONST CHAR8     *Name
  )
{
  if (Equal (Name, "max-fetch-size")) {
    CHAR8  Result[15];
    if (!StorageReady (S)) {
      return Reply (S, "FAILstorage backend not ready");
    }

    CopyMem (Result, "OKAY0x", 6U);
    Hex32 (FetchLimit (S), Result + 6);
    return Reply (S, Result);
  }

  if (AsciiStrnCmp (Name, "partition-size:", 15U) == 0) {
    NP05J_FB_PARTITION_INFO  Info;
    EFI_STATUS               Status;
    CHAR8                    Result[23];

    if (!StorageReady (S)) {
      return Reply (S, "FAILstorage backend not ready");
    }

    Status = PartitionInfo (S, Name + 15, &Info);
    if (Status != EFI_SUCCESS) {
      return Reply (S, (Status == EFI_NOT_FOUND) ? "FAILunknown partition" : "FAILinvalid partition information");
    }

    CopyMem (Result, "OKAY0x", 6U);
    Hex64 (Info.Bytes, Result + 6);
    return Reply (S, Result);
  }

  if (Equal (Name, "version")) {
    return Reply (S, "OKAY0.4");
  }

  if (Equal (Name, "product")) {
    return Reply (S, "OKAY" NP05J_FASTBOOT_PRODUCT);
  }

  if (Equal (Name, "serialno")) {
    return Reply (S, "OKAY" NP05J_FASTBOOT_SERIAL);
  }

  if (Equal (Name, "version-bootloader")) {
    return Reply (S, "OKAY" NP05J_FASTBOOT_BOOTLOADER_VER);
  }

  if (Equal (Name, "is-userspace")) {
    return Reply (S, "OKAYno");
  }

  if (Equal (Name, "storage-policy")) {
    return Reply (S, Np05jFastbootStorageWritable (S) ? "OKAYsunuefi-partition-writes" : "OKAYread-only");
  }

  if (Equal (Name, "max-download-size")) {
    return Reply (S, S->DiagnosticsOnly ? "OKAY0x00000000" : "OKAY0x04000000");
  }

  if (Equal (Name, "SunUEFI:ram-boot")) {
    return Reply (S, "OKAYdisabled");
  }

  if (Equal (Name, "download-size")) {
    CHAR8  Result[13];
    CopyMem (Result, "OKAY", 4U);
    Hex32 (S->Complete ? (UINT32)S->Received : 0U, Result + 4);
    return Reply (S, Result);
  }

  if (Equal (Name, "all")) {
    CONST CHAR8  *Rows[6];
    Rows[0] = "INFOproduct:" NP05J_FASTBOOT_PRODUCT;
    Rows[1] = "INFOversion:0.4";
    Rows[2] = "INFOversion-bootloader:" NP05J_FASTBOOT_BOOTLOADER_VER;
    Rows[3] = "INFOserialno:" NP05J_FASTBOOT_SERIAL;
    Rows[4] = Np05jFastbootStorageWritable (S)
              ? "INFOstorage-policy:sunuefi-partition-writes"
              : "INFOstorage-policy:read-only";
    Rows[5] = S->DiagnosticsOnly
              ? "INFOmax-download-size:0x00000000"
              : "INFOmax-download-size:0x04000000";

    for (UINTN I = 0U; I < ARRAY_SIZE (Rows); I++) {
      EFI_STATUS  Status = Reply (S, Rows[I]);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }

    return Reply (S, "OKAY");
  }

  if (S->Query != NULL) {
    CHAR8       Result[65];
    EFI_STATUS  Status;

    ZeroMem (Result, sizeof (Result));
    CopyMem (Result, "OKAY", 4U);
    Status = S->Query (S->Context, Name, Result + 4);
    if (!EFI_ERROR (Status)) {
      Result[64] = '\0';
      return Reply (S, Result);
    }

    if (Status != EFI_UNSUPPORTED) {
      return Status;
    }
  }

  return Reply (S, "FAILunknown variable");
}

STATIC
EFI_STATUS
Download (
  IN OUT NP05J_FASTBOOT  *S,
  IN     CONST CHAR8     *Arg
  )
{
  UINT32      Size = 0U;
  CHAR8       Result[13];
  EFI_STATUS  Status;

  if (AsciiStrLen (Arg) != 8U) {
    return Reply (S, "FAILdownload length must be eight hex digits");
  }

  for (UINTN I = 0U; I < 8U; I++) {
    CHAR8   C = Arg[I];
    UINT32  N;

    if ((C >= '0') && (C <= '9')) {
      N = (UINT32)(C - '0');
    } else if ((C >= 'a') && (C <= 'f')) {
      N = (UINT32)(C - 'a' + 10);
    } else if ((C >= 'A') && (C <= 'F')) {
      N = (UINT32)(C - 'A' + 10);
    } else {
      return Reply (S, "FAILinvalid download length");
    }

    Size = (Size << 4) | N;
  }

  if ((Size == 0U) || (Size > NP05J_FASTBOOT_MAX_DOWNLOAD)) {
    return Reply (S, "FAILdownload exceeds RAM limit");
  }

  Np05jFastbootReset (S);
  S->Download = (UINT8 *)AllocateZeroPool (Size);
  if (S->Download == NULL) {
    return Reply (S, "FAILnot enough RAM");
  }

  S->Expected = Size;
  CopyMem (Result, "DATA", 4U);
  Hex32 (Size, Result + 4);
  Status = Reply (S, Result);
  if (EFI_ERROR (Status)) {
    Np05jFastbootReset (S);
    return Status;
  }

  S->Receiving = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
HashDownload (
  IN OUT NP05J_FASTBOOT  *S
  )
{
  STATIC CONST CHAR8  Digits[] = "0123456789abcdef";
  UINT8               Hash[32];
  CHAR8               Line[61];

  if (!S->Complete || (S->Download == NULL)) {
    return Reply (S, "FAILno complete RAM payload");
  }

  Np05jFastbootSha256 (S->Download, S->Received, Hash);
  ZeroMem (Line, sizeof (Line));
  CopyMem (Line, "INFO", 4U);

  for (UINTN Part = 0U; Part < 2U; Part++) {
    EFI_STATUS  Status;
    for (UINTN I = 0U; I < 16U; I++) {
      Line[4U + I * 2U] = Digits[Hash[Part * 16U + I] >> 4];
      Line[5U + I * 2U] = Digits[Hash[Part * 16U + I] & 15U];
    }

    Line[36] = '\0';
    Status   = Reply (S, Line);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return Reply (S, "OKAY");
}

STATIC
BOOLEAN
ParseFetchHex (
  IN  CONST CHAR8  *Text,
  OUT UINT64       *Value
  )
{
  UINTN   N = 0U;
  UINT64  V = 0ULL;

  if ((Text[0] != '0') || (Text[1] != 'x')) {
    return FALSE;
  }

  for (Text += 2; *Text != '\0'; Text++) {
    UINT8  Digit;
    CHAR8  C = *Text;

    if ((C >= '0') && (C <= '9')) {
      Digit = (UINT8)(C - '0');
    } else if ((C >= 'a') && (C <= 'f')) {
      Digit = (UINT8)(C - 'a' + 10);
    } else if ((C >= 'A') && (C <= 'F')) {
      Digit = (UINT8)(C - 'A' + 10);
    } else {
      return FALSE;
    }

    if (++N > 16U) {
      return FALSE;
    }

    V = (V << 4) | Digit;
  }

  if (N == 0U) {
    return FALSE;
  }

  *Value = V;
  return TRUE;
}

STATIC
EFI_STATUS
Fetch (
  IN OUT NP05J_FASTBOOT  *S,
  IN OUT CHAR8           *Arg
  )
{
  CHAR8                    *OffsetArg = NULL;
  CHAR8                    *SizeArg   = NULL;
  UINT64                   Offset;
  UINT64                   Size;
  NP05J_FB_PARTITION_INFO  Info;
  EFI_STATUS               Status;
  UINT8                    *Data;
  UINT8                    *Scratch   = NULL;
  UINTN                    Done       = 0U;
  UINTN                    Block;
  UINT64                   Lba;
  UINTN                    Within;
  UINTN                    Full;

  if (!StorageReady (S)) {
    return Reply (S, "FAILstorage backend not ready");
  }

  for (CHAR8 *P = Arg; *P != '\0'; P++) {
    if (*P == ':') {
      if (SizeArg != NULL) {
        return Reply (S, "FAILinvalid fetch arguments");
      }

      *P = '\0';
      if (OffsetArg == NULL) {
        OffsetArg = P + 1;
      } else {
        SizeArg = P + 1;
      }
    }
  }

  if ((OffsetArg == NULL) ||
      (SizeArg == NULL) ||
      !PartitionName (Arg) ||
      !ParseFetchHex (OffsetArg, &Offset) ||
      !ParseFetchHex (SizeArg, &Size) ||
      (Size == 0ULL) ||
      (Size > FetchLimit (S)))
  {
    return Reply (S, "FAILinvalid fetch range");
  }

  Status = PartitionInfo (S, Arg, &Info);
  if (Status != EFI_SUCCESS) {
    return Reply (S, (Status == EFI_NOT_FOUND) ? "FAILunknown partition" : "FAILinvalid partition information");
  }

  if ((Offset > Info.Bytes) || (Size > (Info.Bytes - Offset))) {
    return Reply (S, "FAILfetch outside partition");
  }

  Data = (UINT8 *)AllocateZeroPool ((UINTN)Size);
  if (Data == NULL) {
    return Reply (S, "FAILnot enough RAM");
  }

  Block  = Info.BlockSize;
  Lba    = Offset / Block;
  Within = (UINTN)(Offset % Block);

  if ((Within != 0U) || ((Size % Block) != 0ULL)) {
    Scratch = (UINT8 *)AllocateZeroPool (Block);
    if (Scratch == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Finished;
    }
  }

  if (Within != 0U) {
    UINTN  Bytes = MIN ((UINTN)Size, Block - Within);
    Status = S->Storage.ReadBlocks (S->Storage.Context, &Info, Lba, Block, Scratch);
    if (Status != EFI_SUCCESS) {
      goto Finished;
    }

    CopyMem (Data, Scratch + Within, Bytes);
    Done += Bytes;
    Lba++;
  }

  Full = ((UINTN)Size - Done) / Block * Block;
  if (Full != 0U) {
    Status = S->Storage.ReadBlocks (S->Storage.Context, &Info, Lba, Full, Data + Done);
    if (Status != EFI_SUCCESS) {
      goto Finished;
    }

    Done += Full;
    Lba  += Full / Block;
  }

  if (Done < (UINTN)Size) {
    if (Scratch == NULL) {
      Scratch = (UINT8 *)AllocateZeroPool (Block);
      if (Scratch == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto Finished;
      }
    }

    Status = S->Storage.ReadBlocks (S->Storage.Context, &Info, Lba, Block, Scratch);
    if (Status != EFI_SUCCESS) {
      goto Finished;
    }

    CopyMem (Data + Done, Scratch, (UINTN)Size - Done);
  }

  {
    CHAR8  Result[13];
    CopyMem (Result, "DATA", 4U);
    Hex32 ((UINT32)Size, Result + 4);
    Status = Reply (S, Result);
    if (Status != EFI_SUCCESS) {
      goto Released;
    }

    Status = S->Send (S->Context, Data, (UINTN)Size);
    if (Status != EFI_SUCCESS) {
      goto Released;
    }

    Status = Reply (S, "OKAY");
    goto Released;
  }

Finished:
  Status = Reply (S, (Status == EFI_OUT_OF_RESOURCES) ? "FAILnot enough RAM" : "FAILpartition read failed");

Released:
  ZeroMem (Data, (UINTN)Size);
  FreePool (Data);
  if (Scratch != NULL) {
    ZeroMem (Scratch, Block);
    FreePool (Scratch);
  }

  return Status;
}

EFI_STATUS
Np05jFastbootInit (
  OUT NP05J_FASTBOOT  *State,
  IN  VOID            *Context,
  IN  NP05J_FB_SEND   Send,
  IN  NP05J_FB_LOG    Log OPTIONAL
  )
{
  if ((State == NULL) || (Send == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (State, sizeof (*State));
  State->Context = Context;
  State->Send    = Send;
  State->Log     = Log;
  return EFI_SUCCESS;
}

VOID
Np05jFastbootReset (
  IN OUT NP05J_FASTBOOT  *State
  )
{
  if (State == NULL) {
    return;
  }

  if (State->FlashExecuting) {
    State->FlashCancelRequested = TRUE;
    return;
  }

  CancelFlash (State);

  if ((State->Upload != NULL) && !State->UploadBorrowed) {
    ZeroMem (State->Upload, State->UploadBytes);
    FreePool (State->Upload);
  }

  State->Upload         = NULL;
  State->UploadBytes    = 0U;
  State->UploadBorrowed = FALSE;

  if (State->Download != NULL) {
    ZeroMem (State->Download, State->Expected);
    FreePool (State->Download);
  }

  State->Download        = NULL;
  State->Expected        = 0U;
  State->Received        = 0U;
  State->Receiving       = FALSE;
  State->Complete        = FALSE;
  State->RebootRequested = FALSE;
  State->ExitRequested   = FALSE;
}

EFI_STATUS
Np05jFastbootRestrictToDiagnostics (
  IN OUT NP05J_FASTBOOT  *State,
  IN     BOOLEAN         AllowReboot
  )
{
  BOOLEAN  Interrupted;

  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (State->FlashExecuting) {
    return EFI_NOT_READY;
  }

  if (!AllowReboot) {
    State->DiagnosticsRebootBlocked = TRUE;
  }

  if (State->DiagnosticsOnly) {
    return EFI_SUCCESS;
  }

  Interrupted = Np05jFastbootFlashPending (State);
  Np05jFastbootReset (State);
  State->DiagnosticsOnly = TRUE;
  return Interrupted ? Reply (State, "FAILpartition write interrupted") : EFI_SUCCESS;
}

EFI_STATUS
Np05jFastbootSetStorage (
  IN OUT NP05J_FASTBOOT          *State,
  IN     CONST NP05J_FB_STORAGE  *Storage OPTIONAL
  )
{
  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((State->Flash != NULL) || State->FlashExecuting) {
    return EFI_NOT_READY;
  }

  if (Storage == NULL) {
    ZeroMem (&State->Storage, sizeof (State->Storage));
    return EFI_SUCCESS;
  }

  if ((Storage->Ready == NULL) ||
      (Storage->Info == NULL) ||
      (Storage->ReadBlocks == NULL) ||
      (Storage->MaxFetchBytes > NP05J_FASTBOOT_MAX_FETCH_BYTES) ||
      ((Storage->MaxFetchBytes % 4096U) != 0U))
  {
    return EFI_INVALID_PARAMETER;
  }

  State->Storage = *Storage;
  return EFI_SUCCESS;
}

EFI_STATUS
Np05jFastbootStageCopy (
  IN OUT NP05J_FASTBOOT  *State,
  IN     CONST VOID      *Data,
  IN     UINTN           Bytes
  )
{
  UINT8  *Copy;

  if ((State == NULL) ||
      (Data == NULL) ||
      (Bytes == 0U) ||
      (Bytes > NP05J_FASTBOOT_MAX_DOWNLOAD) ||
      State->Receiving)
  {
    return EFI_INVALID_PARAMETER;
  }

  if (State->Flash != NULL) {
    return EFI_NOT_READY;
  }

  Copy = (UINT8 *)AllocateZeroPool (Bytes);
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Copy, Data, Bytes);
  if ((State->Upload != NULL) && !State->UploadBorrowed) {
    ZeroMem (State->Upload, State->UploadBytes);
    FreePool (State->Upload);
  }

  State->Upload         = Copy;
  State->UploadBytes    = Bytes;
  State->UploadBorrowed = FALSE;
  return EFI_SUCCESS;
}

EFI_STATUS
Np05jFastbootPacket (
  IN OUT NP05J_FASTBOOT  *State,
  IN     CONST VOID      *Data,
  IN     UINTN           Bytes
  )
{
  CONST UINT8  *Raw;
  CHAR8        Cmd[65];

  if ((State == NULL) || (State->Send == NULL) || ((Data == NULL) && (Bytes != 0U))) {
    return EFI_INVALID_PARAMETER;
  }

  if ((State->Flash != NULL) || State->FlashExecuting) {
    return EFI_NOT_READY;
  }

  if (State->Receiving) {
    if (Bytes == 0U) {
      return EFI_SUCCESS;
    }

    if (Bytes > (State->Expected - State->Received)) {
      Np05jFastbootReset (State);
      return Reply (State, "FAILdownload overflow");
    }

    CopyMem (State->Download + State->Received, Data, Bytes);
    State->Received += Bytes;
    if (State->Received == State->Expected) {
      State->Receiving      = FALSE;
      State->Complete       = TRUE;
      State->Upload         = State->Download;
      State->UploadBytes    = State->Received;
      State->UploadBorrowed = TRUE;
      return Reply (State, "OKAY");
    }

    return EFI_SUCCESS;
  }

  if (Bytes == 0U) {
    return EFI_SUCCESS;
  }

  if (Bytes > 64U) {
    return Reply (State, "FAILcommand too long");
  }

  Raw = (CONST UINT8 *)Data;
  for (UINTN I = 0U; I < Bytes; I++) {
    if ((Raw[I] < 0x20U) || (Raw[I] > 0x7EU)) {
      return Reply (State, "FAILinvalid command bytes");
    }
  }

  CopyMem (Cmd, Data, Bytes);
  Cmd[Bytes] = '\0';

  State->CommandsHandled++;
  CopyMem (State->LastCommand, Cmd, Bytes + 1U);

  if (State->DiagnosticsOnly &&
      (AsciiStrnCmp (Cmd, "getvar:", 7U) != 0) &&
      !Equal (Cmd, "upload") &&
      !Equal (Cmd, "oem help") &&
      !Equal (Cmd, "oem status") &&
      !Equal (Cmd, "oem ramlog") &&
      !Equal (Cmd, "oem screenshot") &&
      !Equal (Cmd, "reboot") &&
      !Equal (Cmd, "continue"))
  {
    return Reply (State, "FAILonly diagnostics available after boot failure");
  }

  if (Equal (Cmd, "oem help")) {
    return Help (State);
  }

  if (AsciiStrnCmp (Cmd, "getvar:", 7U) == 0) {
    return GetVar (State, Cmd + 7);
  }

  if (AsciiStrnCmp (Cmd, "download:", 9U) == 0) {
    return Download (State, Cmd + 9);
  }

  if (AsciiStrnCmp (Cmd, "fetch:", 6U) == 0) {
    return Fetch (State, Cmd + 6);
  }

  if (AsciiStrnCmp (Cmd, "flash:", 6U) == 0) {
    return FlashStart (State, Cmd + 6, FALSE);
  }

  if (AsciiStrnCmp (Cmd, "erase:", 6U) == 0) {
    return FlashStart (State, Cmd + 6, TRUE);
  }

  if (Equal (Cmd, "upload")) {
    CHAR8       Result[13];
    EFI_STATUS  Status;

    if ((State->Upload == NULL) || (State->UploadBytes == 0U)) {
      return Reply (State, "FAILno staged RAM payload");
    }

    CopyMem (Result, "DATA", 4U);
    Hex32 ((UINT32)State->UploadBytes, Result + 4);
    Status = Reply (State, Result);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = State->Send (State->Context, State->Upload, State->UploadBytes);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    return Reply (State, "OKAY");
  }

  if (Equal (Cmd, "oem sha256")) {
    return HashDownload (State);
  }

  if (Equal (Cmd, "oem discard")) {
    Np05jFastbootReset (State);
    return Reply (State, "OKAY");
  }

  if (Equal (Cmd, "oem setup") ||
      Equal (Cmd, "oem shell") ||
      Equal (Cmd, "oem simpleinit") ||
      Equal (Cmd, "oem boot-stable"))
  {
    EFI_STATUS  Status;
    if (State->Diagnostic == NULL) {
      return Reply (State, "FAILUI navigation backend unavailable");
    }

    Status = State->Diagnostic (State->Context, State, Cmd);
    return (Status == EFI_UNSUPPORTED) ? Reply (State, "FAILUI navigation backend unavailable") : Status;
  }

  if (Equal (Cmd, "oem status") ||
      Equal (Cmd, "oem ramlog") ||
      Equal (Cmd, "oem screenshot"))
  {
    EFI_STATUS  Status;
    if (State->Diagnostic == NULL) {
      return Reply (State, "FAILdiagnostic service unavailable");
    }

    Status = State->Diagnostic (State->Context, State, Cmd);
    return (Status == EFI_UNSUPPORTED) ? Reply (State, "FAILdiagnostic command unavailable") : Status;
  }

  if (Equal (Cmd, "oem log")) {
    EFI_STATUS  Status;
    if (State->Log == NULL) {
      return Reply (State, "FAILlog service unavailable");
    }

    Status = State->Log (State->Context, State->Send);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    return Reply (State, "OKAY");
  }

  if (Equal (Cmd, "reboot")) {
    EFI_STATUS  Status;
    if (State->DiagnosticsRebootBlocked) {
      return Reply (State, "FAILresource cleanup incomplete; read logs and hold power");
    }

    Status = Reply (State, "OKAY");
    if (!EFI_ERROR (Status)) {
      State->RebootRequested = TRUE;
    }

    return Status;
  }

  if (Equal (Cmd, "continue")) {
    EFI_STATUS  Status;
    if (State->DiagnosticsRebootBlocked) {
      return Reply (State, "FAILresource cleanup incomplete; read logs and hold power");
    }

    Status = Reply (State, "OKAY");
    if (!EFI_ERROR (Status)) {
      State->ExitRequested = TRUE;
    }

    return Status;
  }

  if (Equal (Cmd, "boot")) {
    return Reply (State, "FAILRAM boot handoff not implemented in debug v1");
  }

  return Reply (State, "FAILcommand disabled by RAM-only policy");
}
