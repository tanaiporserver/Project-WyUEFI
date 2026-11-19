# Returning to Android & Recovery

SunUEFI does not replace the stock bootloader. Normal scanning and booting do not modify Android partitions; when manually performing [UEFI Fastboot flashing](fastboot.md#flashing-partitions), only the specified target is written. If something goes wrong, refer to the table below.

| Symptom | Cause | What to do |
| --- | --- | --- |
| Want to return to Android from Linux | The saved boot route is Linux | Run `piano-next-boot android --reboot`, or click "Return to Android" on the desktop |
| Want to switch back to Linux or Android from Android | The route is saved in the record area of the BOOT partition | Re-select it on the **Boot** page of the Boot Helper; without the helper, see [Changing the Boot Route via Command Line](#changing-the-boot-route-via-command-line) below |
| Screen stuck on stock Fastboot | No OS was selected, or the device was not rebooted after flashing | Run `fastboot reboot` on your computer |
| Computer detects `SunUEFI-piano` | The firmware is still running | Run `fastboot -s SunUEFI-piano reboot`; the firmware will save settings, flush storage, and reboot. When the boot selection menu appears, use the volume keys to choose Android |
| Completely unresponsive | System hang or stuck in early boot | Hold the power button to force a reboot until the boot splash appears. Hold duration varies by device; use your known stock force-reboot method |
| Want to enter Xiaomi stock Recovery | Need stock recovery features | Run `reboot recovery` in Android, or use the system reboot menu. This does not trigger SunUEFI and does not change the saved boot route |

If you still cannot return to Android after a force reboot, do not flash anything else. Note the screen state and the output of `fastboot devices` on your computer before asking for help.

## Insufficient Boot Partition Space During Updates

First run `sudo bootctl --print-esp-path` to locate the boot partition mount point, then check available space in that directory with `df -h`. Use `uname -r` and `sudo bootctl list` to distinguish the running kernel from older versions.
Keep the current version and at least one known-working fallback version, and uninstall only unused older kernels along with their matching version-specific packages.
Use the system package manager to uninstall them so the corresponding boot files are removed cleanly; do not manually wipe the boot partition.
If an update was already interrupted due to insufficient space, free up space and regenerate the boot files for the failed version first.
Verify that the kernel, initramfs, and Device Tree are all present before rebooting; see the [Build Guide](../devel/building.md#from-package-installation-to-boot-entries).

## Changing the Boot Route via Command Line

Run as root in Android. Select the BOOT partition based on the active slot:

```sh
BOOT=/dev/block/by-name/boot$(getprop ro.boot.slot_suffix)
piano-boot-request status --device "$BOOT"
piano-boot-request set --device "$BOOT" --target android
```

Valid values for `--target` are `android`, `menu`, `linux`, and `setup`. When no valid record exists in the BOOT partition, the system boots into Android by default.

## Stock Recovery

SunUEFI does not install to the Recovery partition. The stock bootloader does not load `vendor_boot` or `pvmfw` for a standalone Recovery image, causing boot to halt at Fastboot, so the installer rejects `--recovery`. If you previously flashed a similar image yourself, restore the stock Recovery image. Entering Recovery while a Linux boot route is saved has not been tested on-device yet.

## About Your Data

`fastboot boot` runs strictly in memory. The installer and Boot Helper only write to the `sunuefi_esp` and `sunuefi_root` partitions and do not touch any other partitions. Combined BOOT modifies the BOOT partition; restoring it to stock requires the stock BOOT image, or you can restore it losslessly with the [Combined BOOT restore tool](../devel/boot/combined-boot.md). Back up the BOOT partition before making changes.
