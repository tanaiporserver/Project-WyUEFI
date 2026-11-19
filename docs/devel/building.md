# Build Guide

Build UEFI images, the Android Boot Helper, and distribution system images on a PC or in CI using `./build.sh`. All builds run strictly locally or inside containers and never write to a connected tablet.

## Environment Requirements

| Target | Build Host | Container Privileges |
| --- | --- | --- |
| UEFI image, Android Boot Helper module | x86_64 or ARM64 Linux with Docker | Standard container |
| Full system images (Debian GNOME / KDE, Nyarch GNOME, SteamOS) | ARM64 Linux with Docker | `--privileged`, for chroot and mounting |

GitHub Actions runs host checks on `ubuntu-26.04` and builds firmware, kernels,
and distribution images on `ubuntu-26.04-arm`. Actual build dependencies come
from the Debian build container below, managed separately from the GitHub
runner's Ubuntu version.

## Fetching Sources and Host Checks

```sh
./build.sh sources     # Fetch pinned upstream submodules without recursively downloading test data
./build.sh check       # Host checks; no tablet required
```

Upstream sources live under `upstream/`, pinned to specific commits. Local modifications live in `patches/`, `uefi/`, `linux/`, and `tools/`. Builds apply patches inside working copies under `build/`; do not edit those build copies directly.
When building only one component, use `./build.sh sources --group firmware`, `--group kernel`,
or `--group devices` to fetch only the submodules for that stage. They share the same source lock
as a full build and skip submodules for other stages as well as upstream test data.

## Build Container

The base image is recorded in `config/build-container.json` and defined in `containers/Dockerfile`. The Debian base image is pulled from ECR Public first, falling back to Docker Hub if metadata download fails; both sources are pinned to the same OCI digest.

```sh
python3 tools/build_container.py     # Build the sunuefi-builder image
```

APT package versions are recorded, though bit-for-bit reproducible output is not guaranteed.

## UEFI and Boot Helper

When building only firmware and the Boot Helper, run `./build.sh sources --group firmware` first.

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  git config --global --add safe.directory /workspace
  git config --global --add safe.directory "/workspace/*"
  python3 -m venv .venv
  source .venv/bin/activate
  python -m pip install -r requirements-build.txt
  ./build.sh uefi
  ./build.sh module
