---
name: sm8750-dxe-porting
description: >-
  Guides EDK2/Project Mu (Mu-Silicium) porting and AArch64 DXE binary patching
  for Qualcomm Snapdragon 8 Elite (SM8750 / Pakala) devices. Use when porting a
  new SM8750 device, extracting uefi.elf firmware volumes, configuring Pakala
  MemoryMapLib/APRIORI/DXE includes, debugging EL1 bring-up exceptions, or
  binary-patching vendor DXE (.efi) modules on Oryon cores.
---

# SM8750 (`Pakala`) UEFI Porting & DXE Binary Patching

Use this runbook when bringing up or debugging a Qualcomm Snapdragon 8 Elite
(SM8750 / `Pakala`) target in `Mu-Silicium`.

For per-driver reverse-engineering recipes and SM8750 hardware/memory-map
constraints, read [dxe-patching-catalog.md](./references/dxe-patching-catalog.md).

## Architectural Context

- **Stock UEFI partition layout:** Qualcomm SM8650–SM8750 stores UEFI in
  `uefi_a` / `uefi_b` (`uefi.elf`) alongside `xbl_config` and `imagefv` (rather
  than legacy `xbl`).
- **Chainloaded EL1 execution:** Custom Mu-Silicium UEFI is wrapped with
  `BootShim` into an Android `boot.img` and chainloaded by `abl` at `EL1` like a
  Linux kernel. Because stock `uefi.elf` + `abl` have already initialized
  hardware and locked security milestones (TZ/TME/RPMB), vendor DXEs cannot all
  be reused unmodified.
- **Reference SM8750 targets in this repo:**
  - Silicon package: `Silicon/Qualcomm/PakalaPkg/`
  - Reference devices: `Platforms/OnePlus/dodgePkg/` (`Binaries/dodge/`) and
    `Platforms/Xiaomi/annibalePkg/` (`Binaries/annibale/`).

## Hard Safety Rules for AArch64 Binary Patching (Oryon / CLANG16+)

1. **Preserve PAC/BTI prologue and epilogue pairs strictly:**
   SM8750 DXE binaries are compiled with Qualcomm `CLANG16+` using Pointer
   Authentication (PAC) and Branch Target Identification (BTI) (`PACIASP`,
   `AUTIASP`, `RETAA`, `BTI`). Never overwrite a `PACIASP` without matching the
   return path, or the Oryon core will immediately trigger a Pointer
   Authentication Fault.
   - Early `EFI_SUCCESS` return (no PAC): `MOV X0, #0; RET` (`00 00 80 D2 C0 03 5F D6`).
   - Early `EFI_SUCCESS` return (with PAC): keep `PACIASP`, then
     `MOV X0, #0; AUTIASP; RET` (or `MOV X0, #0; RETAA`).
   - NOP instruction (little-endian): `1F 20 03 D5` (`0xD503201F`).
2. **Always recalculate the PE32+ header checksum after patching:**
   After modifying any `.efi` binary, recompute and write the PE Optional Header
   checksum (for example using Python `pefile`:
   `pe.OPTIONAL_HEADER.CheckSum = pe.generate_checksum()`) so `GenFv` and the
   UEFI PE/COFF loader do not reject the image.
3. **Preserve original stock `.efi` files for diffing:**
   Follow the Mu-Silicium convention inside
   `Binaries/<device>/QcomPkg/Drivers/<DriverName>/`: keep the unmodified
   `<Driver>.efi` beside `<Driver>.patched.efi`, and point the driver's `.inf`
   `[Binaries.AARCH64]` entry to `PE32|<Driver>.patched.efi|*`. Document every
   patch in `Binaries/<device>/README.md`.

## End-to-End Porting Workflow

### 1. Extract Stock Firmware Volume (`uefi_a` / `uefi.elf`)

- Dump `uefi_a` (or `uefi_b`) from stock firmware matching the device's slot.
- Extract PE32+ AArch64 DXE modules (`.efi`), `.depex` files, `APRIORI`, and
  `uefiplat.cfg` using `UEFITool` (`uefiextract`) or `uefi-firmware-parser`.

