/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  ไลบรารีอ่านตาราง RAM Partition ของ Qualcomm พร้อมบังคับเพดาน RamLimitGb จาก BIOS Setup
  สำหรับดีบัก Windows on ARM (เช่น จำกัด 4/8/12/16 GiB เพื่อตรวจสอบปัญหา DMA/high-RAM)
**/

#include <PiDxe.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/Np05jBiosConfigLib.h>
#include <Library/RamManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EFIRamPartition.h>

#include <Np05jBiosConfig.h>

STATIC EFI_RAMPARTITION_PROTOCOL *mRamPartitionProtocol = NULL;
STATIC RamPartitionEntry         *mRamPartition         = NULL;
STATIC UINT32                     mRamPartitionCount    = 0;

EFI_STATUS
GetUsableMemoryRanges (
  OUT EFI_MEMORY_RANGE **Range,
  OUT UINT8             *RangeCount
  )
{
  EFI_STATUS        Status     = EFI_SUCCESS;
  EFI_MEMORY_RANGE *LocalRange = NULL;
  NP05J_BIOS_CONFIG Config;
  UINT64            CapBytes   = 0;
  UINT64            AccBytes   = 0;
  UINT8             OutCount   = 0;

  if ((Range == NULL) || (RangeCount == NULL) || (mRamPartition == NULL) || (mRamPartitionCount == 0U)) {
    return EFI_NOT_READY;
  }

  Np05jBiosConfigLoad (&Config);
  if (Config.RamLimitGb > 0U) {
    CapBytes = (UINT64)Config.RamLimitGb * SIZE_1GB;
    DEBUG ((DEBUG_WARN, "NP05J_RAM: Applying BIOS RamLimitGb=%u GiB\n", (UINT32)Config.RamLimitGb));
  }

  LocalRange = AllocateZeroPool (sizeof (EFI_MEMORY_RANGE) * mRamPartitionCount);
  if (LocalRange == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  for (UINT32 Index = 0; Index < mRamPartitionCount; Index++) {
    UINT64 Base   = mRamPartition[Index].Base;
    UINT64 Length = mRamPartition[Index].AvailableLength;

    if (Length == 0ULL) {
      continue;
    }

    if (CapBytes > 0ULL) {
      if (AccBytes >= CapBytes) {
        break;
      }
      if ((CapBytes - AccBytes) < Length) {
        Length = (CapBytes - AccBytes) & ~((UINT64)EFI_PAGE_MASK);
      }
      if (Length == 0ULL) {
        break;
      }
      AccBytes += Length;
    }

    LocalRange[OutCount].Address = Base;
    LocalRange[OutCount].Length  = Length;
    OutCount++;
  }

  *Range      = LocalRange;
  *RangeCount = OutCount;

Cleanup:
  if (mRamPartition != NULL) {
    FreePool (mRamPartition);
    mRamPartition      = NULL;
    mRamPartitionCount = 0;
  }
  return Status;
}

EFI_STATUS
EFIAPI
Np05jGetRamPartitions (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (&gEfiRamPartitionProtocolGuid, NULL, (VOID **)&mRamPartitionProtocol);
  if (EFI_ERROR (Status) || (mRamPartitionProtocol == NULL)) {
    DEBUG ((DEBUG_ERROR, "NP05J_RAM: Failed to locate RAM Partition Protocol (%r)\n", Status));
    return Status;
  }

  Status = mRamPartitionProtocol->GetRamPartitions (mRamPartitionProtocol, NULL, &mRamPartitionCount);
  if (EFI_ERROR (Status) && (Status != EFI_BUFFER_TOO_SMALL)) {
    DEBUG ((DEBUG_ERROR, "NP05J_RAM: Failed to query RAM partition count (%r)\n", Status));
    return Status;
  }

  mRamPartition = AllocateZeroPool (sizeof (RamPartitionEntry) * mRamPartitionCount);
  if (mRamPartition == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = mRamPartitionProtocol->GetRamPartitions (mRamPartitionProtocol, mRamPartition, &mRamPartitionCount);
  if (EFI_ERROR (Status)) {
    FreePool (mRamPartition);
    mRamPartition      = NULL;
    mRamPartitionCount = 0;
    DEBUG ((DEBUG_ERROR, "NP05J_RAM: Failed to read RAM partitions (%r)\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}