'
```

`./build.sh uefi` uses board inputs from `vendor/piano` and `patches/firmware`, producing `artifacts/product/PianoUEFI-product.img` and `manifest.json`. A full UEFI build also requires locally extracted stock assets; see [Local Inputs](local-inputs.md).

`./build.sh module` reads the freshly built product, compiles the pre-boot selector and six static ARM64 utilities, and outputs `artifacts/android/SunUEFI-Piano-<version>.zip`, `manifest.json`, and `SHA256SUMS`. It requires no tablet, stock BOOT, or private dump directory—the Android kernel is extracted from the device's active BOOT during module installation. By default, it prepares the compilation sysroot from signature-verified Debian ARM64 packages without executing any ARM64 binaries from them. For inputs and installation behavior, see [Android Boot Helper](boot/android-module.md).

Low-level Combined BOOT tools are also provided individually:

```sh
./build.sh trampoline --stock-boot <current-rom-boot.img> --output <output-dir>   # Generate the pre-boot selector entry
./build.sh boot-repack                                                            # Build the native BOOT repack / restore tool
./build.sh boot-request --sysroot ...                                             # Build piano-boot-request
./build.sh module-package --inspect                                               # Module packaging tool; inspect existing inputs only
```

See [Combined BOOT](boot/combined-boot.md) for how it works.

## Full Systems

All targets share the same kernel and UEFI while building their own root filesystems:

| Target | Root Filesystem | Build Directory |
| --- | --- | --- |
| `debian-gnome` | Debian sid + GNOME | `build/distros/release` |
| `debian-kde` | Debian sid + KDE Plasma | `build/distros/release` |
| `debian-denial` | Debian sid + Denial | `build/distros/release` |
| `nyarch-gnome` | Arch Linux ARM + GNOME (Nyarch) | `build/distros/nyarch-gnome-piano` |
| `arch-denial` | Arch Linux ARM + Denial | `build/distros/arch-denial-piano` |
| `steamos` | Valve ARM64 SteamOS | `build/distros/steamos-piano` |

[Release configuration](../../config/release.json) selects the kernel version and [patch series](../../patches/linux/README.md),
including hardware enablement, Gunyah, and CachyOS BORE scheduler patches. The exact commits used
are recorded in `build/release/source-manifest.json`, and the kernel and modules are output to
`artifacts/kernels/release/`. See [Kernel and Scheduler Configuration](linux/kernel.md) for details.

When rebuilding the kernel, the previous artifact manifest is invalidated first and the new one is published atomically only after compilation finishes, preventing an old `Image` from being paired with new modules.

### Debian

```sh
./build.sh linux
./build.sh release-rootfs                    # Optionally pass --desktop kde or --desktop denial
./build.sh package --root-size-mib 8192
python3 tools/package_sparse_images.py --bundle artifacts/release --output artifacts/fastboot-sparse
```

If a root filesystem already exists, run `./build.sh esp` to generate only the boot image (ESP) without re-exporting the root image.
The boot image defaults to BLS Type #1: the kernel, initramfs, and Device Tree (DTB) are placed
separately under `/linux/<release>/`, with boot entries in `/loader/entries/`.
Selecting "Boot Linux" in SimpleInit enters `systemd-boot` with a 3-second menu timeout, allowing
you to choose retained older kernels.
Pass `./build.sh esp --boot-layout uki` to bundle these files and kernel parameters into
`/EFI/Linux/linux-<release>.efi` (UKI) instead.
UKI packaging uses systemd's `ukify`, with the stub taken from the Debian ARM64
`systemd-boot-efi` package pinned and verified in `config/release.json`; the default BLS build does
not require `ukify` or the stub.
The same package provides ARM64 `systemd-boot`, installed on the ESP at
`EFI/systemd/systemd-bootaa64.efi` and the standard fallback path `EFI/BOOT/BOOTAA64.EFI`.
Individually extracted files can be passed via `--stub` and `--systemd-boot`, and the packaging tool via `--ukify`.

BusyBox, partition-detection utilities, and dependent libraries for the initramfs are also pinned
and verified in `config/release.json` and unpacked in a temporary directory rather than copied from
any distribution rootfs. Kernel modules and keyboard firmware use the same shared product inputs,
so all distributions built in the same run can use the same boot image.

When a base system has already been built, run `./build.sh release-rootfs --resume` to update the kernel, hardware packages, and configuration without rebuilding the desktop rootfs.
Overlay packages that require rebuilding reuse the isolated sid build root used in CI. Resuming preserves the SunUEFI repository in the desktop rootfs, while overlay package compilation uses only official Debian repositories.
`./build.sh rootfs --distro ID --desktop ID --plan` prints the build plan for a distribution and desktop combination; the table above lists the release targets.

Default parallelism for kernel and desktop packages is chosen based on CPU quota and available memory, and can be overridden with `--jobs`.
Desktop packages share `build/ccache` (or `CCACHE_DIR`); FFmpeg, Mutter, GSD, and GDM
verify and reuse complete runtime packages when available. If an FFmpeg build is interrupted, pass
`--resume` to keep object files for the same source tree.
On an x86 host, build with `./build.sh ffmpeg --sysroot <completed-sid-arm64-rootfs>`: the compiler,
linker, and `make` run natively on the host CPU while headers and libraries come from the ARM64
sysroot, and QEMU only executes target binaries.
The `--sysroot` / `--rootfs` entry points for other desktop packages use the same native toolchain setup.
The host must have Clang, LLVM, AArch64 binutils, `patchelf`, and `ccache` installed; Ninja-based projects also require host Ninja.
Cross-compilation uses the GNU assembler and linker to preserve Debian symbol versioning tables and debug information handling.
Product CI runs on native ARM64 builders and caches Debian runtime packages matching the source and patches.

Debian's Mesa, FFmpeg, and sensors packages are built in isolated copies of the sid rootfs without installing host container libraries or mixing in `trixie-backports`. `--sysroot` never modifies the input rootfs; package versions, source archives, patches, and binary hashes are recorded in each output directory. All three products share the official Mesa 27 development source pinned in `config/mesa.json`: Debian keeps the full upstream sid recipe, and Nyarch keeps the full Arch Linux ARM recipe. The official source already includes the 830v1 and DirectX-Headers fixes, so the legacy 26.2 backports have been removed. SteamOS's renderonly height fix is still kept separately because that upstream commit only aligns buffer width and does not guarantee trailing padding for modes whose height is not aligned to 4 rows.

When an existing sid ARM64 rootfs is available, these three package groups can be rebuilt individually:

```sh
sudo ./build.sh mesa --sysroot build/distros/debian-sid/rootfs
sudo ./build.sh sensors --sysroot build/distros/debian-sid/rootfs
sudo ./build.sh ffmpeg --sysroot build/distros/debian-sid/rootfs
```

### Shared Builds and Caching

**Build products** prepares the ARM64 build container once, then builds UEFI and the Android module, kernel packages, device configuration, and the camera package in parallel. Overlay packages are then built separately for Debian, Arch Linux ARM, and SteamOS; different desktops on the same distribution share hardware overlay packages. The publish stage signs and uploads packages to R2, and only after verifying signatures and indexes from the public domain does each system install that batch of packages from its respective repository. Intermediate artifacts do not add user-facing downloads, while logs from all stages are merged into `logs.zip`.

Debian overlay packages are compiled inside an ARM64 build root bootstrapped from officially signed sid repositories. Each package installs its build and runtime dependencies according to its native recipe. The full desktop environment, text editor, and Chromium are installed during the system image stage; dependencies of those applications do not affect overlay package builds.

The KDE image explicitly installs `plasma-desktop` and `upower`. Because minimal installation skips
recommended packages, installing `task-kde-desktop` alone neither pulls in the Plasma desktop shell
nor guarantees battery service availability. Image verification checks both installed packages and
the actual desktop shell files.

Product runs on the same branch queue in batches, keeping only the latest queued run. Because
`pacman` sync databases list only the latest version of each package, an already-started run
finishes image assembly before the next batch replaces the public index.
Whenever either Arch-based image is built, SensorProxy packages for both distributions are checked
and published together. The shared `piano-sensors` package requires the SSC-enabled packages for
both distributions; publishing updates the SteamOS overlay repo first and then the shared repo.
If you resume from an older build directory that still copied sensor services directly, the
installer refuses to overwrite unowned files; specify a new build output directory instead.
Distributions within the same batch still build in parallel.
Image assembly reads the package manifest passed down after the publish job verifies signatures; even when a rerun rebuilds packages, it will not read back a stale manifest.

Within a single workflow run, the builder image, shared firmware and kernel, device packages, and
overlay packages are passed between jobs via GitHub Actions artifacts rather than relying on
cross-run caches; after download, original build manifests and archive digests are still verified.
On success, intermediate archives for that run are deleted so downloads contain only release products and logs; failed archives are retained for seven days to support reruns.

Completion caches are published only after build, source, and output verification succeeds. Cache keys include the builder, pinned sources, patches, configuration, architecture, workspace path, and actual container image ID. Kernel restore preserves `Image`, the full module set, and the configuration, symbol table, and headers needed for out-of-tree modules; the native kernel entry point still verifies `SOURCE`, compiler, config, module hashes, and `vermagic`. If verification mismatches, the root job stops rather than recompiling on its own or mixing stale artifacts. The UEFI cache preserves original build markers, core module identities, and complete file digests, and never marks an incomplete build as finished.

The kernel binds only to the `kernel` sub-object of `config/release.json`, recording canonicalized JSON content and its SHA256. Changing sensors, Mesa, login, or other rootfs configuration does not invalidate the kernel cache, whereas kernel configuration, source, or toolchain changes still do. Older kernel manifests stored only the digest of the entire config file and cannot reconstruct the sub-object; a normal rebuild is required once to generate the new record, and old manifests are never rewritten to skip a build.

Pinned LLVM archives, intermediate firmware objects, and `ccache` are cached separately to avoid re-downloading the toolchain on every run or losing the entire object cache on small changes. Only the compressed LLVM archive is cached, avoiding filling Actions cache with the 10+ GiB extracted tree. Build containers are keyed by `Dockerfile`, pinned base image, and builder, and both archive digest and OCI image ID are verified on load. Each shared cache has a single writer job, while local entry points serialize updates with file locks and publish cache records atomically.
On an LLVM cache miss, the same official archive is re-downloaded and verified against its pinned SHA256.

When building multiple distributions sequentially on a local machine, use the same entry point inside the prepared build environment:

```sh
python3 tools/build/shared_products.py ensure --kind firmware
python3 tools/build/shared_products.py ensure --kind kernel
# Subsequent steps only verify shared products; stop if inputs mismatch
python3 tools/build/shared_products.py verify --kind firmware
python3 tools/build/shared_products.py verify --kind kernel
```

These commands are intended only for CI or maintainer local shared builds and do not alter rootfs installation. Overlay packages cache only completed package archives and manifests, never half-installed build roots. Before reuse, a fresh distribution base is still prepared and checked against the actual build container, sources and recipes, kernel interfaces used, installed packages, repository configuration, and indexes; Nyarch additionally verifies the exact Git commits built for its apps. Any input mismatch triggers a rebuild so rolling repositories are never pinned to stale indexes.

Debian checks signed sid `Packages` and `Sources` files, with sub-builders sharing the selected index; Arch-based systems use the active databases configured by their respective `pacman` setups and still enforce distribution signature policies when installing packages. On a cache hit, every package name, version, architecture, and SHA256 is verified individually. Full root images are never stored in build caches. Initial compilation, repository refreshes, rootfs installation, and image export still take time.
Editing only host-check entry points does not invalidate product caches; UEFI variable-handling scripts affect only the firmware cache.

When CI actually rebuilds overlay packages, it appends a native package revision using the run number and attempt number: Debian appends `+sunuefi.<run-number>.<attempt-number>` to the recipe version, and Arch uses a numeric `pkgrel` sub-revision. Multiple binary packages from the same source package keep the same version. On cache hits or when using officially signed Debian FastRPC packages directly, the original version and files are preserved; local builds continue to use the recipe version by default.

`./build.sh camera-package` builds both ARM64 native package formats for `piano-camera` inside the build container.
The camera package uses distribution userspace interface headers and static libraries rather than depending on a specific kernel build.
Its cache is keyed by camera source, dependency patches, recipe, version, and builder; updating only the kernel reuses the existing package.
The release version and minimum kernel metapackage version are defined in `camera_package` in `config/release.json`;
bump `revision` whenever modifying already-published package contents instead of overwriting different files under the same version.

Both local and GitHub full builds go through the same `release-rootfs` entry point. The default system includes a Chinese UI, Pinyin input method, Chromium, GNOME animations, automatic Bluetooth address initialization, and stylus components. After rootfs generation, these packages and required files are verified and recorded under `default_system` in `manifest.json`; missing any item fails the build.

Debian builds strictly against `sid` without mixing `stable` or `backports`. `release-rootfs` compiles sensors, FFmpeg, and desktop packages in an isolated sid build root, then installs the resulting native packages into the image. CI retains compilation caches keyed by sid configuration and builder, and package builders verify the selected source versions before reuse.

On Debian KDE, KWin launches Fcitx. Plasma's built-in settings portal configuration prefers KDE
and falls back to `xdg-desktop-portal-gtk` for missing settings. GTK applications can choose Wayland
input methods or the Fcitx plugin under XWayland on their own, without setting `GTK_IM_MODULE` globally.

Debian images use `update-alternatives` to select the upstream-signed database shipped with the `wireless-regdb` package.
The database and signature switch together, normal APT upgrades preserve this selection, and the shared kernel continues to verify signatures with the upstream public key.
On an existing Debian installation, run
`sudo update-alternatives --set regulatory.db /lib/firmware/regulatory.db-upstream`;
the kernel reloads the database after reboot.

### Arch Linux ARM and SteamOS

```sh
./build.sh uefi && ./build.sh module && ./build.sh linux
./build.sh nyarch-gnome-rootfs               # Use steamos-rootfs for SteamOS
./build.sh package \
  --rootfs build/distros/nyarch-gnome-piano/rootfs \
  --rootfs-manifest build/distros/nyarch-gnome-piano/manifest.json \
  --initramfs build/distros/nyarch-gnome-piano/initramfs/initramfs.cpio.gz \
  --root-size-mib 16384
