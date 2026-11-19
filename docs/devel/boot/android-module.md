# Android Boot Helper

The SunUEFI Boot Helper installs and maintains the Combined BOOT from Android, providing Android/UEFI boot selection and dedicated storage management. The module installs or updates SunUEFI from the device's current BOOT, preserving the stock kernel or a format-compatible custom GKI; installing boot does not automatically partition storage.

For standard installation steps, see the [Installation Guide](../../user/install-from-artifact.md). This page describes the behavior and implementation of the Boot Helper.

## Usage

1. Download and extract the `SunUEFI-Module` package from **Build products**, then transfer `SunUEFI-Piano-<version>.zip` inside it to the tablet.
2. In a running Android session, install the ZIP from SukiSU's Modules tab. If the same core is already installed, the installer only reads BOOT without rewriting it; otherwise, it prompts for Volume Up to install or update boot, or Volume Down to install only the Boot Helper. If no selection is made within 30 seconds, only the Boot Helper is installed, and boot can be installed later from the Maintenance tab.
3. Reboot normally, then tap **Open WebUI** on the module card.

The WebUI is split into three tabs: **Boot**, **Storage**, and **Maintenance**, using side navigation in landscape and bottom navigation in portrait. The Boot tab offers two boot routes: Android and UEFI. When UEFI is selected, an installed Linux system is selected by default and boots directly; the boot menu remains available as an alternate entry. If no Linux boot files are present, only the boot menu is shown. After choosing a target, tap **Save and restart**; subsequent normal power-ons and reboots follow that choice until changed again.

The new Combined BOOT displays an Android/UEFI chooser screen at power-on and follows the saved boot route after a roughly three-second countdown. Press Volume Up to select Android, Volume Down to select UEFI and boot Linux directly, and press and release the Power key briefly to confirm; this temporary choice does not change the boot route saved in the module. The display output, volume-key selection feedback, default Android boot, and booting Linux from ESP via temporary UEFI selection have all been verified in use on the device. Stock `reboot recovery` precedence is preserved; a standalone entry for firmware settings is not yet exposed.

The Storage tab shows the capacities of the dedicated storage partitions and uses Android's local file picker to select ESP/root images without requiring files to be placed in a fixed directory. Starting with 0.5.2, both raw images and Android sparse images are accepted; capacity is checked against the expanded size, and images are streamed to the partition and read back for verification. The partition names remain `sunuefi_esp` and `sunuefi_root`. The tab provides actions to install images, create partitions, resize root, and delete partitions to return space to Android, presenting an execution plan before proceeding. First-time partitioning on a fresh device has not yet completed on-device verification. Space reclamation uses SukiSU's early init hook to expand F2FS offline on the next boot before continuing into Android; if the early hook is unavailable, deletion is refused beforehand. For the underlying mechanism and capacity checks, see [Android Storage Resizing](android-storage.md).

When updating images, check **Update this image** for the images you want to flash, then tap **Install selected images**. Checking only the boot image (ESP) keeps the existing system and data; checking only the system image (root) overwrites the system partition while keeping boot files; checking both writes both. The confirmation dialog lists the images that will be written. When reopening the page, neither checkbox is checked by default, so previously selected files are not automatically included in a new update.

The UI supports both Chinese and English. Opening the page reads only lightweight status, typically taking around 0.2 seconds; a full read-only BOOT inspection runs only when tapping **Check boot** on the Maintenance tab, taking about two seconds on the current device. The manager's **Action** button only displays the current selection and usage hints; it does not reboot or print raw JSON from the native tools.

**Reinstall boot** is intended for recovery after a HyperOS update. If the current BOOT is still a matching SunUEFI Combined BOOT image, the action only re-adopts the existing boot setup; if the core version differs, it updates the core while keeping the current Android kernel and boot selection; if an OTA replaced BOOT with a supported stock BOOT, it extracts the new kernel from the active slot, embeds the same SunUEFI core bundled in the module, and restores the previous boot selection. It does not automatically hook into OTA updates or pre-modify the inactive slot. Uninstalling the Boot Helper removes only the module while leaving the existing BOOT and boot selection intact; if you need to return to Android, select Android in the WebUI first.

