# Minimal On-Device GCv2 Validation

This procedure is intended for the main task to run after the normal default kernel build completes and recovery packages are available. Hardware submission and completion of an identity gamma LUT have been verified on device with `bde3`; stock Mutter 48.7 crashed the user session with SEGV when restoring an empty LUT. The local Debian package fixes this path and automatically associates outputs without EDID with a standard sRGB profile. Empty-LUT clearing and curve restoration on the new library have passed D-Bus readback and session survival checks; hardware completion and physical visual feedback for warm color shifts are still recorded separately. By default, the generic client still blocks probes when the initial state is empty, rather than guessing what is running in the active process based on installed package versions.

## Read-Only Baseline

1. Cross-check the full test build manifest, installed MSM module hashes, and module load state. Checking `uname -r` alone cannot distinguish `a8` from newer packages that also report `7.2.9`. Inspect the final Device Tree to confirm that DPU declares `mdp/vbif/regdma`, with the third resource at `0x0af80000/0x7000`.
2. Save the raw `dmesg --color=never` output and the properties of the active DPU DRM node. Determine the node from driver binding instead of assuming `card0`. The existing `piano-drm-snapshot /dev/dri/cardN --blobs` tool uses read-only DRM ioctls only; save its raw JSON output, including error counts.
3. Run `piano_gamma_probe.py` without `--probe` on the active GNOME user's session bus. Verify the CRTC API ID, KMS ID, active mode, and the three gamma arrays. The active CRTC must advertise `GAMMA_LUT_SIZE` of 1024 entries; stop if this property is missing or the size does not match, and do not force-enable the GNOME Night Light toggle.

```sh
# Run by the main task inside the active guest GNOME session; read-only by default.
python3 /run/piano_gamma_probe.py > /tmp/gcv2-before-mutter.json
/run/piano-drm-snapshot /dev/dri/cardN --blobs > /tmp/gcv2-before-drm.json
dmesg --color=never > /tmp/gcv2-before-kernel.txt
```

