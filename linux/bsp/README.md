# Piano Device Configuration Layer

This directory stores device configuration decoupled from distribution and desktop choices. It is sourced from a pinned commit of the public `debian-piano` overlay, preserving licenses and per-file SHA256 checksums. `piano-device-config` is built as an `Architecture: all` `.deb` on Debian and an `any` package via `makepkg` on Arch.

The common package contains UCM profiles, driver load parameters, and hardware service units, and also configures session access permissions for the standard DMA-BUF system heap so applications can allocate camera buffers without depending on the camera service. The camera service and its WirePlumber configuration are installed separately by [`piano-camera`](../../docs/devel/hardware/camera-controls.md). The camera package depends on the front/rear camera module parameters in the common package and does not require a custom rootfs.

The stylus BlueZ initializer, systemd service, and radio parameters are installed together by `piano-device-config`. The package manager pulls in BlueZ and Python GI; scripts do not need to be copied from an image, and the package is not tied to a specific kernel version. Coordinates, pressure, and buttons are still reported by the matching kernel driver.

Keyboard backlight uses the standard kernel LED and UPower interfaces. `upower.service.d/20-piano-keyboard.conf` makes UPower wait for keyboard initialization so the initial scan does not miss the backlight node; this file is also installed by the Debian release adaptation tool. For the exact sources of services and programs, see the [Linux Service Source Map](../../docs/devel/linux/services.md).

Bluetooth HID loads `uhid` via the standard `etc/modules-load.d/piano-bluetooth.conf`, which is a local Project SunUEFI configuration. On the kernel side this corresponds to `linux/configs/piano-bluetooth.config`; Full / Next builds only allow adding `CONFIG_UHID=m`, and the module must match the target kernel. This configuration satisfies the prerequisite for BlueZ to create HID input devices and does not imply that stylus tip coordinates, pressure, or tilt are supported.

`usr/lib/piano/bluetooth-address.py` is invoked once by the existing wireless initialization script and reads the stock `persist` partition only for an onboard controller lacking an address. When the stock address is unavailable, it derives a stable address from the SoC model and serial number; already-configured controllers are skipped. It uses standard Bluetooth MGMT, adds no resident daemon, and never writes to stock partitions. Python and mount utilities are declared by the distribution recipes, so manually flashing a generic ESP no longer requires pre-populating an address.

UCM enables all four speakers, and regular stereo streams in PipeWire use standard `simple` remixing to duplicate left and right channels to the two speakers on each side, exposing a single output in normal desktop UI. All four amplifiers keep the stock gain of 235 (−7.5 dB). The microphone uses native mono PCM fed by DMIC2/DEC0, exposed as MONO desktop input with a default digital gain of +40 dB (`VA_DEC0 Volume=124`). This configuration is shared across Debian, Nyarch, and SteamOS with no extra virtual audio devices. The UCM diff is stored in `patches/0001-ucm-piano-microphone-and-speakers.patch` and installed by `piano-device-config`.

Desktop/GDM/dconf settings, user accounts, `machine-id`, SSH, package repositories, `fstab`/swapfile/userdata, `qbootctl` slot writes, and static USB address policies are not extracted. `80-drivers.rules` overrides systemd's rule of the same name and broadly blocks module autoloading, so it is excluded; do not treat this configuration package as a validated full BSP.

```sh
./build.sh bsp inspect --target debian
./build.sh bsp stage --target debian --output /absolute/new-stage
./build.sh bsp package --target debian --format deb --output /absolute/new-deb --version 0.1.0
./build.sh bsp package --target arch --format pacman --output /absolute/new-pacman --version 0.1.0
```

Debian defaults to `sid`, Ubuntu defaults to `noble`, and Arch Linux ARM defaults to `rolling`; Deepin requires passing `--suite` explicitly. For Arch builds, run as a non-root user in an environment with `makepkg` and `base-devel` installed; `--format pacman` produces `.pkg.tar.zst` directly. `manifest.json` in the package directory records the target, file manifest, and package SHA256. The same configuration works across distributions, but native binaries, firmware, kernel modules, and Mesa must still match each target system individually.

All four product images install the configuration through their package manager. Once the repository is configured, use `apt install piano-device-config` or `pacman -S sunuefi/piano-device-config`; for local packages, use `apt install ./<package-name>.deb` or `pacman -U ./<package-name>.pkg.tar.zst`. Both package formats preserve administrator-modified `/etc` files.

