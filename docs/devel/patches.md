# Patch Management and Application Order

Firmware changes are applied through a combination of Git patches and preparation scripts targeting pinned inputs from different upstream repositories. This page documents patch targets, application order, and verification entry points.

## Product Build Order

`tools/build_product.sh` first creates a copy in `build/firmware-workspace/` and applies patches sequentially inside it, keeping upstream submodules clean:

| Order | Step | Target | Description |
| --- | --- | --- | --- |
| 1 | `tools/apply_firmware_patches.py` | `upstream/Mu-Silicium`, `simple-init` | Applies Git patches registered in `patches/firmware/series.json` |
| 2 | `tools/prepare_product_handoff.py apply` | Mu Basecore DXE | Installs late APP identity checks and exit hooks |
| 3 | `tools/prepare_product_pump.py apply` | Mu DXE Event, Silicium library bindings | Installs event dispatch and product pump support |
| 4 | `tools/prepare_product_ui.py apply` | Mu DisplayEngine, SetupBrowser, UiApp | Installs menu navigation and exit handling |
| 5 | `tools/prepare_nv_runtime_guard.py apply` | Basecore RuntimeDxe | Hooks boot-time variable persistence and rejects runtime writes |
| 6 | `tools/build_simpleinit.sh --product-gui-pump` | SimpleInit | Builds the SimpleInit graphical menu |
| 7 | `tools/prepare_product.py` | `pianoProductPkg` | Assembles components, runs early memory checks, and generates the product platform description |
| 8 | `tools/build_stage0.sh product` | Mu Basecore | Builds the firmware (automatically applies Mu `Resources/MuPatches/` patches) |

Once preparation completes, verify each step individually (run from the workspace root):

```sh
python3 build/firmware-workspace/tools/prepare_product_handoff.py verify
python3 build/firmware-workspace/tools/prepare_product_pump.py verify
python3 build/firmware-workspace/tools/prepare_product_ui.py verify
python3 build/firmware-workspace/tools/prepare_nv_runtime_guard.py verify
```

## Linux Patches

Linux kernel patches are organized by kernel version under `patches/linux/`, registered in `patches/linux/7.3/series.json`, and applied automatically during `./build.sh linux`. See [Kernel and Scheduler Configuration](linux/kernel.md) for patch details.

The first five topic bundles—platform, display, input and wireless, media, and audio—remain combined downstream ports rather than split into individually submittable subsystem series. The manifest records the hardware capabilities and checks required to replace each one; these bundles are neither accepted upstream nor ready for direct mainline submission.

`original_commit`, `original_parent`, and `tree` are used to reproduce the local patch chain; upstream status is governed by each entry's `upstream` field. Local rewrites that reference a public patch series may still be marked `downstream`, and a reference link does not mean the rewrite has been submitted. Provenance metadata does not replace per-patch authorship, sign-off, or per-file license declarations, nor does it fabricate missing sign-offs.

### Preparing Patches for Upstream Linux

1. Write product changes back to `patches/linux/`; do not treat `build/kernel-worktrees/` as source. When fixing code newly introduced by the series, amend the original patch and replay subsequent patches. Run `./build.sh check --pattern test_check_patch_series.py` to verify registration, then run `./build.sh linux --configure-only` to prepare the product kernel and confirm that patches apply cleanly in order.
2. In a standalone kernel worktree, replay the candidate commits onto the branch required by the target subsystem. Keep each patch to a single independently reviewable change; when fixing a bug that already exists upstream, verify `Fixes:` against the real commit in the target tree. Export with `git format-patch`, run `scripts/checkpatch.pl --strict` on each patch, and perform the relevant driver build, binding validation, or on-device verification. A product patch chain that replays cleanly does not guarantee that an upstream series applies standalone.
3. Use `scripts/get_maintainer.pl` to find maintainers and mailing lists, and document the tested kernel, device, test results, and any known gaps that affect review. Follow Linux's [Submitting Patches](https://docs.kernel.org/process/submitting-patches.html) and [Coding Assistants](https://docs.kernel.org/process/coding-assistants.html) guidelines to disclose provenance and tool assistance; add your DCO (`Signed-off-by:`) only after personally reviewing the code, licensing, and attribution.

## Package Patches and Product Targets

Package patch series use `patches/<package>/<version>/series.json`. Entries in the `patches` list are applied in order, with each entry's `file` resolved relative to that series directory; FFmpeg's existing `series` / `path` format carries the same meaning. When the same fix applies to multiple versions, reference the same file from each series instead of duplicating the patch. Cross-version paths may use `../`, but must stay inside the repository.

