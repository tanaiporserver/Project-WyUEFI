/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  GPT partition discovery, read-only `fetch`, and allowlisted (`sunuefi_esp`,
  `sunuefi_root`) readback-verified `flash`/`erase` storage backend over
  UEFI BlockIo + PartitionInfo for nubia NP05J.

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/core/usb/PianoFastbootBlockRead.c.
**/

#include "Np05jFastbootCore.h"

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define MAX_PARTITIONS  160U

typedef struct {
  EFI_HANDLE             Handle;
  EFI_BLOCK_IO_PROTOCOL  *Block;
  EFI_BLOCK_READ         Read;
  EFI_BLOCK_WRITE        Write;
  EFI_PARTITION_ENTRY    Gpt;
  HARDDRIVE_DEVICE_PATH  Hd;
  CHAR8                  Name[NP05J_FASTBOOT_PARTITION_NAME + 1U];
  UINT64                 Bytes;
  UINT32                 MediaId;
  UINT32                 BlockSize;
  UINT32                 IoAlign;
  BOOLEAN                MediaReadOnly;
  BOOLEAN                Writable;
  VOID                   *Token;
} NP05J_FB_PARTITION;

STATIC NP05J_FB_PARTITION  mPartitions[MAX_PARTITIONS];
STATIC UINTN               mCount            = 0U;
STATIC BOOLEAN             mReady            = FALSE;
STATIC BOOLEAN             mDirty            = TRUE;
STATIC BOOLEAN             mWriteActive      = FALSE;
STATIC EFI_HANDLE          mUnmountedHandle  = NULL;
STATIC UINTN               mNextToken        = 1U;

STATIC
BOOLEAN
IsPowerOfTwoBlockSize (
  IN UINT32  BlockSize
  )
{
  return ((BlockSize >= 512U) &&
          (BlockSize <= 4096U) &&
          ((BlockSize & (BlockSize - 1U)) == 0U));
}

STATIC
BOOLEAN
ExtractTrailingHardDriveNode (
  IN  EFI_DEVICE_PATH_PROTOCOL  *Path,
  OUT HARDDRIVE_DEVICE_PATH     *OutHd
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  if (Path == NULL) {
    return FALSE;
  }

  for (Node = Path; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
        (DevicePathSubType (Node) == MEDIA_HARDDRIVE_DP) &&
        (DevicePathNodeLength (Node) == sizeof (HARDDRIVE_DEVICE_PATH)))
    {
      EFI_DEVICE_PATH_PROTOCOL  *Next = NextDevicePathNode (Node);
      HARDDRIVE_DEVICE_PATH     *Hd   = (HARDDRIVE_DEVICE_PATH *)Node;

      if (IsDevicePathEndType (Next) &&
          (DevicePathSubType (Next) == END_ENTIRE_DEVICE_PATH_SUBTYPE) &&
          (Hd->MBRType == MBR_TYPE_EFI_PARTITION_TABLE_HEADER) &&
          (Hd->SignatureType == SIGNATURE_TYPE_GUID) &&
          (Hd->PartitionNumber != 0U))
      {
        CopyMem (OutHd, Hd, sizeof (*OutHd));
        return TRUE;
      }
    }
  }

  return FALSE;
}

STATIC
BOOLEAN
GeometryMatches (
  IN CONST HARDDRIVE_DEVICE_PATH  *Hd,
  IN CONST EFI_PARTITION_ENTRY    *Gpt,
  IN UINT64                       LastBlock,
  IN UINT32                       BlockSize
  )
{
  STATIC CONST UINT8  ZeroGuid[16] = { 0 };
  UINT64              Blocks;

  if ((CompareMem (&Gpt->PartitionTypeGUID, ZeroGuid, sizeof (ZeroGuid)) == 0) ||
      (CompareMem (&Gpt->UniquePartitionGUID, ZeroGuid, sizeof (ZeroGuid)) == 0) ||
      (Gpt->StartingLBA == 0ULL) ||
      (Gpt->EndingLBA < Gpt->StartingLBA) ||
      (LastBlock >= (MAX_UINT64 / BlockSize)))
  {
    return FALSE;
  }

  Blocks = Gpt->EndingLBA - Gpt->StartingLBA + 1ULL;
  return ((Blocks == (LastBlock + 1ULL)) &&
          (Hd->PartitionStart == Gpt->StartingLBA) &&
          (Hd->PartitionSize == Blocks) &&
          (CompareMem (Hd->Signature, &Gpt->UniquePartitionGUID, 16U) == 0));
}

