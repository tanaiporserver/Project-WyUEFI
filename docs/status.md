# Project Status

This page lists the current status by feature, including bring-up work completed by maintainers and contributors. The table on the home page is a simplified summary; technical details for each item are in the corresponding pages under `docs/devel/`.

## Test Device

The maintainer tests on a Xiaomi Pad 8 Pro (`piano`, model `25091RP04C`, SM8750P, 16 GB RAM) running stock Android 17 / HyperOS 4 (`OS4.0.5.0.XPYCNXM`) with an unlocked bootloader. Linux selects the BOE or CSOT panel based on the identifier passed by the stock bootloader; see [Panel Selection](devel/hardware/panel-selection.md). Combined BOOT supports both CN variant and Global variant Piano.

This device is partitioned with `sunuefi_esp` (512 MiB FAT32, 4 KiB sectors matching the UFS logical sector size) and `sunuefi_root` (256 GiB ext4), and has run Debian 13 / GNOME, Nyarch / GNOME, and SteamOS 0.6.0. The default kernel is `7.3.3-piano-sunuefi-cachy`, booted from Combined BOOT via UEFI, supporting switching back and forth between Android and Linux. All four distributions share the same kernel, BSP, and stock DSP firmware; the root image automatically expands to fill the partition on first boot.

## Boot & Installation

