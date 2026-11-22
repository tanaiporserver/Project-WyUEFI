##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = nubia_np05j
  PLATFORM_GUID                  = FEE83493-49FB-57BC-B951-74F52963EFCF
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/nubia_np05jPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = nubia_np05jPkg/nubia_np05j.fdf
  # NP05JHW1.0, SoC 639 rev 2.0: six CPUs at 3.5328 GHz and two at 4.32 GHz.
  SOC_TYPE                       = 0
  USE_CUSTOM_DISPLAY_DRIVER      = 0

!include PakalaPkg/PakalaPkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"nubia"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"NP05J"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"REDMAGIC Astra"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"PQ84P01"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"NP05JHW1.0"

  #
  # Early debug console geometry from the selected panel timing.
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|2400
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|1504
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

  #
  # Serial Terminal (VT-UTF8 over USB CDC-ACM)
  #
  gEfiMdePkgTokenSpaceGuid.PcdDefaultTerminalType|3

  #
  # SunUEFI Boot Menu & Input overrides
  #
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVersionString|L"4.1-SunUEFI-v5"
  gEfiMdeModulePkgTokenSpaceGuid.PcdBootManagerMenuFile|{ 0x03, 0x50, 0xE4, 0xA1, 0x1C, 0x9B, 0x2A, 0x4F, 0x8C, 0x3D, 0x10, 0x05, 0x4E, 0x50, 0x30, 0x35 }
  gEfiMdeModulePkgTokenSpaceGuid.PcdConInConnectOnDemand|FALSE
  gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE

[PcdsDynamicDefault]
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoHorizontalResolution|2400
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupVideoVerticalResolution|1504
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|80
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|25
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutColumn|80
  gEfiMdeModulePkgTokenSpaceGuid.PcdSetupConOutRow|25

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|nubia_np05jPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|nubia_np05jPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

  #
  # Omit Qualcomm MassStorage FV boot option (MassStorage.inf is not packed in nubia_np05j.fdf)
  #
  PlatformBootOptionsLib|SiliciumNullPkg/Library/PlatformBootOptionsLibNull/PlatformBootOptionsLibNull.inf

  #
  # NP05J SunUEFI Libraries (Touch, Splash, Unlocked BIOS Config, Hardware Reset, RTC, WoA ACPI)
  #
  Np05jBiosConfigLib|nubia_np05jPkg/Library/Np05jBiosConfigLib/Np05jBiosConfigLib.inf
  Np05jSplashLib|nubia_np05jPkg/Library/Np05jSplashLib/Np05jSplashLib.inf
  Np05jResetLib|nubia_np05jPkg/Library/Np05jResetLib/Np05jResetLib.inf
  RealTimeClockLib|nubia_np05jPkg/Library/Np05jRealTimeClockLib/Np05jRealTimeClockLib.inf
  PlatformBootManagerLib|nubia_np05jPkg/Library/Np05jBootManagerLib/Np05jBootManagerLib.inf
  AcpiTableUpdateLib|nubia_np05jPkg/Library/Np05jAcpiUpdateLib/Np05jAcpiUpdateLib.inf
  CustomizedDisplayLib|MdeModulePkg/Library/CustomizedDisplayLib/CustomizedDisplayLib.inf
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf

  #
  # UEFI Shell Support Libraries
  #
  OrderedCollectionLib|MdePkg/Library/BaseOrderedCollectionRedBlackTreeLib/BaseOrderedCollectionRedBlackTreeLib.inf
  HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
  BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
  ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf

  #
  # Dual-sink SerialPortLib (framebuffer + 256 KiB RAM ring for USB UART)
  #
!if $(TARGET) == DEBUG
  SerialPortLib|nubia_np05jPkg/Library/Np05jSerialPortLib/Np05jSerialPortLib.inf
!endif

[LibraryClasses.common.UEFI_DRIVER, LibraryClasses.common.UEFI_APPLICATION]
  # ใช้ BaseMemoryLibOptDxe (ARM64 NEON/LDP-STP) สำหรับโมดูลที่รันหลังเปิด MMU+FP แล้ว
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf

[BuildOptions]
  # ปรับแต่งโค้ด ARM64 สำหรับ Qualcomm Oryon (SM8750P)
  CLANGPDB:*_*_AARCH64_CC_FLAGS = -O2 -march=armv8.7-a+crypto -mtune=oryon-1

[Components]
  #
  # ACPI Tables
  #
  nubia_np05j/AcpiTables.inf

  #
  # USB CDC-ACM Virtual Serial Console & Low-Level Debug UART
  #
  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  nubia_np05jPkg/Drivers/Np05jUsbSerialDxe/Np05jUsbSerialDxe.inf

  #
  # SunUEFI Touch, Scaled Console, Unlocked BIOS Config & USB Pointer Drivers
  #
  nubia_np05jPkg/Drivers/Np05jBiosConfigDxe/Np05jBiosConfigDxe.inf
  nubia_np05jPkg/Drivers/Np05jTouchDxe/Np05jTouchDxe.inf
  nubia_np05jPkg/Drivers/Np05jGraphicsConsoleDxe/GraphicsConsoleDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseAbsolutePointerDxe/UsbMouseAbsolutePointerDxe.inf

  #
  # Touch-First UEFI Boot Menu / Unlocked BIOS Setup & Built-in UEFI Shell
  #
  nubia_np05jPkg/Application/Np05jBootMenuApp/Np05jBootMenuApp.inf
  MdeModulePkg/Universal/DisplayEngineDxe/DisplayEngineDxe.inf
  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }
  ShellPkg/Application/Shell/Shell.inf {
    <PcdsFixedAtBuild>
      gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE
    <LibraryClasses>
      NULL|ShellPkg/Library/UefiShellLevel2CommandsLib/UefiShellLevel2CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel1CommandsLib/UefiShellLevel1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel3CommandsLib/UefiShellLevel3CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDriver1CommandsLib/UefiShellDriver1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellInstall1CommandsLib/UefiShellInstall1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDebug1CommandsLib/UefiShellDebug1CommandsLib.inf
  }

!if $(TARGET) == DEBUG
!if $(NP05J_SPMI_DEBUGGER) >= 1 AND $(NP05J_SPMI_DEBUGGER) <= 12
  nubia_np05jPkg/Drivers/Np05jSpmiDebuggerDxe/Np05jSpmiDebuggerDxe.inf
!endif
  # Standalone diagnostic for the first BDS session.
  nubia_np05jPkg/Application/Np05jProbe/Np05jProbe.inf
!endif