### 2. Scaffold Device Package & Memory Map

- Copy an existing `Pakala` device (`Platforms/OnePlus/dodgePkg/` and
  `Resources/Configs/dodge.toml`) as the baseline for
  `Platforms/<Vendor>/<codename>Pkg/` and `Resources/Configs/<codename>.toml`.
- Keep `<codename>` consistent across the package directory, `.dsc`, `.fdf`,
  `.dec`, `Resources/Configs/<codename>.toml`, and `Binaries/<codename>/`.
- Translate `[MemoryMap]` from the target device's `uefiplat.cfg` (and live
  DT reserved-memory regions) into
  `Platforms/<Vendor>/<codename>Pkg/Library/MemoryMapLib/MemoryMapLib.c`, and
  configuration keys into `Library/ConfigurationMapLib/`.
  - Verify RAM base, RAM size, `UEFI_FD` (matching `[uefi_fd]` in `<codename>.toml`),
    and `Display_Reserved` framebuffer address/size.
  - Keep all uncached / IMEM / MMIO / SHRM regions strictly uncached (see
    [dxe-patching-catalog.md](./references/dxe-patching-catalog.md)).
- Start bring-up with `USE_CUSTOM_DISPLAY_DRIVER = 0` in `<codename>.dsc` so
  `SimpleFbDxe` reuses `abl`'s initialized framebuffer for on-screen debug logs
  before attempting vendor `DisplayDxe`.

### 3. Populate Binaries, `APRIORI.inc`, and `DXE.inc`

- Place extracted `.efi`, `.depex`, and generated `.inf` files under
  `Binaries/<codename>/`.
- Mirror stock `uefi.elf` dispatch order in `Include/APRIORI.inc` (e.g.,
  `EnvDxe` -> core Mu/Arm DXEs -> `SmemDxe` -> `DALSYSDxe` -> `HWIODxe` ->
  `ChipInfoDxe` -> `PlatformInfoDxe` -> `HALIOMMUDxe` -> `ULog`/`IPCC`/`CmdDb`/
  `PwrUtils`/`Npa`/`Rpmh`/`Mailbox`/`QcomScmi`/`Vcs` -> `ClockDxe` -> `ICBDxe`
  -> `ShmBridgeDxeLA` -> `ScmDxeCompat` -> `TLMM`/`Qup`/`I2C`/`SPMIDxe` ->
  `PmicDxeLa` -> storage `Sdcc`/`UFSDxe` -> `TzDxeLA` -> USB/Buttons/Display).
- Exclude redundant pre-milestone security or OEM-proprietary DXEs (see
  catalog).

### 4. Build & Non-Destructive RAM Boot Test

Run from the repository root (Secure Boot is disabled by default; omit `-s`
during bring-up):

```bash
python3 build_uefi.py -d <codename> -r DEBUG
```

Boot the output image directly in RAM without flashing:

```bash
fastboot boot ./Mu-<codename>.img
```

### 5. Iterative Exception Debugging & DXE Patching Loop

1. Watch serial/framebuffer output for `Loading driver at 0x<BASE> <Name>.efi`.
2. When a hang, `ASSERT`, or synchronous exception occurs, compute the static
   module offset:
   `Offset = ELR - DriverBaseAddress`.
3. Open `<Name>.efi` in Ghidra or IDA Pro (rebase to `0x0` or subtract image
   base) and jump to `Offset`.
4. Determine root cause:
   - Missing dependency protocol -> check `.depex` and `APRIORI.inc`/`DXE.inc`.
   - Re-init crash / `CpuDeadLoop` / `ASSERT` / `ExitBootServices` teardown ->
     apply minimal PAC-safe patch (see
     [dxe-patching-catalog.md](./references/dxe-patching-catalog.md)), save as
     `<Name>.patched.efi`, update PE checksum, point `.inf` to it, rebuild, and
     re-test via `fastboot boot`.
