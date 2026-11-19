# Upstream Projects

SunUEFI does not rewrite everything from scratch. Upstream sources are pinned to specific commits under `upstream/`, while our own modifications live in `patches/`, `uefi/`, `linux/`, and the build tooling. The table below lists what each upstream provides and what we build on top of it.

| Project | What It Provides | What We Built on Top |
| --- | --- | --- |
| [TianoCore EDK2](https://github.com/tianocore/edk2) / [Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) | Core UEFI codebase and Qualcomm platform integration | Piano platform support, boot handoff, storage, input, display inheritance, UEFI Fastboot, plus read-only protection for stock partitions and safe exit |
| [mu_wide_platforms](https://github.com/sunflower2333/mu_wide_platforms) | Project Mu-based UEFI for Qualcomm devices, including Piano platform and ACPI for Xiaomi Pad 8 Pro | Reference for same-device UEFI initialization, ACPI, and Windows boot adaptation |
| [Xiaomi Pad 6S Pro Linux](https://github.com/code002-2/Xiaomi-pad-6s-pro-Linux) / [debian-sheng](https://github.com/ianchb/debian-sheng) | Multi-distro adaptation for Xiaomi tablets, plus charging, stylus, and fingerprint implementations | Reference for MiPPS authentication, magnetic stylus connectivity, and TEE fingerprint integration, ported to Piano's actual hardware |
| [simple-init](https://github.com/BigfootACA/simple-init) | Graphical boot menu inside UEFI | Tablet layout, Chinese UI support, physical button navigation, and integration with firmware background services |
| [linux-piano](https://github.com/blu-sharky/linux-piano) | SM8750 kernel, Device Tree, and board driver baseline | Merged into the stable kernel with fixes for memory/DMA, display, audio, and more; consumed selectively rather than checked out with the repository |
| [CachyOS/kernel-patches](https://github.com/CachyOS/kernel-patches) / [BORE](https://github.com/firelzrd/bore-scheduler) | Interactivity-focused CPU scheduler improvements | Integrated matching-version BORE patches into the shared ARM64 kernel used across all four distributions |
| [librevm](https://github.com/code002-2/librevm) | Mainline interface adaptation for the Android Gunyah driver | Ported to the current kernel with per-boot resource information passed by UEFI; VM execution support is still being debugged |
| [BigfootACA/linux](https://github.com/BigfootACA/linux) · Junhao Xie | Qualcomm display driver improvements | Adopted patches for DSI PLL initialization, DPU video pipeline teardown order, and DSI PHY interface clock management to improve display init and screen-off resume; see [THIRD_PARTY.md](../../THIRD_PARTY.md) for exact origins |
| [debian-piano](https://github.com/blu-sharky/debian-piano) | Debian rootfs build, device services, camera, and input helpers | Dedicated ESP/root, multi-distro BSP (board support package), and unified packaging |
| [piano-firmware](https://github.com/blu-sharky/piano-firmware) | Device firmware (Wi-Fi, Bluetooth, DSP, touch, etc.) and amplifier presets | Device firmware is now imported from official OTA packages into `vendor/firmware`; it is not a full HyperOS image and does not include stock camera algorithms |
| [Mesa](https://mesa3d.org/) | Adreno graphics userspace | Official Mesa 27 development sources with native distro packaging shared across all four operating systems |
| [piano-sensors](https://github.com/blu-sharky/piano-sensors) | Qualcomm sensor (SSC) communication and desktop bridge | Data types, read interfaces, standard SensorProxy integration, and auto-brightness policy |
| [SteamOS-ARM-Port](https://github.com/hashtagbasit/SteamOS-ARM-Port) | Valve ARM64 rootfs extraction method and mobile device porting reference | Reuses the pinned extractor wired into SunUEFI's ESP/root, kernel, and BSP; does not adopt its device launcher or partition layout |
| [MiCode](https://github.com/MiCode) and on-device HyperOS | Reference for stock Device Trees, drivers, and configuration | Cross-referenced for real commands, timings, and parameters, then ported to standard interfaces |

Mesa uses the official 27 development commit pinned in [`config/mesa.json`](../../config/mesa.json), and all four operating systems build their own native packages from that same source tree. The drm/msm chip ID `0xffff44050001` for Adreno 830v1 was merged upstream in [MR43878](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/43878), so the chip ID and D3D12 backport patches are no longer retained, and external Mesa wrapper repositories are no longer referenced. Development commits do not carry release signatures, so the build records the official origin, pinned commit, and archive SHA256; the signed Debian source version is used solely to obtain the distribution packaging recipe and does not misrepresent the Mesa version being built.

SteamOS still requires the standalone renderonly buffer height alignment fix in `patches/mesa/27.0/`. The selected official source still under-allocates tail space for heights not aligned to four rows, so this fix was not removed alongside the chip ID patches; it is not applied to the Debian and Nyarch sources.

Debian software is installed via APT, while Nyarch and SteamOS use native pacman packages. Nyarch is built from Arch Linux ARM and application sources without relying on legacy ISOs. SteamOS is an experimental target. Configurations for Ubuntu, Deepin, and others are not yet included in default builds.

See [THIRD_PARTY.md](../../THIRD_PARTY.md), `.gitmodules`, and `sources.lock.json` for all pinned commits and licenses.
