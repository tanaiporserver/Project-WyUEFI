/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Touch-native Unlocked All-Options BIOS Setup Utility for nubia NP05J.
  Exposes all 9 hardware, boot, display, touch, USB, ACPI/WoA, security,
  NVRAM, diagnostic, and power categories without hidden or locked menus.
**/

#include "Np05jBootMenuApp.h"

#define SETUP_CAT_COUNT   9U
#define ITEMS_PER_PAGE    7U

typedef enum {
  ItemKindToggle = 0,
  ItemKindEnum,
  ItemKindRange,
  ItemKindRotation,
  ItemKindPollRate,
  ItemKindRamLimit,
  ItemKindHexByte,
  ItemKindInfo,
  ItemKindAction
} SETUP_ITEM_KIND;

typedef enum {
  ActNone = 0,
  ActOpenTouchTest,
  ActOpenFileBrowser,
  ActOpenTianoSetup,
  ActOpenUefiShell,
  ActReprobeTouch,
  ActRebootNormal,
  ActRebootRecovery,
  ActRebootBootloader,
  ActRebootEdl9008,
  ActShutdown
} SETUP_ACTION_ID;

typedef struct {
  UINT8            Cat;
  SETUP_ITEM_KIND  Kind;
  CONST CHAR8     *Label;
  CONST CHAR8     *Help;
  UINTN            Offset;
  UINT32           Min;
  UINT32           Max;
  UINT32           Step;
  CONST CHAR8    **EnumNames;
  SETUP_ACTION_ID  Action;
} SETUP_ITEM_DESC;

STATIC CONST CHAR8 *mCatNames[SETUP_CAT_COUNT] = {
  "1. System Info",
  "2. Boot Config",
  "3. CPU & Cooling",
  "4. Mem & Storage",
  "5. Display/Touch",
  "6. USB & Debug",
  "7. OS & Win11 ARM",
  "8. Security/NVRAM",
  "9. Tools & Power"
};

STATIC CONST CHAR8 *mDefaultOsNames[]   = { "Boot Menu", "Win11 ARM", "Linux DT", "Linux ACPI", "Android", "UEFI Shell" };
STATIC CONST CHAR8 *mSplashNames[]      = { "SunUEFI Vector", "Classic Logo", "Verbose Log" };
STATIC CONST CHAR8 *mCpuPerfNames[]     = { "Max Performance", "Balanced", "Power Saver" };
STATIC CONST CHAR8 *mFanNames[]         = { "Auto Curve", "Silent", "Balanced", "Turbo", "Max 100%" };
STATIC CONST CHAR8 *mSmmuNames[]        = { "Detach Non-MDP", "Keep All", "Bypass All" };
STATIC CONST CHAR8 *mDispDrvNames[]     = { "SimpleFB (2400x1504)", "QcomDisplayDxe" };
STATIC CONST CHAR8 *mUsbModeNames[]     = { "Composite (UART+FB)", "SunUEFI Fastboot", "Mass Storage", "Host OTG" };
STATIC CONST CHAR8 *mUsbRoleNames[]     = { "Auto Dual-Role", "Force Device", "Force Host" };
STATIC CONST CHAR8 *mUsbSpeedNames[]    = { "SuperSpeed 10G", "HighSpeed 480M" };
STATIC CONST CHAR8 *mOsCompatNames[]    = { "Auto Detect", "Windows 11 ARM", "Linux DeviceTree", "Linux ACPI" };
STATIC CONST CHAR8 *mAcpiProfNames[]    = { "WoA Full Table Set", "Minimal Debug Set" };
STATIC CONST CHAR8 *mRtSvcNames[]       = { "Full Runtime Services", "Virtual Only" };
STATIC CONST CHAR8 *mIortNames[]        = { "Omit for WoA (Safe)", "Enabled (Linux/SMMU)", "Bypass Identity" };
STATIC CONST CHAR8 *mSecBootNames[]     = { "Disabled (Unlocked)", "Enabled (Enforcing)", "Setup Mode", "Custom DB/KEK" };

#define OFF(Field) ((UINTN)(&(((NP05J_BIOS_CONFIG *)0)->Field)))