Installation, module activation, and WebUI status reads under SukiSU have been verified in use on the device. KernelSU provides an equivalent [WebUI interface](https://kernelsu.org/guide/module-webui.html); official Magisk can display hints via the module Action button, while the graphical UI requires a host such as [MMRL](https://mmrl.dev/). Other hosts have not yet been verified for compatibility.

## BOOT Detection and Writing

Installation and maintenance use the same universal module, choosing the operation based on the current BOOT:

| Current BOOT | Action |
| --- | --- |
| Format-compatible current BOOT (stock or custom GKI) | `probe` reads the current kernel and header metadata; `repack` adds the boot chooser and current UEFI core; first-time installation defaults to Android |
| Combined BOOT matching the module | `adopt` identifies it read-only and saves management state without rewriting BOOT |
| Older Combined BOOT in a known format | `upgrade` fully reconstructs the original BOOT and verifies the wrapper before updating the core; preserves the Android kernel, original header and trailer, existing Root contents, and boot selection |
| Unsupported or corrupted BOOT | Aborts installation and reports the reason instead of guessing the format and flashing anyway |

Android Boot Header v3 and v4 are supported; existing ramdisk and v4 GKI signature sections are preserved and relocated to match the expanded kernel section. The ARM64 kernel must be an uncompressed Image whose first entry instruction is an EFI magic instruction or a standard NOP, and whose second instruction is a direct branch into the kernel. The chooser uses this to return to the original kernel; compressed kernels and proprietary wrappers that require executing other entry instructions still abort installation. `image_size` must cover the kernel image and leave a deterministic runtime region, and load offsets must remain 2 MiB-aligned so that the chooser stack, Device Tree, or initrd do not overlap the kernel. Older Combined BOOT images store original-image reconstruction metadata using `SPLITv1+RSTRv1`; when updating the core, the boot record's generation and CRC are recomputed while keeping the recorded target.

Kernel version, builder identity, and AVB fingerprints are not used as a compatibility allowlist, nor does a missing fingerprint block installation. AVB verification supports SHA256 and SHA512 data digests; when the current BOOT has no digest, its digest does not match the payload, or the digest only covers a shorter older kernel, an in-memory byte-for-byte restoration check is performed first, followed by a physical volume-key confirmation prompt. Press and release Volume Down three times to continue, or press Volume Up or let the 30-second timeout expire to cancel; this confirmation applies only to the current operation and is not persisted. `--confirm-modified-boot` only enables showing this prompt and does not replace key presses. Checks for device model, active slot, entry instructions, available space, full restoration, and write read-back verification always remain enforced.

Before writing boot, the tool verifies the active slot, ROM, BOOT contents, and source image digest; after writing, it syncs and performs a full read-back check. Boot operations never write to Recovery, vbmeta, GPT, or userdata; storage operations are handled by dedicated tools and modify the corresponding filesystems and partition table only after an explicit plan is confirmed. Core upgrades from older builds have been verified against a 96 MiB BOOT file to confirm bit-for-bit reconstruction of the original image; this result does not imply that all HyperOS OTA scenarios have been verified on hardware.

Only the `repack` path that converts the current BOOT into a Combined BOOT for the first time checks bootloader properties; if the properties do not report an unlocked bootloader, confirm using the [physical volume-key prompt in the Installation Guide](../../user/install-from-artifact.md#1-install-the-boot-helper). `piano-boot-repack confirm-bootloader` reads three complete press-and-release cycles exclusively from `gpio-keys`/`pmic_resin` under platform devices, temporarily grabbing input devices while ignoring key-repeat events and pre-held keys. Once confirmed, the shell passes `PIANO_CONFIRMED_UNVERIFIED_BOOTLOADER=1` only to the child process of the current installation; public entry points strip any externally inherited value, and no permanent setting is saved. Before writing, native `repack` rechecks either the bootloader properties or this per-run confirmation, while keeping all model, active slot, source digest, format, bounds, sync, and read-back checks unchanged. Request updates, core upgrades, and exact restoration on an existing Combined BOOT still fully verify the current wrapper without rechecking bootloader properties; dedicated storage is outside AVB verification scope and does not add a bootloader check.

The module source lives in `android/module/`, the native boot tools in `android/native/`, and the online F2FS resizing tool in `tools/android/`. `manager.sh` bridges the WebUI and the native tools: lightweight reads use `piano-boot-request`, while full checks, adopting an existing boot image, upgrades, repacking, and changing boot targets use `piano-boot-repack`.

The Boot Helper no longer writes slot configuration to the ESP. The Linux boot-switch tool directly reads the Qualcomm GPT A/B attributes, validates the active BOOT, and updates the SunUEFI request page inside it, without relying on Android synchronizing extra files to the ESP.

## Building

Inside the build container, build the UEFI product first, then build the module:

```sh
./build.sh uefi
./build.sh module
```

`tools/build_android_product_module.py` extracts the same `fd.bin`, `app.bin`, and `shim.bin` from `artifacts/product/`, compiles the pre-boot chooser, and builds the following static ARM64 tools:

* `piano-boot-repack`: BOOT detection, wrapping, upgrading, and restoration.
* `piano-boot-request`: Reading and modifying the boot record.
* `piano-storage`: Partition status, planning, and image flashing.
* `piano-resize-f2fs`: Android F2FS capacity checks, online shrinking, and early-expansion coordination.
* `fsck.f2fs`, `resize.f2fs`: Early read-only filesystem checks and layout-preserving offline expansion.
* `e2fsck`, `resize2fs`: Checking and resizing the dedicated ext4 partition.

The default build sysroot is unpacked from signature-verified public Debian ARM64 packages without executing foreign-architecture code; an existing ARM64 development sysroot can be supplied via `--sysroot`. e2fsprogs is built from pinned upstream source, and the ZIP includes the corresponding licenses, source URLs, and this project's tool source code. No stock BOOT, old Android kernel, or device-private paths are required, and the ZIP does not contain a stock kernel.

Build outputs are placed in `artifacts/android/`:

* `SunUEFI-Piano-<version>.zip`: Installable module archive.
* `manifest.json`: Build manifest for the module, native tools, and the corresponding UEFI product.
* `SHA256SUMS`: ZIP checksums.

Incremental rebuilds reuse the downloaded toolchain directory, regenerate payloads, and replace output artifacts only after the ZIP is completely generated; custom directories can be specified with `--work` and `--output`. The low-level packaging tool is retained for maintainers who already have prepared payloads and native binaries; to inspect inputs, run:

```sh
./build.sh module-package --inspect
```

In GitHub Actions, both the `uefi` and `debian-gnome` targets build the module in the same run and separately upload the ZIP, build manifest, and checksums. They use the same build entry point and do not require dumping an existing core from a device first. Installing the Combined BOOT via the module and performing first-time partitioning are separate operations: installing boot does not create ESP/root, and the new-partition workflow on the Storage tab still requires on-device verification.

Offline tools must not be used directly on a mounted Android data partition, nor should the current resize path be assumed usable simply because stock libraries include expansion code. For storage details, see [Android Storage Resizing](android-storage.md); for BOOT wrapping and lossless reconstruction, see [Native Repacking Tool](combined-boot.md); for boot routes and stock Recovery precedence, see [Reboot Requests](reboot-request.md).

Flashing ESP/root no longer requires the module to pre-populate a Bluetooth address. The current Linux BSP automatically reads the stock `persist` partition during wireless initialization, falling back to a deterministic address derived from hardware identifiers if the stock address is absent. Public images remain generic, and manual flashing uses the same initialization path; see [Bluetooth Address](../hardware/bluetooth.md).
