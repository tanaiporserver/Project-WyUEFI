# Third-Party Sources and Local Materials

SunUEFI builds on top of these projects: EDK2 / Mu-Silicium provide the UEFI foundation and Qualcomm platform integration, simple-init provides the graphical menu in UEFI, linux-piano and the Linux kernel provide board-level drivers, debian-piano, Mesa, and piano-sensors provide the rootfs, GPU userspace, and sensor communication, device firmware is sourced from official OTA packages, and MiCode along with the device's stock OS serve as comparison references. For what each project provides and what we added, see [Upstream Projects](docs/devel/upstreams.md).

This page is the exact provenance index: pinned commits, the scripts that consume them, and known license pointers. Full commit hashes are authoritative in the linked lock files and scripts. License pointers are taken from in-repository file notices and have not been individually re-verified against upstream license texts.

## Source Dependencies

| Component | Source & Pinned Identity | Usage / Tools in This Repo | License Verification Location & Current Scope |
| --- | --- | --- | --- |
| Mu-Silicium | [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium), `3ec9169f308c01030a7e698072aca5acb2c0a144` | [prepare_piano.py](tools/dev/prepare_piano.py) derives from the OnePlus template; [build_stage0.sh](tools/build_stage0.sh) compiles | Upstream licenses and file notices; [DeviceBuild.py](uefi/platforms/pianoPkg/DeviceBuild.py) retains Microsoft copyright / BSD-2-Clause-Patent. |
| mu_wide_platforms (porting reference) | [sunflower2333/mu_wide_platforms](https://github.com/sunflower2333/mu_wide_platforms), referenced [Piano platform](https://github.com/sunflower2333/mu_wide_platforms/tree/3e1d052064beb6e59cda3cfba87d50e6cc088174/Platforms/PakalaPkg/Device/xiaomi-piano) at commit `3e1d052064beb6e59cda3cfba87d50e6cc088174` | Reference material for Project Mu UEFI, platform initialization, and ACPI on the same device; for active build dependencies, see the Mu-Silicium entry above | Repository specifies BSD-2-Clause; individual source files and bundled binaries follow their respective notices. |
| Mu_Basecore, Mu Plus, Mu OEM Sample, Silicium-ACPI, Device-Binaries | URLs/commits in [sources.lock.json](sources.lock.json) | Mu build tree and [product hooks](tools/prepare_product_pump.py) | Licenses/copyrights of each source; Device-Binaries must additionally be verified per binary artifact. |
| dtc | [dgibson/dtc](https://github.com/dgibson/dtc), `f3451d12532b9d382707c458a2ae2e5aa0e1eee4` | `upstream/dtc`, DTB tools | License and file notices at the pinned upstream commit. |
| simple-init | [BigfootACA/simple-init](https://github.com/BigfootACA/simple-init), `3d66a6e78d519dd050fbebde4db6c5ac933f9aa4` | [prepare_simpleinit.py](tools/prepare_simpleinit.py), [build_simpleinit.sh](tools/build_simpleinit.sh) | Upstream notices; [local runtime](uefi/components/product-pump/simple-init/src/gui/piano_product_runtime.c) / headers are LGPL-3.0-or-later. |
| lwext4 | [gkostka/lwext4](https://github.com/gkostka/lwext4), pinned source and archive digest in [uefi-ext4.json](config/uefi-ext4.json) | [prepare_ext4.py](tools/firmware/prepare_ext4.py) stages the standard ext4 library, UEFI file protocol adapter lives in `uefi/components/ext4/`; formatting and partitioning tools are not built | Most files are BSD-3-Clause; `ext4_extent.c` and `ext4_xattr.c` contain GPL-2.0, so the combined library is treated as GPL-2.0, retaining source copyrights and licenses. |
| f2fs-tools | Official 1.17.0, source and layout-preserving resize patches in [series.json](patches/f2fs-tools/1.17.0/series.json) | Static `fsck.f2fs` / `resize.f2fs` inside the Android module, built by [build_f2fs_tools.py](tools/android/build_f2fs_tools.py) | GPL-2.0; module includes COPYING, source origin, and local patches. |
| Linux / linux-piano | [blu-sharky/linux-piano](https://github.com/blu-sharky/linux-piano), [torvalds/linux](https://github.com/torvalds/linux); rescue pins in [kernel-profiles.json](linux/kernel-profiles.json) | [build_kernel.py](tools/build_kernel.py); full baselines in [full builder](tools/build_piano_full_kernel.py) / [next builder](tools/build_piano_next_full.py) | `COPYING`, `LICENSES/`, and file notices of the selected kernel; does not change the license of host scripts. |
| BigfootACA Qualcomm display patches (Junhao Xie) | [PLL initialization `67b3cbd4bab5`](https://github.com/BigfootACA/linux/commit/67b3cbd4bab5687c3aa3c9fe5a14c37433e28bbf), [DPU stop sequence `8f353c130fee`](https://github.com/BigfootACA/linux/commit/8f353c130fee0fcf09afbe10e7fc58f901931418), [PHY runtime interface clock `b95037aef0a0`](https://github.com/BigfootACA/linux/commit/b95037aef0a0bf5cd612e7517a67644887e9fd5d) | Default kernel patches `0024`, `0026`, and `0034`; fix PLL initialization, video pipeline stop ordering, and runtime management of the PHY interface clock respectively | Original author and Signed-off-by preserved; follows the SPDX and kernel license of the respective Linux source files. The PHY patch only adjusts context around existing local recovery helper code. |
| MiCode board data / public kernel | [MiCode/Xiaomi_Kernel_OpenSource](https://github.com/MiCode/Xiaomi_Kernel_OpenSource), `45fb9bd6ae5ba2942fc1d53e4b6b46ef76992f71`; DTS pin in [source set](docs/archive/linux-bringup/piano-full-linux-candidate.md) | [PianoKeys.c](uefi/core/PianoKeys.c) and related files note the reference source | Original notices retained; local Keys is GPL-2.0-only, TouchProbe is GPL-2.0-or-later. |
| Debian/Piano userspace, Mesa, firmware | Components / exact commits in [Linux source set](docs/archive/linux-bringup/piano-full-linux-candidate.md) | [stage_piano_full_userspace.py](tools/stage_piano_full_userspace.py) and related scripts stage into derived rootfs | Packages, service sources, Mesa, and firmware follow their respective notices. |
| libcamera | [Release source and Debian recipe](config/libcamera.json), [Hans de Goede's CAMSS series and local PIX patches](patches/libcamera/0.8.0/series.json) | `./build.sh libcamera` produces native Debian ARM64 packages | Core and CAMSS are LGPL-2.1-or-later; tools, IPAs, etc. follow upstream [COPYING.rst](https://github.com/libcamera-org/libcamera/blob/v0.8.0/COPYING.rst) and per-file notices; Debian packages retain the copyright manifest. |
| xiaomi-sheng-thp stylus solver | [ianchb/xiaomi-sheng-thp](https://github.com/ianchb/xiaomi-sheng-thp), `34046210932d654a4c0df0121ecc31c008f8148c` | [Piano parameterized core](linux/userspace/pen/piano-pen-core.h) is derived from the standalone matrix solver and filtering code and reads the device's actual calibration config; does not launch the upstream second touch daemon | Apache-2.0; retains [full license](linux/userspace/pen/Apache-2.0.txt), source file SPDX, and [origin / modification notes](linux/userspace/pen/OPEN_CORE_ORIGIN.txt). Currently only real config loading and pressure reporting are complete; coordinates still require verification against an actual stylus matrix. |

`sources.lock.json` only covers part of the dependencies; other pins remain in scripts. For how kernel linked-worktrees are fetched, see [Kernel Reproducible Builds](docs/archive/linux-bringup/linux-reproducible-builds.md).

Additional Xiaomi Pad references include [code002-2/Xiaomi-pad-6s-pro-Linux](https://github.com/code002-2/Xiaomi-pad-6s-pro-Linux/tree/053169b1ed48335d8139694ea6ccaacf52462dd9) and the repositories it references: [debian-sheng](https://github.com/ianchb/debian-sheng), [xiaomi-mipps-auth](https://github.com/ianchb/xiaomi-mipps-auth/tree/9176efbdf276b874fc0a97912a914449a88155fb), and [xiaomi-sheng-fingerprint](https://github.com/ianchb/xiaomi-sheng-fingerprint/tree/76e7301163b0e708f609c9864b3e5833e9f57402). These serve as references for multi-distro integration, MiPPS authentication, and standard libfprint/fprintd integration, respectively. The fingerprint project targets FPC1553, whereas Piano uses a stock Goodix sensor, so sensor protocols are not directly interchangeable. The MiPPS tool is licensed under GPL-2.0-only; the fingerprint backend is Apache-2.0, while libfprint/fprintd patches and QTEE dependencies follow their respective licenses.

The kernel MiPPS [patch](patches/linux/7.3/0027-power-piano-mipps-authentication.patch) ports the protocol constants and digest calculation from `xiaomi-mipps-auth` above, and references [blu-sharky/piano-mipps-auth](https://github.com/blu-sharky/piano-mipps-auth) for handling Piano firmware omitting charger identity reports and the stock command sequence, retaining GPL-2.0-only. Authentication runs in a kernel workqueue without installing upstream userspace services, state files, or notifications. Source commits are recorded under `mipps_authentication` in the [kernel patch manifest](patches/linux/7.3/series.json); protocol details and limitations are documented in the [kernel documentation](docs/devel/linux/kernel.md#usb-type-c).

The SM8750 thermal zone include approach references Cryolitia's
[Piano TSENS overlay](https://github.com/Cryolitia/nixos-config/blob/096e88bfc641784cc291dfe54695c4811d8c61e5/hosts/xiaomi-piano/piano-tsens-temperatures.dts)
and retains BSD-3-Clause. Thermal zone definitions are extracted directly from the pinned kernel's `sm8750.dtsi`
at packaging time; the generated include preserves Qualcomm's copyright and the original file's license without maintaining a second sensor table.

HyperOS 4 [ADSP](vendor/firmware/hyperos4-adsp/README.md) and
[CDSP](vendor/firmware/hyperos4-cdsp/README.md) firmware come from the device's stock firmware partition,
preserving original signatures, filenames, and per-file SHA-256 digests, and are installed into all four distributions according to the manifest.
They are proprietary Qualcomm / Xiaomi firmware and are not covered by this project's source license.

The native Goodix client implements its own transport via the Linux TEE object ioctl and does not link against MinkIPC or quic-teec.
Object C argument layouts and FS/GPFS messages reference [Qualcomm MinkIPC](https://github.com/qualcomm/minkipc),
with the corresponding files retaining Qualcomm copyright and BSD-3-Clause / BSD-3-Clause-Clear licenses.
CBOR encoding and decoding use the public source of [QCBOR](https://github.com/laurencelundblade/QCBOR).
Exact commits, source licenses, and build inputs are recorded in
[`config/fingerprint.json`](config/fingerprint.json) and
`/usr/share/piano-provenance/fingerprint.json` inside the image; corresponding licenses are installed alongside the static client.
This project independently implements native Goodix command framing, communicating through the existing UIO controller and the standard Linux TEE object interface
without using GP sessions; the UIO controller and libfprint backend remain local adaptations.
The stock signed program, as well as the generic anti-spoofing and recognition model parameters extracted from the device's `libgf_hal.so`,
are preserved byte-for-byte and provided as hardware firmware without containing any user fingerprint templates. The extractor locates models by exported symbols
and records output checksums in the firmware manifest; the current manifest does not record per-model offsets or source library digests.
Stock executable libraries are not installed in the system. Provenance records do not grant redistribution authorization for the program or models; see the
[firmware notes](vendor/firmware/hyperos4-fingerprint/README.md) for origin and scope.

The Android Gunyah driver and mainline interface adaptations come from the patchset assembled by
[code002-2/librevm](https://github.com/code002-2/librevm/tree/56918558e2c4303cda803a655ea716869c551b71/packaging/gunyah-driver-7.2),
originally adapted from
[SuperTurtleDev/iceland-mainline-kernel-linux](https://github.com/SuperTurtleDev/iceland-mainline-kernel-linux/tree/fd503a492d381687c143b7085ea79ebce58dcdc7).
The upstream driver is Android common `android17-6.18-2026-06_r1`, commit
`30c0eaeef78d5c19f687ad8418c27b76353ab261`. Local
[`0006-gunyah.patch`](patches/linux/7.3/0006-gunyah.patch) preserves original authors,
copyrights, and per-file notices such as GPL-2.0-only; no Device Trees or memory reservations from other tablets are imported.
These changes interface with the device's existing Gunyah firmware and do not replace Qualcomm's Hypervisor.

BORE 6.8.0 comes from Masahito Suzuki's scheduler implementation and the Linux 7.3 patch maintained by Piotr Gorski in
[CachyOS/kernel-patches](https://github.com/CachyOS/kernel-patches/blob/6266b5f33f4e8caaa452174f0132c8c82f919267/7.3/sched/0001-bore-cachy.patch).
Local [`0007-bore.patch`](patches/linux/7.3/0007-bore.patch) only adjusts
context around `CONFIG_CACHY`-specific defaults and whitespace formatting, preserving the BORE algorithm and default runtime parameters.
The code follows the Linux kernel license; original patch checksums and commits are recorded in
[`series.json`](patches/linux/7.3/series.json). MGLRU, ZRAM,
`schedutil`, and preemption mechanisms are standard Linux features and are not presented as CachyOS-exclusive changes.

FastRPC DSP restart cleanup fixes reference
[three upstream patches](https://github.com/qualcomm-linux/kernel/pull/1212) submitted by Qualcomm's Jianping Li,
adapted locally as [`0011-fastrpc-drain-ssr-invokes.patch`](patches/linux/7.3/0011-fastrpc-drain-ssr-invokes.patch).
They fix initialization ordering on device open, exit notifications for polled invokes, and a race where DMA mappings are torn down before active invokes exit.
Original commits are recorded in the patch body and kernel provenance manifest, following the Linux source file's GPL-2.0-only license.
These changes handle post-crash DSP cleanup and do not imply that the underlying cause of the initial DSP crash is resolved.

SteamOS rootfs extraction uses `scripts/extract_rootfs.py` (GPL-2.0) from [hashtagbasit/SteamOS-ARM-Port](https://github.com/hashtagbasit/SteamOS-ARM-Port/tree/e54dac93afccf76e5fefe998399b6c1b1a23f3a1). The commit and file SHA-256 are pinned in `config/steamos.json`, preserving source file notices on download. That project provides the extraction method and porting reference; SunUEFI uses its own bootloader, device kernel, and BSP. The Valve ARM64 rootfs and public Steam client are recorded in `config/steamos.json` and `config/steam-client.json` respectively, and remain under each component's original license—unaffected by the extraction script being GPL. FFmpeg uses existing BSP patches and is built as a pacman package matching the SteamOS library ABI, retaining full codec configuration and license files.

SteamOS Gamescope uses the mobile adaptation source under
`external-and-mods/gamescope` from the same pinned `SteamOS-ARM-Port` commit, preserving per-file copyrights and
Gamescope's BSD-2-Clause license. Subproject dependencies are sourced from Valve Gamescope 3.16.30
(`ad2763da1c48860f649abfe842a087188dcb6e20`), retaining their respective licenses;
the `6edb42b...` object referenced by the original script is unavailable, so it is not depended upon.
This project adds a [render/display node separation fix](patches/gamescope/0001-drm-separate-render-and-display.patch)
so that the Adreno render device and the standalone DPU display controller can operate together.
Build provenance is recorded in `tools/build_steamos_gamescope.py` and the generated `SOURCE.json`.

KDE components missing from SteamOS are sourced from the [official KDE release archive](https://download.kde.org/stable/).
Versions, URLs, SHA-256 digests, and dependencies are listed under `kde_packages` in `config/steamos.json`,
built against the existing Valve ARM64 system ABI while preserving each component's `LICENSES/`.

SteamOS Manager uses [Valve 26.4.1 source](https://gitlab.steamos.cloud/holo/steamos-manager/-/tree/a0d399defebd60db30ba38325d9cf47dbda49bee),
retaining its MIT license; this project's patch allows platform management to remain operational when optional kernel log tracking fails.
Source, Rust toolchain, and patch digests are pinned by `tools/build_steamos_manager.py`.

Preinstalled compatibility tools are sourced from the [GE-Proton11-11 ARM64 release](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-11),
and the container runtime comes from [Valve Steam Linux Runtime 4.0 ARM64](https://repo.steampowered.com/steamrt4/images/4.0.20260928.262390/).
The binaries along with their bundled Wine, FEX, DXVK, vkd3d-proton, and other components retain their respective licenses,
copyrights, and source availability terms, and are not relicensed under this repository's license. Download identities, digests, and key source commits are pinned in
`config/steam-runtime.json`; the build extracts public toolkits directly and never copies user Steam account data.

## Fonts, Icons, and Display Assets

| Asset | Identity & Usage | Notices / Verification Needed |
| --- | --- | --- |
| Noto CJK | [prepare_simpleinit.py](tools/prepare_simpleinit.py) extracts the SC face from host fonts; host package version is not pinned | Actual hash is written to `source-manifest.json`; font license/notices come from the fetched package. |
| Noto Sans CJK SC setup page glyphs | [prepare_product_console.py](tools/prepare_product_console.py) generates 16×19 bitmaps from the SC face to provide Chinese glyphs via a standard HII font package | SIL OFL 1.1; [full notice](uefi/components/product-support/Library/ConsoleScale/OFL.txt) is bundled with the module and installer package, and hashes of the font and generated data are recorded in the product build manifest. |
| Font Awesome | Same script downloads `FortAwesome/Font-Awesome` `5.15.4` fonts | Actual hash is recorded; content hash is not pinned prior to download; verify font notices for this version. |
| AOSP bitmap font | [Font.h](uefi/platforms/pianoProbePkg/Library/RamLogSerialPortLib/Font.h) | File header retains AOSP copyright and source/binary distribution terms; keep applicable notices on release. |
| TianoCore logo and brand text | [ProductSplashAssets.h](uefi/components/product-support/Library/ProductBootManagerLib/ProductSplashAssets.h) describes a vector interpretation of the six-spoke logo | Retain provenance notes; verify trademark usage scope separately. |

`build_simpleinit.sh` compiles the rootfs containing fonts into linked objects, so they may end up in binaries; notice verification cannot be limited to source files tracked in Git.

## Stock Device Firmware and Binary Redistribution Boundaries

Native PE/DEPEX modules, boot/DTB images, vendor modules, and firmware are stored in gitignored local directories; see [Local Inputs](docs/devel/local-inputs.md) for acquisition and input constraints.

[inventory_native_drivers.py](tools/dev/inventory_native_drivers.py) inventories extracted modules; [prepare_product.py](tools/prepare_product.py) validates and embeds them into the FV; [piano_inherited_clock.py](tools/piano_inherited_clock.py) generates a compatible derivative of the fixed-ROM ClockDxe.

Binary redistribution terms for these materials are **unreviewed**; hashes only prove identity and do not grant distribution rights. Future releases must verify provenance and notices against actual release contents; this page makes no legal or compatibility determinations.