STATIC CONST SETUP_ITEM_DESC mItems[] = {
  /* Category 0: System Info (read-only live telemetry + unlocked state) */
  { 0, ItemKindInfo,   "Device & SoC",         "nubia REDMAGIC Astra (NP05JHW1.0) | SM8750P 6+2 Oryon", 0, 0, 0, 0, NULL, ActNone },
  { 0, ItemKindInfo,   "System Memory",        "24 GiB LPDDR5x | UEFI FD 2.0 MiB | 2400x1504 OLED",     1, 0, 0, 0, NULL, ActNone },
  { 0, ItemKindInfo,   "RTC & PMIC Status",    "Hardware PMK8550 RTC + ChargerEx Battery Telemetry",    2, 0, 0, 0, NULL, ActNone },
  { 0, ItemKindInfo,   "Touch Controller",     "Synaptics S3930TB0A0 TCMv2 SPI4 (GPIOs 48..51,161,162)", 3, 0, 0, 0, NULL, ActNone },
  { 0, ItemKindInfo,   "Storage & ESP Count",  "UFS 4.0 LU0..5 + FAT/ESP volumes scanned at DXE",       4, 0, 0, 0, NULL, ActNone },
  { 0, ItemKindInfo,   "SunUEFI BIOS Status",  "ALL MENUS UNLOCKED | Dual-Copy CRC32 NVRAM+FAT",       5, 0, 0, 0, NULL, ActNone },
  { 0, ItemKindInfo,   "UEFI Fastboot",        "Project SunUEFI Fastboot 0.4 background USB service",  7, 0, 0, 0, NULL, ActNone },

  /* Category 1: Boot Configuration */
  { 1, ItemKindEnum,   "Default Boot Target",  "Target launched automatically after timeout expires",     OFF (DefaultOs),            0, 5,   1, mDefaultOsNames, ActNone },
  { 1, ItemKindRange,  "UEFI Splash Timeout",  "Seconds waiting at SunUEFI splash pills (0 = instant)", OFF (BootTimeoutSec),       0, 30,  1, NULL, ActNone },
  { 1, ItemKindRange,  "Pre-UEFI Timeout",     "Seconds waiting at Android/UEFI BootSelect screen",     OFF (BootSelectTimeoutSec), 0, 15,  1, NULL, ActNone },
  { 1, ItemKindEnum,   "Splash Screen Style",  "SunUEFI vector touch splash, classic, or verbose log",  OFF (SplashStyle),          0, 2,   1, mSplashNames, ActNone },
  { 1, ItemKindToggle, "Fast Boot",            "Skip extra device probing when booting default OS",     OFF (FastBoot),             0, 1,   1, NULL, ActNone },
  { 1, ItemKindToggle, "Auto-Scan ESP Volumes","Discover Windows/Linux EFI loaders across FS* volumes",  OFF (AutoScanEsp),          0, 1,   1, NULL, ActNone },
  { 1, ItemKindRange,  "Boot Watchdog (min)",  "Hardware watchdog failsafe timeout in minutes (0=off)", OFF (WatchdogMinutes),      0, 30,  1, NULL, ActNone },

  /* Category 2: CPU, Clock & Cooling */
  { 2, ItemKindEnum,   "CPU Perf Profile",     "Initial Oryon clock policy before OS takeover",         OFF (CpuPerfProfile),       0, 2,   1, mCpuPerfNames, ActNone },
  { 2, ItemKindHexByte,"Active Core Mask",     "CPU bitmask (0xFF = all 8 cores enabled)",            OFF (ActiveCoreMask),       1, 255, 1, NULL, ActNone },
  { 2, ItemKindEnum,   "NP05J Cooling Mode",   "Thermal dissipation / active cooling control policy",   OFF (FanMode),              0, 4,   1, mFanNames, ActNone },
  { 2, ItemKindRange,  "Thermal Throttle (C)", "SoC junction soft-throttle temperature target",         OFF (ThermalLimitC),        65, 110, 5, NULL, ActNone },
  { 2, ItemKindToggle, "SCMI Clock Handoff",   "Expose SCMI/PSCI domains cleanly to HLOS/Linux",        OFF (ScmiHandoff),          0, 1,   1, NULL, ActNone },

  /* Category 3: Memory & Storage */
  { 3, ItemKindRamLimit,"RAM Cap Override",    "0 = Full 24 GiB, or cap to 4/8/12/16 GiB for debug",    OFF (RamLimitGb),           0, 16,  4, NULL, ActNone },
  { 3, ItemKindToggle, "UFS Write Protect",    "Read-only safety lock on Android raw block LUNs",       OFF (UfsWriteProtect),      0, 1,   1, NULL, ActNone },
  { 3, ItemKindToggle, "UFS Cache Coherency",  "Publish _CCA=1 DMA coherency for UFS controller",       OFF (UfsCacheCoherency),    0, 1,   1, NULL, ActNone },
  { 3, ItemKindEnum,   "Pre-OS SMMU Policy",   "Configure SMMU context banks before ExitBootServices",  OFF (SmmuPreOsMode),        0, 2,   1, mSmmuNames, ActNone },
  { 3, ItemKindToggle, "PCIe0 Controller",     "Enable SM8750 PCIe Gen4 Root Complex before OS handoff", OFF (Pcie0Enable),          0, 1,   1, NULL, ActNone },

  /* Category 4: Display, Console & Touch */
  { 4, ItemKindRotation,"Display Rotation",    "Logical screen orientation (0 / 90 / 180 / 270 deg)",   OFF (DisplayRotation),      0, 270, 90, NULL, ActNone },
  { 4, ItemKindRange,  "Console Text Scale",   "1x=300x94, 2x=150x39, 3x=100x26 chars (next boot)",   OFF (ConsoleScale),         1, 3,   1, NULL, ActNone },
  { 4, ItemKindEnum,   "Display Driver Mode",  "SimpleFbDxe (2400x1504 GOP) or QcomDisplayDxe",         OFF (DisplayDriverMode),    0, 1,   1, mDispDrvNames, ActNone },
  { 4, ItemKindToggle, "Touchscreen Enabled",  "Enable Synaptics S3930 TCMv2 AbsolutePointer driver",   OFF (TouchEnable),          0, 1,   1, NULL, ActNone },
  { 4, ItemKindToggle, "Touch Swap X/Y",       "Swap portrait sensor axes to 2400x1504 landscape",      OFF (TouchSwapXY),          0, 1,   1, NULL, ActNone },
  { 4, ItemKindToggle, "Touch Invert X",       "Mirror horizontal touch axis",                          OFF (TouchInvertX),         0, 1,   1, NULL, ActNone },
  { 4, ItemKindToggle, "Touch Invert Y",       "Mirror vertical touch axis",                            OFF (TouchInvertY),         0, 1,   1, NULL, ActNone },
  { 4, ItemKindPollRate,"Touch Poll Rate (Hz)","TCMv2 SPI4 polling frequency (50 / 100 / 120 Hz)",      OFF (TouchPollRateHz),      50, 120, 50, NULL, ActNone },
  { 4, ItemKindToggle, "On-Screen Keyboard",   "Enable touch virtual keyboard helper in UEFI apps",     OFF (VirtualKeyboardEnable),0, 1,   1, NULL, ActNone },
  { 4, ItemKindAction, "Launch Touch Test",    "Open live canvas & 4-corner Synaptics S3930 test",      0, 0, 0, 0, NULL, ActOpenTouchTest },

  /* Category 5: USB & Debug */
  { 5, ItemKindEnum,   "UEFI USB Mode",        "USB CDC-ACM UART, Fastbootd, Mass Storage, or Host",    OFF (UsbMode),              0, 3,   1, mUsbModeNames, ActNone },
  { 5, ItemKindEnum,   "USB-C Role Policy",    "Force Type-C OTG host, device, or auto DRP detection",  OFF (UsbCRolePolicy),       0, 2,   1, mUsbRoleNames, ActNone },
  { 5, ItemKindEnum,   "USB PHY Max Speed",    "SuperSpeed 10Gbps vs HighSpeed 480Mbps fallback",       OFF (UsbPhySpeed),          0, 1,   1, mUsbSpeedNames, ActNone },
  { 5, ItemKindRange,  "Debug Log Level",      "0=Silent, 1=Error, 2=Warn, 3=Info, 4=Verbose",          OFF (DebugLogLevel),        0, 4,   1, NULL, ActNone },
  { 5, ItemKindToggle, "Early FB Log Overlay", "Stream early serial messages into framebuffer/ring",    OFF (EarlyFbLogEnable),     0, 1,   1, NULL, ActNone },

  /* Category 6: OS & Windows on ARM */
  { 6, ItemKindEnum,   "OS Compat Profile",    "Active HLOS preset for Windows 11 ARM or Linux",        OFF (OsCompatProfile),      0, 3,   1, mOsCompatNames, ActNone },
  { 6, ItemKindEnum,   "ACPI Table Profile",   "Select full WoA table set or minimal bring-up tables",  OFF (AcpiTableProfile),     0, 1,   1, mAcpiProfNames, ActNone },
  { 6, ItemKindToggle, "KernelErrataPatcher",  "Master toggle for Silicium Winload EL2 errata hooks",   OFF (KepEnable),            0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "Patch EL1 ACTLR",      "Enable Qualcomm CPU errata ACTLR_EL1 patch",            OFF (KepPatchActlr),        0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "Patch PMU AMCNTEN",    "Enable Windows ARM64 cycle counter access patch",       OFF (KepPatchAmcnten),      0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "Patch GICv3 IRM",      "Route GIC interrupts safely for Windows HAL",           OFF (KepPatchGicIrm),       0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "Patch PSCI MemProt",   "Bypass PSCI memory protection halt during Win11 boot",  OFF (KepPatchPsciMemProt),  0, 1,   1, NULL, ActNone },
  { 6, ItemKindEnum,   "UEFI Runtime Services","Full SetVirtualAddressMap vs virtual-only mode",        OFF (RuntimeServicesMode),  0, 1,   1, mRtSvcNames, ActNone },
  { 6, ItemKindEnum,   "ACPI IORT / SMMU",     "Omit IORT for safe WoA boot, or keep Enabled for Linux", OFF (AcpiSmmuIortMode),     0, 2,   1, mIortNames, ActNone },
  { 6, ItemKindToggle, "ACPI USB0 (QCOM0E08)", "Expose DWC3 USB controller in DSDT for Windows/Linux",  OFF (AcpiUsbEnable),        0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "ACPI Touch (SYNA3930)","Expose Synaptics S3930 SPI4 HID device in DSDT",       OFF (AcpiTouchEnable),      0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "ACPI PCIe (MCFG)",     "Keep MCFG / PCIe root complex active in ACPI",          OFF (AcpiPcieEnable),       0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "ACPI Buttons (PWRB)",  "Expose Power & Volume key array in DSDT",               OFF (AcpiButtonsEnable),    0, 1,   1, NULL, ActNone },
  { 6, ItemKindToggle, "DTB Fixup on Boot",    "Apply memory/framebuffer fixups when booting Linux DT", OFF (DtbFixupEnable),       0, 1,   1, NULL, ActNone },

  /* Category 7: Security & NVRAM */
  { 7, ItemKindEnum,   "Secure Boot Policy",   "Unlocked by default; configure PK/KEK/DB mode",         OFF (SecureBootMode),       0, 3,   1, mSecBootNames, ActNone },
  { 7, ItemKindToggle, "Allow Unsigned .EFI",  "Permit booting unsigned OS loaders, shells & tools",    OFF (AllowUnsignedEfi),     0, 1,   1, NULL, ActNone },
  { 7, ItemKindInfo,   "Unlocked Menus Flag",  "Always 1 (All 9 BIOS Setup categories unlocked)",       6, 0, 0, 0, NULL, ActNone },
  { 7, ItemKindToggle, "Dual-Copy Persistence","Save NVRAM variable + FAT bios-a.bin / bios-b.bin",     OFF (NvramPersistence),     0, 1,   1, NULL, ActNone },

  /* Category 8: Tools, Diagnostics & Power */
  { 8, ItemKindAction, "TianoCore Setup (UiApp)", "Launch genuine Project SunUEFI EDK2 SetupBrowser UI", 0, 0, 0, 0, NULL, ActOpenTianoSetup },
  { 8, ItemKindAction, "Touch Test & Calib",   "Live drawing canvas + 4-corner S3930 verification",     0, 0, 0, 0, NULL, ActOpenTouchTest },
  { 8, ItemKindAction, "Boot From File (.EFI)","Browse FAT/ESP partitions and launch any .efi binary",  0, 0, 0, 0, NULL, ActOpenFileBrowser },
  { 8, ItemKindAction, "Launch UEFI Shell",    "Start built-in EDK2 Shell from firmware volume",        0, 0, 0, 0, NULL, ActOpenUefiShell },
  { 8, ItemKindAction, "Re-Probe Touch IC",    "Power-cycle / reset Synaptics S3930 over SPI4",         0, 0, 0, 0, NULL, ActReprobeTouch },
  { 8, ItemKindAction, "Reboot Normal",        "Cold reset into default boot target",                   0, 0, 0, 0, NULL, ActRebootNormal },
  { 8, ItemKindAction, "Reboot Recovery",      "Write PMK8550 SDAM recovery reason & cold reset",       0, 0, 0, 0, NULL, ActRebootRecovery },
  { 8, ItemKindAction, "Reboot Bootloader",    "Write PMK8550 SDAM fastboot reason & cold reset",       0, 0, 0, 0, NULL, ActRebootBootloader },
  { 8, ItemKindAction, "Reboot EDL (9008)",    "Program TCSR download cookie via SCM & enter EDL 9008", 0, 0, 0, 0, NULL, ActRebootEdl9008 },
  { 8, ItemKindAction, "Shutdown System",      "Power off nubia NP05J completely",                     0, 0, 0, 0, NULL, ActShutdown }
};