`targets` registers the CI product names that consume that version, matching `TARGETS` in `tools/ci_build_targets.py`: firmware and kernel use `uefi` and `linux` respectively, while root systems use the four distribution targets. For PipeWire, the two Debian desktops use 1.6.9, Nyarch uses 1.6.11, and SteamOS uses 1.6.8. The local series for GTK 3.24.52, IBus 1.5.34, KWin 6.6.5, and Mesa 27.0 are used by the SteamOS recipe. Nyarch's Material You Colors 30 and Blur my Shell 75, as well as SteamOS's KScreen 6.6.5 and Manager 26.4.1, also select patches through their respective versioned series.

The firmware series registers both `uefi` and the four root system targets; the kernel series registers `linux` and the four root system targets. F2FS tools are installed into the Android Boot Helper rather than the Linux root system; because the four root system products also build the companion helper, its series registers `uefi` and the four root system targets. The Gamescope series is used only by `steamos`.

Every patch in a registered series must define `upstream`. Provenance for a shared file only needs to be recorded once at one reference site; if all patches in a series truly share the same provenance and drop conditions, `upstream` may also be recorded at the series top level:

| Field | Meaning |
| --- | --- |
| `status` | `merged` means the fix is in an upstream commit; `submitted` means there is an active submission entry; `downstream` means a project-local or other downstream fix |
| `url` | HTTPS URL of the upstream commit, submission entry, or downstream source |
| `commit` | Full upstream commit hash, when known |
| `drop_when` | Which source release containing the fix to switch to, and which functional checks must pass before the patch can be dropped |

`merged` does not mean the distribution currently in use already includes the fix. For example, PipeWire's five GStreamer fixes are on upstream master, but the 1.6.8, 1.6.9, and 1.6.11 release sources still require backports; the GTK 3 maintenance branch already has the tool atom fix, but the pinned 3.24.52 release does not include it yet. Always inspect the actual source of the selected release before dropping a patch rather than relying solely on version numbers or commit dates.

### Version Selection and Drop Boundaries

| Series or Build Dependency | Product Selection | Basis for Dropping or Keeping |
| --- | --- | --- |
| Mesa | All four products share official 27 development sources; SteamOS separately retains a height fix | No longer depends on the external `piano-mesa` repository, trimmed recipes, 830v1 chip IDs, or D3D12 backport patches; SteamOS's renderonly allocation shortfall still exists |
| GSD, GDM, Mutter | Actual GNOME 51 versions from Debian sid and Nyarch | Dropped legacy GNOME 48/50-specific series and duplicate patches; kept the package versions actually consumed by each product while sharing the same fix files |
| UPower | Native packages from each distribution | Already includes keyboard backlight hotplug handling; dropped the old 1.90.9 backport and dedicated builder, letting GSD refresh the standard backlight object |
| FFmpeg, sensors | Native sid recipes; Arch-based targets build against their respective ABIs | Dropped FFmpeg 7 and `trixie-backports`-specific sources; `libssc` vector interfaces are kept only in sensor series that have consumers |
| Fingerprint transport | Self-contained Linux TEE object invocation | Dropped MinkIPC/`quic-teec` link dependencies and adapter patches; UIO and the local `libfprint` backend still have consumers—see [Fingerprint implementation](hardware/led-ir-fingerprint.md#side-fingerprint-sensor) for limitations |
| Camera and legacy stylus decoding | No standard camera replacement exists yet; the new kernel does not invoke userspace stylus decoding | Do not drop implementations still responsible for video, 3A, firmware initialization, or offline comparison just for a smaller diff; drop conditions follow the corresponding series and [Feature comparison](hardware/camera-controls.md#existing-service-and-standard-replacement) |

Run `./build.sh check --pattern test_check_patch_series.py` to enforce these rules; Build products runs the same check before selecting build targets.
The check covers every series in the current workspace without relying on Git history or network access. Every series must register valid product targets, every patch must register provenance and drop conditions, and every patch file must be referenced by a series.
The check merges shared references by resolved file path while rejecting duplicate application, mismatched digests, and conflicting provenance records. Distinct files are checked for duplicates using SHA256 and Git patch-id; plain unified diffs without a `diff --git` header are also compared.
The check does not determine whether a fix has been merged upstream, nor does it delete patches automatically.