| Item | Status |
| --- | --- |
| Combined BOOT: Android passthrough | Working. Normal boot enters stock Android |
| Combined BOOT: Save Linux route & reboot | Working. Route is saved on the Android side; after rebooting, boots Linux from internal storage via UEFI |
| Standard EFI boot | Production builds use standard UKIs loaded by the resident core without systemd-boot installed; generic boot entries, BootNext, and arbitrary partition discovery are not yet implemented. See [Development Checklist](devel/release.md#standard-uefi-boot-and-storage) |
| Switch back to Android from Linux | Working. Run `piano-next-boot android --reboot`; see [Linux Next-Boot Entry](devel/boot/linux-next-boot.md) |
| Stock Recovery | Preserved. `reboot recovery` enters Mi Recovery. Behavior when a persistent Linux preference and Recovery are both active has not yet been tested on device |
| Standalone Recovery installation | Not working. ABL does not load `vendor_boot` or `pvmfw` on the "Recovery with built-in kernel" branch, stalling boot in Fastboot; Combined BOOT takes the normal boot path to bypass this. The installer rejects `--recovery` |
| Boot route management | Module saves the Android/UEFI route and reuses it on normal boots; the pre-boot selector screen displays for ~3 seconds, where volume keys temporarily override the choice and the power key confirms. Temporary selections do not overwrite the saved route |
| Combined BOOT online installation | Module extracts the stock Android kernel from the current BOOT partition and installs or updates the same UEFI core; after a HyperOS update, tap "Reinstall Boot". Existing core upgrades have been verified on device, though not all OTA scenarios are covered yet. See [Stock BOOT Repacking](devel/boot/combined-boot.md) |
| One-click installer | Updates existing `sunuefi_esp` and `sunuefi_root`; UEFI reads the stock Bluetooth address read-only and passes it to Linux via a standard Device Tree property, independent of installation-time configuration. First-time partitioning on a new tablet returns `NEW_INSTALL_NOT_READY` |
| Android Root module | Provided as an installable ZIP with boot selection, boot verification, reinstallation, and dedicated storage management. SukiSU installation and WebUI work; space reclamation after deletion works via early offline F2FS expansion. First-time partitioning on new devices and root size adjustment still need on-device verification. See [Boot Helper](devel/boot/android-module.md) |

## UEFI

| Item | Status |
| --- | --- |
| UEFI storage | Provides standard BlockIO read/write and ESP file access. Scanning reads GPT only and never auto-writes to disk; explicit flashing requests operate only on the specified target |
| Handoff & exit | Cleans up USB, UFS, clocks, and SMMU before booting Linux; refuses handoff if safety cannot be confirmed |
| Display | Displays menu after entering UEFI from Combined BOOT; defers stock ClockDxe CESTA/media and DCD startup initialization to preserve the inherited display state. See [Display Bring-up](devel/boot/uefi-display.md) |
| Menu / Setup / Shell | SimpleInit menu, TianoCore Chinese Setup UI, and 4x font scaling work; volume keys navigate, power key confirms, and both volume keys return. See [Setup UI](devel/boot/uefi-console.md); Shell has not yet been verified on device |
| Y700 Gen 4 touchscreen | Single-finger menu tap and swipe work on NT36536; see [Driver Notes](devel/hardware/y700-touch.md) |
| Piano touchscreen, keyboard, USB Host | Official keyboard input in firmware Setup is confirmed, and the touchscreen can read data frames. Touch tap, touchpad, and USB Host still need on-device interaction verification. Menu retains volume and power key controls |
| UEFI Fastboot | Runs in the menu, firmware Setup, and Shell; supports logs, screenshots, read, staging, flashing, and reboot. Raw/sparse writes and read-back verified on boot partitions. See [UEFI Fastboot](user/fastboot.md) for commands |
| EFI variables | Regular non-volatile variables are stored in dual-copy files under `persist/sunuefi/`; no UFS writes occur after ExitBootServices. See [Variable Storage](devel/boot/uefi-console.md#uefi-variables-in-persist) |
| Standard EFI boot | Selects OS by UEFI boot order. Verified booting SteamOS's standalone kernel, initramfs, and Device Tree via systemd-boot, as well as legacy UKIs |

## Linux & Hardware

| Item | Status | Known Issues |
| --- | --- | --- |
| Display | 3200×2136, 120 Hz default, lower-resolution scaling, 144 Hz, KDE HDR / wide color gamut toggle, 10-bit output, Night Light, standard Gamma/Degamma, and screen-off resume | 12-bit/FRC and color depth selection not yet provided; see [Display Color Depth](devel/hardware/display-depth.md) |
| GPU | Adreno-accelerated GNOME | Continuous resizing of some GTK windows commits ~30 frames/s; still distinguishing client-side processing from frame callback waits. See [Window Resize](devel/hardware/window-resize.md) |
| Touchscreen | Kernel reports standard multitouch directly; tap, drag, long press, and gestures work at ~144 Hz | 360 Hz not implemented |
| Keyboard / Touchpad | Typing, touchpad, backlight, and sleep resume work; backlight integrates with UPower / GNOME | — |
| Stylus | Focus Pen Pro positioning and pressure-sensitive drawing via kernel evdev at 120 Hz; GNOME native Graphics Tablet panel recognizes the physical SPI device; preserves magnetic attachment, battery level, charge stop when full, and auto re-pairing | Coverage for both styluses, gestures, and palm rejection: see [Stylus Protocol](devel/hardware/pen-protocol.md) and [Known Issues](user/known-issues.md#stylus) |
| Wi-Fi | Working | Fresh installations must save their own network connections |
| Bluetooth | Working with `p81` board-level NVM; fixed short connection range; automatically reads the device's stock address at boot | Provides a stable hardware-ID-derived fallback when the stock address is missing; see [Bluetooth Configuration](devel/hardware/bluetooth.md) |
| Linux USB 3 | Standard Type-C orientation and role switching, 5 Gbit/s USB debug networking | Continuing to verify initial connection timing; see [USB Type-C](devel/linux/kernel.md#usb-type-c) |
| External storage | USB flash drives, external HDDs, and card readers work; FAT32 / exFAT / NTFS read/write | — |
| Speakers | Quad-channel playback, volume control, and sleep resume; preserves stock amp gain 235 | — |
| Microphone | Recording, native mono ALSA / PipeWire input, and sleep resume; hardware gain +40 dB | — |
| Camera | Front/rear preview, video recording, AE/AF/AWB, and first-frame recovery via CAMSS/TFE hardware ISP | Implementation in [Camera & Flash](devel/hardware/camera-flash.md); manual controls in [Camera Controls](devel/hardware/camera-controls.md) |
| Camera privacy LED | Physical green indicator lights up while camera is active, toggling with video stream and front/rear camera switching | — |
| Flash | Dual standard LEDs accept torch on/off requests, read back zero when off, with no fault flags | Actual light output and exposure synchronization not yet confirmed; see [Implementation Notes](devel/hardware/camera-flash.md) |
| Sensors | Auto-rotation, ambient light, proximity, compass, and standard SensorProxy interface | Implementation in [Sensors](devel/hardware/sensors.md) |
| Auto-brightness | Works with desktop integration; GNOME policy follows stock lux thresholds and delays without modifying sensor data | See [Auto-Brightness](devel/hardware/auto-brightness.md) |
| Night Light / Color temperature | GNOME Night Light works; auto-generates an ICC profile for the built-in display | Generic sRGB profile, not stock factory calibration |
| Power | Dual batteries, PD and Xiaomi fast charging, hub charging and OTG, UPower, lid-close resume, suspend, and RTC/power-key wake; 2 min screen blank, auto-suspend after 5 min on battery / 10 min on AC | Disk hibernate disabled. See [Power Button](devel/hardware/power-button.md) |
| GNOME power profiles | Power Saver / Balanced / Performance profiles work (Balanced default), provided by TuneD and `tuned-ppd` | Profiles adjust CPU/GPU ceilings and do not change idle power draw; see [Power Profiles](devel/hardware/power-profiles.md) |
| Video codecs | ✅ | Iris hardware acceleration and V4L2 encode/decode work; adapted FFmpeg packages provided |
| SteamOS / Proton / FEX | ✅ | Game Mode and Desktop Mode work normally; ARM64 Proton and FEX work well (some individual games need their own launch options, unrelated to this project) |
| Fingerprint | Legacy transport path completed standalone Linux enrollment, storage, and authentication; self-contained Linux TEE path completed initialization and `fprintd` enumeration | New path still needs enrollment, template reload, and match/mismatch re-verification (legacy results are not carried over); see [Fingerprint Implementation](devel/hardware/led-ir-fingerprint.md#side-fingerprint-sensor) |
| IR / RGB LED | IR sends a 1-second packet in a single transmission via standard LIRC; RGB exposes a standard LED interface | IR carrier, envelope, and range unmeasured; RGB LED physical location unconfirmed |
| Default desktop | Chinese UI, Pinyin input method, Chromium browser, GNOME animations enabled | — |
| Windows / WinPE | Goal | Not yet booted |

### Other Distributions

CI builds Debian / GNOME, Debian / KDE, Nyarch / GNOME, and SteamOS, producing the module, ESP, sparse root images for each distribution, temporary-boot UEFI, and logs. Debian / GNOME, Nyarch / GNOME, and SteamOS have been run on device; full on-device experience for Debian / KDE still needs verification. All distributions share the same kernel and BSP.

The 120 Hz default is provided by a kernel panel driver patch and recorded in `config/release.json`, rather than switching refresh rates only after desktop login. BOE/CSOT panels are still selected by device identifier without hardcoding the vendor or removing other display modes. Suspend is enabled through standard systemd/logind interfaces, triggered by GNOME according to the idle timeouts above. Hibernate, hybrid-sleep, and suspend-then-hibernate remain disabled.
