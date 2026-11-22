/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Main Entry & SunUEFI Light Touch-First UEFI Boot Menu for nubia NP05J.
**/

#include "Np05jBootMenuApp.h"

#define STARTUP_COUNT 6U
#define UTIL_COUNT    4U
#define ITEM_COUNT    (STARTUP_COUNT + UTIL_COUNT)

typedef struct {
  CONST CHAR8 *Title;
  CONST CHAR8 *Subtitle;
} NP05J_BOOT_ITEM;

STATIC CONST NP05J_BOOT_ITEM mBootItems[ITEM_COUNT] = {
  /* Stacked SELECT STARTUP rows (0..5) */
  { "ANDROID",                  "Stock Android HLOS (Cold Reset)" },
  { "WINDOWS 11 ARM",           "Scan FS* for bootmgfw.efi (WoA ACPI)" },
  { "LINUX (DEVICETREE)",       "Boot GRUB / systemd-boot / UKI (DTB)" },
  { "LINUX (ACPI)",             "Boot ARM64 Linux via ACPI Tables" },
  { "BIOS SETUP (UNLOCKED)",    "All 9 Categories Unlocked + S3930 Calibration" },
  { "TIANOCORE SETUP (UIAPP)",  "Genuine Project SunUEFI EDK2 Firmware Setup" },
  /* Utility pills below startup stack (6..9) */
  { "UEFI SHELL",               "Built-in Shell" },
  { "BOOT FILE (.EFI)",         "Browse FAT / ESP" },
  { "TOUCH TEST",               "S3930 Diagnostics" },
  { "POWER / RESET",            "Recovery / EDL" }
};

STATIC
VOID
RunPowerModal (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  STATIC CONST CHAR8 *mPwrLabels[6] = {
    "Reboot Normal (Android / BootSelect)",
    "Reboot to Recovery",
    "Reboot to Bootloader / Fastboot",
    "Reboot to EDL (Qualcomm 9008)",
    "Shutdown / Power Off",
    "Cancel"
  };
  NP05J_RECT                    Box   = { 560, 260, 1280, 960 };
  NP05J_RECT                    Btns[6];
  INTN                          Focus = 0;
  BOOLEAN                       Redraw = TRUE;

  Np05jUiDrainInput (Ctx);

  while (TRUE) {
    NP05J_INPUT_EVENT Ev;

    if (Redraw) {
      Np05jUiDrawRoundedCard (Ctx, Box, 20, COL_PANEL, COL_BORDER, 3);
      Np05jUiDrawText5x7Centered (Ctx, (NP05J_RECT){ Box.X, Box.Y + 34, Box.W, 56 }, 4U, COL_TEXT, "POWER & HARDWARE RESET MODE");
      Np05jUiDrawText5x7Centered (Ctx, (NP05J_RECT){ Box.X, Box.Y + 94, Box.W, 36 }, 2U, COL_SUBTEXT, "PMK8550 SDAM0x71 / TCSR SCM 9008");

      for (UINTN I = 0; I < 6U; I++) {
        Btns[I] = (NP05J_RECT){ Box.X + 80, Box.Y + 150 + (INT32)I * 124, Box.W - 160, 102 };
        {
          BOOLEAN Sel = ((INTN)I == Focus);
          Np05jUiDrawRoundedCard (
            Ctx,
            Btns[I],
            14,
            (I == 3U && !Sel) ? COL_RED_BG : (Sel ? COL_CARD_HI : COL_CARD),
            (I == 3U) ? COL_GOLD : (Sel ? COL_ACCENT : COL_BORDER),
            Sel ? 4 : 2
            );
          Np05jUiDrawText5x7Centered (
            Ctx,
            Btns[I],
            3U,
            (I == 5U) ? COL_SUBTEXT : (Sel ? COL_ACCENT_DARK : COL_TEXT),
            mPwrLabels[I]
            );
        }
      }
      Np05jUiPresent (Ctx);
      Redraw = FALSE;
    }

    if (EFI_ERROR (Np05jUiPollInput (Ctx, 100, &Ev))) {
      continue;
    }
    if (Ev.Kind == Np05jInputBack) {
      return;
    }
    if (Ev.Kind == Np05jInputUp) {
      Focus  = (Focus > 0) ? (Focus - 1) : 5;
      Redraw = TRUE;
      continue;
    }
    if (Ev.Kind == Np05jInputDown) {
      Focus  = (Focus + 1) % 6;
      Redraw = TRUE;
      continue;
    }

    INTN Pick = -1;
    if (Ev.Kind == Np05jInputEnter) {
      Pick = Focus;
    } else if (Ev.Kind == Np05jInputTap) {
      if (!Np05jRectContains (Box, Ev.X, Ev.Y)) {
        return;
      }
      for (UINTN I = 0; I < 6U; I++) {
        if (Np05jRectContains (Btns[I], Ev.X, Ev.Y)) {
          Pick = (INTN)I;
          break;
        }
      }
    }

    if (Pick >= 0) {
      switch (Pick) {
        case 0: Np05jExecuteReset (Np05jResetNormal); break;
        case 1: Np05jExecuteReset (Np05jResetRecovery); break;
        case 2: Np05jExecuteReset (Np05jResetBootloader); break;
        case 3:
        {
          EFI_STATUS St = Np05jExecuteReset (Np05jResetEdl9008);
          Np05jUiSetBanner (Ctx, COL_RED, "EDL 9008 entry failed: %r", St);
          return;
        }
        case 4: Np05jExecuteReset (Np05jResetShutdown); break;
        case 5: return;
      }
    }
  }
}