Older images copied these files directly. When migrating, check each conflicting path reported during installation with `dpkg-query -S` or `pacman -Qo` to confirm no other package owns it, then move the old files to a backup directory while preserving paths and permissions. After the package installs cleanly, compare the old `/etc` settings item by item and merge needed changes into the new configuration; if installation fails, restore the backup as-is. Files owned by another package should be handled through that package rather than force-overwritten. Run `./build.sh bsp inspect --against /absolute/target-root` on a host computer to inspect a migration target read-only.

If you previously edited `/usr/share/piano/pen/p81c-radio.json`, save your modified copy to `/etc/piano/pen-radio.json`; the stylus service in newer images reads that path first.

`layers.json` records existing producers for the native runtime, firmware sources, and kernel version constraints; it does not read personal build outputs. Component licenses such as kernel GPL and source BSD are preserved per component, and firmware remains subject to its original redistribution terms. Mesa builds from the official 27 development source pinned in `config/mesa.json` using each distribution's native recipe rather than copying Debian binaries to other distributions. The 830v1 chip ID and DirectX-Headers fixes are already in upstream source and need no backporting; the SteamOS renderonly display buffer height fix has not been merged yet and is kept separately.

## Audio Topology Package

`piano-audio-topology` provides the open-source AudioReach topology loaded by the Qualcomm ASoC driver. It does not contain stock ADSP firmware or tuning. All four images install it via APT or the shared `sunuefi` repository; the driver continues loading the same file from the standard firmware directory with no additional services.

For local builds, add `--audio-topology` to the `bsp package` command above. Before installing on an older system, run `bsp inspect --audio-topology --against /absolute/target-root` to check ownership of identically named files and compressed variants. Back up untracked legacy files using the migration steps above; if a distribution package already owns a file of the same name, keep its ownership.

## Local Firmware Packages

`piano-firmware-touch` contains only the BOE and CSOT touch firmware files for Xiaomi Pad 8 Pro. Extract both files from `odm/firmware` of a stock ROM matching the version listed in the [firmware manifest](../../vendor/firmware/piano-touch/manifest.json), then pass that directory and the target system root:

```sh
./build.sh bsp package --touch-firmware /absolute/odm/firmware \
  --against /absolute/target-root --target debian --format deb --output build/touch-deb
./build.sh bsp package --touch-firmware /absolute/odm/firmware \
  --against /absolute/target-root --target arch --format pacman --output build/touch-pacman
```

`piano-firmware-display` contains the BOE and CSOT display calibration files; the kernel loads `qcom/sm8750/piano-{boe,csot}-display.bin` according to panel model, with no extra service needed. Point `--display-firmware` to `vendor/firmware/piano-display` in this repository or the output directory of the [calibration generator](../../vendor/firmware/piano-display/README.md). All four product images ship calibration via native packages; calibration packages are not published to public repositories. The build verifies both files against the [firmware manifest](../../vendor/firmware/piano-display/manifest.json).

```sh
./build.sh bsp package --display-firmware vendor/firmware/piano-display \
  --against /absolute/target-root --target debian --format deb --output build/display-deb
./build.sh bsp package --display-firmware vendor/firmware/piano-display \
  --against /absolute/target-root --target arch --format pacman --output build/display-pacman
```

SteamOS and Nyarch share the same `any` pacman package. Both touch and display packages explicitly read local inputs. Building the display package alone does not require a target; before installing, use `--against` to check package ownership and identically named `.xz` or `.zst` firmware files on the target system. If an older image has untracked files, back them up using the migration steps above before building; never overwrite files owned by other packages. If the target system changes after building, re-check with `./build.sh bsp inspect` using the original component flag and `--against <target-root>`. `pacman` checks only exact path matches on install and does not check compressed aliases.

Copy the package to the target system and install it with `apt install ./<package-name>.deb` or `pacman -U ./<package-name>.pkg.tar.zst`. Roll back by reinstalling a saved older package, or uninstall explicitly with `apt remove <package-name>` or `pacman -R <package-name>`. Display calibration loads the next time the kernel initializes the internal panel; standard SDR remains usable after removing calibration. If your system embeds calibration in the initramfs, rebuild it using your distribution's procedure before rebooting.
