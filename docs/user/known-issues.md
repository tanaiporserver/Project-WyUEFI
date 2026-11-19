# Known Issues

Organized by user-facing symptoms, listing the impact and workaround for each item. For overall feature coverage, see [Project Status](../status.md).

## First-Time Partitioning

The Boot Helper provides **Create Dedicated Partitions**, but first-time partitioning on a fresh tablet has not yet been verified on hardware. If `sunuefi_esp` and `sunuefi_root` already exist on your tablet, select and flash the images directly without repartitioning. If you run into issues during partitioning, return to Android first and follow [Recovery](recovery.md).

**Delete & Reclaim Android Space** requires SukiSU with early-init configuration support; under other managers, the button is disabled and displays the reason. After confirming deletion, tap **Reboot to Finish Reclaiming Space**; early maintenance will expand the filesystem and continue booting Android. If an older module already deleted the partitions without increasing Android storage capacity, update the module and use the same button to complete the process—do not format or delete again.

## UEFI Menu and USB

The SimpleInit menu and TianoCore setup screens render properly; use the volume keys to navigate and the power button to confirm. The official keyboard already works in firmware setup, while UEFI touchscreen, touchpad, and USB Host are not yet usable for regular input or external peripherals. UEFI Fastboot remains connected across the menu, firmware setup, and UEFI Shell.

When a computer detects the UEFI device, you can query the firmware:

```sh
fastboot -s SunUEFI-piano getvar product
```

Returning `piano-sunuefi` confirms that the firmware is running. You can then run `oem boot-stable` to boot Linux or `reboot` to return to Android; see [UEFI Fastboot](fastboot.md). If the computer does not detect the device, see [Recovery](recovery.md).

## Power and Sleep

* Use suspend for daily sleep; hibernate (suspend-to-disk), hybrid sleep, and suspend-then-hibernate are disabled.
* Shutting down occasionally reboots into Android instead; the root cause is not yet known.
* When connected directly to a stock Xiaomi charger, fast-charging authentication completes automatically, and actual power adjusts with battery level and temperature. At high battery levels, charging switches to a 9 V / up to 2.5 A buck path rather than sustaining the rated 67 W. Battery drain can still occur if gaming load exceeds input power; exiting the game and letting the tablet cool down mitigates this.

## Display and Graphics

* Defaults to 3200×2136 at 120 Hz; refresh rates and lower-resolution source modes can be switched. Panel transport remains native resolution at 10-bit, with no other transport bit-depth options.
* KDE Plasma can toggle HDR and wide color gamut (both off by default). EDR is a separate brightness policy rather than an HDR mode; 12-bit FRC and 8/10/12-bit selection are not yet available.
* Some GTK windows drop to a low frame rate (around 30 frames committed per second) while continuously resizing.
* Night Light uses a generic sRGB profile rather than stock factory color calibration.

## Stylus

Focus Pen Pro supports positioning and pressure-sensitive writing at 120 Hz. The stylus is reported directly by the kernel; GNOME's native Drawing Tablet settings provide display mapping and calibration, and KDE provides display mapping, both barrel buttons, and pressure curve settings, with no extra configuration tool required. Select 120 Hz when using the stylus; pen input may still be unavailable at 144 Hz.

If touching the pen to the screen types a numpad `+`, update BlueZ. On Debian, run `sudo apt update` and upgrade as usual; on older Nyarch and Arch Denial installations, run `sudo pacman -Syu sunuefi/bluez-sunuefi` once and confirm replacing `bluez`. Pairing data is preserved; if you modified configs under `/etc/bluetooth/`, merge back any `.pacsave` files created by `pacman`. Newer images ship with the fixed package preinstalled and update normally thereafter. SteamOS continues using Valve's BlueZ.

Event mappings for short squeeze, long squeeze, double tap, and barrel swipe are retained, but have not yet been individually verified physically on the new kernel. Full palm rejection, weak hover, edge tracking, and tilt accuracy have also not yet been benchmarked, so drawing may still be affected by palm touches or edge inaccuracies. Older images supported hover, positioning, pressure, and side buttons on the standard Focus Pen, but the new kernel still lacks hardware test coverage with the standard pen. KDE settings have been checked on the full SteamOS image, though the effects of button actions and pressure curves on writing have not been tested individually.

## Camera and Other Hardware

* Both torch channels and V4L2 strobe flash have been verified to emit light. Standard camera apps cannot trigger the flash automatically yet, and no constant-on torch toggle is preinstalled on the desktop.
* Linux does not share fingerprints enrolled in Android. On Debian KDE, you can enroll fingerprints in System Settings and use them to unlock the Plasma lock screen; SDDM login still requires a password.
* Any special launch parameters required by Windows games (such as *Genshin Impact*) stem from the game or runtime itself and are unrelated to the firmware or OS.
* Windows / WinPE support is a project goal and does not boot yet.

## External Storage

* USB flash drives, external hard drives, and card readers work in Linux with full read/write support for FAT32, exFAT, and NTFS.
* Earlier images only included FAT32 support. Connecting exFAT or NTFS media detects the device and shows partitions, but opening them fails with `Filesystem type ... not configured in kernel`; switching to a newer image with this support resolves it. This is not a media or port defect.

## Other

* On a fresh installation, configure and save your Wi-Fi network manually through NetworkManager.
* The standalone SunUEFI image in Recovery has been dropped; see [Recovery](recovery.md#stock-recovery).
* Desktop and hardware features are primarily verified on the `25091RP04C` variant with a CSOT panel.
* `debian-kde` is built by CI; its desktop and hardware features have not been verified item-by-item on hardware as thoroughly as GNOME.
* SteamOS is experimental, and game compatibility varies by title; see [SteamOS](steamos.md).
