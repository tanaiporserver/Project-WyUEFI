/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Hardware reboot-mode & EDL 9008 support for nubia NP05J (SM8750).

  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)
  uefi/components/reset/PianoReset.c.
  Verified against Device_np05j/nubia_np05j-live.dts:
    - PMK8550 sdam@7100 restart@48 bits [7:1]: 1=recovery, 2=bootloader
    - SPMI v7 arbiter: core=0x0C400000, cfg=0x0C42D000, obs=0x0C440000, rw=0x0C500000
    - TCSR dload-mode: 0x01FD9000 via SCM 0xC2000501 / 0xC2000502
**/

#include <Uefi.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Np05jResetLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define NP05J_SPMI_CORE       0x0C400000U
#define NP05J_SPMI_CFG        0x0C42D000U
#define NP05J_SPMI_OBS        0x0C440000U
#define NP05J_SPMI_RW         0x0C500000U

#define NP05J_PMK_SDAM_PPID   0x071U
#define NP05J_RESTART_REG     0x48U
#define NP05J_TCSR_DLOAD_ADDR 0x01FD9000U

STATIC
EFI_STATUS
SpmiWait (
  IN UINTN Base
  )
{
  for (UINTN Index = 0; Index < 1000; Index++) {
    UINT32 Status = MmioRead32 (Base + 8);
    if ((Status & 1U) != 0U) {
      return ((Status & 0xEU) != 0U) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    if (gBS != NULL) {
      gBS->Stall (1);
    }
  }
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
WriteSdamRebootReason (
  IN UINT8 Value
  )
{
  UINT32     Version = MmioRead32 (NP05J_SPMI_CORE);
  UINT32     Count   = MmioRead32 (NP05J_SPMI_CORE + 4) & 0x7FFU;
  UINTN      Apid    = MAX_UINTN;
  EFI_STATUS Status;

  if ((Version < 0x70000000U) || (Version >= 0x80000000U) || (Count == 0U) || (Count > 1024U)) {
    return EFI_UNSUPPORTED;
  }

  for (UINTN Index = 0; Index < Count; Index++) {
    UINT32 Map = MmioRead32 (NP05J_SPMI_CORE + 0x2000U + 4U * Index);
    if ((Map != 0U) && (((Map >> 8) & 0xFFFU) == NP05J_PMK_SDAM_PPID)) {
      if ((MmioRead32 (NP05J_SPMI_CFG + 4U * Index) & 7U) == 0U) {
        Apid = Index;
      }
    }
  }

  if (Apid == MAX_UINTN) {
    return EFI_ACCESS_DENIED;
  }

  {
    UINTN ReadCh  = NP05J_SPMI_OBS + 0x20U * Apid;
    UINTN WriteCh = NP05J_SPMI_RW + 0x1000U * Apid;
    UINT8 Before;
    UINT8 After;

    MmioWrite32 (ReadCh, (1U << 27) | (NP05J_RESTART_REG << 4));
    Status = SpmiWait (ReadCh);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Before = (UINT8)MmioRead32 (ReadCh + 0x18);
    After  = (UINT8)((Before & 1U) | (UINT8)(Value << 1));

    MmioWrite32 (WriteCh + 0x10, After);
    MemoryFence ();
    MmioWrite32 (WriteCh, NP05J_RESTART_REG << 4);
    Status = SpmiWait (WriteCh);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    MmioWrite32 (ReadCh, (1U << 27) | (NP05J_RESTART_REG << 4));
    Status = SpmiWait (ReadCh);
    if (!EFI_ERROR (Status) && ((UINT8)MmioRead32 (ReadCh + 0x18) != After)) {
      Status = EFI_DEVICE_ERROR;
    }

    DEBUG ((DEBUG_WARN, "NP05J_RESET_REASON before=%02x after=%02x status=%r\n", Before, After, Status));
    return Status;
  }
}

STATIC
EFI_STATUS
CallQcomScm (
  IN  UINTN  Function,
  IN  UINTN  Count,
  IN  UINTN  ArgA,
  IN  UINTN  ArgB,
  OUT UINTN *Result OPTIONAL
  )
{
  UINTN Call  = Function;
  UINTN Token = 0;

  for (UINTN Index = 0; Index < 8; Index++) {
    ARM_SMC_ARGS Args;
    Args.Arg0 = Call;
    Args.Arg1 = Count;
    Args.Arg2 = ArgA;
    Args.Arg3 = ArgB;
    Args.Arg4 = 0;
    Args.Arg5 = 0;
    Args.Arg6 = Token;
    Args.Arg7 = 0;

    ArmCallSmc (&Args);
    if (Args.Arg0 == 1) {
      Call  = 1;
      Token = Args.Arg6;
      continue;
    }
    if (Args.Arg0 != 0) {
      return EFI_DEVICE_ERROR;
    }
    if (Result != NULL) {
      *Result = Args.Arg1;
    }
    return EFI_SUCCESS;
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
SetEdlDownloadMode (
  VOID
  )
{
  UINTN      Value = 0;
  EFI_STATUS Status;

  Status = CallQcomScm (0xC2000501U, 1, NP05J_TCSR_DLOAD_ADDR, 0, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = CallQcomScm (0xC2000502U, 2, NP05J_TCSR_DLOAD_ADDR, 1, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = CallQcomScm (0xC2000501U, 1, NP05J_TCSR_DLOAD_ADDR, 0, &Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return (Value == 1U) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
EFIAPI
Np05jSetRebootReason (
  IN NP05J_RESET_TARGET Target
  )
{
  switch (Target) {
    case Np05jResetRecovery:
      return WriteSdamRebootReason (1);
    case Np05jResetBootloader:
      return WriteSdamRebootReason (2);
    case Np05jResetEdl9008:
      return SetEdlDownloadMode ();
    case Np05jResetNormal:
    case Np05jResetShutdown:
      return EFI_SUCCESS;
    default:
      return EFI_INVALID_PARAMETER;
  }
}

EFI_STATUS
EFIAPI
Np05jExecuteReset (
  IN NP05J_RESET_TARGET Target
  )
{
  EFI_STATUS     Status;
  EFI_RESET_TYPE ResetType;

  Status = Np05jSetRebootReason (Target);
  if (EFI_ERROR (Status) && (Target == Np05jResetEdl9008)) {
    return Status;
  }

  ResetType = (Target == Np05jResetShutdown) ? EfiResetShutdown : EfiResetCold;
  if ((gRT == NULL) || (gRT->ResetSystem == NULL)) {
    return EFI_UNSUPPORTED;
  }

  gRT->ResetSystem (ResetType, EFI_SUCCESS, 0, NULL);
  CpuDeadLoop ();
  return EFI_DEVICE_ERROR;
}
