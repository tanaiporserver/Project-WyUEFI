# Getting Started

Follow these five steps to install Linux on your Xiaomi Pad 8 Pro (`piano`) for the first time. Android and its data remain intact.

## Prerequisites

* A Xiaomi Pad 8 Pro (`piano`). Both 16 GB and 24 GB models use the same image; the firmware detects the actual memory size at boot from the layout provided by the stock bootloader.
* Unlocked bootloader with KernelSU or SukiSU installed.
* Sufficient free storage on the tablet and a backup of important data. The dedicated partitions carve space out of Android's data partition.

## Choose an OS

All distributions share the same boot image (ESP, EFI System Partition); only the system image (root, root filesystem) differs.

| System Image | OS | Minimum Root Partition | Best For |
| --- | --- | --- | --- |
| `debian-gnome` | Debian sid + GNOME | 8 GiB | General-purpose daily desktop |
| `debian-kde` | Debian sid + KDE Plasma | 8 GiB | Users who prefer KDE Plasma; see [Known Issues](known-issues.md#other) for limitations |
| `debian-denial` | Debian sid + Denial | 8 GiB | Users who want APT and the Denial touch desktop |
| `nyarch-gnome` | Arch Linux ARM + GNOME (Nyarch) | 16 GiB | Rolling-release packages and the Nyarch theme |
| `arch-denial` | Arch Linux ARM + Denial | 16 GiB | Users who want pacman and the Denial touch desktop |
| `steamos` | Valve SteamOS ARM64 | 16 GiB | Gaming; see [SteamOS](steamos.md) for details |

## Installation

Download three packages from the same workflow run of [Build products](https://github.com/slwyts/Project-SunUEFI/actions/workflows/build-products.yml): `SunUEFI-Module` (Boot Helper module), `SunUEFI-ESP` (boot image), and `SunUEFI-Root-<distro>` (system image). Copy the extracted module ZIP and both `.sparse.img` files to the tablet, then:

1. Install the module ZIP on the Modules page in KernelSU or SukiSU. Press Volume Up when prompted to install the boot setup, then reboot.
2. Tap **Open WebUI** on the module card to enter the Boot Helper.
3. Open the **Storage** tab, tap **Create Dedicated Partitions**, choose the storage size allocated to the new OS, confirm, and reboot once. Skip this step if `sunuefi_esp` and `sunuefi_root` already exist on the tablet.
4. Reopen the Boot Helper, select the boot image and system image under **Install or Update OS** on the **Storage** tab, tap **Install Selected Images**, and wait for it to finish.
5. Open the **Boot** tab, select **UEFI**, then select **Linux**, and tap **Save and Reboot**.

For detailed instructions and tips on each step, see the [Installation Guide](install-from-artifact.md). Step 3 has not yet been verified on a fresh tablet; see [Known Issues](known-issues.md#first-time-partitioning).

## First Boot

On first boot, the root filesystem expands to fill the entire partition, so startup takes longer than usual. Once the desktop appears, log in with username `piano` and password `piano123456` (auto-login is enabled by default). The lock screen and `sudo` use the same password; change it in System Settings or by running `passwd`.

## Repositories and Updates

Images with the SunUEFI repository preconfigured can be updated directly through the package manager. On Debian, run `sudo apt update && sudo apt upgrade`; on Arch-based systems (Nyarch, Arch Denial, and SteamOS), run `sudo pacman -Syu`. Before updating the kernel, run `sudo bootctl --print-esp-path` to confirm the boot partition is mounted. If a reboot fails, select the previous kernel from the systemd-boot menu.

### Manually Adding the APT Repository

This repository targets Debian **sid ARM64**; do not add it to Debian stable. Download the [archive signing public key](https://sunuefi-repo.okassets.uk/keys/sunuefi-archive-keyring.gpg), run `gpg --show-keys --fingerprint sunuefi-archive-keyring.gpg`, and verify that the primary key fingerprint is `1B89 034B 2C88 8929 63C0 C1CA D7D5 F9A7 080E 6560`.

```sh
sudo install -m 0644 sunuefi-archive-keyring.gpg /usr/share/keyrings/sunuefi-archive-keyring.gpg
sudoedit /etc/apt/sources.list.d/sunuefi.sources
```

Add the following to `sunuefi.sources`:

```text
Types: deb
URIs: https://sunuefi-repo.okassets.uk/apt
Suites: sid
Components: main
Architectures: arm64
Signed-By: /usr/share/keyrings/sunuefi-archive-keyring.gpg
```

### Manually Adding the pacman Repository

After downloading and verifying the same signing public key, add it to pacman's keyring:

```sh
sudo pacman-key --add sunuefi-archive-keyring.gpg
sudo pacman-key --lsign-key 1B89034B2C88892963C0C1CAD7D5F9A7080E6560
sudoedit /etc/pacman.conf
```

On Nyarch, add only `[sunuefi]`. On SteamOS, add both repositories below in the exact order shown. Place them above the distribution's existing repositories while keeping the existing repositories and signature settings intact.

```ini
[sunuefi-steamos]
SigLevel = Required TrustedOnly
Server = https://sunuefi-repo.okassets.uk/pacman/$repo/os/$arch

[sunuefi]
SigLevel = Required TrustedOnly
Server = https://sunuefi-repo.okassets.uk/pacman/$repo/os/$arch
```

Save the file and run `sudo pacman -Syu`. `sunuefi-steamos` is exclusively for SteamOS; do not mix Valve packages into Nyarch.

## Charging Logs

`piano-charge-monitor.service` is disabled by default. To monitor charging, run in a terminal:

```sh
sudo systemctl start piano-charge-monitor.service
```

It logs every 2 seconds while plugged in and every 60 seconds on battery, resampling immediately whenever power is connected or disconnected. Logs include supply voltage, current, estimated power, fast-charging protocol and authentication status, dual-cell battery telemetry, temperatures, thermal throttling levels, and CPU/GPU frequencies. It is read-only: it does not modify charging settings or prevent suspend; sampling pauses during suspend and resumes on wake.

Logs are stored in `/var/log/piano-charge-monitor/`. Older logs are compressed automatically and capped at 20 MiB total, rotating out the oldest entries when full. To export logs or stop monitoring:

```sh
sudo tar -C /var/log -czf "$HOME/charging-logs.tar.gz" piano-charge-monitor
sudo systemctl stop piano-charge-monitor.service
```

To enable logging automatically at boot, run `sudo systemctl enable --now piano-charge-monitor.service`. To disable it at boot and stop it, run `sudo systemctl disable --now piano-charge-monitor.service`.

## Returning to Android

In Linux, run `piano-next-boot android --reboot`, or click **Return to Android** on the desktop. Selecting Android with the volume keys on the boot selection screen applies to that boot only. For other scenarios, see [Recovery](recovery.md).

If installation fails or you prefer flashing from a computer, see [Install from Computer](install-from-computer.md). To try Linux temporarily before installing, see [Temporary Boot](temporary-boot.md).