STATIC
UINT8 *
FieldPtrU8 (
  IN OUT NP05J_BIOS_CONFIG *Cfg,
  IN     UINTN              Offset
  )
{
  return ((UINT8 *)Cfg) + Offset;
}

STATIC
UINT16 *
FieldPtrU16 (
  IN OUT NP05J_BIOS_CONFIG *Cfg,
  IN     UINTN              Offset
  )
{
  return (UINT16 *)(((UINT8 *)Cfg) + Offset);
}

STATIC
VOID
FormatItemValue (
  IN  NP05J_UI_CTX          *Ctx,
  IN  CONST SETUP_ITEM_DESC *Item,
  OUT CHAR8                 *Out,
  IN  UINTN                  OutSize
  )
{
  NP05J_BIOS_CONFIG *Cfg = &Ctx->Config;

  switch (Item->Kind) {
    case ItemKindToggle:
      AsciiSPrint (Out, OutSize, "%a", (*FieldPtrU8 (Cfg, Item->Offset) != 0U) ? "ENABLED" : "DISABLED");
      break;
    case ItemKindEnum:
    {
      UINT8 V = *FieldPtrU8 (Cfg, Item->Offset);
      if (V <= Item->Max) {
        AsciiSPrint (Out, OutSize, "%a", Item->EnumNames[V]);
      } else {
        AsciiSPrint (Out, OutSize, "%u", (UINT32)V);
      }
      break;
    }
    case ItemKindRange:
      AsciiSPrint (Out, OutSize, "%u", (UINT32)*FieldPtrU8 (Cfg, Item->Offset));
      break;
    case ItemKindRotation:
      AsciiSPrint (Out, OutSize, "%u deg", (UINT32)*FieldPtrU16 (Cfg, Item->Offset));
      break;
    case ItemKindPollRate:
      AsciiSPrint (Out, OutSize, "%u Hz", (UINT32)*FieldPtrU8 (Cfg, Item->Offset));
      break;
    case ItemKindRamLimit:
    {
      UINT8 R = *FieldPtrU8 (Cfg, Item->Offset);
      if (R == 0U) {
        AsciiSPrint (Out, OutSize, "Full 24 GiB");
      } else {
        AsciiSPrint (Out, OutSize, "%u GiB Cap", (UINT32)R);
      }
      break;
    }
    case ItemKindHexByte:
      AsciiSPrint (Out, OutSize, "0x%02x", (UINT32)*FieldPtrU8 (Cfg, Item->Offset));
      break;
    case ItemKindAction:
      AsciiSPrint (Out, OutSize, "EXECUTE >");
      break;
    case ItemKindInfo:
    {
      if (Item->Offset == 0U) {
        AsciiSPrint (Out, OutSize, "NP05J / SM8750P");
      } else if (Item->Offset == 1U) {
        AsciiSPrint (Out, OutSize, "24 GiB / 2400x1504");
      } else if (Item->Offset == 2U) {
        BOOLEAN Bat = FALSE;
        BOOLEAN Chg = FALSE;
        UINT32  Mv  = 0;
        Np05jQueryPowerStatus (&Bat, &Chg, &Mv);
        AsciiSPrint (Out, OutSize, "%umV %a", Mv, Chg ? "(USB+)" : "(BAT)");
      } else if (Item->Offset == 3U) {
        NP05J_TOUCH_INFO_PROTOCOL *Ti = NULL;
        NP05J_TOUCH_TELEMETRY      Tm;
        if (!EFI_ERROR (gBS->LocateProtocol (&gNp05jTouchInfoProtocolGuid, NULL, (VOID **)&Ti)) && (Ti != NULL)) {
          Ti->GetTelemetry (Ti, &Tm);
          AsciiSPrint (Out, OutSize, "%a (%u f)", Tm.Initialized ? Tm.PartNumber : "OFFLINE", Tm.FrameCount);
        } else {
          AsciiSPrint (Out, OutSize, "NO PROTOCOL");
        }
      } else if (Item->Offset == 4U) {
        AsciiSPrint (Out, OutSize, "%u FAT/ESP vol", (UINT32)Np05jCountFileSystems ());
      } else if (Item->Offset == 5U) {
        AsciiSPrint (Out, OutSize, "Seq #%lu CRC OK", Ctx->Config.Sequence);
      } else if (Item->Offset == 7U) {
        NP05J_FASTBOOT_STATUS FbSt;
        if ((Ctx->Fastboot != NULL) && !EFI_ERROR (Ctx->Fastboot->GetStatus (Ctx->Fastboot, &FbSt)) && FbSt.Configured) {
          CONST CHAR8 *Spd = "USB";
          switch (FbSt.BusSpeed) {
            case 1: Spd = "LS"; break;
            case 2: Spd = "FS"; break;
            case 3: Spd = "HS"; break;
            case 4: Spd = "SS"; break;
            case 5: Spd = "SSP"; break;
            default: break;
          }
          AsciiSPrint (Out, OutSize, "SunUEFI-np05j (%a, %u GPT)", Spd, FbSt.PartitionsFound);
        } else {
          AsciiSPrint (Out, OutSize, "SunUEFI-np05j (Waiting USB)");
        }
      } else {
        AsciiSPrint (Out, OutSize, "UNLOCKED");
      }
      break;
    }
  }
}

