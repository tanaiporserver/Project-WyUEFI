# Local Inputs and Reproducibility Boundaries

This page lists the local inputs required for a full build. The repository excludes files ignored by `.gitignore`; the table below explains which external inputs each build target needs and which script reads or verifies them.

## Inputs by Build Target

| Target | Required Inputs | Verification Source | Preparation |
| --- | --- | --- | --- |
| Product firmware | `vendor/piano/` (board DTB, xbl-config DTB, native PE/DEPEX) | Hash verification via `tools/piano_vendor_inputs.py` | Sanitized board files are included in the repository; verify with `python3 tools/piano_vendor_inputs.py` |
| Rescue RAM kernel | Kernel source at the pinned commit, config, compiler, static ARM64 BusyBox | `linux/kernel-profiles.json` | `python3 tools/build_kernel.py` |
| Full Linux kernel | Clean kernel worktree, shared config, compiler | `build/release/source-manifest.json` | `./build.sh linux` |
| Full Device Tree | ROM base DTB, live DTB, overlays, dtc/fdtoverlay | `tools/build_piano_full_dtb.py` | `python3 tools/compose_piano_dtb.py` |
| Disk rootfs and ESP | ARM64 root filesystem, kernel and modules, BusyBox, DTB | Paired verification in `tools/package_release.py` | `./build.sh package` |

## Stock Extractions and Native Modules

- `tools/dev/capture_device.py` extracts stock partitions and the Device Tree from a rooted tablet; `--output` and `--serial` are required.
- The native PE, DEPEX, and DTB files in `vendor/piano/` have been sanitized to remove host-specific serial numbers, seeds, and boot arguments; `tools/piano_vendor_inputs.py` pins and verifies their hashes.
- Historical GPT captures are used only for test replay. Product firmware reads the current device's GPT directly and does not compare it against historical full-table snapshots.

## Environment and Fonts

The firmware build environment uses `upstream/Mu-Silicium`, `.venv`, and the BaseTools patches in `tools/build_stage0.sh`. See [Third-Party Sources](../../THIRD_PARTY.md) for font sources and checksums.
