# Piano Stable / Next EFI File Tree

This file tree is prepared on the host computer without connecting to the device, modifying GPT, or writing files to UFS. In the template, Stable/Next, Android, Recovery, and Shell all start as inactive; selectable Linux/Shell entries are generated only after build artifacts pass validation. `BootManifest.json` always sets `HOST_ONLY_NOT_DEPLOYED` and `storage_location=UNASSIGNED` explicitly—generating this directory must not be treated as UFS deployment or a successful boot.

## Generate a Configuration Plan Without Artifacts

```bash
python3 tools/prepare_boot_files.py --plan-only --output artifacts/piano-boot-plan
python3 tools/prepare_boot_files.py --check-tree artifacts/piano-boot-plan
```

The plan contains valid native SimpleInit JSON, `BootManifest.json`, and two empty directories, with no fake Image/DTB/initramfs.

## Prepare a Draft Tree from Two Independent Kernel Artifacts

```bash
python3 tools/prepare_boot_files.py \
  --stable-manifest artifacts/kernels/stable/ram/manifest.json \
  --next-manifest artifacts/kernels/next/ram/manifest.json \
  --initramfs /absolute/path/to/ram-initramfs.cpio.gz \
  --allow-unverified --output artifacts/piano-boot-candidate
python3 tools/prepare_boot_files.py --check-tree artifacts/piano-boot-candidate
```

To prepare only one entry, pass only its manifest; the other entry remains inactive. `--stable-initramfs` and `--next-initramfs` can be specified separately, and `--mode userspace-debug` selects another exact configuration set. `--shell-efi` accepts only an AARCH64 EFI application and copies it to `/EFI/Piano/Shell.efi`; when omitted, the Shell file menu entry remains inactive, and any Shell already present in the FV is not assumed to exist as a file on disk.

The tool rejects: incomplete builds; commits/`base_commit` that do not match the current `linux/kernel-profiles.json`; canonical patch commits that do not match the local pinned source Git range; `source_dirty` not explicitly `false`; mismatched config/fragment/Image/DTB hashes or image sizes; missing explicit DTB; incomplete or concatenated FDTs; missing executable `/init`; and forced or non-empty built-in downstream command lines. Every input file is constrained by the pinned SimpleInit `<128 MiB` load limit, and the DTB is additionally constrained by the ARM64 2 MiB limit. The tool verifies the EFI Image's ARM64 magic, AARCH64 PE32+ header, and EFI application subsystem. The initramfs input may be raw `newc` or gzip `newc`, and the output is always gzip, requiring gzip decompression support in the kernel.

Hardware-unverified kernel manifests are rejected by default. `--allow-unverified` explicitly permits staging a draft tree, without marking it as rescue or setting Stable as the default. Only a Stable artifact whose manifest explicitly sets `hardware_verified=true` becomes the preferred rescue entry; Next is always an independent experimental entry and never automatically becomes the rescue default. Both baselines currently produced by the build tooling have not been verified on hardware, so the expected status is `UNVERIFIED_HOST_STAGED`. This status does not prove that the board Device Tree topology, UFS/SMMU EBS boot handoff, or other hardware features are correct.

## File Layout

```text
simpleinit.static.uefi.json          # Auto-loaded entry point for the pinned PCD
EFI/Piano/BootManifest.json          # Explicit locate marker recording SHA256 for all files
EFI/Piano/simpleinit.static.uefi.json # Identical copy for manual import/inspection
EFI/Piano/Stable/Image.efi
EFI/Piano/Stable/piano.dtb
EFI/Piano/Stable/initramfs.cpio.gz
EFI/Piano/Stable/config
EFI/Piano/Stable/BuildManifest.json
EFI/Piano/Next/...                   # Completely independent parallel set of files
EFI/Piano/Shell.efi                  # Present only when --shell-efi is passed
```

Neither Stable nor Next is assigned a hardcoded `fs0:`. `locates.piano.by_file` matches only `\EFI\Piano\BootManifest.json`, and Linux boot paths use `locate://piano/EFI/Piano/Stable/...` or `Next/...`. There are no fallback search paths for userdata root, MTP DTB, Android boot partitions, or arbitrary directories; if a file is missing, loading fails. Future deployments should use a dedicated FAT volume containing only a single copy of this marker to avoid ambiguity in SimpleInit's "first matching volume" algorithm when multiple volumes contain trees with the same paths.

## Verified Pinned Source Conventions

- `src/confd/json_conf.c:19`: Native JSON objects recursively map to configuration keys; the current build enables JSONC.
- `src/confd/uefi.c:84` and `SimpleInit.dec:46`: `\simpleinit.static.uefi.json` is read from the root of every volume and works as a static configuration include even on read-only volumes. Generating this file tree does not modify that code.
- `src/filesystem/layer/uefi.c:150`, `src/linux-boot/loader.c:198`: Paths are resolved using `locate://TAG/path`. The `@TAG:/path` syntax from older documentation no longer matches the current generic `fs_open` path.
- `src/linux-boot/conf.c:153`: Reads `kernel`, `dtb`, and `initrd`; the boolean key is `use_uefi`, not the legacy `use_efi` from older documentation. Inherited kernel-FDT cmdline is disabled; `rdinit=/init` is passed explicitly without adding `root=`.
- `src/linux-boot/fdt.c:149`: Installs the final processed DTB into the EFI FDT configuration table; `ramdisk.c:75` installs the Linux initrd device path and `LoadFile2`. The Linux EFI stub receives its command line via `LoadedImage`'s UTF-16 `LoadOptions`, and does not use the extra `dtb=`/`initrd=` file command-line arguments assembled here.
- `src/boot/efi.c:92`: The Shell configuration key is `options_widechar`, and the file path uses `efi_file`. Currently, RAM-loaded SimpleInit lacks a reliable FV/SFS `DeviceHandle`, so `efi_fv_guid` lookups targeting the "FV containing the current image" cannot be assumed to work.
- `src/gui/interface/core/bootmenu.c:249`: Entries with `enabled=false` are hidden. The inactive Android/Recovery descriptions remain in the configuration and `BootManifest.json`, but are not presented as clickable bootable entries. `BOOT_NONE` has no boot handler.

The template sets `boot.timeout=-1` to preserve manual selection. In the current bring-up code, `src/main/uefimain.c:43` forces this value to 150 seconds after `confd_init`, overriding the timeout from the loaded configuration; this existing source file is not modified here. To rely entirely on the file-based menu timing, remove or scope that override during future SimpleInit integration and re-verify the UI.

`BootManifest.json` preserves the original `BuildManifest.json` hash, exact source/base/canonical commits, config/fragment hashes, compiler, kernel release, DTB source/validation/model/compatible metadata, and initramfs verification details. `--check-tree` re-verifies all file hashes and sizes, both copies of the configuration file, and the current source pin; existing output directories are never overwritten.

Host verification: `python3 tests/unit/test_prepare_boot_files.py`. The tests use kernels/FDTs explicitly marked `TEST FIXTURE ONLY` in a temporary directory, delete them on completion, and never publish them as artifacts.

The [official Linux EFI stub documentation](https://docs.kernel.org/admin-guide/efi-stub.html) describes the arm64 Image PE/COFF format and DTB/`LoadFile2` initrd conventions; the [official ARM64 booting documentation](https://docs.kernel.org/arch/arm64/booting.html) specifies DTB size, memory, and DMA quiescence requirements. For exact configuration keys, refer to the currently pinned SimpleInit C source code.
