# nubia NP05J (REDMAGIC Astra / SM8750P) — SunUEFI Hybrid & Windows on ARM Port

This package adapts components from [Project SunUEFI](https://github.com/slwyts/Project-SunUEFI) onto Mu-Silicium for **nubia NP05J (REDMAGIC Astra, Qualcomm SM8750P / Pakala, 24 GiB LPDDR5, 2400×1504 OLED)**.

## Implemented Components

### 1. Pre-UEFI `boot-selection` & `RelocShim` (`Tools/BootSelect/`)
- **Linux ARM64 Image Entry (`Entry.S`)** + **Position-Independent Relocator (`RelocShim.S`)** with `dc cvau` / `ic iallu` cache coherence.
- **`SPLITv1` / `NEXTv1` / `RSTRv1` container & handoff (`BootRequest.c/.h`, `Include/Np05jBootHandoff.h`)** writing `SUNUEFI-EMBEDv1` v2 to `0xA7FFF100`.
- **Pre-UEFI Synaptics `S3930TB0A0` TCM v2 touch (`EarlyTouch.c`)** over TLMM GPIO-SPI4 (`48..51`, RST `161`, IRQ `162`) with save/restore of pin mux before OS launch, plus **SPMI v7 keys (`EarlyKeys.c`)** (`0x013`/`0x18D`).
- **2400×1504 Direct Framebuffer Cards (`EarlySplash.c`)**: **ANDROID**, **UEFI BOOT MENU**, **BIOS SETUP**.

### 2. End-to-End UEFI Touch, Console & Splash Stack
- **`Drivers/Np05jTouchDxe`**: Synaptics S3930TB0A0 TCM v2 driver with RPMh (`ldob4` 1.8V / `ldob14` 3.2V) voting, dynamic TCM2 report config parser + primary-finger hold (`Include/Np05jSynapticsTcm2.h`), `EFI_ABSOLUTE_POINTER_PROTOCOL`, `NP05J_TOUCH_INFO_PROTOCOL` telemetry/calibration, and `ExitBootServices` hardware restore.
- **`UsbMouseDxe` & `UsbMouseAbsolutePointerDxe`**: External USB mouse & touch pointer support.
- **`Library/Np05jSplashLib`**: High-DPI vector TianoCore splash + progress bar + touch `[BOOT MENU]` / `[BIOS SETUP]` pill hitboxes + ACPI BGRT via `EDKII_BOOT_LOGO2_PROTOCOL`.
- **`Drivers/Np05jGraphicsConsoleDxe`**: Scaled logical console (`1x`–`3x`, default `2x` -> `1200×752`) leaving native `2400×1504` GOP untouched for apps and OS handoff.

### 3. Touch-First Boot Menu & Unlocked All-Options BIOS Setup (`Application/Np05jBootMenuApp`)
- **Boot Menu (`Np05jBootMenu.c`)**: 3×3 touch tiles (**Windows 11 ARM**, **Linux (DeviceTree)**, **Linux (ACPI)**, **Android Boot**, **BIOS Setup**, **Boot From File**, **UEFI Shell**, **Touch Test**, **Power & Reboot**) with `Np05jBootManagerLib` automatic fallback so failed boots always return to the touch menu.
- **Unlocked BIOS Setup (`Np05jBiosSetup.c`)**: All 9 categories unlocked (**System Info**, **Boot Config**, **CPU & Cooling**, **Mem & Storage**, **Display/Touch**, **USB & Debug**, **OS & Win11 ARM**, **Security/NVRAM**, **Tools & Power**) with presets and dual-copy CRC32 persistence (`Np05jBiosConfigLib` / `Np05jBiosConfigDxe`: NVRAM variable + FAT `\NP05J\bios-a.bin` / `bios-b.bin`).
- **Interactive Tools**: `Np05jTouchTest.c` (live draw canvas + 4-corner verify + calibration), `Np05jFileBrowser.c` (`.efi` launcher across `FS*`), built-in `Shell.inf`, and hardware resets (`Np05jResetLib` for Recovery/Bootloader/EDL 9008).

### 4. Windows on ARM (WoA) Enhancements
- **`Library/Np05jRealTimeClockLib`**: PMK8550 RTC (`SPMI PPID 0x061`) + ARM generic timer progression avoiding invalid `Day=0`.
- **ACPI (`Silicium-ACPI/Platforms/ZTE/nubia_np05j/`)**: Expanded `DSDT.asl` (`CPU0..7`, `UFS0`, `GIO0`, `UARD`, `USB0`, `SPI4` + `SYNA3930`/`PNP0C50` `TPL0`, `PWRB`, `BTNS` with patchable AML gates) plus `MCFG.dsl`, `SPCR.dsl`, and `DBG2.dsl`.
- **`Library/Np05jAcpiUpdateLib`**: Runtime DSDT variable patching and configurable `IORT` removal (`AcpiSmmuIortMode == 0`) / `MCFG` removal.

### 5. Android Boot Helper Preview (`AndroidHelper/`)
- **`native/np05j-boot-repack.c`**: Lossless Android Boot v4 `inspect`, `repack` (`SPLITv1`), `request` (`NEXTv1`), `clear-request`, and SHA-256 verified `restore` (`RSTRv1`), compiled for both host and static AArch64.
- **`module/` & `build_helper_module.py`**: KernelSU / SukiSU / APatch / Magisk module + EN/TH/ZH WebUI packaged into `Build/nubia_np05jPkg/NP05J-SunUEFI-Helper.zip`.

## Build & Host Verification Commands

```bash
# UEFI (3.0 MiB FD) DEBUG & RELEASE
python3 build_uefi.py -d nubia_np05j -r DEBUG
python3 build_uefi.py -d nubia_np05j -r RELEASE

# Pre-UEFI BootSelect & RelocShim
make -C Platforms/ZTE/nubia_np05jPkg/Tools/BootSelect clean all

# Android Boot Repacker & KSU/SukiSU Module ZIP
make -C Platforms/ZTE/nubia_np05jPkg/AndroidHelper/native clean all
python3 Platforms/ZTE/nubia_np05jPkg/AndroidHelper/build_helper_module.py

# Host Unit Tests & BMP Preview
make -C Platforms/ZTE/nubia_np05jPkg/Tests clean test
```

## Credits
- Pre-UEFI `boot-selection` / `RelocShim` / `SPLITv1` / `NEXTv1` / `RSTRv1` architecture, high-DPI vector splash concept, console scaling, hardware reset, and Android KSU WebUI design adapted from [Project SunUEFI](https://github.com/slwyts/Project-SunUEFI).
