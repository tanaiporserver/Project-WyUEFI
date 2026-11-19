# Piano Auto-Brightness Policy

Adds an optional per-device profile inside GNOME's own power manager without adding extra services or altering raw sensor readings. `piano.ini` matches only `xiaomi,piano` with the measurement unit `lux`, waiting 1000/1000/5000 ms along the stock three-threshold curves. Other devices and setups without a matching profile keep upstream behavior. The profile installs to `/usr/share/gnome-settings-daemon/ambient-profiles/piano.ini`; the rebuilt GSD package must also be installed, as copying the profile alone does not enable this feature in an unpatched GSD.

Brightness adjustments use relative changes from the stock 108-point lux→nits curve and KTZ8866 exponential code values, anchored to the user's manual brightness while preserving GNOME smoothing. Dimming stops while the display is off. For curve sources, coefficients, zero-brightness recovery, and absolute brightness bounds, see the [Auto-Brightness documentation](../../../../docs/devel/hardware/auto-brightness.md).

## Default Release / CI

Standard default builds run `./build.sh gsd` to produce Debian `sid` packages, and the rootfs build consumes `build/gsd/runtime` alongside `SOURCE.json`. The tool clones a completed `sid` ARM64 base without modifying the input, restoring DNS, configuring APT source repositories, and installing build dependencies only inside the copy. The input must retain both binary and source indices for `sid`, verified against the Debian archive keyring.

```sh
sudo ./build.sh gsd \
  --output build/gsd --sysroot /absolute/project/build/completed-root --jobs 4
```

The source version is selected from the `Source` field of the target `sid` ARM64 binary candidate, then verified against the signed `Sources`, `.dsc`, and archive SHA256 checksums. The cache is keyed to that source, the target desktop ABI, and the shared patches; packages from other Debian suites or older GNOME versions are never reused. The build follows the original `debian/rules`, `debian/control`, and `quilt` workflow, skipping the upstream test suite while preserving original features. Output collects the `daemon` and `common` runtime packages, `.changes`, `.buildinfo`, and provenance metadata.

## Debian sid / GNOME 51

Uses the `gnome-settings-daemon` source package from the distribution's signed APT repository along with its normal build dependencies. Debian and Nyarch share the ambient policy, keyboard hotplug, and boot lock-screen state patches in `51.0/series.json`; this series covers verified 51.0 and 51.1 releases without duplicating patches per distribution. When preparing a copy in a new directory, the tool appends these patches to the existing `quilt` series and applies them via `dpkg-source`; it never modifies the input directory or installs packages.

```sh
apt-get source gnome-settings-daemon
sudo apt-get build-dep gnome-settings-daemon
python3 linux/desktops/gnome/ambient-policy/packaging/prepare_package.py \
  debian /absolute/new/gsd-piano-build --source /absolute/gnome-settings-daemon-51.0
cd /absolute/new/gsd-piano-build
dpkg-buildpackage -b -us -uc
```

The distribution source must be a version supported by the series and use the standard `3.0 (quilt)` format; context mismatches fail during preparation and patch errors must not be ignored. This produces standard `gnome-settings-daemon` packages with `+sunuefi1` appended to the upstream source version. Replace the packages through the distribution's normal install/upgrade flow, then log out and back in for the new power manager to take effect.

## Arch / Arch Linux ARM 51

GNOME 51 uses the Shell brightness interface. The Arch recipe selects the version from the target `pacman` repository, requires the target's installed GSD to match the repository version, and reads the corresponding official release tarball SHA256 from the shared series. The prepared output records both source and patch digests and no longer downgrades repository 51.1 to 51.0.

```sh
python3 linux/desktops/gnome/ambient-policy/packaging/prepare_package.py \
  arch /absolute/new/gsd-piano-package --rootfs /absolute/project/build/distros/nyarch-gnome/rootfs
cd /absolute/new/gsd-piano-package
makepkg -s
```

The Nyarch package keeps the repository's upstream version and appends `.1` to the repository `pkgrel`; for example, repository `51.1-1` produces `51.1-1.1`. Display blanking and input wake continue to be handled through standard desktop and kernel interfaces.