STATIC
VOID
AdjustItemValue (
  IN OUT NP05J_UI_CTX          *Ctx,
  IN     CONST SETUP_ITEM_DESC *Item,
  IN     INT32                  Dir
  )
{
  NP05J_BIOS_CONFIG *Cfg = &Ctx->Config;

  switch (Item->Kind) {
    case ItemKindToggle:
    {
      UINT8 *P = FieldPtrU8 (Cfg, Item->Offset);
      *P = (*P != 0U) ? 0U : 1U;
      Ctx->ConfigModified = TRUE;
      break;
    }
    case ItemKindEnum:
    case ItemKindRange:
    case ItemKindHexByte:
    {
      UINT8 *P   = FieldPtrU8 (Cfg, Item->Offset);
      INT32  Val = (INT32)(*P) + Dir * (INT32)Item->Step;
      if (Val < (INT32)Item->Min) {
        Val = (INT32)Item->Max;
      } else if (Val > (INT32)Item->Max) {
        Val = (INT32)Item->Min;
      }
      *P = (UINT8)Val;
      Ctx->ConfigModified = TRUE;
      break;
    }
    case ItemKindRotation:
    {
      UINT16 *P   = FieldPtrU16 (Cfg, Item->Offset);
      INT32   Val = (INT32)(*P) + Dir * 90;
      if (Val < 0) {
        Val = 270;
      } else if (Val > 270) {
        Val = 0;
      }
      *P = (UINT16)Val;
      Ctx->ConfigModified = TRUE;
      break;
    }
    case ItemKindPollRate:
    {
      UINT8 *P = FieldPtrU8 (Cfg, Item->Offset);
      if (*P <= 50U) {
        *P = (Dir > 0) ? 100U : 120U;
      } else if (*P <= 100U) {
        *P = (Dir > 0) ? 120U : 50U;
      } else {
        *P = (Dir > 0) ? 50U : 100U;
      }
      Ctx->ConfigModified = TRUE;
      break;
    }
    case ItemKindRamLimit:
    {
      UINT8 *P   = FieldPtrU8 (Cfg, Item->Offset);
      INT32  Val = (INT32)(*P) + Dir * 4;
      if (Val < 0) {
        Val = 16;
      } else if (Val > 16) {
        Val = 0;
      }
      *P = (UINT8)Val;
      Ctx->ConfigModified = TRUE;
      break;
    }
    default:
      break;
  }
}

