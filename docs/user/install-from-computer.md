# Flashing Images from a Computer

This is a fallback method for updating existing `sunuefi_esp` and `sunuefi_root` partitions on the tablet using stock Fastboot from a computer. For routine installation, use the Boot Helper on the tablet; see https://github.com/slwyts/Project-SunUEFI/blob/main/docs/user/install-from-artifact.md .

> [!WARNING]
> Flashing `sunuefi_root` overwrites all files on the dedicated system partition; back up your data first. Never flash these images to any other Android partitions.

## Prerequisites

* The tablet has an unlocked bootloader and dedicated partitions (`sunuefi_esp` and `sunuefi_root`) already created with the Boot Helper. If not, tap **Create Dedicated Partitions** on the **Storage** tab in the Boot Helper first.
* A computer with Android platform-tools (`adb` and `fastboot`) installed and a USB-C data cable.
* Download `SunUEFI-ESP` and your desired `SunUEFI-Root-<distro>` from the same [Build products](https://github.com/slwyts/Project-SunUEFI/actions/workflows/build-products.yml) run, and extract `esp.sparse.img` and `root-<distro>.sparse.img`. These are Android sparse images and must be flashed with stock Fastboot—do not write them directly with `dd`.

## Flashing

1. Enable USB debugging in Android, connect the tablet to your computer, reboot into stock Fastboot, and check the device serial number:

   ```sh
   adb reboot bootloader
   fastboot devices
   ```

2. Replace `SERIAL` below with the serial number from the previous step, and replace the root image filename with the distribution you downloaded:

   ```sh
   fastboot -s SERIAL -S 128M flash sunuefi_root root-debian-gnome.sparse.img
   fastboot -s SERIAL -S 128M flash sunuefi_esp esp.sparse.img
   fastboot -s SERIAL reboot
   ```

The boot setup itself is still installed and maintained through the Boot Helper on the tablet and is not handled here.

## Verifying the Installation

After the tablet reboots into Android, select **UEFI** and **Linux** on the **Boot** tab in the Boot Helper, then save and reboot. On first boot, Linux automatically expands the root filesystem to fill the partition and reads the Bluetooth address automatically. The login username is `piano` and the password is `piano123456`.

## Troubleshooting

If boot fails or you want to return to Android, see https://github.com/slwyts/Project-SunUEFI/blob/main/docs/user/recovery.md .
