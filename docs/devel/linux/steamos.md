# SteamOS ARM64

This target uses the Valve SteamOS ARM64 root filesystem on top of SunUEFI's Combined BOOT, UEFI, kernel, Device Tree, and dedicated storage partitions. It does not require replacing ABL and does not use the handheld partition installer from SteamOS-ARM-Port.

This is an experimental build target. The default boot entry is **Steam Gaming Mode**, with KDE as Desktop Mode. Gaming Mode display, input, and switching between Gaming Mode and KDE all work. Milthm and Genshin Impact run; Proton/FEX compatibility remains per-game, so this does not mean all Windows games work.

## On-Device Screenshots

| Steam Gaming Mode | Genshin Impact In-Game |
| --- | --- |
| ![Chinese Steam Gaming Mode and game library](../../assets/gallery/steamos-gaming-mode.webp) | ![Genshin Impact character entering the game world](../../assets/gallery/steamos-genshin-world.webp) |
| **Genshin Impact Official Launcher** | **Magical Girl Witch Trials** |
| ![Genshin Impact official launcher](../../assets/gallery/steamos-genshin-launcher.webp) | ![Magical Girl Witch Trials story scene](../../assets/gallery/steamos-witch-trial.webp) |

## Download and Installation

Select `steamos` in GitHub Actions under **Build products → Run workflow**. Once the build finishes, download `SunUEFI-Module`, `SunUEFI-ESP`, and `SunUEFI-Root-steamos`. The ESP image is shared with other distributions. The installation sequence in the Boot Helper is identical to Debian: create the dedicated storage partitions, select the ESP and root images, flash, and boot.

The root image is 16 GiB, so the dedicated root partition must be at least the same size. The user account is `piano` with password `piano123456`. Launching Steam for the first time requires an internet connection to check for client updates and sign in.

Steam's system update entry uses pacman package updates and shares repositories with KDE Discover; it does not invoke Valve's A/B image updater. Device-specific packages for Mesa, Gamescope, FFmpeg, sensors, and platform management are currently pinned via `IgnorePkg` and updated with BSP images. This is only a development measure to avoid overwriting device adaptations and does not yet constitute full rolling-update support: local packages may still need rebuilding after other libraries upgrade. Kernel and BSP updates are still flashed via the SunUEFI Boot Helper. Steam client self-updates remain enabled.

IBus Pinyin is included by default; press `Ctrl+Space` to switch between Chinese and English. KDE launches the native Wayland input method through KWin, while Gaming Mode provides GTK, Qt, XIM, and SDL input through `gamescope-ibus.service`, which stops with the session. The build overrides the English-only defaults from the Frame account and drops duplicate desktop autostart entries; exiting Gaming Mode clears the dedicated input environment to avoid interfering with KDE. The image installs Chromium via pacman and removes Frame's Flatpak installer entry. KDE only shows the launcher provided by the Chromium package itself.

The IBus Xwayland candidate window uses the X display of the same session instead of creating a parentless Wayland popup, preventing text boxes from losing focus and clearing preedit text when candidates appear. Native Wayland still uses the dedicated input-method surface. This fix is built into the `ibus` and `libibus` packages, and the package update policy retains both adapted packages.