STATIC
VOID
ApplyLiveTouchAndSave (
  IN OUT NP05J_UI_CTX *Ctx
  )
{
  NP05J_BIOS_CONFIG_PROTOCOL *CfgProto = NULL;
  NP05J_TOUCH_INFO_PROTOCOL  *Ti       = NULL;
  EFI_STATUS                  Status;

  if (!EFI_ERROR (gBS->LocateProtocol (&gNp05jBiosConfigProtocolGuid, NULL, (VOID **)&CfgProto)) &&
      (CfgProto != NULL) && (CfgProto->SetConfig != NULL))
  {
    Status = CfgProto->SetConfig (CfgProto, &Ctx->Config, TRUE);
  } else {
    Status = Np05jBiosConfigSave (&Ctx->Config);
  }

  Np05jBiosConfigLoad (&Ctx->Config);

  if (!EFI_ERROR (gBS->LocateProtocol (&gNp05jTouchInfoProtocolGuid, NULL, (VOID **)&Ti)) && (Ti != NULL)) {
    Ti->ApplyCalibration (
          Ti,
          Ctx->Config.TouchSwapXY,
          Ctx->Config.TouchInvertX,
          Ctx->Config.TouchInvertY,
          Ctx->Config.TouchPollRateHz
          );
  }

  Ctx->ConfigModified = FALSE;
  Np05jUiSetBanner (
    Ctx,
    EFI_ERROR (Status) ? COL_RED : COL_GREEN,
    "BIOS configuration saved to NVRAM + Dual-Copy FAT (%r)",
    Status
    );
}

STATIC
VOID
ExecuteSetupAction (
  IN OUT NP05J_UI_CTX    *Ctx,
  IN     SETUP_ACTION_ID  Action
  )
{
  switch (Action) {
    case ActOpenTouchTest:
      Np05jRunTouchTest (Ctx);
      break;
    case ActOpenFileBrowser:
      Np05jRunFileBrowser (Ctx);
      break;
    case ActOpenTianoSetup:
      Np05jLaunchTianoSetup (Ctx);
      break;
    case ActOpenUefiShell:
      Np05jLaunchEfiShell (Ctx);
      break;
    case ActReprobeTouch:
    {
      NP05J_TOUCH_INFO_PROTOCOL *Ti = NULL;
      if (!EFI_ERROR (gBS->LocateProtocol (&gNp05jTouchInfoProtocolGuid, NULL, (VOID **)&Ti)) && (Ti != NULL)) {
        EFI_STATUS St = Ti->Reinit (Ti);
        Np05jUiRefreshHandles (Ctx);
        Np05jUiSetBanner (Ctx, EFI_ERROR (St) ? COL_RED : COL_GREEN, "Touch IC Re-Probe: %r", St);
      }
      break;
    }
    case ActRebootNormal:
      Np05jExecuteReset (Np05jResetNormal);
      break;
    case ActRebootRecovery:
      Np05jExecuteReset (Np05jResetRecovery);
      break;
    case ActRebootBootloader:
      Np05jExecuteReset (Np05jResetBootloader);
      break;
    case ActRebootEdl9008:
    {
      EFI_STATUS St = Np05jExecuteReset (Np05jResetEdl9008);
      Np05jUiSetBanner (Ctx, COL_RED, "EDL 9008 failed (%r)", St);
      break;
    }
    case ActShutdown:
      Np05jExecuteReset (Np05jResetShutdown);
      break;
    default:
      break;
  }
}