python3 tools/package_sparse_images.py --bundle artifacts/release --output artifacts/fastboot-sparse
```

Arch Denial uses `./build.sh arch-denial-rootfs` with `arch-denial-piano` paths; SteamOS uses `./build.sh steamos-rootfs` with `steamos-piano` paths. Nyarch starts from an Arch Linux ARM base system, installs packages from signed repositories, and then builds Nyarch applications and native hardware packages for `piano`; sources and extensions are configured in `config/nyarch.json`. See [SteamOS ARM64](linux/steamos.md) for SteamOS build details.

Both Denial images install the compositor and its matching Flutter Engine from the [Denial signed repositories](https://github.com/denialwm/denial/blob/main/docs/INSTALL.md#repository-trust-paths). Debian pulls only Denial's own packages from that repository while keeping system dependencies on `sid`; Arch Linux ARM continues to use its own rolling repositories. SDDM launches the stock Denial Wayland session with the touch layout enabled by default. Camera and other hardware continue to use Piano's standard drivers and distribution packages, without installing Phonewave's board services or Android containers. Auto-rotation in Denial is not yet wired to standard SensorProxy; select screen orientation manually in Display Settings.

When an isolated ARM64 `pacman` build root is available, full Mesa packages can be built and installed independently. This modifies the specified build root, so never point it at a delivered product rootfs:

```sh
sudo ./build.sh mesa build/mesa-nyarch --format pacman --rootfs build/distros/nyarch-builder/rootfs --jobs 8
sudo ./build.sh mesa build/mesa-steamos --format pacman --rootfs build/distros/steamos-builder/rootfs --steamos --jobs 8
```

After an interruption, rerun with the same arguments plus `--resume` to retain the configured source tree and object files. The SteamOS build keeps the `deckard-mesa-linux-aarch64` package name and the renderonly height patch; Nyarch keeps Arch Linux ARM's native split packages.

Both product builds verify the Mesa source, full driver options, and ARM64 packages. CI caches native packages separately; before reuse, it checks the source, recipe, and package digests, and after installation it re-verifies ELF binaries and target system dependencies so mismatched rolling-distro caches are never introduced into an image.

Each product installs `piano-firmware-display` via APT or `pacman`, and rootfs assembly verifies
ownership, size, and SHA256 for both calibration files. CI shares these two packages for image
assembly only and does not upload them to repositories.
Other device firmware continues to be verified against the component manifests under `vendor/firmware/` before installation.
When resuming an existing rootfs, old calibration files are migrated only if their contents match and no other package owns them.

### Standalone Packages

The CPU model is provided by `linux/dts/piano-cpu-model.dtso`, which adds standard `model` properties to all eight CPU nodes; a kernel patch exposes it as `model name` in `/proc/cpuinfo` so GNOME displays Snapdragon 8 Elite. MIDR, core topology, clocks, and board model remain unchanged.

`./build.sh gsd --sysroot <completed-base-rootfs>` compiles `gnome-settings-daemon` with Piano's auto-brightness policy in an isolated copy using stock lux thresholds and delays without modifying sensor readings, and installs it via APT. The default `release-rootfs` runs this automatically, and the policy matches only the `xiaomi,piano` board compatible string.

`./build.sh mutter` selects the Mutter source from the signed sid index and shares two fixes with Nyarch: empty gamma curve recovery and canceled touch grab handling. The standard sRGB fallback for EDID-less panels is already provided by the distribution and is no longer patched separately. Standard color management services and user settings remain unchanged. See [Mutter adaptation notes](../../linux/desktops/gnome/patches/mutter/README.md) for sources and patches.
If a standalone build used a custom output directory, pass it via `release-rootfs --mutter-dir <output-dir>/runtime`; source, patches, and package digests are verified before assembly.

## Package and Update Standardization

Hardware support should be installable into a clean supported distribution and upgraded, removed,
or rolled back via the distribution's package manager. Product images are a combination of these
packages and first-boot defaults—not the only environment where hardware enablement can work. The
table below distinguishes components already delivered as native packages from files still copied
by build scripts; remaining implementation work is tracked in the [development checklist in Release Requirements](release.md#remaining-development-work).

| Component | Current Installation Method | Standardization Direction |
| --- | --- | --- |
| Linux kernel and modules | Native Debian `bindeb-pkg` and Arch `pacman-pkg` packages | Install and maintain matching modules per package, with metapackages tracking the latest release |
| Device Tree | Combines board DTB, overlays, and binding conversions, packaged per kernel version | Pick the matching version when generating boot files; converge toward an in-tree board Device Tree |
| Linux device firmware | Display calibration installed via `piano-firmware-display`; touch firmware can be [built as a local package](../../linux/bsp/README.md#local-firmware-packages); other firmware still installed by assembly scripts | Distributions provide generic firmware; device packages fill in board-specific firmware |
| ALSA UCM, udev, stylus calibration, and audio topology | Release images install `piano-device-config` and `piano-audio-topology` | Update open-source topology via its standalone package; continue packaging remaining hardware runtime files by component |
| Mesa, FFmpeg | Native Debian and `pacman` packages already in place | Preserve full target-distribution recipes, features, and ABI; update normally from repositories |
| Sensors | Six native packages on Debian; shared `piano-sensors` on Nyarch and SteamOS, with FastRPC, `libssc`, and SensorProxy packaged against each distribution's library ABI | Continue exposing ambient light and orientation via the standard SensorProxy interface |
| PipeWire | Rebuilt native same-version package group on Debian; rebuilt capture plugin on Nyarch and SteamOS | Preserve distribution dependency relationships; drop backports once upstream releases include the fix |
| Fingerprint | Uses target-distribution `libfprint` / `fprintd` packages; CLI and service config installed separately | Standard packages own all required files; authentication results continue coming from `libfprint` / `fprintd` |
| Camera | Both package managers install `piano-camera`; `v4l2loopback` installed per matching kernel | Remove the transitional service once the standard `libcamera` path takes over |
| Stylus and diagnostic tools | `piano-device-config` installs the stylus BlueZ service and dependencies; diagnostic tools still copied by build scripts | Package developer tools separately so they are not dependencies of stylus input |
| GNOME / KDE / SteamOS components | Native packages exist for Mutter, GDM, GSD, `libwacom`, Gamescope, etc.; default settings and session files still use overlays | Maintain one patch set for shared fixes; separate device config from product defaults without overwriting user settings |

Linux device firmware covers ADSP, CDSP, Goodix signed programs and models, `ath12k`, Bluetooth, GPU,
VPU, internal display, keyboard, and touchscreen files, with provenance recorded in `config/release.json`
and component manifests.
Licensing and redistribution terms for stock firmware must be checked item by item and cannot be
superseded by this project's source license.
Generic Adreno firmware is provided by `firmware-qcom-soc` on Debian and `linux-firmware-qcom` on
Arch Linux ARM and Valve SteamOS. Stock AQE and SQE binaries are not identical to these public files
and should not be substituted by filename alone; board ZAP firmware also differs from the signed
Qualcomm reference-board version.
SunUEFI firmware and the Android Boot Helper follow a separate update chain; installing a Linux
kernel or hardware package must never overwrite Android BOOT as a side effect.

### Kernel Packages

Debian prefers reusing the selected kernel's `make bindeb-pkg`, providing ABI-versioned
`linux-image-<release>` and `linux-headers-<release>` packages with metapackages depending on the
latest release.
Arch-based systems use the kernel source's `pacman-pkg` and `PKGBUILD` / `makepkg` to provide
release-named kernel and headers packages, with `linux-piano` and `linux-piano-headers`
metapackages tracking the latest release so older kernels and modules can coexist; the device kernel
must never declare that it satisfies binary module dependencies for the stock distribution `linux`
package.
Both share the same source, patches, config, and version definitions—never install a `.deb` as a
`pacman` package.

The kernel release string must distinguish changes that affect module compatibility so different
drivers never collide under the same `/usr/lib/modules/<release>/`. Module and kernel build
configuration and exported symbols must match rather than comparing `vermagic` alone. The headers
package must include generated headers, config, scripts, and `Module.symvers` required for
out-of-tree module builds; userspace UAPI headers exported by `make headers_install` cannot
substitute for it, nor should installing the device kernel replace the distribution's
`linux-libc-dev`.
Cross-builds must also verify that tools bundled in the headers package execute on the target device.

```sh
./build.sh linux-package --format debian --output artifacts/packages/kernel/debian
./build.sh linux-package --format pacman --output artifacts/packages/kernel/pacman
```

CI packages kernels on ARM64 builders. Cross-packaging requires an ARM64 GNU toolchain and libc;
kernel and package builds share the same compilation flags. Pass `--plan` to inspect commands without running them.
The headers package is verified for `Module.symvers`, generated headers, and target-compatible build
tool architecture; only kernel and out-of-tree module headers are published, omitting the
`linux-libc-dev` produced by `bindeb-pkg`.
The kernel release string incorporates digests of the source, config, and toolchain along with the
pinned `abi_epoch` in `config/release.json` rather than the timestamp of the latest project commit.
Rebuilding from scratch with the same inputs produces the same module directory. When publishing a
new kernel binary or headers, advance `abi_epoch` to a Unix timestamp greater than the previous
release and advance `package_epoch` to a value at least as recent.
`linux-piano-dtb-<release>` ships the complete board DTB alongside it; `piano-boot-config` wires
Debian kernel hooks or `pacman` transaction hooks, distribution initramfs generators, and
`kernel-install`.
When updating only the board DTB, metapackages, or boot configuration, advance only `package_epoch`.
Package versions compare by `abi_epoch` then `package_epoch`, leaving the existing kernel ABI intact
while ensuring package managers refresh boot files when upgrading an installed kernel's Device Tree.
When changing only Debian kernel packaging rules, increment the Debian package revision while keeping the kernel ABI unchanged.
Release systems retain the initial boot image's `linux` entry token via `/etc/kernel/entry-token`;
when packages are installed on an independently installed distribution, `kernel-install` chooses that system's token.

DTBs are built and installed per kernel version so older kernels never boot with an overwritten newer Device Tree.
Long-term, board descriptions will converge into `arch/arm64/boot/dts/qcom/` inside the kernel tree
to reduce external transformations; dynamic per-boot memory and firmware resources remain supplied
by UEFI via the standard Device Tree fixup protocol.

### From Package Installation to Boot Entries

Normal upgrades should follow this pipeline instead of reflashing the entire ESP:

```text
APT / pacman installs kernel, modules, matching DTB, and required firmware
  → builds out-of-tree modules and runs depmod
  → distribution initramfs tool generates the versioned initramfs
  → kernel-install installs kernel, initramfs, and DTB using the selected layout
  → systemd-boot discovers the BLS entry or UKI