STATIC
BOOLEAN
ExtractPartitionName (
  IN  CONST CHAR16  *Wide,
  OUT CHAR8         Out[NP05J_FASTBOOT_PARTITION_NAME + 1U]
  )
{
  UINTN  N = 0U;

  ZeroMem (Out, NP05J_FASTBOOT_PARTITION_NAME + 1U);
  for (; (N < NP05J_FASTBOOT_PARTITION_NAME) && (Wide[N] != 0U); N++) {
    UINT16  C = Wide[N];
    if (!(((C >= 'a') && (C <= 'z')) ||
          ((C >= 'A') && (C <= 'Z')) ||
          ((C >= '0') && (C <= '9')) ||
          (C == '_') || (C == '-') || (C == '.')))
    {
      return FALSE;
    }

    Out[N] = (CHAR8)C;
  }

  Out[N] = '\0';
  for (UINTN I = N; I < NP05J_FASTBOOT_PARTITION_NAME; I++) {
    if (Wide[I] != 0U) {
      return FALSE;
    }
  }

  return (N > 0U);
}

STATIC
EFI_STATUS
Current (
  IN CONST NP05J_FB_PARTITION  *P
  )
{
  EFI_BLOCK_IO_PROTOCOL        *B  = NULL;
  EFI_PARTITION_INFO_PROTOCOL  *Pi = NULL;
  EFI_DEVICE_PATH_PROTOCOL     *D  = NULL;
  HARDDRIVE_DEVICE_PATH        Hd;

  if ((gBS->HandleProtocol (P->Handle, &gEfiBlockIoProtocolGuid, (VOID **)&B) != EFI_SUCCESS) ||
      (gBS->HandleProtocol (P->Handle, &gEfiPartitionInfoProtocolGuid, (VOID **)&Pi) != EFI_SUCCESS) ||
      (gBS->HandleProtocol (P->Handle, &gEfiDevicePathProtocolGuid, (VOID **)&D) != EFI_SUCCESS))
  {
    return EFI_NO_MEDIA;
  }

  if ((B != P->Block) ||
      (B == NULL) ||
      (B->Revision < EFI_BLOCK_IO_PROTOCOL_REVISION) ||
      (B->ReadBlocks != P->Read) ||
      (B->ReadBlocks == NULL) ||
      (B->WriteBlocks != P->Write) ||
      (B->Media == NULL) ||
      (B->Media->MediaPresent != TRUE) ||
      (B->Media->ReadOnly != P->MediaReadOnly) ||
      (B->Media->LogicalPartition != TRUE) ||
      (B->Media->MediaId != P->MediaId) ||
      (B->Media->IoAlign != P->IoAlign) ||
      (B->Media->BlockSize != P->BlockSize) ||
      (B->Media->LastBlock != ((P->Bytes / P->BlockSize) - 1ULL)) ||
      (Pi == NULL) ||
      (Pi->Revision < EFI_PARTITION_INFO_PROTOCOL_REVISION) ||
      (Pi->Type != PARTITION_TYPE_GPT) ||
      (CompareMem (&Pi->Info.Gpt, &P->Gpt, sizeof (P->Gpt)) != 0) ||
      !ExtractTrailingHardDriveNode (D, &Hd) ||
      (CompareMem (&Hd, &P->Hd, sizeof (Hd)) != 0) ||
      !GeometryMatches (&Hd, &Pi->Info.Gpt, B->Media->LastBlock, P->BlockSize))
  {
    return EFI_MEDIA_CHANGED;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
ScanPartitions (
  VOID
  )
{
  EFI_HANDLE  *Handles = NULL;
  UINTN       Count    = 0U;
  EFI_STATUS  Status;

  if (mWriteActive) {
    return;
  }

  mCount = 0U;
  ZeroMem (mPartitions, sizeof (mPartitions));
  mDirty = FALSE;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || (Handles == NULL)) {
    if (Handles != NULL) {
      FreePool (Handles);
    }

    return;
  }

  for (UINTN N = 0U; (N < Count) && (mCount < MAX_PARTITIONS) && (mNextToken < MAX_UINTN); N++) {
    EFI_DEVICE_PATH_PROTOCOL     *D  = NULL;
    EFI_PARTITION_INFO_PROTOCOL  *Pi = NULL;
    EFI_BLOCK_IO_PROTOCOL        *B  = NULL;
    HARDDRIVE_DEVICE_PATH        Hd;
    CHAR8                        Name[NP05J_FASTBOOT_PARTITION_NAME + 1U];
    NP05J_FB_PARTITION           *P;

    if ((gBS->HandleProtocol (Handles[N], &gEfiDevicePathProtocolGuid, (VOID **)&D) != EFI_SUCCESS) ||
        !ExtractTrailingHardDriveNode (D, &Hd))
    {
      continue;
    }

    if ((gBS->HandleProtocol (Handles[N], &gEfiPartitionInfoProtocolGuid, (VOID **)&Pi) != EFI_SUCCESS) ||
        (Pi == NULL) ||
        (Pi->Revision < EFI_PARTITION_INFO_PROTOCOL_REVISION) ||
        (Pi->Type != PARTITION_TYPE_GPT))
    {
      continue;
    }

    if ((gBS->HandleProtocol (Handles[N], &gEfiBlockIoProtocolGuid, (VOID **)&B) != EFI_SUCCESS) ||
        (B == NULL) ||
        (B->Revision < EFI_BLOCK_IO_PROTOCOL_REVISION) ||
        (B->ReadBlocks == NULL) ||
        (B->Media == NULL) ||
        (B->Media->MediaPresent != TRUE) ||
        (B->Media->LogicalPartition != TRUE) ||
        !IsPowerOfTwoBlockSize (B->Media->BlockSize) ||
        !GeometryMatches (&Hd, &Pi->Info.Gpt, B->Media->LastBlock, B->Media->BlockSize))
    {
      continue;
    }

    if (!ExtractPartitionName (Pi->Info.Gpt.PartitionName, Name)) {
      continue;
    }

    P                = &mPartitions[mCount++];
    P->Handle        = Handles[N];
    P->Block         = B;
    P->Read          = B->ReadBlocks;
    P->Write         = B->WriteBlocks;
    P->Gpt           = Pi->Info.Gpt;
    P->Hd            = Hd;
    P->MediaReadOnly = B->Media->ReadOnly;
    P->Writable      = (!B->Media->ReadOnly) &&
                       (B->WriteBlocks != NULL) &&
                       ((AsciiStrCmp (Name, "sunuefi_esp") == 0) ||
                        (AsciiStrCmp (Name, "sunuefi_root") == 0));
    P->BlockSize     = B->Media->BlockSize;
    P->IoAlign       = B->Media->IoAlign;
    P->Token         = (VOID *)(UINTN)mNextToken++;
    CopyMem (P->Name, Name, sizeof (P->Name));
    P->Bytes   = (B->Media->LastBlock + 1ULL) * P->BlockSize;
    P->MediaId = B->Media->MediaId;
  }

  FreePool (Handles);
}

STATIC
VOID
EnsureScanned (
  VOID
  )
{
  if (!mReady) {
    return;
  }

  if (mDirty || (mCount == 0U)) {
    ScanPartitions ();
  }
}

STATIC
EFI_STATUS
StorageReady (
  IN VOID  *Context
  )
{
  (VOID)Context;
  if (!mReady) {
    return EFI_NOT_READY;
  }

  EnsureScanned ();
  if (mCount == 0U) {
    return EFI_NOT_READY;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
StorageInfo (
  IN  VOID                     *Context,
  IN  CONST CHAR8              *ExactName,
  OUT NP05J_FB_PARTITION_INFO  *Out
  )
{
  NP05J_FB_PARTITION  *Found = NULL;
  EFI_STATUS          Status;

  (VOID)Context;
  if ((ExactName == NULL) || (Out == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Out, sizeof (*Out));
  if (!mReady) {
    return EFI_NOT_READY;
  }

  EnsureScanned ();
  for (UINTN N = 0U; N < mCount; N++) {
    if (AsciiStrCmp (ExactName, mPartitions[N].Name) == 0) {
      if (Found != NULL) {
        return EFI_NO_MAPPING;
      }

      Found = &mPartitions[N];
    }
  }

  if (Found == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = Current (Found);
  if (Status != EFI_SUCCESS) {
    mDirty = TRUE;
    EnsureScanned ();
    Found = NULL;
    for (UINTN N = 0U; N < mCount; N++) {
      if (AsciiStrCmp (ExactName, mPartitions[N].Name) == 0) {
        Found = &mPartitions[N];
        break;
      }
    }

    if (Found == NULL) {
      return EFI_NOT_FOUND;
    }

    Status = Current (Found);
    if (Status != EFI_SUCCESS) {
      return Status;
    }
  }

  CopyMem (Out->Name, Found->Name, sizeof (Out->Name));
  Out->Bytes     = Found->Bytes;
  Out->BlockSize = Found->BlockSize;
  Out->ReadOnly  = !Found->Writable;
  Out->Token     = Found->Token;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
LookupAndValidate (
  IN  CONST NP05J_FB_PARTITION_INFO  *Info,
  OUT NP05J_FB_PARTITION             **OutPart
  )
{
  NP05J_FB_PARTITION  *P = NULL;
  EFI_STATUS          Status;

  if (!mReady) {
    return EFI_NOT_READY;
  }

  if ((Info == NULL) || (OutPart == NULL) || (Info->Token == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  for (UINTN N = 0U; N < mCount; N++) {
    if (mPartitions[N].Token == Info->Token) {
      P = &mPartitions[N];
      break;
    }
  }

  if (P == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Info->Bytes != P->Bytes) ||
      (Info->BlockSize != P->BlockSize) ||
      (Info->ReadOnly == P->Writable) ||
      (AsciiStrnCmp (Info->Name, P->Name, sizeof (Info->Name)) != 0))
  {
    return EFI_INVALID_PARAMETER;
  }

  Status = Current (P);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  *OutPart = P;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
StorageReadBlocks (
  IN  VOID                           *Context,
  IN  CONST NP05J_FB_PARTITION_INFO  *Info,
  IN  UINT64                         Lba,
  IN  UINTN                          Bytes,
  OUT VOID                           *Buffer
  )
{
  NP05J_FB_PARTITION  *P = NULL;
  EFI_STATUS          Status;
  VOID                *Token;
  VOID                *Bounce = NULL;
  VOID                *Aligned;

  (VOID)Context;
  Status = LookupAndValidate (Info, &P);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if ((Buffer == NULL) ||
      (Bytes == 0U) ||
      ((Bytes % P->BlockSize) != 0U) ||
      (Bytes > NP05J_FASTBOOT_MAX_BLOCK_IO_BYTES) ||
      (Lba >= (P->Bytes / P->BlockSize)) ||
      ((Bytes / P->BlockSize) > ((P->Bytes / P->BlockSize) - Lba)))
  {
    return EFI_INVALID_PARAMETER;
  }

  Aligned = Buffer;
  if ((P->IoAlign > 1U) && (((UINTN)Buffer % P->IoAlign) != 0U)) {
    Bounce = AllocateZeroPool (Bytes + P->IoAlign);
    if (Bounce == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Aligned = (VOID *)(((UINTN)Bounce + P->IoAlign - 1U) & ~((UINTN)P->IoAlign - 1U));
  }

  Token  = P->Token;
  Status = P->Block->ReadBlocks (P->Block, P->MediaId, Lba, Bytes, Aligned);
  if ((Status == EFI_SUCCESS) && (Bounce != NULL)) {
    CopyMem (Buffer, Aligned, Bytes);
  }

  if (Bounce != NULL) {
    ZeroMem (Bounce, Bytes + P->IoAlign);
    FreePool (Bounce);
  }

  if (Status != EFI_SUCCESS) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  if (!mReady || (P->Token != Token)) {
    return EFI_MEDIA_CHANGED;
  }

  return Current (P);
}

STATIC
EFI_STATUS
StoragePrepareWrite (
  IN VOID                           *Context,
  IN CONST NP05J_FB_PARTITION_INFO  *Info
  )
{
  NP05J_FB_PARTITION               *P = NULL;
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs = NULL;
  VOID                             *Token;

  (VOID)Context;
  Status = LookupAndValidate (Info, &P);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if (!P->Writable || (P->Block->WriteBlocks == NULL)) {
    return EFI_WRITE_PROTECTED;
  }

  Token        = P->Token;
  mWriteActive = TRUE;

  if (gBS->HandleProtocol (P->Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs) == EFI_SUCCESS) {
    if (EfiGetCurrentTpl () != TPL_APPLICATION) {
      mWriteActive = FALSE;
      return EFI_ACCESS_DENIED;
    }

    Status = gBS->DisconnectController (P->Handle, NULL, NULL);
    if (EFI_ERROR (Status)) {
      mWriteActive = FALSE;
      return Status;
    }

    if (gBS->HandleProtocol (P->Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs) == EFI_SUCCESS) {
      mWriteActive = FALSE;
      return EFI_ACCESS_DENIED;
    }

    mUnmountedHandle = P->Handle;
  }

  if (!mReady || (P->Token != Token)) {
    mWriteActive = FALSE;
    return EFI_MEDIA_CHANGED;
  }

  Status = Current (P);
  if (Status != EFI_SUCCESS) {
    mWriteActive = FALSE;
  }

  return Status;
}

STATIC
EFI_STATUS
StorageWriteBlocks (
  IN VOID                           *Context,
  IN CONST NP05J_FB_PARTITION_INFO  *Info,
  IN UINT64                         Lba,
  IN UINTN                          Bytes,
  IN CONST VOID                     *Buffer
  )
{
  NP05J_FB_PARTITION               *P  = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs = NULL;
  EFI_STATUS                       Status;
  VOID                             *Token;
  VOID                             *Bounce = NULL;
  VOID                             *Aligned;

  (VOID)Context;
  Status = LookupAndValidate (Info, &P);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if (!P->Writable || (P->Block->WriteBlocks == NULL)) {
    return EFI_WRITE_PROTECTED;
  }

  if ((Buffer == NULL) ||
      (Bytes == 0U) ||
      ((Bytes % P->BlockSize) != 0U) ||
      (Bytes > NP05J_FASTBOOT_MAX_BLOCK_IO_BYTES) ||
      (Lba >= (P->Bytes / P->BlockSize)) ||
      ((Bytes / P->BlockSize) > ((P->Bytes / P->BlockSize) - Lba)))
  {
    return EFI_INVALID_PARAMETER;
  }

  if (gBS->HandleProtocol (P->Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs) == EFI_SUCCESS) {
    return EFI_ACCESS_DENIED;
  }

  if ((P->IoAlign > 1U) && (((UINTN)Buffer % P->IoAlign) != 0U)) {
    Bounce = AllocateZeroPool (Bytes + P->IoAlign);
    if (Bounce == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Aligned = (VOID *)(((UINTN)Bounce + P->IoAlign - 1U) & ~((UINTN)P->IoAlign - 1U));
    CopyMem (Aligned, Buffer, Bytes);
  } else {
    Aligned = (VOID *)(UINTN)Buffer;
  }

  Token  = P->Token;
  Status = P->Block->WriteBlocks (P->Block, P->MediaId, Lba, Bytes, Aligned);

  if (Bounce != NULL) {
    ZeroMem (Bounce, Bytes + P->IoAlign);
    FreePool (Bounce);
  }

  if (Status != EFI_SUCCESS) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }

  if (!mReady || (P->Token != Token)) {
    return EFI_MEDIA_CHANGED;
  }

  return Current (P);
}

STATIC
EFI_STATUS
StorageFlush (
  IN VOID                           *Context,
  IN CONST NP05J_FB_PARTITION_INFO  *Info
  )
{
  NP05J_FB_PARTITION  *P = NULL;
  EFI_STATUS          Status;
  EFI_HANDLE          Reconnect;
  VOID                *Token = (Info != NULL) ? Info->Token : NULL;

  (VOID)Context;
  Status = LookupAndValidate (Info, &P);
  if ((Status == EFI_SUCCESS) && !P->Writable) {
    Status = EFI_WRITE_PROTECTED;
  }

  if (Status == EFI_SUCCESS) {
    if (P->Block->FlushBlocks != NULL) {
      Status = P->Block->FlushBlocks (P->Block);
    }

    if (Status == EFI_SUCCESS) {
      Status = (!mReady || (P->Token != Token)) ? EFI_MEDIA_CHANGED : Current (P);
    } else if (!EFI_ERROR (Status)) {
      Status = EFI_DEVICE_ERROR;
    }
  }

  Reconnect        = mUnmountedHandle;
  mWriteActive     = FALSE;
  mUnmountedHandle = NULL;
  mDirty           = TRUE;

  if ((Reconnect != NULL) && (EfiGetCurrentTpl () == TPL_APPLICATION)) {
    gBS->ConnectController (Reconnect, NULL, NULL, TRUE);
  }

  return Status;
}

STATIC CONST NP05J_FB_STORAGE  mStorage = {
  NULL,
  StorageReady,
  StorageInfo,
  StorageReadBlocks,
  StoragePrepareWrite,
  StorageWriteBlocks,
  StorageFlush,
  NP05J_FASTBOOT_MAX_FETCH_BYTES
};

EFI_STATUS
Np05jFastbootStorageInit (
  VOID
  )
{
  if (mReady) {
    return EFI_SUCCESS;
  }

  mReady           = TRUE;
  mDirty           = TRUE;
  mWriteActive     = FALSE;
  mUnmountedHandle = NULL;
  ScanPartitions ();
  return EFI_SUCCESS;
}

VOID
Np05jFastbootStorageMarkDirty (
  VOID
  )
{
  if (!mWriteActive) {
    mDirty = TRUE;
  }
}

VOID
Np05jFastbootStorageStop (
  VOID
  )
{
  mReady           = FALSE;
  mDirty           = FALSE;
  mWriteActive     = FALSE;
  mUnmountedHandle = NULL;
  mCount           = 0U;
  ZeroMem (mPartitions, sizeof (mPartitions));
}

CONST NP05J_FB_STORAGE *
Np05jFastbootStorageGet (
  VOID
  )
{
  return mReady ? &mStorage : NULL;
}

VOID
Np05jFastbootStorageGetStats (
  OUT UINT32  *Total,
  OUT UINT32  *Writable
  )
{
  UINT32  W = 0U;

  if (mReady) {
    EnsureScanned ();
    for (UINTN N = 0U; N < mCount; N++) {
      if (mPartitions[N].Writable) {
        W++;
      }
    }
  }

  if (Total != NULL) {
    *Total = (UINT32)mCount;
  }

  if (Writable != NULL) {
    *Writable = W;
  }
}
