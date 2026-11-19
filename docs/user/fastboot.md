# UEFI Fastboot

UEFI Fastboot is a background USB Fastboot service provided while SunUEFI is running. It lets you read logs, capture screenshots, read and write partitions, and request boot actions. It works even if the screen is blank or no menu is displayed, and exits once the firmware hands control over to Linux.

## Differences from Stock Fastboot and Linux USB

| Name | Device Serial | When Available | Flashing |
| --- | --- | --- | --- |
| Stock bootloader Fastboot | Tablet's real serial number | `adb reboot bootloader` | Yes |
| UEFI Fastboot | `SunUEFI-piano` | While SunUEFI is running | Yes, by exact partition name |
| Linux USB networking / SSH | None | After Linux boots | N/A |

All commands below include `-s SunUEFI-piano` to ensure you target the UEFI service rather than the stock bootloader. The service stays connected across the UEFI menu, firmware setup, and Shell; switching pages does not disconnect it.

On boot failure, the error screen keeps status, log, and screenshot commands available as long as the USB connection remains active. Running `reboot` or `continue` requests a restart; the firmware saves settings, flushes storage writes, and safely shuts down peripherals first. `OKAY` confirms the request was received. If the tablet stays on the error screen, hold the Power button to reboot, then press Volume Up on the early boot selection screen to return to Android.

When an OTG adapter and peripherals (such as a keyboard or USB flash drive) are plugged into the USB port, the port is used by those devices and UEFI Fastboot is offline. Unplug them and reconnect the tablet to a computer to restore it.

## Checking Status

```sh
fastboot -s SunUEFI-piano oem help             # Available commands and arguments
fastboot -s SunUEFI-piano getvar product        # piano-sunuefi
fastboot -s SunUEFI-piano getvar version
fastboot -s SunUEFI-piano getvar storage-policy
fastboot -s SunUEFI-piano oem status
```

`oem status` also reports the failure stage of the latest Device Tree fixup, cold-boot memory check results, and the status and buffer header of the latest log snapshot. This information remains readable even when logs cannot be exported; querying it does not re-probe hardware or alter boot conditions.

## Logs and Screenshots

```sh
fastboot -s SunUEFI-piano oem ramlog
fastboot -s SunUEFI-piano get_staged uefi-ramlog.txt      # Up to 256 KiB

fastboot -s SunUEFI-piano oem screenshot
fastboot -s SunUEFI-piano get_staged uefi-screenshot.bmp  # 24-bit BMP
```

Screenshots are captured from the framebuffer and may not match what is currently visible on the physical display.

## Reading Partitions

```sh
fastboot -s SunUEFI-piano fetch <partition> <file>
```

Read-only. Each request transfers up to 256 KiB; the `fastboot` tool handles chunking automatically.

## Flashing Partitions

```sh
fastboot -s SunUEFI-piano flash sunuefi_esp esp.img
fastboot -s SunUEFI-piano flash sunuefi_root root.img
fastboot -s SunUEFI-piano reboot
```

Both raw images and Android sparse images are supported; the host `fastboot` tool splits large images according to the download size limit. SunUEFI Fastboot only permits flashing `sunuefi_esp` and `sunuefi_root`. On these two partitions, `erase` zeroes the entire partition, while `flash` writes the ranges covered by the image and leaves sparse holes untouched.

Unmount the target partition before flashing. If any files are open on the target partition, close them and retry. Each written chunk is read back and verified, then synced upon completion. If a write error occurs, re-flash the complete image before booting from that partition.

## Booting and Exiting

| Command | Action |
| --- | --- |
| `oem boot-stable` | Selects an OS according to firmware boot order; defaults to systemd-boot |
| `oem setup`, `oem shell`, `oem simpleinit` | Switches to UEFI setup, Shell, or the SimpleInit menu |
| `reboot` | Saves settings, safely shuts down peripherals, and reboots using the saved boot route |
| `continue` | Also reboots without changing the boot selection and does not directly boot Linux |
| `stage <file>` | Downloads a file into memory (max 64 MiB) |
| `oem sha256`, `oem discard` | Computes the SHA-256 hash of staged data, or discards it |

## Unsupported Commands

`flashing`, slot switching, and unknown `oem` commands are rejected. Use `oem ramlog` to read logs; `oem log` will fail. `boot <file>` is not yet a general-purpose OS or EFI boot entry.

If your computer does not detect `SunUEFI-piano`, see [Recovery](recovery.md).
