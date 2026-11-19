# Temporary Boot

Load UEFI into memory for a test run with `fastboot boot` without flashing BOOT. The next reboot will still use the original boot path. For regular installation, use the [Boot Helper](install-from-artifact.md).

Temporary booting does not install Linux. To boot an already-installed Linux system, the tablet must have `sunuefi_esp` and `sunuefi_root`. Only UEFI itself is temporary: firmware settings and files saved inside Linux can still be written to storage.

## Prerequisites

* Xiaomi Pad 8 Pro (`piano`) with an unlocked bootloader and important data backed up.
* A computer with `adb` and `fastboot` installed, plus a USB-C data cable.
* `SunUEFI-UEFI-Fastboot.img`: download `SunUEFI-UEFI-Fastboot` from Build products and extract it. You can also build it yourself; outputs are placed in `artifacts/product/`—see the [Build Guide](../devel/building.md).

This image contains only UEFI, without an Android kernel. By contrast, the Combined BOOT installed by the Boot Helper packages this same UEFI together with the device's current Android kernel.

## 1. Enter Stock Fastboot

Enable USB debugging in Android and connect to your computer:

```sh
adb reboot bootloader
fastboot devices
fastboot getvar unlocked      # Should show unlocked: yes
```

## 2. Temporarily Boot UEFI

```sh
fastboot boot SunUEFI-UEFI-Fastboot.img
```

An `OKAY` response only means the stock bootloader accepted the image. Next, confirm that UEFI is running:

```sh
fastboot -s SunUEFI-piano getvar product     # Should return piano-sunuefi
```

If the query returns nothing, UEFI may still be running with USB not yet connected; try running it a few more times.

## 3. Boot Linux or Return to Android

If Linux is already installed on the tablet, request booting from the ESP:

```sh
fastboot -s SunUEFI-piano oem boot-stable
```

UEFI selects a bootloader according to the boot order. By default, systemd-boot on the boot partition (ESP) loads the kernel and initramfs, then boots into the installed Linux system. To exit instead, reboot:

```sh
fastboot -s SunUEFI-piano reboot
```

## Troubleshooting

* White screen or unresponsive display: run `getvar product` first, then check [Known Issues](known-issues.md#uefi-menu-and-usb).
* Hung device or not detected by the computer: see [Recovery](recovery.md).
* Need logs or screenshots: see [UEFI Fastboot](fastboot.md).
