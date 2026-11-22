/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  PlatformBootManagerLib for nubia NP05J integrating Project SunUEFI high-DPI
  vector splash + touch pill polling, pre-UEFI SUNUEFI-EMBEDv1 handoff,
  persistent BIOS config, and touch-native Boot Menu / BIOS Setup fallback.
**/

#include <Library/AcpiTableUpdateLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootGraphics.h>
#include <Library/BootGraphicsLib.h>
#include <Library/DebugLib.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MsBootOptionsLib.h>
#include <Library/Np05jBiosConfigLib.h>
#include <Library/Np05jSplashLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformBootManagerLib.h>
#include <Library/PrintLib.h>
#include <Library/SecureBootHandlerLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/SimplePointer.h>

#include <Configuration/BootDevices.h>
#include <Np05jBiosConfig.h>
#include <Np05jBootHandoff.h>

#include "Np05jBootManager.h"

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGopProtocol    = NULL;
STATIC NP05J_BIOS_CONFIG             mBiosConfig;
STATIC EFI_EVENT                     mSplashTouchEvt = NULL;
STATIC BOOLEAN                       mSplashPillHit  = FALSE;

STATIC
VOID
PostGopSetup (
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL **GopProtocol
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *LocalGop = NULL;

  if (GopProtocol == NULL) {
    return;
  }

  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&LocalGop);
  if (EFI_ERROR (Status) && (Status != EFI_UNSUPPORTED)) {
    return;
  }

  if (Status == EFI_UNSUPPORTED) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&LocalGop);
    if (EFI_ERROR (Status) || (LocalGop == NULL)) {
      return;
    }
    gBS->InstallProtocolInterface (&gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, EFI_NATIVE_INTERFACE, LocalGop);
  }

  *GopProtocol = LocalGop;
}

STATIC
VOID
PollSplashTouchPills (
  VOID
  )
{
  EFI_HANDLE *Handles = NULL;
  UINTN       Count   = 0;

  if ((mGopProtocol == NULL) || mSplashPillHit) {
    return;
  }

  if (!EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL, &Count, &Handles)) &&
      (Handles != NULL))
  {
    UINT32 W = mGopProtocol->Mode->Info->HorizontalResolution;
    UINT32 H = mGopProtocol->Mode->Info->VerticalResolution;

    for (UINTN I = 0; I < Count; I++) {
      EFI_ABSOLUTE_POINTER_PROTOCOL *Ptr = NULL;
      EFI_ABSOLUTE_POINTER_STATE     St;

      if ((Count > 1U) && (Handles[I] == gST->ConsoleInHandle)) {
        continue;
      }

      if (!EFI_ERROR (gBS->HandleProtocol (Handles[I], &gEfiAbsolutePointerProtocolGuid, (VOID **)&Ptr)) && (Ptr != NULL)) {
        while (!EFI_ERROR (Ptr->GetState (Ptr, &St))) {
          if (((St.ActiveButtons & EFI_ABSP_TouchActive) != 0U) &&
              (Ptr->Mode->AbsoluteMaxX > Ptr->Mode->AbsoluteMinX) &&
              (Ptr->Mode->AbsoluteMaxY > Ptr->Mode->AbsoluteMinY))
          {
            UINT64 SpanX = Ptr->Mode->AbsoluteMaxX - Ptr->Mode->AbsoluteMinX;
            UINT64 SpanY = Ptr->Mode->AbsoluteMaxY - Ptr->Mode->AbsoluteMinY;
            UINT32 DispX = (UINT32)(((St.CurrentX - Ptr->Mode->AbsoluteMinX) * W) / SpanX);
            UINT32 DispY = (UINT32)(((St.CurrentY - Ptr->Mode->AbsoluteMinY) * H) / SpanY);

            NP05J_SPLASH_ACTION Act = Np05jSplashHitTest (DispX, DispY);
            if (Act == Np05jSplashActionBootMenu) {
              Np05jSetEnterBootMenu (FALSE);
              mSplashPillHit = TRUE;
              Np05jSplashSetProgress (mGopProtocol, 100, "Opening SunUEFI Touch Boot Menu...");
            } else if (Act == Np05jSplashActionBiosSetup) {
              Np05jSetEnterBootMenu (TRUE);
              mSplashPillHit = TRUE;
              Np05jSplashSetProgress (mGopProtocol, 100, "Opening Unlocked BIOS Setup...");
            }
          }
        }
      }
    }
    FreePool (Handles);
  }
}

STATIC
VOID
EFIAPI
SplashTouchTimerCallback (
  IN EFI_EVENT Event,
  IN VOID     *Context
  )
{
  PollSplashTouchPills ();
}

STATIC
VOID
LaunchCustomBootMenu (
  VOID
  )
{
  EFI_STATUS                   Status;
  EFI_BOOT_MANAGER_LOAD_OPTION LoadOption;

  Status = MsBootOptionsLibGetBootManagerMenu (&LoadOption, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: MsBootOptionsLibGetBootManagerMenu failed (%r)\n", __FUNCTION__, Status));
    return;
  }

  DEBUG ((DEBUG_INFO, "NP05J_BDS: Launching Np05jBootMenuApp...\n"));
  EfiBootManagerBoot (&LoadOption);
  DEBUG ((DEBUG_INFO, "NP05J_BDS: Np05jBootMenuApp returned (%r)\n", LoadOption.Status));
  EfiBootManagerFreeLoadOption (&LoadOption);
}

VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  )
{
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);
  EfiBootManagerDispatchDeferredImages ();
  MsBootOptionsLibRegisterDefaultBootOptions ();

  EfiBootManagerUpdateConsoleVariable (ConIn,  (EFI_DEVICE_PATH_PROTOCOL *)&KeypadDevicePath,      NULL);
  EfiBootManagerUpdateConsoleVariable (ConIn,  (EFI_DEVICE_PATH_PROTOCOL *)&UsbKeyboardDevicePath, NULL);
  EfiBootManagerUpdateConsoleVariable (ConOut, (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath,     NULL);
  EfiBootManagerUpdateConsoleVariable (ErrOut, (EFI_DEVICE_PATH_PROTOCOL *)&DisplayDevicePath,     NULL);

  DeviceBootManagerBeforeConsole (NULL, NULL);
}

VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
  )
{
  CONST volatile NP05J_EMBED_RECORD *Rec = (CONST volatile NP05J_EMBED_RECORD *)(UINTN)NP05J_EMBED_RECORD_ADDR;

  Np05jBiosConfigLoad (&mBiosConfig);
  PostGopSetup (&mGopProtocol);

  /* Honor SUNUEFI-EMBEDv1 request written by pre-UEFI BootSelect */
  if ((AsciiStrnCmp ((CONST CHAR8 *)Rec->Magic, NP05J_EMBED_RECORD_MAGIC, 15) == 0) &&
      (Rec->Version == NP05J_EMBED_RECORD_VERSION) &&
      (Rec->Size == NP05J_EMBED_RECORD_SIZE))
  {
    if (Rec->Target == NP05J_BOOT_TARGET_UEFI_MENU) {
      Np05jSetEnterBootMenu (FALSE);
    } else if ((Rec->Target == NP05J_BOOT_TARGET_BIOS_SETUP) ||
               (Rec->Target == NP05J_BOOT_TARGET_WINDOWS) ||
               (Rec->Target == NP05J_BOOT_TARGET_LINUX))
    {
      /* Np05jBootMenuApp checks Rec->Target to open Setup or directly launch OS */
      Np05jSetEnterBootMenu (Rec->Target == NP05J_BOOT_TARGET_BIOS_SETUP);
    }
  } else if (mBiosConfig.DefaultOs == NP05J_OS_BOOT_MENU) {
    Np05jSetEnterBootMenu (FALSE);
  }

  if ((mGopProtocol != NULL) && (mBiosConfig.SplashStyle != NP05J_SPLASH_VERBOSE)) {
    Np05jDrawSplash (mGopProtocol, mBiosConfig.SplashStyle);
    Np05jSplashRegisterBootLogo2 (mGopProtocol);
  } else {
    DisplayBootGraphic (BG_SYSTEM_LOGO);
  }

  Np05jRegisterKeyCallback ((EFI_DEVICE_PATH_PROTOCOL *)&KeypadDevicePath);
  DeviceBootManagerAfterConsole ();

  if (mSplashTouchEvt == NULL) {
    if (!EFI_ERROR (gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, SplashTouchTimerCallback, NULL, &mSplashTouchEvt))) {
      gBS->SetTimer (mSplashTouchEvt, TimerPeriodic, 250000ULL); /* 25 ms */
    }
  }
}

VOID
EFIAPI
PlatformBootManagerOnDemandConInConnect (
  VOID
  )
{
  DeviceBootManagerOnDemandConInConnect ();
}

VOID
EFIAPI
PlatformBootManagerProcessBootCompletion (
  IN EFI_BOOT_MANAGER_LOAD_OPTION *BootOption
  )
{
  return;
}

VOID
EFIAPI
PlatformBootManagerPriorityBoot (
  IN OUT UINT16 **BootNext
  )
{
  if (mSplashTouchEvt != NULL) {
    gBS->SetTimer (mSplashTouchEvt, TimerCancel, 0);
    gBS->CloseEvent (mSplashTouchEvt);
    mSplashTouchEvt = NULL;
  }

  Np05jUnregisterKeyCallback ();

  if (!Np05jShouldEnterBootMenu ()) {
    return;
  }

  LaunchCustomBootMenu ();
}

VOID
EFIAPI
PlatformBootManagerWaitCallback (
  IN UINT16 TimeoutRemain
  )
{
  UINT16 Timeout = PcdGet16 (PcdPlatformBootTimeOut);
  UINT8  Pct     = 100U;

  PollSplashTouchPills ();

  if ((mGopProtocol == NULL) || (mBiosConfig.SplashStyle == NP05J_SPLASH_VERBOSE)) {
    return;
  }

  if (Timeout > 0U) {
    UINT16 Elapsed = (Timeout >= TimeoutRemain) ? (Timeout - TimeoutRemain) : Timeout;
    Pct = (UINT8)((Elapsed * 100U) / Timeout);
  }

  if (!mSplashPillHit) {
    CHAR8 Msg[96];
    AsciiSPrint (
      Msg,
      sizeof (Msg),
      "Touch [BOOT MENU] / [BIOS SETUP] or Vol+ / Vol- (%us)",
      (UINT32)TimeoutRemain
      );
    Np05jSplashSetProgress (mGopProtocol, Pct, Msg);
  }
}

VOID
EFIAPI
PlatformBootManagerUnableToBoot (
  VOID
  )
{
  DeviceBootManagerUnableToBoot ();

  /* Always present touch Boot Menu & BIOS Setup instead of shutting down */
  LaunchCustomBootMenu ();
}

VOID
EFIAPI
PlatformBootManagerBdsEntry (
  VOID
  )
{
  EFI_STATUS Status;

  Status = SetupSecureBoot ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "%a: SetupSecureBoot returned %r\n", __FUNCTION__, Status));
  }

  UpdateAcpiTables ();
  DeviceBootManagerBdsEntry ();
}