`/run` is a temporary deployment path; the sources are the [Gamma client](../../../tools/dev/piano_gamma_probe.py) and the [read-only DRM collector](../../../tools/android/piano_drm_snapshot.c). The script calls `GetResources`, `GetCrtcGamma`, and `SetCrtcGamma` on the [Mutter 48.7 DisplayConfig interface](https://raw.githubusercontent.com/GNOME/mutter/48.7/data/dbus-interfaces/org.gnome.Mutter.DisplayConfig.xml); the API ID comes from the current configuration and must not be substituted with the DRM object ID directly.

**Three empty arrays can represent the original bypass state, but cannot be safely restored as-is.** On `bde3`, CRTC 104/105 initially reported `GAMMA_LUT_SIZE=1024` and `GAMMA_LUT=0`, with active CRTC 104 mapped to Mutter API ID 0. In Mutter 48.7, [read_crtc_gamma](https://github.com/GNOME/mutter/blob/48.7/src/backends/native/meta-kms-crtc.c#L147) records the supported size first and leaves the LUT as `NULL` when the blob ID is 0; the [getter](https://github.com/GNOME/mutter/blob/48.7/src/backends/native/meta-crtc-kms.c#L98) then returns `NULL` as a size-0 LUT. A DRM snapshot documents backend capability, but does not prove that the full `SetCrtcGamma` path can restore empty arrays. The current client refuses to probe when the initial state is empty, and `--restore-from` also rejects empty backups; it will not substitute an identity curve and pretend to have restored the original bypass state.

On `bde3`, the identity submission logged `ctl1 dspp_mask0x60000 iova0x1000 words1554` at boottime 659.136439 s and `completed status0x8` at 659.136542 s, with matching Set/Get readback. Roughly 3 seconds later, attempting an empty-LUT restore terminated UID 1000's GNOME Shell with `status11/SEGV`, causing GDM to recreate the session; the logs showed no kernel Oops or REGDMA timeout. The reachable source path is that `SetCrtcGamma` caches a non-`NULL` size-0 LUT object, [meta_kms_update_set_crtc_gamma](https://github.com/GNOME/mutter/blob/48.7/src/backends/native/meta-kms-update.c#L554) passes it to [meta_gamma_lut_copy_to_size](https://github.com/GNOME/mutter/blob/48.7/src/backends/meta-crtc.c#L278), and the latter computes slot counts with size 0 and accesses index `i-1` of the empty array. Because no core stack trace was captured, this source trace must not be documented as a confirmed crash stack.

The [minimal Mutter patch](../../../linux/desktops/gnome/patches/mutter/0001-kms-empty-gamma-bypass.patch) treats both size-0 LUTs and `NULL` as bypass at the KMS update entry point, while non-empty curves still go through the original resampling path. It targets upstream 48.7; the current root filesystem records `libmutter-16-0 48.7-0+deb13u1` and `gnome-shell 48.7-0+deb13u2`. Using the pinned Debian source package, original `rules`/`control`, and standard `quilt` workflow, the ARM64 source and five runtime packages were built as version `48.7-0+deb13u1+sunuefi1`. After installing them and performing a clean GDM restart, saving the current 1024-entry curve → setting empty arrays → reading back three empty arrays → restoring the original curve produced matching D-Bus responses and curve readbacks, while keeping the same `piano` session 15 intact and GDM active. This check proves full empty-LUT interface round-trip behavior and session survival on the new library, but does not by itself prove optical recovery on the panel. The fact that the kernel atomic layer accepts blob ID 0 does not substitute for full userspace validation, nor does it require adding a legacy gamma callback.

The current backend emits `drm_dbg_kms` logs on submission and completion, but lacks positive logs for successful initialization, per-DSPP binding, and clean teardown. The presence of `GAMMA_LUT` indicates that usable ops were installed, but alone cannot confirm the output of each physical DSPP. The earlier standalone logging draft was based on the old CTL enumeration loop in `281` and was not included in `a589`; after the formal `0006` test fix switched to a catalog loop, that draft can no longer be applied directly and must be updated before a normal build. The required positive log messages should still be emitted only after successful reset, resource initialization, ops binding, and quiesce completion, without adding extra register reads, synthetic completion events, or new control paths.

During the test window, the main task may save the current `/sys/module/drm/parameters/debug` value, set only the KMS bit `0x04`, and restore the original value afterward. When this bit is unset, the absence of submission debug logs must not be interpreted as no DMA occurring; existing error logs must still be preserved.

## Standard Night Light Entry Point

In GSD 48.1, [NightLightPreview](https://github.com/GNOME/gnome-settings-daemon/blob/48.1/plugins/color/gsd-color-manager.c#L284) temporarily applies the current `night-light-temperature` and reverts to the normal policy after the timeout. [set_forced](https://github.com/GNOME/gnome-settings-daemon/blob/48.1/plugins/color/gsd-night-light.c#L573) restores `Temperature` to 6500K when Night Light is disabled. Mutter's [update_white_point](https://github.com/GNOME/mutter/blob/48.7/src/backends/meta-color-device.c#L1384) uses the gamma LUT capacity, and [generate_gamma_lut](https://github.com/GNOME/mutter/blob/48.7/src/backends/meta-color-profile.c#L502) produces non-empty arrays on both the with-VCGT and without-VCGT paths rather than deriving size 0 from empty `GetCrtcGamma` arrays. A ready assigned color profile is required; without one, the code returns early without updating gamma. `NightLightSupported=true` only indicates that gamma capacity is available.

When the user is ready to observe the panel and the GNOME session is alive, the main task can trigger a standard preview without altering Night Light enablement or schedule settings:

```sh
gdbus call --session --dest org.gnome.SettingsDaemon.Color \
  --object-path /org/gnome/SettingsDaemon/Color \
  --method org.gnome.SettingsDaemon.Color.NightLightPreview 3
```

This call must run on the active user's session bus. The 6500K state after the preview ends is a normal non-empty color curve (potentially including profile calibration) and is not equivalent to restoring the initial `GAMMA_LUT=0` bypass state. Whether the display actually shifts warmer still depends on visual observation of the panel and new hardware completion logs.

A subsequent standard 10-second preview on `bde3` was accepted and transitioned `Temperature` from 6500 to 6395, but the gamma blob remained 0 during and after the preview, with no new REGDMA activity. The DSI device in `colord` was `Enabled` and `Embedded`, yet had no associated profile despite all required packages and services being present. In upstream 48.7, [ensure_device_profile](https://github.com/GNOME/mutter/blob/48.7/src/backends/meta-color-store.c#L492) returns `FALSE` early when there is no EDID checksum and never reaches the existing no-EDID sRGB fallback; without a default profile, the assigned profile remains `NULL` and white-point updates return immediately. The no-EDID fix is included in the Debian package above: it keys the cache by device ID, and when `colord` connects successfully with no default profile present, it populates `Filename` and `MAPPING_device_id` on the standard sRGB fallback and establishes a default profile through the existing soft-association path, while keeping existing user profiles and EDID paths intact. After installation and a GDM restart, the DSI output automatically associated with the no-EDID ICC profile, `GetCrtcGamma` returned three 1024-entry curves, and the kernel remained healthy. Standard sRGB is explicitly uncalibrated for this panel; hardware submission and physical warming feedback under normal Night Light still need to be recorded independently, and must not be substituted with a fake EDID or by force-toggling the UI.

## Identity Then Warm Probe with Non-Empty Backups

The following direct gamma checks apply only when all three initial arrays have 1024 entries; when the initial state is empty, the client refuses to write and you must wait for the patched Mutter package and empty-restore verification. First verify that Night Light and any other color clients are in a steady state so two clients do not modify gamma concurrently. Run separate identity and warm probe windows, each backing up the original gamma and restoring it in `finally`. In the commands below, `ACTUAL_ID` comes from the read-only listing, and neither backup path may already exist:

```sh
python3 /run/piano_gamma_probe.py --crtc ACTUAL_ID --probe identity \
  --drm-snapshot /tmp/gcv2-before-drm.json \
  --backup /tmp/gcv2-identity-original.json --hold-seconds 10 \
  > /tmp/gcv2-identity-mutter.json
python3 /run/piano_gamma_probe.py --crtc ACTUAL_ID --probe warm \
  --drm-snapshot /tmp/gcv2-before-drm.json \
  --backup /tmp/gcv2-warm-original.json --hold-seconds 10 \
  > /tmp/gcv2-warm-mutter.json
```

Between the two windows, inspect the identity result, restore log, and kernel log, and proceed to the warm probe only if no faults occurred. The warm probe applies an explicit RGB gain of `1.0/0.8/0.6` to exercise the hardware pipeline rather than targeting a calibrated Kelvin color temperature. During each hold window, capture an additional read-only DRM blob snapshot and `dmesg`; `start_uptime` in the report can be used to filter new kernel messages for that window. The script also attempts to restore on normal termination signals; if the process is forcefully killed or loses D-Bus connectivity, the backup file is preserved and can be restored with:

```sh
python3 /run/piano_gamma_probe.py --restore-from /tmp/gcv2-warm-original.json
```

Restore accepts non-empty backups only, re-queries the current display configuration, and matches strictly against the original KMS CRTC. If the target is no longer active, it fails explicitly rather than writing to a guessed output. Software readback mismatch or restore failure exits with status 2; exit status 0 only indicates that the interface round-trip and software readback succeeded.

## Evaluating All Four Result Categories Together

The offline [report script](../../../tools/dev/piano_gcv2_report.py) parses the captured artifacts without modifying the system:

```sh
python3 tools/dev/piano_gcv2_report.py \
  --snapshot ACTUAL_DRM_JSON --kernel-log ACTUAL_DMESG_TEXT \
  --since-uptime ACTUAL_PROBE_START
```

- **Software properties**: The CRTC's 8192-byte blob contains 1024 RGB/reserved entries, identity and warm updates match the requested curves, and non-empty backup restoration returns the original curve. Restoring the original bypass state (blob ID 0) must be verified separately with the patched Mutter package. If the JSON contains errors, preserve them as-is and do not count the capture as complete.
- **DMA**: Check `ctl`, a non-zero `dspp_mask` covering all assigned DSPPs, the IOVA, and word count in submission logs, paired with the subsequent `REGDMA completed` message. Bits 17..20 map to physical DSPP 0..3; do not assume in advance which two DSPPs a run will use. Missing completion or any timeout/SMMU fault disqualifies the run; the logged `done` comes from hardware polling.
- **Dual-half output**: Inspect the panel directly—identity must not alter colors, warm must cover both halves without seam desynchronization, and colors must return to normal after restore. Confirm the physical mixer seam orientation against resource allocation and current screen rotation. Desktop screenshots, PipeWire captures, and EFI framebuffer dumps occur before hardware gamma and cannot prove that the panel shifted warmer. No overlay or software tinting is used in place of hardware.
- **Teardown**: After the main task confirms healthy color updates, perform a clean return to Android or reboot via the existing recovery entry point. Collect the previous boot log or `pstore` to check quiesce behavior and faults. The logging patch's `quiesce complete inflight=0` message must appear at actual teardown; current builds without that log must not claim complete positive teardown logging.

For this first color check, do not inject SMMU faults, perform arbitrary MMIO reads/writes, or test sysfs unbind, DPMS, or suspend. If reset persistently fails to acknowledge, the teardown path retains mappings and keeps waiting, preventing subsequent component/devres teardown from proceeding; that is not a successful recovery. Recovery is handled by the main task based on device state and may require a platform hard reboot.

After completing this pass, use GNOME's standard Night Light to confirm desktop integration. GC2 uses a standard 10-bit LUT; a warm shift must not be over-interpreted as completion of HDR, IGC5, 12-bit FRC, or high-precision panel calibration.

The five Debian packages (`48.7-0+deb13u1+sunuefi1`) have been built and installed via APT. After restarting GDM, the DSI device automatically associated with the generated sRGB profile, with three 1024-entry gamma curves and an 8192-byte DRM blob. Saving the active curves → clearing to empty → reading back three empty arrays → restoring the original curves all succeeded without logging out `piano` session 15. This confirms that the empty-LUT restore bug in Mutter is fixed; physical dual-half output under standard Night Light still awaits on-device visual preview. The generic sRGB profile is not a measured panel calibration. Logs are recorded locally in `mutter-sunuefi1-profile-state.txt`, `mutter-sunuefi1-gamma-readonly.txt`, and `mutter-sunuefi1-empty-restore.txt`.