VOID
Np05jRunBiosSetup (
  IN OUT NP05J_UI_CTX *Ctx,
  OUT    BOOLEAN      *RequestReboot
  )
{
  UINT8   CurCat     = 0;
  UINTN   CatScroll  = 0;
  INTN    Selected   = 0;
  BOOLEAN NeedRedraw = TRUE;

  if (RequestReboot != NULL) {
    *RequestReboot = FALSE;
  }

  Np05jUiDrainInput (Ctx);

  while (TRUE) {
    CONST SETUP_ITEM_DESC *CatItems[32];
    UINTN                  CatCount = 0;
    NP05J_RECT             CatBtns[SETUP_CAT_COUNT];
    NP05J_RECT             MinusRect[ITEMS_PER_PAGE];
    NP05J_RECT             ValRect[ITEMS_PER_PAGE];
    NP05J_RECT             PlusRect[ITEMS_PER_PAGE];
    NP05J_RECT             RowRect[ITEMS_PER_PAGE];
    NP05J_RECT             BtnPgUp;
    NP05J_RECT             BtnPgDn;
    NP05J_RECT             BottomBtns[6];
    NP05J_INPUT_EVENT      Ev;

    for (UINTN I = 0; (I < ARRAY_SIZE (mItems)) && (CatCount < 32U); I++) {
      if (mItems[I].Cat == CurCat) {
        CatItems[CatCount++] = &mItems[I];
      }
    }
    if ((Selected < 0) || ((UINTN)Selected >= CatCount)) {
      Selected = (CatCount > 0U) ? 0 : -1;
    }

    BtnPgUp = (NP05J_RECT){ 438, 138, 220, 56 };
    BtnPgDn = (NP05J_RECT){ 674, 138, 220, 56 };

    /* Bottom toolbar buttons */
    {
      INT32 By = (INT32)Ctx->ScreenH - 96;
      INT32 Bx = 42;
      INT32 W  = 372;
      for (UINTN B = 0; B < 6U; B++) {
        BottomBtns[B] = (NP05J_RECT){ Bx + (INT32)B * (W + 16), By, W, 72 };
      }
    }

    if (NeedRedraw) {
      CHAR8 Sub[96];

      Np05jUiClear (Ctx, COL_SURFACE);
      AsciiSPrint (
        Sub,
        sizeof (Sub),
        "ALL MENUS UNLOCKED | Config: %a | Tap tabs & controls or Vol+/Vol-/Power",
        Ctx->ConfigModified ? "MODIFIED (Unsaved)" : "SYNCED"
        );
      Np05jUiDrawHeader (Ctx, "SUNUEFI UNLOCKED BIOS SETUP (NP05J)", Sub);

      /* Left sidebar rail: SunUEFI #EAF0EE surface + #D7EAE3 active pill */
      Np05jUiDrawRoundedCard (Ctx, (NP05J_RECT){ 28, 134, 384, (INT32)Ctx->ScreenH - 258 }, 16, COL_RAIL, COL_BORDER, 2);
      for (UINTN C = 0; C < SETUP_CAT_COUNT; C++) {
        CatBtns[C] = (NP05J_RECT){ 44, 152 + (INT32)C * 116, 352, 98 };
        {
          BOOLEAN Active = (C == CurCat);
          Np05jUiDrawRoundedCard (
            Ctx,
            CatBtns[C],
            14,
            Active ? COL_RAIL_HI : COL_PANEL,
            Active ? COL_GREEN : COL_BORDER,
            Active ? 3 : 2
            );
          Np05jUiDrawText5x7Centered (Ctx, CatBtns[C], 2U, Active ? COL_GREEN : COL_TEXT, mCatNames[C]);
        }
      }

      /* Right category header + page controls */
      Np05jUiDrawRoundedCard (Ctx, (NP05J_RECT){ 432, 134, (INT32)Ctx->ScreenW - 460, 64 }, 14, COL_PANEL, COL_BORDER, 2);
      Np05jUiDrawRoundedCard (Ctx, BtnPgUp, 12, COL_SURFACE, COL_BORDER, 2);
      Np05jUiDrawText5x7Centered (Ctx, BtnPgUp, 2U, COL_TEXT, "< Prev");
      Np05jUiDrawRoundedCard (Ctx, BtnPgDn, 12, COL_SURFACE, COL_BORDER, 2);
      Np05jUiDrawText5x7Centered (Ctx, BtnPgDn, 2U, COL_TEXT, "Next >");

      {
        CHAR8 PgStr[64];
        AsciiSPrint (PgStr, sizeof (PgStr), "%a  (Items %u-%u of %u)",
          mCatNames[CurCat],
          (UINT32)(CatScroll + 1U),
          (UINT32)(((CatScroll + ITEMS_PER_PAGE) < CatCount) ? (CatScroll + ITEMS_PER_PAGE) : CatCount),
          (UINT32)CatCount);
        Np05jUiDrawText5x7 (Ctx, 920, 158, 2U, COL_GOLD_SHADE, PgStr);
      }

      /* Right option cards */
      for (UINTN R = 0; R < ITEMS_PER_PAGE; R++) {
        UINTN Idx = CatScroll + R;
        if (Idx >= CatCount) {
          break;
        }
        RowRect[R] = (NP05J_RECT){ 432, 212 + (INT32)R * 144, (INT32)Ctx->ScreenW - 460, 128 };
        MinusRect[R] = (NP05J_RECT){ RowRect[R].X + RowRect[R].W - 600, RowRect[R].Y + 26, 92, 76 };
        ValRect[R]   = (NP05J_RECT){ RowRect[R].X + RowRect[R].W - 498, RowRect[R].Y + 26, 376, 76 };
        PlusRect[R]  = (NP05J_RECT){ RowRect[R].X + RowRect[R].W - 112, RowRect[R].Y + 26, 92, 76 };

        {
          CONST SETUP_ITEM_DESC *It    = CatItems[Idx];
          BOOLEAN                IsSel = ((INTN)Idx == Selected);
          CHAR8                  ValStr[64];

          Np05jUiDrawRoundedCard (
            Ctx,
            RowRect[R],
            14,
            IsSel ? COL_CARD_HI : COL_PANEL,
            IsSel ? COL_ACCENT : COL_BORDER,
            IsSel ? 3 : 2
            );
          Np05jUiDrawText5x7 (Ctx, RowRect[R].X + 24, RowRect[R].Y + 28, 3U, IsSel ? COL_ACCENT_DARK : COL_TEXT, It->Label);
          Np05jUiDrawText5x7 (Ctx, RowRect[R].X + 24, RowRect[R].Y + 76, 2U, COL_SUBTEXT, It->Help);

          FormatItemValue (Ctx, It, ValStr, sizeof (ValStr));

          if ((It->Kind == ItemKindInfo) || (It->Kind == ItemKindAction) || (It->Kind == ItemKindToggle)) {
            NP05J_RECT WideBtn = { RowRect[R].X + RowRect[R].W - 480, RowRect[R].Y + 26, 456, 76 };
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL BFill  = COL_SURFACE;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL BColor = COL_BORDER;
            EFI_GRAPHICS_OUTPUT_BLT_PIXEL TColor = COL_TEXT;

            if (It->Kind == ItemKindToggle) {
              BOOLEAN On = (*FieldPtrU8 (&Ctx->Config, It->Offset) != 0U);
              BFill  = On ? COL_GREEN_BG : COL_RED_BG;
              BColor = On ? COL_GREEN : COL_RED;
              TColor = On ? COL_GREEN : COL_RED;
            } else if (It->Kind == ItemKindAction) {
              BFill  = COL_CARD_HI;
              BColor = COL_ACCENT;
              TColor = COL_ACCENT_DARK;
            } else {
              BFill  = COL_SURFACE;
              BColor = COL_BORDER;
              TColor = COL_ACCENT_DARK;
            }

            ValRect[R]   = WideBtn;
            MinusRect[R] = (NP05J_RECT){ -10, -10, 0, 0 };
            PlusRect[R]  = (NP05J_RECT){ -10, -10, 0, 0 };
            Np05jUiDrawRoundedCard (Ctx, WideBtn, 12, BFill, BColor, 2);
            Np05jUiDrawText5x7Centered (Ctx, WideBtn, 2U, TColor, ValStr);
          } else {
            Np05jUiDrawRoundedCard (Ctx, MinusRect[R], 12, COL_SURFACE, COL_ACCENT, 2);
            Np05jUiDrawText5x7Centered (Ctx, MinusRect[R], 3U, COL_ACCENT, "-");
            Np05jUiDrawRoundedCard (Ctx, ValRect[R], 12, COL_SURFACE, COL_BORDER, 2);
            Np05jUiDrawText5x7Centered (Ctx, ValRect[R], 2U, COL_GOLD_SHADE, ValStr);
            Np05jUiDrawRoundedCard (Ctx, PlusRect[R], 12, COL_SURFACE, COL_ACCENT, 2);
            Np05jUiDrawText5x7Centered (Ctx, PlusRect[R], 3U, COL_ACCENT, "+");
          }
        }
      }

      /* Bottom actions with SunUEFI emerald-teal primary CTAs */
      {
        STATIC CONST CHAR8 *BtnLbl[6] = {
          "Preset: Win11 ARM",
          "Preset: Linux DT",
          "Reset Defaults",
          "Save & Apply",
          "Save & Reboot",
          "< Back to Menu"
        };
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL BtnFill[6] = {
          COL_PANEL, COL_PANEL, COL_PANEL, COL_GREEN, COL_GREEN, COL_RED_BG
        };
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL BtnBdr[6] = {
          COL_ACCENT, COL_PURPLE, COL_GOLD_SHADE, COL_GREEN, COL_GREEN, COL_RED
        };
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL BtnInk[6] = {
          COL_ACCENT_DARK, COL_PURPLE, COL_GOLD_SHADE, COL_WHITE, COL_WHITE, COL_RED
        };
        for (UINTN B = 0; B < 6U; B++) {
          Np05jUiDrawRoundedCard (Ctx, BottomBtns[B], 14, BtnFill[B], BtnBdr[B], 2);
          Np05jUiDrawText5x7Centered (Ctx, BottomBtns[B], 2U, BtnInk[B], BtnLbl[B]);
        }
      }

      if (Ctx->StatusValid) {
        /* Compact status bar above bottom row when active */
        Np05jUiDrawRoundedCard (Ctx, (NP05J_RECT){ 432, (INT32)Ctx->ScreenH - 166, (INT32)Ctx->ScreenW - 460, 54 }, 12, COL_PANEL, Ctx->StatusColor, 2);
        Np05jUiDrawText5x7Centered (Ctx, (NP05J_RECT){ 432, (INT32)Ctx->ScreenH - 166, (INT32)Ctx->ScreenW - 460, 54 }, 2U, Ctx->StatusColor, Ctx->StatusBanner);
      }

      Np05jUiPresent (Ctx);
      NeedRedraw = FALSE;
    }

    if (EFI_ERROR (Np05jUiPollInput (Ctx, 100, &Ev))) {
      continue;
    }

    if (Ev.Kind == Np05jInputFastbootAction) {
      if (Ev.FbAction == Np05jFbActionOpenBootMenu) {
        return;
      }
      if (Ev.FbAction == Np05jFbActionOpenShell) {
        Np05jLaunchEfiShell (Ctx);
        NeedRedraw = TRUE;
        continue;
      }
      if ((Ev.FbAction == Np05jFbActionReboot) || (Ev.FbAction == Np05jFbActionContinue)) {
        if (Ctx->ConfigModified) {
          ApplyLiveTouchAndSave (Ctx);
        }
        Np05jExecuteReset (Np05jResetNormal);
        return;
      }
      if (Ev.FbAction == Np05jFbActionBootStable) {
        if (EFI_ERROR (Np05jLaunchLinux (Ctx, FALSE))) {
          Np05jLaunchWindowsArm (Ctx);
        }
        NeedRedraw = TRUE;
        continue;
      }
      NeedRedraw = TRUE;
      continue;
    }

    if (Ev.Kind == Np05jInputBack) {
      return;
    }

    if (Ev.Kind == Np05jInputUp) {
      if (Selected > 0) {
        Selected--;
        if ((UINTN)Selected < CatScroll) {
          CatScroll = (UINTN)Selected;
        }
      } else if (CurCat > 0U) {
        CurCat--;
        CatScroll = 0;
        Selected  = 0;
      }
      NeedRedraw = TRUE;
      continue;
    }

    if (Ev.Kind == Np05jInputDown) {
      if ((Selected >= 0) && ((UINTN)(Selected + 1) < CatCount)) {
        Selected++;
        if ((UINTN)Selected >= (CatScroll + ITEMS_PER_PAGE)) {
          CatScroll = (UINTN)Selected - ITEMS_PER_PAGE + 1U;
        }
      } else if ((CurCat + 1U) < SETUP_CAT_COUNT) {
        CurCat++;
        CatScroll = 0;
        Selected  = 0;
      }
      NeedRedraw = TRUE;
      continue;
    }

    if (Ev.Kind == Np05jInputLeft) {
      if ((Selected >= 0) && ((UINTN)Selected < CatCount)) {
        AdjustItemValue (Ctx, CatItems[Selected], -1);
        NeedRedraw = TRUE;
      }
      continue;
    }

    if (Ev.Kind == Np05jInputRight) {
      if ((Selected >= 0) && ((UINTN)Selected < CatCount)) {
        AdjustItemValue (Ctx, CatItems[Selected], 1);
        NeedRedraw = TRUE;
      }
      continue;
    }

    if (Ev.Kind == Np05jInputEnter) {
      if ((Selected >= 0) && ((UINTN)Selected < CatCount)) {
        CONST SETUP_ITEM_DESC *It = CatItems[Selected];
        if (It->Kind == ItemKindAction) {
          ExecuteSetupAction (Ctx, It->Action);
        } else {
          AdjustItemValue (Ctx, It, 1);
        }
        NeedRedraw = TRUE;
      }
      continue;
    }

    if (Ev.Kind == Np05jInputTap) {
      /* Category sidebar tap */
      for (UINTN C = 0; C < SETUP_CAT_COUNT; C++) {
        if (Np05jRectContains (CatBtns[C], Ev.X, Ev.Y)) {
          CurCat     = (UINT8)C;
          CatScroll  = 0;
          Selected   = 0;
          NeedRedraw = TRUE;
          break;
        }
      }
      if (NeedRedraw) {
        continue;
      }

      if (Np05jRectContains (BtnPgUp, Ev.X, Ev.Y)) {
        CatScroll  = (CatScroll >= ITEMS_PER_PAGE) ? (CatScroll - ITEMS_PER_PAGE) : 0U;
        NeedRedraw = TRUE;
        continue;
      }
      if (Np05jRectContains (BtnPgDn, Ev.X, Ev.Y)) {
        if ((CatScroll + ITEMS_PER_PAGE) < CatCount) {
          CatScroll += ITEMS_PER_PAGE;
        }
        NeedRedraw = TRUE;
        continue;
      }

      /* Bottom toolbar */
      if (Np05jRectContains (BottomBtns[0], Ev.X, Ev.Y)) {
        Np05jBiosConfigApplyPreset (&Ctx->Config, Np05jPresetWindowsOnArm);
        Ctx->ConfigModified = TRUE;
        Np05jUiSetBanner (Ctx, COL_ACCENT, "Applied Win11 ARM preset (Save & Apply to persist)");
        NeedRedraw = TRUE;
        continue;
      }
      if (Np05jRectContains (BottomBtns[1], Ev.X, Ev.Y)) {
        Np05jBiosConfigApplyPreset (&Ctx->Config, Np05jPresetMainlineLinux);
        Ctx->ConfigModified = TRUE;
        Np05jUiSetBanner (Ctx, COL_PURPLE, "Applied Mainline Linux DT preset (Save & Apply to persist)");
        NeedRedraw = TRUE;
        continue;
      }
      if (Np05jRectContains (BottomBtns[2], Ev.X, Ev.Y)) {
        Np05jBiosConfigReset (&Ctx->Config);
        Ctx->ConfigModified = FALSE;
        Np05jUiSetBanner (Ctx, COL_GOLD, "Reset all BIOS options to defaults and persisted");
        NeedRedraw = TRUE;
        continue;
      }
      if (Np05jRectContains (BottomBtns[3], Ev.X, Ev.Y)) {
        ApplyLiveTouchAndSave (Ctx);
        NeedRedraw = TRUE;
        continue;
      }
      if (Np05jRectContains (BottomBtns[4], Ev.X, Ev.Y)) {
        ApplyLiveTouchAndSave (Ctx);
        if (RequestReboot != NULL) {
          *RequestReboot = TRUE;
        }
        Np05jExecuteReset (Np05jResetNormal);
        return;
      }
      if (Np05jRectContains (BottomBtns[5], Ev.X, Ev.Y)) {
        return;
      }

      /* Row option tap */
      for (UINTN R = 0; (R < ITEMS_PER_PAGE) && ((CatScroll + R) < CatCount); R++) {
        if (Np05jRectContains (RowRect[R], Ev.X, Ev.Y)) {
          CONST SETUP_ITEM_DESC *It = CatItems[CatScroll + R];
          Selected = (INTN)(CatScroll + R);
          if (It->Kind == ItemKindAction) {
            if (Np05jRectContains (ValRect[R], Ev.X, Ev.Y) || Np05jRectContains (RowRect[R], Ev.X, Ev.Y)) {
              ExecuteSetupAction (Ctx, It->Action);
            }
          } else if (Np05jRectContains (MinusRect[R], Ev.X, Ev.Y)) {
            AdjustItemValue (Ctx, It, -1);
          } else if (Np05jRectContains (PlusRect[R], Ev.X, Ev.Y) || Np05jRectContains (ValRect[R], Ev.X, Ev.Y)) {
            AdjustItemValue (Ctx, It, 1);
          }
          NeedRedraw = TRUE;
          break;
        }
      }
    }
  }
}