GTK 3 XInput2 property events query stylus atoms using the device's own X display rather than the default display (which may be Wayland), preventing crashes when switching sessions, changing virtual terminals, or exiting the desktop while IBus has both displays open simultaneously. Upstream GTK 3 has [Alynx Zhou's corresponding fix](https://gitlab.gnome.org/GNOME/gtk/-/commit/c3b824a8f435dc975b2bee21a81254e3bb5a2229); the local patch retains the XI2 portion and is built with the `gtk3` package without disabling the Xwayland candidate window or altering the stylus device source.

Natural scrolling for the touchpad is enabled by default in Gaming Mode. Steam has no settings page for this; in KDE Desktop Mode, it can be adjusted in System Settings under Touchpad. Both sessions store their input preferences independently.

The GPU render node and the display controller are probed separately. When automatically selecting a display device, Gamescope skips the firmware framebuffer `simpledrm` and waits up to 10 seconds for native KMS while dispatching device events, avoiding grabbing the placeholder display's pixel format and 60 Hz mode on first boot. DRM node numbers are not hardcoded; explicit device selection and DRM lease paths remain unchanged. On timeout, it prints the reason, exits, and closes unselected device handles, without adding userspace driver load-order workarounds or boot delay scripts.

## Building

Use the project's ARM64 Debian build container with `requirements-build.txt` installed, running as root in a private mount namespace. The steps match CI:

```sh
./build.sh sources
./build.sh uefi
./build.sh module
./build.sh linux
./build.sh steamos-rootfs
./build.sh package \
  --rootfs build/distros/steamos-piano/rootfs \
  --rootfs-manifest build/distros/steamos-piano/manifest.json \
  --initramfs build/distros/steamos-piano/initramfs/initramfs.cpio.gz \
  --root-size-mib 16384
./build.sh installer --bundle artifacts/release
```

`config/steamos.json` pins the Valve image, casync data source, full image SHA256, and extractor commit. `config/steam-client.json` pins the URL, size, and SHA256 of each public ARM64 handheld client package. `config/steam-runtime.json` pins the complete ARM64 Proton and Steam Linux Runtime. The build never reads any logged-in Steam directory from the host PC or tablet.

The base uses Valve's ARM64 `vr/main` 0.6.0, build `20261106.6191448`. It updates independently from the x86 Steam Deck builds and the Frame Beta branch; channels are not mixed based on version numbers.

Images normalize file timestamps to the source commit time by default; when `SOURCE_DATE_EPOCH=0` is explicitly set, one second is used instead of zero. Qt 6.11 treats a zero file timestamp as invalid, causing SDDM to skip all login configuration and fall back to the default Xorg path, which cannot drive the internal display. Valid timestamps ensure the Wayland and autologin configuration take effect directly on first boot.

The preinstalled Steam `.installed` file records directories, regular files, and symlinks in the client's expected format: length `-1` for directories, `-2` for symlinks, and the ZIP CRC of the target path string as the symlink checksum. Recording symlinks as regular files makes the client treat the installation as corrupt on every launch and force a redownload. The generic bootstrap preserves original symlinks, and client restarts pass both file and executable verification; the build cache also checks the generator digest and regenerates after format fixes. Normal online updates and account login are unaffected.

The PC installer converts images into Android sparse format using a fixed-size buffer before handing them to Fastboot for chunked transfer. `fastboot -S` itself only limits transfer chunk size and may still read an entire large raw image into memory, so it cannot replace pre-conversion. When flashing the 16 GiB root image on hardware, the fixed Fastboot peaks at roughly 69 MiB of memory, and roughly 103 MiB for ESP.

`tools/build_steamos_rootfs.py` assembles the root filesystem, and `tools/stage_arch_piano.py` shares firmware, kernel modules, and device services with Nyarch. `tools/stage_steamos_desktop.py` handles differences in KDE, Gamescope, and headset-specific services. The original VR session and its disk cleanup logic that automatically deletes games are not enabled.

ARM64 package builds and package-management commands run in a separate PID namespace; when a command exits, the kernel reaps its background child processes so temporary mounts can be cleanly unmounted afterward. This also handles package scripts that chroot or hold open `/dev` file descriptors. The build environment explicitly sets `SYSTEMD_IN_CHROOT=1` so PID 1 inside the new PID namespace is not mistaken for a booted system. Hardware CI previously exited after a full upgrade and locale generation because `/dev` remained busy; that exit path has been fixed. After background processes exit, leftover Unix sockets under `/etc/pacman.d/gnupg/S.*` are removed while preserving keys and trust data; otherwise, the final image scan would reject the `S.gpg-agent` special file even after all packages built successfully. Local successes relying on cached packages, manually installed dependencies, or hand-built packages cannot substitute for full CI assembly and packaging.

Each pacman package first runs `makepkg --verifysource` to download sources and verify recipe checksums. On failure, it waits 15 seconds and then 30 seconds for up to three attempts total; persistent failures abort the build without substituting sources or skipping integrity checks. Once sources pass verification, compilation runs only once—compile, patch, or packaging errors are not retried.

The original device's `steamos-dump-info.service` is also disabled. It writes headset EEPROM, battery, A/B slot, and U-Boot environment queries into the kernel log, producing misleading messages on the tablet such as "no battery" and "partition does not exist". The tablet's battery is reported through standard `power_supply` and UPower, and boot uses SunUEFI's ESP and dedicated root partition.

`usb=acm-ncm` in `/etc/piano/linux-debug.conf` provides both USB networking and an ACM serial port. The serial port is used to capture kernel logs independently; the implementation lives in the shared BSP at `usr/lib/piano/usb-network` and does not spawn an extra login shell or persistent logging daemon.

## FFmpeg and Graphics Runtime

FFmpeg is built from source by default as a native pacman package replacing the version in the rootfs, preserving the full codec configuration and dependencies of Valve's original package. The builder selects patches according to the target system's FFmpeg 7, 8, or 9 library ABI; newer SteamOS repositories ship FFmpeg 8.1.1, with documented source manifests for state-transition, legacy frame-buffer retention, P010 10-bit output, and encoder NV12 visible-line copy patches. The cache validates both the patch series and package hash, rebuilding and reinstalling whenever the series changes. Encountering an unsupported library version stops the build rather than mixing shared libraries across generations.

This overrides system FFmpeg and applications using it. Private codec libraries bundled with Steam or Electron are not system packages and cannot be forcibly replaced with ABI-incompatible shared libraries; their hardware video paths must be verified separately.

Mesa 27.0.0-devel in Valve's base image already includes Adreno 830v1, allowing Gaming Mode to render with Turnip; however, its native GL package lacks the GBM path required for a split MSM display node, preventing KWin from allocating display buffers. The build pins official Mesa 27 development sources via `config/mesa.json` (shared across all three products) and uses the full native Arch recipe to produce runtime packages under Valve's package names rather than stripping it down to a Freedreno-only build. The cache checks both the generated build recipe and package hash and never downgrades to older adapted packages. Softpipe retains the display-only buffer allocation fallback without forcing software rendering on the desktop. The Wayland protocol build dependency comes from the checksum manifest inside that Mesa commit itself and does not replace system Wayland libraries. Package naming retains Valve's `deckard-mesa-linux-aarch64`; x86 guest graphics runtime libraries remain untouched, and no Debian shared libraries are installed into SteamOS.

When allocating display buffers through Freedreno's renderonly path, buffer height is aligned up to the GPU's 4-row storage boundary while keeping the resource's visible dimensions unchanged. Otherwise, buffers for modes such as 2400×1602 or 1280×854 are smaller than the GPU import layout and are rejected by Mesa before modesetting. The fix only adds trailing padding to the buffer without altering display modes, DSI timings, or kernel size checks. Because the current official development commit still lacks this fix, the standalone SteamOS patch is kept; the chip ID and DirectX-Headers backports have been removed.

Plasma Wayland has been verified on hardware with Freedreno for OpenGL and Turnip for Vulkan; these results do not substitute for hardware video decoding or game compatibility testing.

Gamescope uses the reference project's mobile device adaptation with added support for split DRM nodes: the GPU node handles rendering, and the KMS-capable DPU node handles output. Gaming Mode runs at 3200×2136, and menus stay at 120 Hz per session configuration instead of automatically switching to the panel's maximum 144 Hz. The session uses `gamescope-session.service` so SteamOS Manager can detect Gaming Mode and switch to the desktop. After signing in, menus and switching back and forth between Gaming Mode and the KDE desktop all work. Milthm launches under ARM64 Proton and renders on Adreno.

Genshin Impact (CN) 7.1.0 enters the game world through the official launcher. Launching requires setting `STEAM_FEX_MULTIBLOCK=0` in the Proton environment; otherwise, thread suspension after scene loading deadlocks with FEX's intra-block spin, leaving the screen frozen on the user agreement page. See [Proton/FEX Cooperative Suspend](steamos-proton-fex-suspend.md) for root cause and verification.

The image preinstalls GE-Proton11-11 ARM64 and Steam Linux Runtime 4.0 ARM64, with tool files fully unpacked at build time and the default compatibility tool configured. Proton bundles FEX, DXVK, and vkd3d-proton, avoiding downloading an entire runtime on first game launch. Source commits and binary checksums for each component are recorded in `config/steam-runtime.json`. Steam login, game content, and subsequent updates still require internet access.

Backlight control follows SteamOS's restricted sysfs write interface, acquiring the required permissions before the session starts. `patches/gamescope/0002-backlight-avoid-double-dimming.patch` fixes the software brightness fallback in mobile Gamescope: when hardware backlight exists, lack of write permission must not cause the current brightness to also be multiplied into the framebuffer. Otherwise, moving the cursor triggers GPU composition and dims the screen, while keyboard input restores direct scanout and brightens it again. Software dimming and composited output remain active when no hardware backlight is present. The Gamescope build cache tracks digests of the source, patches, and package configuration, rebuilding on any change.

MSM does not expose the AMD plane color-conversion interface used by Gamescope. When color management is enabled, the same SDR gamut, gamma, and Night Mode transforms are applied via Vulkan to prevent color shifts between composited output (when the cursor is visible) and direct scanout (when the cursor disappears); direct scanout remains available when color management is disabled. This fallback adds one GPU composition pass and is not yet wired to the display controller's generic color pipeline.

Touchpad scrolling uses libinput's `natural_scroll` setting, applying only to devices tagged `ID_INPUT_TOUCHPAD` without inverting mouse wheels or touchscreens. Setting `GAMESCOPE_TOUCHPAD_NATURAL_SCROLL=0` in the gaming session environment restores traditional scroll direction after re-entering the session. KDE's default is configured in `linux/desktops/kde/config/kcminputrc`.

Gamescope's pointer adaptation preserves libinput's accelerated deltas while still providing raw relative motion to games. Stylus input delivers position, pressure, and tilt over the standard tablet-v2 protocol; interfaces without tablet protocol support, such as Steam, use a pointer-click compatibility path. This adaptation lives in `patches/gamescope/0006-input-tablet-and-accelerated-pointer.patch`.

`0007-compositor-preserve-input-damage.patch` fixes dropped input events while the compositor waits for display completion: it consumes the current frame's redraw flag before rendering while preserving pointer events arriving during the wait, and retains the original flag if frame pacing defers rendering. Measured touchpad report intervals have a median of ~7.1 ms. With the fix applied, under 120 Hz Gaming Mode with automatic GPU frequency scaling, a 4-second capture of continuous pointer movement displayed 477 frames with a dominant interval of 8.33 ms. This measurement covers cursor updates and does not mean all games achieve 120 FPS.

FFmpeg's H.264 8-bit test invokes Iris, decodes 60 frames of 720p video, and exits cleanly. HEVC Main10 has fixed P010 format mapping, decoding 60 frames of 720p without errors and producing a first frame byte-identical to software decoding. This verifies 10-bit video decoding and does not mean the HDR display pipeline is complete.

Nyarch's FFmpeg uses adaptations and ABI matching its own current packages; the 7.1 package here cannot be installed directly there.

## Aligning with the Handheld Experience

The base image is Valve's `VARIANT_ID=vr`, not the Steam Deck desktop image. Cross-referencing the [official Steam Deck 3.9.3 package manifest](https://steamdeck-images.steamos.cloud/steamdeck/20261015.100/steamdeck-20261015.100-3.9.3.manifest.pacman), the build adds KDE UI modules for Bluetooth, NetworkManager, display settings, and system info. The current base package is Valve ARM64 0.6.0, fully upgraded from Valve's public rolling repositories during build. `[core]` and `[extra]` come from `holo-packages.steamos.cloud/archlinux-deckard/archlinux/main`, not Arch Linux ARM repositories (`nyarch-gnome` is the target based on Arch Linux ARM). The public rolling repository also differs from the snapshot repository pinned to the original base build, so packages in it cannot be assumed to be rebuilt against the same library versions.

Repository package `.BUILDINFO` records from 2026-10-22 show that `libkscreen 6.6.3-2`, `kidletime 6.24.0-2`, `kwindowsystem 6.24.0-2`, and `powerdevil 6.6.3-1` were all compiled against Qt 6.10.2, whereas the upgraded system has Qt 6.11.0. This caused Wayland private-interface version mismatches: the brightness service saw no internal display, and Power Management triggered `SIGBUS` in the window plugin. These components are rebuilt as pacman packages from matching official KDE sources against the current Qt per `config/steamos.json`; `kscreen` also carries a build-order patch. The cache verifies the recipe, patches, Qt package version, and output package digests; cached packages are also reinstalled on resumed builds so earlier base upgrades do not overwrite them.

KWin 6.6.5 carries an additional generic backlight fix: the internal display uses maximum hardware backlight plus software brightness in HDR mode, so leaving HDR must invalidate the brightness cache to restore the user's setting through the standard backlight interface. The backported patch has merged upstream in [KWin !10404](https://invent.kde.org/plasma/kwin/-/merge_requests/10404) and landed in the 6.8 branch via [!10407](https://invent.kde.org/plasma/kwin/-/merge_requests/10407). Drop the local backport once Valve's package includes it; it does not alter HDR capability detection or output formats. Patched components always use the local package matching the recipe; repository packages that merely match the version and Qt symbols cannot replace these fixes.

After updating on hardware, `kscreen-doctor` detects the internal display properly, and setting 50% via the standard brightness interface results in both requested and actual backlight values of `1024/2047`. The Power Management page opens repeatedly, and screenshots show normal sleep options along with 2-minute screen-off and 10-minute suspend on AC power. A debug-time sleep inhibitor makes newer logind return `inhibited`, which causes KDE to temporarily hide sleep options; stopping the temporary inhibitor restores the capability query to `yes` without modifying desktop configs or faking hardware capabilities.

Local rebuilds only solve the current package combination. A long-term solution for routine `pacman -Syu` requires a unified base repository and a signed Piano adaptation repository rebuilt alongside dependency upgrades. Letting Arch Linux ARM and Valve base packages overwrite each other directly will not automatically migrate differing package names, versions, and private interfaces.

`stage_steamos_desktop.py` disables headset-specific services, original-device MAC address setup, `by-partsets/shared/home`, and offload mounts, and switches the desktop entry to native Plasma Wayland. Disabling the offload target alone is insufficient: user login and tmpfiles dependencies can still trigger mount jobs underneath it, causing repeated waits for nonexistent partitions.

Wi-Fi is managed by NetworkManager with wpa_supplicant, alongside Steam's natively supported `STEAM_USE_WPASUPPLICANT=1`. Editing NetworkManager configuration alone is not enough: on startup, Steam asks Manager to switch backends according to its own preference and may write iwd back.

Bluetooth is enabled by default. Because Steam applies its own Bluetooth preference in `-steamos3` mode, the build writes `1` to `InstallConfigStore/System/Bluetooth/Enabled` in a fresh `config/config.vdf`. After that, toggling is handled through normal Steam and desktop settings rather than a service repeatedly forcing power on.

CPU pinning limits left over from Frame on system services and user sessions are removed so applications can use all eight cores. SteamOS Manager does not expose factory reset (which depends on Valve A/B partitions) or Steam Deck-specific BIOS, dock, and fan controls. Reinstalling the OS is still done by flashing the root image via the SunUEFI Boot Helper. The ARM64 Steam client currently reports the CPU name as `Unknown` and reads frequency from CPU0's maximum frequency; the kernel's `/proc/cpuinfo` already exposes the full SoC name, all eight cores, and both frequency clusters.

`prepare_steam_client.py` handles first-login client extraction, launches the ARM64 binary directly, and preserves normal restart behavior on exit code 42. Preinstalled files include the client installation manifest so first launch does not redownload the entire client just because the manifest is missing. Online updates and sign-in remain handled by Steam. The launcher also creates the `sdkarm64` symlink and maintains valid log output so games do not exit due to a missing `steam-launch-wrapper` or invalid stdout passed into the runtime container. Gamescope forwards touch events by default, allowing games to receive multitouch input, though playability still depends on game support.

SteamOS Manager builds from Valve 26.4.1 sources with patches in `patches/steamos-manager/26.4.1/series.json`: when tracefs is absent, it disables kernel diagnostic capture while keeping desktop switching and other management features working. `tools/build_steamos_manager.py` pins sources, toolchain, and digests, and builds a native package matching the system.

In Gaming Mode, a short power-button press suspends via logind; in KDE, PowerDevil takes over with defaults of 2 minutes to screen-off and 5 minutes on battery / 10 minutes on AC to suspend. KDE recognizes 30, 48, 50, 60, 90, 120, and 144 Hz, with 120→60→120 Hz switching verified on hardware. Steam auto-brightness is not yet wired up: the ambient light sensor exposes illuminance via SensorProxy, while the client currently scans IIO directly, so forcing the toggle on alone will not fix it.

The KDE session explicitly selects the DRM backend so compositor restarts do not inherit stale display variables and accidentally pick a nested backend. KWin uses Adreno OpenGL ES 3.2 hardware composition; the Qt Quick software backend is not enabled, and game OpenGL/Vulkan capabilities are unaffected. Previously, the desktop OpenGL path triggered a QRhi context failure when edge-tiling preview created an internal Qt window, causing the compositor to exit.

KDE's Power Saver, Balanced, and Performance modes use the [shared TuneD power profiles](../hardware/power-profiles.md), each setting real CPU and GPU frequency caps. SteamOS Manager identifies the tablet via Device Tree and wires the gaming UI's three profiles to the same PowerProfiles service. The gaming UI's `low-power` maps to PowerProfiles' `power-saver`; switching profiles on the desktop syncs to the gaming side. CPU limits and boost are controlled uniformly by the profiles, so the gaming UI no longer exposes independent controls for those two settings that could override a profile. Regular pacman updates installing a newer Manager also install `piano-power-profiles`; after the power service restarts, the gaming session reloads the available profiles.

2026-10-21 hardware log: after cleaning up original-device services, Linux 7.3 kernel and system services boot in ~18 seconds excluding UEFI, reaching the Steam login screen. Full Gaming Mode, mode switching, hardware video decoding, and ADSP stability on 7.3 remain under investigation. Against the same SteamOS rootfs, ADSP and sensors work normally on 7.2.9, while 7.3 hits DSP crashes and is not yet released as an upgrade.

Further A/B testing confirmed: 7.3 still crashes with BORE, Gunyah, MGLRU, and dynamic preemption disabled, and replacing the older PAS and FastRPC drivers together does not resolve it either. Audio runs alone when sensors are stopped, and restarting the sensor service reproduces the ADSP watchdog. A draft patch retaining firmware memory mappings had no effect and was removed from the default build. 7.2.9 serves as the working baseline while keeping the newer SteamOS session fixes.

Subsequently reverting the entire SCM/PAS firmware interface, MDT loader, and their callers to 7.2.9—and then also reverting FastRPC and RPMh power domains—still reproduced the same ADSP watchdog failure on 7.3. These diagnostic reverts are not merged into default product patches and do not constitute an upgrade fix.

On 2026-10-22, three Qualcomm FastRPC teardown fixes were merged: initializing the device reference first, waking polling calls on DSP exit, and waiting for active calls to finish before tearing down DMA mappings. Hardware on 7.3 still reproduces the original DSP watchdog; these fixes address teardown races and do not prove that audio, sensors, or overall stability have been restored.

Audio initialization also removed an invalid readiness query inside `apm_probe()`: APR has not registered its receive callback yet at that point, so the DSP's normal reply was dropped, causing a fixed 5-second wait. The subsequent PRM driver still queries readiness through the normal interface. PRM queries also wait for both the APM and RPMsg parent devices to finish binding so synchronous child probing does not block GLINK from posting receive buffers; the status query uses a GPR message without an extra APM payload header. When SPF has not yet registered, firmware may return AudioReach `AR_ENOTEXIST=19` via a generic error reply; the callback treats this as not-ready, completes the wait immediately, and lets PRM defer probing instead of ignoring the reply until the 5-second timeout. Other DSP errors remain failures. Consecutive normal reboots show no `0x01001021` timeouts, while recording and stock gain are preserved.

Further hardware isolation pinpointed the trigger to runtime suspend of the VA microphone device: 7.3's PM-clock transition releases the last two shared core votes, immediately followed by the ADSP watchdog; holding those two votes keeps recording end and device idle working normally. `0013` holds registration-time references to `LPASS_HW_MACRO_VOTE` and `LPASS_HW_DCODEC_VOTE` for SM8750 only, leaving sample clocks managed by runtime PM and releasing the references on probe failure or removal. Other SoCs keep their original behavior. This fixes the reproduced trigger but does not mean long-term stability or standby power consumption has been fully tested.

Under normal PipeWire/WirePlumber, bidirectional test-tone playback and recording between the PC and tablet succeeded; ADSP stays running afterward with no diagnostic config disabling audio services. All four amplifiers run at stock gain 235 and microphone digital gain at +40 dB, sharing the same UCM as Debian.

The system has completed one `deep` suspend and PMIC alarm wakeup with the kernel logging success and both the desktop and ADSP recovering. The PMIC RTC reads the stock time offset handed off read-only by UEFI via NVMEM to set the system date at boot; when the offset is missing, calendar initialization is skipped while relative alarms remain functional. See [RTC and Stock Time](../hardware/sleep.md#rtc-and-stock-time) for provenance and safety constraints. Standby power draw, prolonged suspend, and recovery of all peripherals still need further verification.

## Sources

The root filesystem extractor comes from [SteamOS-ARM-Port](https://github.com/hashtagbasit/SteamOS-ARM-Port/tree/e54dac93afccf76e5fefe998399b6c1b1a23f3a1), retaining attribution under GPL-2.0. The Valve OS, Steam client, and bundled components remain governed by their respective licenses; the reference repository's GPL license does not cover those binaries. Exact download identities and build outputs are recorded in `SOURCE.json`, the rootfs `manifest.json`, and `packages.tsv`.
