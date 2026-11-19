# Contributing Guide

Project SunUEFI provides a unified UEFI and Linux port for the Xiaomi Pad 8 Pro (`piano`), with Windows on ARM as a long-term goal. Bug reports, test results, and code contributions are welcome.

## Reporting Issues

In GitHub Issues, clearly specify: the build used (commit hash or image hash), what you did, and what you observed. If UEFI Fastboot is available, attach logs exported with `fastboot -s SunUEFI-piano oem ramlog` and `get_staged`; on Linux, attach `dmesg` and `journalctl` logs for relevant services. Also note your display panel and RAM capacity; current runs use the CSOT configuration.

## Submitting Code

1. Break changes into small, clear Pull Requests explaining why the change is needed and how it was verified.
2. Directory layout: UEFI lives in `uefi/` and `patches/`; kernel configs, Device Tree files, and userspace live in `linux/`; Android-side tools live in `android/`. See the [Repository Map](docs/devel/repository-map.md) for details.
3. Run `./build.sh check` before submitting; see the [Building Guide](docs/devel/building.md) for build instructions.
4. Use Conventional Commits for commit messages, e.g., `fix(uefi): fix ...` or `feat(linux): ...`.
5. Follow [AGENTS.md](AGENTS.md) when using AI coding assistants.

## Conventions

* **Single core**: New features and fixes go into the same UEFI codebase and the same product image; do not create separate standalone test images.
* **Protect stock data**: Scanning and booting must never write to stock partitions. When the user explicitly initiates flashing, operate only on validated targets and ranges; cleanly shut down any claimed devices before handing control over to the OS.
* **Standard interfaces first**: Implement hardware support in kernel drivers and Device Tree whenever possible so clean distributions work out of the box; use standard interfaces such as BlueZ, ALSA/UCM, IIO, DRM/KMS, and UPower for desktop integration.
* **Be explicit about completion status**: Clearly distinguish between "code compiles", "driver loads", and "actually works on the tablet", and explicitly note what remains unfinished.