```

Debian reuses the kernel and initramfs hooks provided by `initramfs-tools` and `systemd-boot`;
`55-initrd.install` hands the generated initramfs to systemd. Even when installing a versioned
kernel package directly, APT configures the matching Device Tree and `piano-boot-config` before
generating boot files.
Debian may load Digital Signal Processor (DSP) drivers before mounting root. The `piano-boot-config`
initramfs hook includes installed ADSP and CDSP firmware headers and segments, and upgrading boot
configuration also triggers an initramfs rebuild. On-device testing confirmed both DSPs come up
automatically on cold boot, bringing Type-C and USB networking online while leaving retained older
boot entries untouched.

Arch invokes `kernel-install` from `pacman` transactions, reuses the upstream `50-mkinitcpio.install`,
and masks the two preset-mode `mkinitcpio` transaction hooks per the
[ArchWiki `kernel-install` guide](https://wiki.archlinux.org/title/Kernel-install)
to prevent duplicate generation.
When firmware, initramfs tools, or related boot components are updated, transaction hooks rebuild
boot files for all installed kernels.
SteamOS's Frame storage grouping, recovery, ADB, and firmware hooks do not apply to this tablet;
Piano's `mkinitcpio` configuration skips only those hooks while keeping all other system and
user-configured hooks.

`systemd-boot` itself is supplied by the distribution. Release images enable systemd's
`systemd-boot-update.service`, which updates the copy on the boot partition from the packaged EFI
binary without writing UEFI variables. `piano-boot-config` orders the service after any scheduled
boot partition mount, without starting extra mount jobs when no boot partition mount is configured.

Nyarch, Arch Denial, and SteamOS mount the ESP directly at `/boot`, keeping kernels, initramfs images, and the boot menu on one partition.
Debian mounts the ESP at `/boot/efi`, while APT-managed kernel files remain in `/boot` on the root filesystem.
Self-installed distributions may also use standard mount points such as `/efi`. Run `bootctl --print-esp-path`
to inspect the ESP path and `bootctl --print-boot-path` to see where `kernel-install` writes boot files.
Without a separate XBOOTLDR partition, the two are usually identical.

When installing a Piano kernel, `piano-boot-config` checks whether the boot partition can hold the
new files after the initramfs / UKI is generated and before systemd writes them. When updating an
existing entry on FAT, only the incremental growth is reserved; installing another version requires
room for the full new entry. If space is insufficient, installation aborts early and prompts you to
remove unused older kernels through the package manager rather than deleting existing entries to make room.

`/etc/kernel/install.conf` defaults to `layout=bls`; change it to `layout=uki` for UKIs.
On Arch-based systems, run `sudo kernel-install add-all`; on Debian, where kernel files live in `/boot`,
run `sudo kernel-install add "$(uname -r)" "/boot/vmlinuz-$(uname -r)"`,
or specify the release and kernel image explicitly for any other version. BLS uses systemd's
`90-loaderentry.install`; UKI uses `60-ukify.install` and `90-uki-copy.install`.
`/etc/kernel/cmdline` overrides the packaged default kernel command line, and
`/etc/kernel/devicetree` overrides the default DTB selection; these administrator configs take precedence over `/usr/lib/kernel/`.
Package upgrades preserve editable configs such as `/etc/kernel/install.conf`.
The root device can be set to `root=UUID=…` or `root=PARTUUID=…`; the packaged default is `root=LABEL=PIANOROOT`,
without private parameters used only by the first-boot initramfs.

Boot files are named by installation entry token and kernel version, and menu titles come from the target system's `os-release`;
you can still switch to another bootloader through standard UEFI boot entries.
The shared initial boot entry sets no sort key so distribution-generated entries take priority after an upgrade;
you can still select the older entry from the menu or use your own kernel parameters, naming, and update flow.

Rollback requires keeping old boot files, modules, and DTBs together. Both Debian and Arch device
kernels use distinct release-versioned package names so metapackage upgrades never overwrite older
versions; simply pick the older entry in the `systemd-boot` menu.
The camera `v4l2loopback` module also coexists per release, installed in the matching version by the
kernel metapackage with module indexes updated by the package manager.
Arch does not generate the optional `fallback` initramfs by default—it merely bundles extra modules
for the same kernel and does not replace keeping an older kernel version.
Uninstalling an older kernel removes its boot entry without touching entries from other systems.
Native packages require systemd 258 or newer. Even if the same version was switched from BLS to UKI,
uninstalling still invokes systemd's BLS plugin to clean up the old entry.
Before updating, ensure the ESP is properly mounted with sufficient free space; if initramfs
generation fails, fix the error first and keep the existing kernel and boot files rather than
uninstalling the older version.

### Repositories and File Ownership

Signed APT and `pacman` repositories are provided so Nyarch and SteamOS build and publish against
their respective library ABIs, replacing long-term `IgnorePkg` pins on adapted packages in SteamOS
with normal dependency and version updates.
Packaged binaries are never placed under `/usr/local`, which is reserved for the administrator;
default configs, services, and state directories follow distribution conventions, and upgrades
preserve passwords, machine identity, pairings, games, and user modifications.
Regular userspace components depend only on the interfaces and library versions they actually use
and are not rebuilt across the board whenever the kernel is rebuilt.

Package management and hardware interface standardization proceed independently. Packaging the
existing camera daemon / loopback or fingerprint UIO backend natively does not mean standard
CAMSS / `libcamera` or an upstream fingerprint control model is complete.
Sources and recipes are available alongside packages, and local copies of generic patches are removed once accepted upstream.

The R2 repository top-level layout is `apt/`, `pacman/`, and `keys/`, served publicly at
`https://sunuefi-repo.okassets.uk`. APT uses `apt/dists/sid` and
`apt/pool/main`. `pacman` follows `$repo/os/$arch`:
`sunuefi` provides shared kernels, DTBs, device configuration, and Arch Linux ARM rebuilt packages.
Nyarch enables only `sunuefi`; SteamOS places `sunuefi-steamos` ahead of `sunuefi`, and only
packages rebuilt specifically for Valve's userspace go into the overlay repo. For identically named
packages, the earlier repository takes precedence, while unoverridden packages come from `sunuefi`.

