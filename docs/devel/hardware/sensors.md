# Piano Sensors

Release builds use the pinned [piano-sensors](https://github.com/blu-sharky/piano-sensors/tree/65a92202db65ad13493b43fbf15d6abb3b7bcf92) commit `65a92202db65ad13493b43fbf15d6abb3b7bcf92`, located in `upstream/piano-sensors-current`. It provides the standard FastRPC/SSC userspace, device data import, and `iio-sensor-proxy` startup ordering. Local patches add read-only import of the vendor file service configuration and load `socinfo` before starting the PD; no custom daemon is written.

## Build and Package Sources

Run inside a Debian sid root ARM64 builder; on other hosts, pass `--sysroot` pointing to a completed sid ARM64 root directory:

```sh
./build.sh sources
./build.sh sensors
./build.sh release-rootfs
```

`sensors` writes to `build/sensors` by default. It requires the source `HEAD` to match the pinned commit with a clean working tree, copies the source into `upstream/` under the output directory, and applies the patches listed in `config/release.json`. The builder fetches `libssc` and `iio-sensor-proxy` from signed sid `Sources`, verifies the SHA-256 of the `.dsc` and all archives, and builds packages through the original Debian quilt, Meson, and debhelper recipes without running the upstream trixie build script. The upstream checkout stays clean.

```sh
sudo ./build.sh sensors build/sensors --sysroot build/distros/debian-sid/rootfs --jobs 8
```

`--sysroot` copies the input root directory read-only; build dependencies, APT sources, and build tools are only installed into an isolated copy inside the output directory. `--libssc-only` remains available to build only the library and CLI. Importer patches are registered against upstream package version 5; target source versions and local package revisions are recorded centrally in `patches/piano-sensors/5/build.json`, and runtime dependencies are generated against the target sid libraries. The full package cache verifies target library versions, signed source identity, original Debian recipes, and patch hashes; passing `--resume` on the same directory reuses completed source packages and partial build objects without re-downloading verified archives or reconfiguring finished packages.

The official CI runtime set consists of six packages:

| Package | Version for Current Pinned Source |
| --- | --- |
| `piano-sensors` | sid build `5+sunuefi2`, arch `all` |
| `fastrpc-support`, `libfastrpc1` | standard sid package `1.0.9-1`, arch `arm64` |
| `libssc2`, `libssc-bin` | `0.4.4-2.1+piano1+sunuefi3`, arch `arm64` |
| `iio-sensor-proxy` | `3.9-1+piano1+sunuefi1`, arch `arm64` |

FastRPC uses standard binary packages verified against signed sid `contrib` `Packages` indices instead of downloading `trixie-backports` packages. The base recipe enables both binary and source indices for `main contrib`. The `libssc` property type fixes and public vector interface, as well as the `iio-sensor-proxy` startup claim-order fix, are not yet present in these sid sources, so the corresponding patches are retained. Versions, signed index and archive hashes, and the six-package set are all recorded in the output manifest.

`tools/build_release_rootfs.py --sensors-dir` defaults to `build/sensors/runtime`. All six `.deb` files must sit directly in that directory; `SOURCE` and `SHA256SUMS` may reside together in either its parent directory or the directory itself. After a successful top-level build, the written `SOURCE` file contains:

```json
{
  "source_url": "https://github.com/blu-sharky/piano-sensors.git",
  "source_commit": "65a92202db65ad13493b43fbf15d6abb3b7bcf92",
  "patches": [
    {
      "path": "patches/piano-sensors/0001-import-vendor-reg-config.patch",
      "sha256": "<patch SHA-256 recorded at build time>"
    },
    {
      "path": "patches/piano-sensors/0002-fix-libssc-property-types.patch",
      "sha256": "<patch SHA-256 recorded at build time>"
    },
    {
      "path": "patches/piano-sensors/0003-libssc-raw-vector-reports.patch",
      "sha256": "<patch SHA-256 recorded at build time>"
    }
  ]
}
```

The rootfs builder validates the source fields, patch list and patch hashes, and the SHA-256, package name, architecture, and exact version of each runtime package, requiring all six packages to be present. Upstream CI trixie binary packages do not satisfy the sid release input requirements. Paths in the checksum file are relative to its containing directory, so extraction must preserve the `runtime/` layout; dev packages, debug packages, `.changes`, and `.buildinfo` remain in `sources/` for build verification and do not need to be distributed with the runtime set.

These packages are installed via standard APT with signature checking intact to resolve dependencies. The current release rootfs baseline also requires `erofs-utils`, `libqmi-glib5`, `libqmi-proxy`, and `libqrtr-glib0`; APT resolves `libmbim-glib4` and `libmbim-proxy` as well, while the remaining runtime libraries follow the existing rootfs ABI. Offline installations must also supply all of these dependencies.

## Startup and Device Data

ADSP is auto-started by kernel `remoteproc`; userspace starts in the order `piano-sensors-import.service` → `adsprpcd-sensorspd.service` → `iio-sensor-proxy.service`. The sensors PD depends on stock configuration import and waits for standard FastRPC device nodes rather than project scripts to boot the DSP. `ExecStartPre` loads `socinfo` and `fastrpc`. After `adsprpcd sensorspd` starts, the upstream script waits up to 60 seconds and queries `accelerometer`, `light`, `proximity`, and `compass` in sequence. On timeout, startup still proceeds and logs the missing types, so an active service does not guarantee all four sensor types are functional.

The upstream package masks `adsprpcd.service` (root PD) and `adsprpcd_audiopd.service`, and overrides FastRPC's rule that auto-starts all DSPs via `/etc/udev/rules.d/59-fastrpc-remoteproc.rules`. The kernel handles ADSP boot before the sensors PD starts; the current audio GPR path does not depend on the audio PD.

On first boot, the importer populates `/var/lib/piano-sensors` from this tablet's own ODM configuration, vendor file service configuration, and `persist` registry/calibration data. Vendor `/etc/sensors/sns_reg_config` is read via a read-only device-mapper target and `dump.erofs`, verified non-empty in a temporary file, atomically saved to `/var/lib/piano-sensors/vendor/sns_reg_config`, and linked to `/vendor/etc/sensors/sns_reg_config`. ODM also uses a read-only mapping; `persist` remains mounted `ro,noload`, and subsequent ADSP updates are written to the imported copy. The build does not copy user data from the internal logs directory, nor does it distribute these configuration or calibration files with the image.

By default, the importer reads primary metadata slot 0 of `super` and selects `odm_a` and `vendor_a`. Verify this against the current ROM's logical partition layout before deployment: `PIANO_SENSORS_ODM_NAME` / `PIANO_SENSORS_VENDOR_NAME` override partition names, `PIANO_SENSORS_ODM` / `PIANO_SENSORS_VENDOR` supply an existing read-only EROFS device or image directly, and `PIANO_SENSORS_VENDOR_LINK` overrides the symlink path. The name variables do not change how metadata slot 0 is read. Successful package installation alone does not guarantee that the current ROM's configuration was imported.

The stock vendor text file on this device is version 1, containing input/output/property paths without the `hw_platform` mapping seen in examples from other devices; the file of the same name under `persist` is a parsed-JSON timestamp cache, and the two are not interchangeable. Field logs confirmed that SSC QMI service 400 is reachable and the `registry` SUID is discoverable, while the `accel` query returned an empty UID. Readings after completing the configuration still require verification after a clean reboot; do not mask empty UIDs by extending timeouts or injecting hardcoded platform values.

`system_heap` is not currently a production service dependency. During a cold boot on 2026-10-08 with this heap enabled, reverse RPC returned `0xe`, whereas the earlier `fastrpc` ioctl fallback was able to read JSON. Confirmed differences between the two paths:

* [FastRPC 1.0.7 rpcmem](https://github.com/qualcomm/fastrpc/blob/v1.0.7/src/rpcmem_linux.c) prefers opening `/dev/dma_heap/system` and falls back to the `fastrpc` allocation ioctl only if opening fails. The former imports a DMA-BUF backed by regular pages; the latter allocates via `dma_alloc_coherent` on `compute-cb`, constrained by its DMA mask.
* The current [fastrpc mapping](../../../patches/linux/7.3/0011-fastrpc-drain-ssr-invokes.patch) takes only the first scatter-gather (SG) DMA address and passes a single contiguous address range to the DSP. Under an identity/direct domain, discontiguous physical SG lists cannot satisfy this representation; a translated DMA domain allows the IOMMU to provide a contiguous IOVA.
* The minimum listener buffer size is 4 KiB, so single-segment cases also need checking. High physical pages under `DMA32` may bounce through SWIOTLB; `system_heap` provides CPU/device sync callbacks, yet neither `rpcmem`/listener invokes DMA-BUF CPU sync APIs nor does `fastrpc` perform matching SG sync. Until the failing buffer's physical/DMA addresses, SG segment count, and sync state are captured, bouncing is not treated as a confirmed root cause.
* In [AEE error definitions](https://github.com/qualcomm/fastrpc/blob/v1.0.7/inc/AEEStdErr.h), `0xe` is `AEE_EBADPARM` and `0x14` is `AEE_EUNSUPPORTED`; `mod_table` log messages cannot be interpreted directly as kernel `-EFAULT` or an SMMU fault.

`patches/linux/7.3/0001-platform.patch` selects a standard DMA domain for SM8750 `compute-cb` while keeping existing policies for other devices; simply dropping preloading does not cover cases where camera or other features enable the heap. A temporary namespace was used only for an earlier allocation-path comparison and is not a production solution.

The local native package was built as `5+sunuefi1` using a Debian trixie ARM64 sysroot, QEMU, and standard `dpkg-buildpackage -b -us -uc -aarm64`, keeping `socinfo` and removing `system_heap` preloading. `build/sensors-patched/runtime` reuses the other five packages from official CI; `SOURCE`, `SHA256SUMS`, and `BUILD.json` in the parent directory record source/patch/package hashes and build provenance, and can be passed directly to `--sensors-dir`. A passing package build does not mean the DMA mapping issue above is resolved.

## Runtime Status

On 2026-10-08, after booting the current `a8+CPUCP` build via normal BOOT, the standard SSC client returned 3-axis gyroscope data, continuous compass readings, and 0 lux for ambient light. Gyroscope support already exists in `libssc`; although omitted from CLI help, `ssccli --sensor gyroscope --timeout 3` works. The `m/s` unit printed in its output is not a verified angular velocity unit; future fixes must follow the protocol rather than converting values based on that string.

After verifying data types against the device's stock `sensors.qsh.so`, normal `libssc` SUID queries confirmed availability of `rgb` (STK3BCX), `cct_front` (STK3BCX front color temperature), `ambient_light_back` and `ambient_light_back_strm` (SIP1328 rear ambient light), and `flicker` (SIP1328). Timed subscriptions to front color temperature, rear ambient light, and flicker all received raw SSC reports and closed cleanly. The first two use standard float payload 1025; flicker uses a separate format 769, and unknown fields are not forced into lux, Kelvin, or Hz labels. Rear ambient light events carry six floating-point components, which the stock handler forwards verbatim to Android. This proves channel connectivity and data delivery, not full semantic, desktop policy, or calibration verification.

The library also had three property declaration bugs: `available`, `sample-rate`, and `stream-type` were registered as strings while their getters wrote boolean/float/uint values, causing GLib type-check failures on read. The standard packages fix this via Debian quilt and build standard ARM64 library, CLI, dev, and GIR packages using the original `debian/rules`. Default builds include this patch; to rebuild only the library, run `./build.sh sensors build/libssc-only --libssc-only`, though those two packages alone cannot substitute for the full six-package release set. The fix neither alters sensor payloads nor pads values with constants. This issue is at a different layer from QMI returning measurements; fixing property declarations does not constitute completed hardware calibration.

The matching updated `libssc2` and `libssc-bin` packages were installed on the device and standard SensorProxy was restarted. Property reads returned `available=true`, `sample-rate=26.0`, and `stream-type=0`; the gyroscope continued returning 3-axis data, and stderr no longer emitted the type assertions above. The full six-package input passed the rootfs consumer check; because the next root image had not yet been generated at that time, the on-device installation result should not be equated with a deployed new image. GIR files are generated in the same source build, and the library SONAME remains 2.

A subsequent `+sunuefi2` revision added the [public vector interface and timed client](ssc-vector.md). The default release configuration enables patch 0003, and subscriptions for front color temperature, rear ambient light, and flicker on the tablet all received data and closed cleanly. It preserves all components, assigns no physical units to unknown data, adds no new daemon, and leaves GNOME's existing ambient light interface unchanged. Standard package dependencies prevent mixing the new client with the old library.

Raw logs are stored in the internal logs directory (`a8-ssc-gyroscope.txt`, `a8-ssc-vendor-inventory.jsonl`, and `a8-ssc-vendor-payloads.jsonl`). Stock ELFs are only inspected statically and are never executed under Linux.

On 2026-10-08, `7.2.9-piano-gnome-g086a94c4529d` booted via normal BOOT with all six `compute-cb` instances reporting `iommu_group/type` as `DMA`. The standard `5+sunuefi1` service ran with `/dev/dma_heap/system` visible and `InaccessiblePaths`, `RootDirectory`, and `RootImage` all empty, without using the earlier temporary workaround that hid the heap.

SensorProxy reported `HasAccelerometer/Light/Proximity/Compass` all as `true`; a three-second sample returned accelerometer values around `(8.49, -0.08, 4.75) m/s²`, compass around `195°`, and `LightLevel` at `153`. This confirms that SSC data reads work under normal heap conditions in testing, but cannot be generalized to claim that all buffers, all sensors, or suspend/resume are verified. Desktop auto-rotation still requires on-device verification. An earlier `g086` boot failed to expose a usable debug interface; this successful boot does not explain that loss of connectivity.

GNOME's D-Bus subscribers included `gnome-shell`, `gsd-power`, and `gsd-media-keys`, with `ambient-enabled=true`. During a natural lighting change, `LightLevel` dropped from 138 to 124 and the KTZ8866 backlight dropped from 1256 to 1135 without manual brightness writes, confirming that standard desktop auto-brightness responds; the full brightness range has not yet been tested. Auto-rotation is not currently managed by Mutter: the keyboard cover exposes a mouse/touchpad without a tablet-mode switch, so GNOME 48.7 exits touch-mode per standard policy. Rotation with the keyboard detached remains to be verified, and this policy is not forcibly bypassed.

Local raw logs are stored in the internal logs directory, including `g086-live-journal.txt`. Standard service logs can be read with:

```sh
systemctl status piano-sensors-import adsprpcd-sensorspd iio-sensor-proxy
journalctl -b -u piano-sensors-import -u adsprpcd-sensorspd -u iio-sensor-proxy
```
