/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  RealTimeClockLib for nubia NP05J (SM8750 / PMK8550).

  Avoids the UEFI-invalid Day=0 bug in RealTimeClockLibNull and provides
  safe monotonic ARM generic timer progression across DXE and OS runtime
  without touching TZ-protected SPMI observer registers in APRIORI DXE.
**/

#include <PiDxe.h>
#include <Library/ArmGenericTimerCounterLib.h>
#include <Library/DebugLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/TimeBaseLib.h>
#include <Library/TimerLib.h>

#define NP05J_DEFAULT_EPOCH   1795046400ULL /* 2026-11-19 00:00:00 UTC */
#define NP05J_MIN_VALID_EPOCH 1704067200ULL /* 2024-01-01 00:00:00 UTC */

STATIC UINT64 mBootEpochBase   = NP05J_DEFAULT_EPOCH;
STATIC INT64  mUserEpochOffset = 0;

STATIC
UINT64
GetCurrentEpochSeconds (
  VOID
  )
{
  UINT32 Freq           = ArmGenericTimerGetTimerFreq ();
  UINT64 ElapsedSeconds = (Freq != 0U) ? (GetPerformanceCounter () / Freq) : 0ULL;
  INT64  Epoch          = (INT64)mBootEpochBase + (INT64)ElapsedSeconds + mUserEpochOffset;

  if (Epoch < (INT64)NP05J_MIN_VALID_EPOCH) {
    Epoch = (INT64)NP05J_DEFAULT_EPOCH + (INT64)ElapsedSeconds;
  }

  return (UINT64)Epoch;
}

EFI_STATUS
EFIAPI
LibGetTime (
  OUT EFI_TIME              *Time,
  OUT EFI_TIME_CAPABILITIES *Capabilities
  )
{
  UINT32 Freq;
  UINT64 Epoch;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Freq = ArmGenericTimerGetTimerFreq ();
  if (Capabilities != NULL) {
    Capabilities->Accuracy   = 0;
    Capabilities->Resolution = (Freq != 0U) ? Freq : 19200000U;
    Capabilities->SetsToZero = FALSE;
  }

  Epoch = GetCurrentEpochSeconds ();
  EpochToEfiTime ((UINTN)Epoch, Time);
  Time->Nanosecond = 0;
  Time->TimeZone   = 0;
  Time->Daylight   = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
LibSetTime (
  IN EFI_TIME *Time
  )
{
  UINT64 NewEpoch;
  UINT64 CurrentBase;
  UINT32 Freq;
  UINT64 Elapsed;

  if ((Time == NULL) || !IsTimeValid (Time)) {
    return EFI_INVALID_PARAMETER;
  }

  NewEpoch    = (UINT64)EfiTimeToEpoch (Time);
  Freq        = ArmGenericTimerGetTimerFreq ();
  Elapsed     = (Freq != 0U) ? (GetPerformanceCounter () / Freq) : 0ULL;
  CurrentBase = mBootEpochBase + Elapsed;
  mUserEpochOffset = (INT64)NewEpoch - (INT64)CurrentBase;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
LibGetWakeupTime (
  OUT BOOLEAN  *Enabled,
  OUT BOOLEAN  *Pending,
  OUT EFI_TIME *Time
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
LibSetWakeupTime (
  IN  BOOLEAN   Enabled,
  OUT EFI_TIME *Time
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
LibRtcInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "Np05jRealTimeClockLib: ArchTimer monotonic RTC base_epoch=%lu\n", mBootEpochBase));
  return EFI_SUCCESS;
}