`./build.sh repo --repo apt|sunuefi|sunuefi-steamos --key <fingerprint> --output <new-dir> <package-files...>`
generates signed indexes; adding `--publish` uploads them using `R2_ENDPOINT`, `R2_BUCKET`,
`R2_ACCESS_KEY_ID`, and `R2_SECRET_ACCESS_KEY`. Publishing uploads immutable package files and
signatures before updating indexes, rejects any content change under an existing filename, retains
older packages, and never performs remote deletions.
APT uploads SHA512 and SHA256 `by-hash` indexes before updating `InRelease`.
After publishing, both URLs are verified against the public domain so APT does not fall back to a
potentially cached `Packages.gz`.
The next publish resumes from this signed index.
Product builds verify the selected package names, versions, and SHA-256 checksums in the new
`pacman` index before uploading it.
If the index already selects a newer version, publishing aborts; rebuild with a version higher than the published one.
A new ABI for the same upstream kernel version must advance `abi_epoch`; both APT and `pacman`
publishing enforce this so alphabetical hash ordering never determines upgrade direction.
Because `pacman` databases and detached signatures are separate objects, brief signature
verification failures may occur mid-publish; retry instead of disabling signature checks. If an
upload is interrupted, the publishing tool can still resume from the last complete index.

References: [Debian kernel packages and hooks](https://kernel-team.pages.debian.net/kernel-handbook/ch-update-hooks.html),
[Arch kernel packaging](https://wiki.archlinux.org/title/Kernel/Arch_build_system),
[`kernel-install`](https://www.freedesktop.org/software/systemd/man/latest/kernel-install.html).

## Artifact Locations

| Directory | Contents |
| --- | --- |
| `artifacts/product/` | `PianoUEFI-product.img` and `manifest.json` |
| `artifacts/android/` | Boot Helper module `SunUEFI-Piano-<version>.zip`, `manifest.json`, `SHA256SUMS` |
| `artifacts/kernels/release/` | Kernel `Image`, modules, and `manifest.json` |
| `artifacts/release/` | `package` output: `esp.img`, `root.ext4.img`, and `manifest.json` |
| `artifacts/fastboot-sparse/` | Sparse `esp.img` and `root.ext4.img`, plus `manifest.json` and `SHA256SUMS` |
| `artifacts/installer/` | Default output directory for `installer` |

## CI Downloads

The GitHub Actions **Build products** workflow runs automatically on relevant changes to `main` and can also be triggered manually for `all`, `uefi`, `linux`, or any individual distribution. Documentation-only edits do not trigger builds, with the sole exception of `docs/user/install-from-artifact.md`, which triggers a full product build. Target selection rules live in `tools/ci_build_targets.py`.

| Download Name | Contents |
| --- | --- |
| `SunUEFI-Module` | Android Boot Helper module ZIP |
| `SunUEFI-UEFI-Fastboot` | Standalone UEFI image for `fastboot boot` |
| `SunUEFI-ESP` | Shared sparse ESP image for all distributions |
| `SunUEFI-Root-<distro>` | Sparse root image for each [build target](#full-systems) |
| `logs` | Merged build logs, source manifests, and verification records |

The module, UEFI image, and ESP are uploaded once per run, while root images are uploaded per distribution. During installation, pair the chosen distribution's root image with the ESP from the same build run; see the [Installation Guide](../user/install-from-artifact.md) for steps.

## Installer

`./build.sh installer` and `./build.sh install` do two different things.

`installer` only stages files on the computer. It copies `install_piano.py`, launcher scripts `install.sh` and `install.cmd`, `INSTALL.md`, and checksum records into a new directory; `--bundle artifacts/release` includes disk images under `bundle/`, while `--product` includes only the UEFI image. The target directory must be new—existing directories are rejected.

`install` actually connects to the tablet and exposes three actions via `tools/install_piano.py`:

| Action | Description |
| --- | --- |
| `inspect` | Read-only inspection of the tablet's partition table (default) |
| `plan` | Inspect the tablet and images, then generate an installation plan |
| `apply --plan <plan.json> --bundle <bundle-dir> --execute` | Update existing `sunuefi_esp` and `sunuefi_root` partitions and read back to verify |

The CLI installer only updates existing dedicated partitions; initial partitioning is performed in the Boot Helper. Nothing is written unless `--execute` is passed.

## Further Reading

[Repository Map](repository-map.md), [Testing](testing.md), [Release Requirements](release.md), [Contributing Guide](../../CONTRIBUTING.md).