STATIC
CONST CHAR8 *
FastbootSpeedStr (
  IN UINT8 Speed
  )
{
  switch (Speed) {
    case 1: return "LS";
    case 2: return "FS";
    case 3: return "HS";
    case 4: return "SS";
    case 5: return "SSP";
    default: return "WAIT";
  }
}

STATIC
VOID
HandleFastbootAction (
  IN OUT NP05J_UI_CTX          *Ctx,
  IN     NP05J_FASTBOOT_ACTION  Action
  )
{
  BOOLEAN RebootReq = FALSE;

  switch (Action) {
    case Np05jFbActionOpenSetup:
      Np05jRunBiosSetup (Ctx, &RebootReq);
      break;
    case Np05jFbActionOpenShell:
      Np05jLaunchEfiShell (Ctx);
      break;
    case Np05jFbActionOpenBootMenu:
      Np05jUiSetBanner (Ctx, COL_ACCENT, "Fastboot: SunUEFI Boot Menu active");
      break;
    case Np05jFbActionBootStable:
      switch (Ctx->Config.DefaultOs) {
        case NP05J_OS_WINDOWS_ARM:
          Np05jLaunchWindowsArm (Ctx);
          break;
        case NP05J_OS_LINUX_DT:
          Np05jLaunchLinux (Ctx, FALSE);
          break;
        case NP05J_OS_LINUX_ACPI:
          Np05jLaunchLinux (Ctx, TRUE);
          break;
        case NP05J_OS_ANDROID:
          Np05jExecuteReset (Np05jResetNormal);
          break;
        case NP05J_OS_UEFI_SHELL:
          Np05jLaunchEfiShell (Ctx);
          break;
        case NP05J_OS_BOOT_MENU:
        default:
          if (EFI_ERROR (Np05jLaunchLinux (Ctx, FALSE))) {
            Np05jLaunchWindowsArm (Ctx);
          }
          break;
      }
      break;
    case Np05jFbActionReboot:
    case Np05jFbActionContinue:
      if (Ctx->ConfigModified) {
        Np05jBiosConfigSave (&Ctx->Config);
        Ctx->ConfigModified = FALSE;
      }
      Np05jExecuteReset (Np05jResetNormal);
      break;
    default:
      break;
  }
  Np05jUiDrainInput (Ctx);
}

STATIC
VOID
ActivateTile (
  IN OUT NP05J_UI_CTX *Ctx,
  IN     UINTN         Index
  )
{
  BOOLEAN RebootReq = FALSE;

  switch (Index) {
    case 0:
      Np05jExecuteReset (Np05jResetNormal);
      break;
    case 1:
      Np05jLaunchWindowsArm (Ctx);
      break;
    case 2:
      Np05jLaunchLinux (Ctx, FALSE);
      break;
    case 3:
      Np05jLaunchLinux (Ctx, TRUE);
      break;
    case 4:
      Np05jRunBiosSetup (Ctx, &RebootReq);
      break;
    case 5:
      Np05jLaunchTianoSetup (Ctx);
      break;
    case 6:
      Np05jLaunchEfiShell (Ctx);
      break;
    case 7:
      Np05jRunFileBrowser (Ctx);
      break;
    case 8:
      Np05jRunTouchTest (Ctx);
      break;
    case 9:
      RunPowerModal (Ctx);
      break;
    default:
      break;
  }
  Np05jUiDrainInput (Ctx);
}

