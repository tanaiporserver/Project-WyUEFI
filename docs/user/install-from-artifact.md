# Installation Guide

Install Linux directly on the tablet using the SunUEFI Boot Helper: create dedicated partitions, flash the boot image and system image, then select Linux as the boot target. No computer or device serial number is required. For a brief walkthrough of a first-time installation, see [Getting Started](getting-started.md).

## Prerequisites

Your tablet must have an unlocked bootloader, KernelSU or SukiSU installed, and all important data backed up. Official Magisk requires a WebUI-capable manager such as MMRL.

Download three archives from the same complete run of [Build products](https://github.com/slwyts/Project-SunUEFI/actions/workflows/build-products.yml):

| Archive | File After Extraction | Purpose |
| --- | --- | --- |
| `SunUEFI-Module` | `SunUEFI-Piano-<version>.zip` | Boot Helper module |
| `SunUEFI-ESP` | `esp.sparse.img` | Boot image (ESP) |
| `SunUEFI-Root-<distro>` | `root-<distro>.sparse.img` | System image (root) |

Choose a system image from the [distribution list](getting-started.md#choose-an-os). There is only one ESP image and one module package, shared across all distributions. Development builds manually triggered for `uefi` or `linux` do not contain a full system. `SunUEFI-UEFI-Fastboot` is only for [temporary boot](temporary-boot.md), and `logs` is for troubleshooting builds; neither is needed for installation.

Save the module ZIP and both image files on the tablet. Do not extract the module ZIP; place the image files anywhere convenient so you can select them with the file chooser in the Boot Helper. Flashing sparse images requires Boot Helper 0.5.2 or newer.

## 1. Install the Boot Helper

Select the module ZIP on the Modules page in KernelSU or SukiSU. When prompted during installation:

* **Volume Up**: Install or update the boot setup while keeping the current Android kernel.
* **Volume Down**: Install only the Boot Helper; you can tap **Reinstall Boot** later on the **Maintenance** tab.

If the same boot version is already installed, it is skipped automatically. Reboot normally after installation finishes, then tap **Open WebUI** on the module card.

Custom GKI kernels are also preserved as-is as long as they use a compatible boot format—you do not need to revert to the stock kernel first. If the installer reports that the current BOOT checksum is missing or does not match its contents, confirm that it is the boot image currently booting Android normally, then press and release **Volume Down** three times within 30 seconds as prompted to continue; pressing **Volume Up** or letting it time out cancels the installation. Unrecognized kernel entry points, insufficient space, or images that cannot be restored intact will still abort the installation and display the reason.

> [!WARNING]
> When installing the boot setup for the first time, or reinstalling after a HyperOS update restores the stock BOOT, if the installer reports that it cannot confirm the bootloader is unlocked, a status-spoofing module may be hiding the real state. Writing the boot image on a device that only has temporary root and is not actually unlocked can brick the device. Once you are certain the device is genuinely unlocked, press and release the physical **Volume Down** key three times within 30 seconds to continue. Pressing **Volume Up** or letting it time out exits without writing anything. This confirmation applies only to the current installation.

## 2. Create Dedicated Partitions

Open the **Storage** tab, tap **Create Dedicated Partitions**, choose the capacity to allocate to the new system, and confirm. Reboot normally once it completes, then reopen the Boot Helper. Skip this step if `sunuefi_esp` and `sunuefi_root` already exist on the tablet. For notes on first-time partitioning on a new tablet, see [Known Issues](known-issues.md#first-time-partitioning).

## 3. Install System Images

Under **Install or Update System** on the **Storage** tab, select `esp.sparse.img` and `root-<distro>.sparse.img`, tap **Install Selected Images**, confirm, and wait for completion without rebooting or powering off. Flashing overwrites any existing OS and files on the dedicated partitions; Android data is unaffected.

## 4. Boot Linux

On the **Boot** tab, select **UEFI**, then **Linux**, and tap **Save & Reboot**. Subsequent normal boots follow this selection, and you can also temporarily choose Android from the boot selection menu. For login credentials and first-boot notes, see [Getting Started](getting-started.md#first-boot).

## Verifying the Installation

Tap **Check Boot** on the **Maintenance** tab; a "Boot OK" status confirms that both the module and boot setup are intact. Once the tablet reboots into the Linux desktop, installation is complete.

## Updating the System

Use your distribution's built-in package manager for routine updates. Images configured with the signed SunUEFI repository automatically update the project-provided kernel, Device Tree, and other packaged device components without reflashing the system image.

* Debian: run `sudo apt update && sudo apt upgrade`.
* Nyarch and SteamOS: run `sudo pacman -Syu`. SteamOS can also be updated in [Steam or Discover](steamos.md#system-updates).

Newer Arch images include `piano-sensors` out of the box and update it alongside the system. Sensor services on older images already work and do not need to be reinstalled. If `pacman` reports existing file conflicts when installing `piano-sensors` manually, check file ownership with `pacman -Qo <conflicting-path>`. Move only unowned legacy services or configs to a backup directory—keeping files owned by other packages, customized settings, and data in `/var/lib/piano-sensors`—then run `sudo pacman -Syu piano-sensors`. Reboot and verify sensors with `monitor-sensor`.

Before updating the kernel, run `sudo bootctl --print-esp-path` to verify that the boot partition is mounted. Newer pacman-based images mount it at `/boot`, while Debian mounts it at `/boot/efi`. If a new kernel fails to boot after a reboot, select the previous kernel from the 3-second `systemd-boot` menu. Reflashing the system image is only for reinstalling or switching distributions and overwrites existing Linux files; back up your data beforehand.

After a HyperOS update, tap **Reinstall Boot** on the **Maintenance** tab in the Boot Helper to restore the Combined BOOT.

## Troubleshooting

If boot fails, the screen stays stuck in Fastboot, or you want to return to Android, see [Recovery](recovery.md). When the Boot Helper is unavailable, you can update existing partitions by [flashing from a computer](install-from-computer.md).
