/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Unlocked All-Options BIOS Configuration structure for nubia NP05J (SM8750).
**/

#ifndef _NP05J_BIOS_CONFIG_H_
#define _NP05J_BIOS_CONFIG_H_

#define NP05J_BIOS_CONFIG_MAGIC       0x4E504243U /* 'NPBC' */
#define NP05J_BIOS_CONFIG_VERSION     1U
#define NP05J_BIOS_CONFIG_VAR_NAME    L"Np05jBiosConfig"
#define NP05J_BIOS_CONFIG_FILE_A      L"\\NP05J\\bios-a.bin"
#define NP05J_BIOS_CONFIG_FILE_B      L"\\NP05J\\bios-b.bin"
#define NP05J_BIOS_FILE_MAGIC         0x4E504E56U /* 'NPNV' */

/* DefaultOs */
#define NP05J_OS_BOOT_MENU            0
#define NP05J_OS_WINDOWS_ARM          1
#define NP05J_OS_LINUX_DT             2
#define NP05J_OS_LINUX_ACPI           3
#define NP05J_OS_ANDROID              4
#define NP05J_OS_UEFI_SHELL           5

/* SplashStyle */
#define NP05J_SPLASH_SUNUEFI_VECTOR   0
#define NP05J_SPLASH_CLASSIC          1
#define NP05J_SPLASH_VERBOSE          2

/* CpuPerfProfile */
#define NP05J_CPU_PERF_MAX            0
#define NP05J_CPU_PERF_BALANCED       1
#define NP05J_CPU_PERF_POWERSAVE      2

/* FanMode */
#define NP05J_FAN_AUTO                0
#define NP05J_FAN_SILENT              1
#define NP05J_FAN_BALANCED            2
#define NP05J_FAN_TURBO               3
#define NP05J_FAN_MAX_100             4

/* SmmuPreOsMode */
#define NP05J_SMMU_DETACH_NON_MDP     0
#define NP05J_SMMU_KEEP_ALL           1
#define NP05J_SMMU_BYPASS_ALL         2

/* UsbMode */
#define NP05J_USB_CDC_SERIAL          0
#define NP05J_USB_FASTBOOT            1
#define NP05J_USB_MASS_STORAGE        2
#define NP05J_USB_HOST_OTG            3

/* OsCompatProfile */
#define NP05J_COMPAT_AUTO             0
#define NP05J_COMPAT_WINDOWS_11_ARM   1
#define NP05J_COMPAT_LINUX_DT         2
#define NP05J_COMPAT_LINUX_ACPI       3

/* AcpiTableProfile */
#define NP05J_ACPI_PROFILE_WOA_FULL   0
#define NP05J_ACPI_PROFILE_MINIMAL    1

/* AcpiSmmuIortMode */
#define NP05J_IORT_OMIT_FOR_WOA       0
#define NP05J_IORT_ENABLED            1
#define NP05J_IORT_BYPASS             2

/* SecureBootMode */
#define NP05J_SB_DISABLED             0
#define NP05J_SB_ENABLED              1
#define NP05J_SB_SETUP_MODE           2
#define NP05J_SB_CUSTOM               3

typedef enum {
  Np05jPresetDefault = 0,
  Np05jPresetWindowsOnArm,
  Np05jPresetMainlineLinux,
  Np05jPresetMax
} NP05J_BIOS_PRESET;

#pragma pack(1)
typedef struct {
  UINT32  Magic;
  UINT32  Version;
  UINT32  Size;
  UINT32  Crc32;
  UINT64  Sequence;

  /* 1. Boot Configuration */
  UINT8   DefaultOs;
  UINT8   BootTimeoutSec;
  UINT8   BootSelectTimeoutSec;
  UINT8   SplashStyle;
  UINT8   FastBoot;
  UINT8   AutoScanEsp;
  UINT8   WatchdogMinutes;
  UINT8   ReservedBoot;

  /* 2. CPU, Clock & Cooling */
  UINT8   CpuPerfProfile;
  UINT8   ActiveCoreMask;
  UINT8   FanMode;
  UINT8   ThermalLimitC;
  UINT8   ScmiHandoff;
  UINT8   ReservedCpu[3];

  /* 3. Memory & Storage */
  UINT8   RamLimitGb;         /* 0 = Full 24 GiB, else 4/8/12/16 */
  UINT8   UfsWriteProtect;    /* 1 = RO safe, 0 = RW unlocked */
  UINT8   UfsCacheCoherency;  /* 1 = Enabled */
  UINT8   SmmuPreOsMode;      /* 0..2 */
  UINT8   Pcie0Enable;        /* 0/1 */
  UINT8   ReservedMem[3];

  /* 4. Display, Console & Touch */
  UINT16  DisplayRotation;    /* 0, 90, 180, 270 */
  UINT8   ConsoleScale;       /* 1, 2, 3 */
  UINT8   DisplayDriverMode;  /* 0 = SimpleFB, 1 = QcomDisplayDxe */
  UINT8   TouchEnable;
  UINT8   TouchSwapXY;
  UINT8   TouchInvertX;
  UINT8   TouchInvertY;
  UINT8   TouchPollRateHz;    /* 50, 100, 120 */
  UINT8   VirtualKeyboardEnable;
  UINT8   ReservedDisp[2];

  /* 5. USB & Debug */
  UINT8   UsbMode;
  UINT8   UsbCRolePolicy;     /* 0 = Auto, 1 = Device, 2 = Host */
  UINT8   UsbPhySpeed;        /* 0 = SuperSpeed, 1 = HighSpeed */
  UINT8   DebugLogLevel;      /* 0..4 */
  UINT8   EarlyFbLogEnable;
  UINT8   ReservedUsb[3];

  /* 6. OS & Windows on ARM */
  UINT8   OsCompatProfile;
  UINT8   AcpiTableProfile;
  UINT8   KepEnable;
  UINT8   KepPatchActlr;
  UINT8   KepPatchAmcnten;
  UINT8   KepPatchGicIrm;
  UINT8   KepPatchPsciMemProt;
  UINT8   RuntimeServicesMode; /* 0 = Enabled, 1 = VirtualOnly */
  UINT8   AcpiSmmuIortMode;    /* 0 = Omit for WoA, 1 = Enabled, 2 = Bypass */
  UINT8   AcpiUsbEnable;
  UINT8   AcpiTouchEnable;
  UINT8   AcpiPcieEnable;
  UINT8   AcpiButtonsEnable;
  UINT8   DtbFixupEnable;
  UINT8   ReservedOs[2];

  /* 7. Security & NVRAM */
  UINT8   SecureBootMode;
  UINT8   AllowUnsignedEfi;
  UINT8   AllMenusUnlocked;   /* Always 1: full unlocked BIOS */
  UINT8   NvramPersistence;   /* 1 = Variable + Dual-Copy FAT */
  UINT8   ReservedSec[4];
} NP05J_BIOS_CONFIG;

typedef struct {
  UINT32            FileMagic;
  UINT32            FileVersion;
  UINT64            Sequence;
  UINT32            PayloadCrc32;
  UINT32            Reserved;
  NP05J_BIOS_CONFIG Config;
} NP05J_BIOS_FILE_IMAGE;
#pragma pack()

#endif /* _NP05J_BIOS_CONFIG_H_ */