STATIC
VOID
CheckAutoLaunchHandoff (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  CONST volatile NP05J_EMBED_RECORD *Rec = (CONST volatile NP05J_EMBED_RECORD *)(UINTN)NP05J_EMBED_RECORD_ADDR;
  UINT8                              OpenSetup = 0;
  UINTN                              VarSize   = sizeof (OpenSetup);
  BOOLEAN                            RebootReq = FALSE;

  /* Consume splash-pill OpenSetup trigger */
  if ((gRT != NULL) &&
      !EFI_ERROR (gRT->GetVariable (L"Np05jOpenSetup", &gNp05jBiosConfigGuid, NULL, &VarSize, &OpenSetup)))
  {
    gRT->SetVariable (L"Np05jOpenSetup", &gNp05jBiosConfigGuid, 0, 0, NULL);
    if (OpenSetup != 0U) {
      Np05jRunBiosSetup (Ctx, &RebootReq);
      return;
    }
  }

  /* Honor pre-UEFI BootSelect SUNUEFI-EMBEDv1 handoff if valid */
  if ((AsciiStrnCmp ((CONST CHAR8 *)Rec->Magic, NP05J_EMBED_RECORD_MAGIC, 15) == 0) &&
      (Rec->Version == NP05J_EMBED_RECORD_VERSION) &&
      (Rec->Size == NP05J_EMBED_RECORD_SIZE))
  {
    if (Rec->Target == NP05J_BOOT_TARGET_BIOS_SETUP) {
      Np05jRunBiosSetup (Ctx, &RebootReq);
    } else if (Rec->Target == NP05J_BOOT_TARGET_WINDOWS) {
      Np05jLaunchWindowsArm (Ctx);
    } else if (Rec->Target == NP05J_BOOT_TARGET_LINUX) {
      Np05jLaunchLinux (Ctx, FALSE);
    }
  }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  NP05J_UI_CTX Ctx;
  EFI_STATUS   Status;
  INTN         Focus      = 0;
  BOOLEAN      NeedRedraw = TRUE;

  EfiBootManagerConnectAll ();

  Status = Np05jUiInit (&Ctx);
  if (EFI_ERROR (Status)) {
    Print (L"NP05J BootMenu: GOP init failed (%r)\n", Status);
    return Status;
  }

  CheckAutoLaunchHandoff (&Ctx);
  Np05jUiDrainInput (&Ctx);

  while (TRUE) {
    NP05J_RECT        ItemRect[ITEM_COUNT];
    NP05J_INPUT_EVENT Ev;
    INT32             ColX    = 420;
    INT32             ColW    = 1560;
    INT32             StartY  = 314;

    for (UINTN I = 0; I < STARTUP_COUNT; I++) {
      ItemRect[I] = (NP05J_RECT){ ColX, StartY + (INT32)I * 122, ColW, 108 };
    }
    for (UINTN U = 0; U < UTIL_COUNT; U++) {
      ItemRect[STARTUP_COUNT + U] = (NP05J_RECT){ ColX + (INT32)U * (372 + 24), 1074, 372, 88 };
    }

    if (NeedRedraw) {
      CHAR8                      PillTxt[112];
      NP05J_TOUCH_INFO_PROTOCOL *TouchInfo = NULL;
      NP05J_TOUCH_TELEMETRY      Telem;
      NP05J_FASTBOOT_STATUS      FbStatus;
      BOOLEAN                    Bat = FALSE;
      BOOLEAN                    Chg = FALSE;
      UINT32                     Mv  = 0;

      ZeroMem (&Telem, sizeof (Telem));
      if (!EFI_ERROR (gBS->LocateProtocol (&gNp05jTouchInfoProtocolGuid, NULL, (VOID **)&TouchInfo)) &&
          (TouchInfo != NULL))
      {
        TouchInfo->GetTelemetry (TouchInfo, &Telem);
      }
      ZeroMem (&FbStatus, sizeof (FbStatus));
      if (Ctx.Fastboot != NULL) {
        Ctx.Fastboot->GetStatus (Ctx.Fastboot, &FbStatus);
      }
      Np05jQueryPowerStatus (&Bat, &Chg, &Mv);

      Np05jUiClear (&Ctx, COL_BG);
      Np05jUiDrawSunBrand (&Ctx, 132, 168U, 17U, "SunUEFI / NP05J");

      AsciiSPrint (
        PillTxt,
        sizeof (PillTxt),
        "24GB | %umV%a | TOUCH:%a | FB:%a | %u VOLS",
        Mv,
        Chg ? "+" : "",
        Telem.Initialized ? "ON" : "OFF",
        FbStatus.Configured ? FastbootSpeedStr (FbStatus.BusSpeed) : "WAIT",
        (UINT32)Np05jCountFileSystems ()
        );
      {
        INT32 Pw = (INT32)((AsciiStrLen (PillTxt) * 6U - 1U) * 2U) + 32;
        NP05J_RECT PillR = { (INT32)Ctx.ScreenW - Pw - 42, 28, Pw, 40 };
        Np05jUiDrawRoundedCard (&Ctx, PillR, 10, COL_SURFACE, COL_BORDER, 2);
        Np05jUiDrawText5x7Centered (&Ctx, PillR, 2U, COL_SUBTEXT, PillTxt);
      }

      Np05jUiDrawText5x7 (&Ctx, ColX, 266, 4U, Np05jRgb (86, 91, 99), "SELECT STARTUP");
      for (UINTN I = 0; I < STARTUP_COUNT; I++) {
        BOOLEAN    Focused = ((INTN)I == Focus);
        NP05J_RECT R       = ItemRect[I];

        Np05jUiDrawRoundedCard (
          &Ctx,
          R,
          12,
          Focused ? COL_CARD_HI : COL_CARD,
          Focused ? COL_ACCENT : COL_BORDER,
          Focused ? 4 : 3
          );
        Np05jUiDrawText5x7 (&Ctx, R.X + 44, R.Y + 24, 4U, Focused ? COL_ACCENT_DARK : COL_TEXT, mBootItems[I].Title);
        Np05jUiDrawText5x7 (&Ctx, R.X + 44, R.Y + 66, 2U, COL_SUBTEXT, mBootItems[I].Subtitle);

        if (Focused) {
          INT32 SelW = (8 * 6 - 1) * 3;
          Np05jUiDrawText5x7 (&Ctx, R.X + R.W - 44 - SelW, R.Y + 43, 3U, COL_ACCENT, "SELECTED");
        }
      }

      for (UINTN U = 0; U < UTIL_COUNT; U++) {
        UINTN      Idx     = STARTUP_COUNT + U;
        BOOLEAN    Focused = ((INTN)Idx == Focus);
        NP05J_RECT R       = ItemRect[Idx];

        Np05jUiDrawRoundedCard (
          &Ctx,
          R,
          12,
          Focused ? COL_CARD_HI : COL_SURFACE,
          Focused ? COL_ACCENT : COL_BORDER,
          Focused ? 4 : 2
          );
        Np05jUiDrawText5x7Centered (
          &Ctx,
          R,
          3U,
          Focused ? COL_ACCENT_DARK : COL_TEXT,
          mBootItems[Idx].Title
          );
      }

      if (Ctx.StatusValid && (Ctx.StatusBanner[0] != '\0')) {
        NP05J_RECT BanR = { ColX, 1192, ColW, 54 };
        Np05jUiDrawRoundedCard (&Ctx, BanR, 12, COL_PANEL, Ctx.StatusColor, 3);
        Np05jUiDrawText5x7Centered (&Ctx, BanR, 2U, Ctx.StatusColor, Ctx.StatusBanner);
      }

      Np05jUiDrawText5x7 (
        &Ctx,
        144,
        1344,
        4U,
        COL_SUBTEXT,
        "F12: TIANOCORE SETUP   ESC: BIOS SETUP   TAP ANY OPTION"
        );
      Np05jUiDrawText5x7 (
        &Ctx,
        144,
        1404,
        3U,
        COL_MUTED,
        "VOLUME +/-: NAVIGATE   POWER: SELECT / CONFIRM"
        );
      Np05jUiPresent (&Ctx);
      NeedRedraw = FALSE;
    }

    if (EFI_ERROR (Np05jUiPollInput (&Ctx, 150, &Ev))) {
      continue;
    }

    if (Ev.Kind == Np05jInputFastbootAction) {
      HandleFastbootAction (&Ctx, Ev.FbAction);
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputF12) {
      Np05jLaunchTianoSetup (&Ctx);
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputBack) {
      BOOLEAN RebootReq = FALSE;
      Np05jRunBiosSetup (&Ctx, &RebootReq);
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputLeft) {
      if (Focus > (INTN)STARTUP_COUNT) {
        Focus--;
      } else if (Focus == (INTN)STARTUP_COUNT) {
        Focus = (INTN)ITEM_COUNT - 1;
      } else {
        Focus = (INTN)ITEM_COUNT - 1;
      }
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputRight) {
      if (Focus >= (INTN)STARTUP_COUNT) {
        Focus = (INTN)STARTUP_COUNT + (((Focus - (INTN)STARTUP_COUNT) + 1) % (INTN)UTIL_COUNT);
      } else {
        Focus = (INTN)STARTUP_COUNT;
      }
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputUp) {
      Focus      = (Focus > 0) ? (Focus - 1) : ((INTN)ITEM_COUNT - 1);
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputDown) {
      Focus      = (Focus + 1) % (INTN)ITEM_COUNT;
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputEnter) {
      ActivateTile (&Ctx, (UINTN)Focus);
      NeedRedraw = TRUE;
    } else if (Ev.Kind == Np05jInputTap) {
      for (UINTN I = 0; I < ITEM_COUNT; I++) {
        if (Np05jRectContains (ItemRect[I], Ev.X, Ev.Y)) {
          Focus = (INTN)I;
          ActivateTile (&Ctx, I);
          NeedRedraw = TRUE;
          break;
        }
      }
    }
  }

  Np05jUiFree (&Ctx);
  return EFI_SUCCESS;
}
