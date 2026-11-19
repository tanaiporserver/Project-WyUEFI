# Running Checks

Run host-side checks locally via `./build.sh check` without connecting the tablet. Changes involving storage writes or hardware interaction must also be verified on an actual tablet.

## Host Checks

Routine changes require only Python 3 and do not touch any device:

```sh
./build.sh check
```

This command checks product configurations, log size limits, RAM log collection via mock ADB, stale build artifacts, DMA logs, USB diagnostic reads, and the test scripts themselves. Output shows the result and skip count for each test file individually. Any skipped test in the `portable` group is treated as a failure.

To inspect the scope or run the full Python unit test suite:

```sh
# List test files only, without running them
./build.sh check --list
./build.sh check --group python-all --list

# Run all Python tests under tests/unit/
./build.sh check --group python-all
```

`python-all` runs all tests in `tests/unit/test_*.py` with a default 120-second timeout per file. Some tests depend on upstream headers, a C compiler, or specific input files; if prerequisites are missing, they skip or fail with the reason printed in the output. Passing `--no-skips` treats any skipped test or an unmatched `--pattern` as a failure.

`./build.sh check --group ci --list` lists the host checks run in CI, including native shell tests that do not require private samples.
Other checks requiring local firmware, toolchains, or captured samples are run individually according to each test script's instructions.

## Device Verification

When claiming a feature works on hardware, state:

* The image version or commit hash tested;
* The boot method (Combined BOOT, temporary `fastboot boot`, or booting from an installed OS);
* Observed behavior, such as what appears on screen, whether physical buttons and touchscreen respond, and whether returning to Android works cleanly.

Host checks only verify parsing and mock logic; a successful build or a registered protocol does not prove hardware functionality works.

### Hardware Acceptance on a Standard Rootfs

When validating "still works on another distro", use a matching kernel, Device Tree, and standard firmware directory, and install that distro's libinput, BlueZ, libfprint/fprintd, libcamera, and V4L2 tools. Do not pre-install project-specific input or camera services and count the result as standard component support. When validating download and installation flows, perform a first boot using a full product image; running a new binary temporarily on an already-running older system only proves the checks specific to that binary.

| Subsystem | Checks and Operations | Required Results |
| --- | --- | --- |
| Stylus | `libinput list-devices`, `libinput debug-events`; drawing, hover, tilt, buttons, palm rejection, magnetic attachment, 120/144 Hz, suspend/resume, and firmware reload with both styluses | One physical `tablet-tool`, coordinates aligned with the pen tip, no lingering pressure on lift/disconnect; no THP/hidraw userspace decoding or uinput. See [Stylus implementation](hardware/pen-protocol.md#kernel-input-and-licensing-boundaries) for current gaps in BlueZ initialization, weak hover, and 144 Hz |
| Fingerprint | Verify sensor control model, firmware licensing, and enrollment authorization first, then test enrollment, multiple fingers, verification, listing, deletion, cancellation, and timeout via distro `fprintd` | Uses an accepted device control model and standard TEE interface without relying on UIO; never run enrollment/verification that may acquire a HAT while RPMB writes are disabled. See [Fingerprint implementation](hardware/led-ir-fingerprint.md#side-fingerprint-sensor) for current boundaries |
| Camera | `cam -l`, `v4l2-compliance`; per-mode still capture and sustained video recording on front and rear cameras, measuring resolution, frame intervals, dropped frames, and A/V sync; verify auto/manual exposure, ISO, white balance, rear autofocus, flash, and recovery | libcamera configures TFE directly and the IPA uses hardware statistics; record the exact frames where exposure and gain take effect, verify ISO matches calibration data, and do not count mode enumeration as a full video recording result. The standard path does not depend on loopback, control sockets, or a software ISP. See [Camera interface](hardware/camera-controls.md#kernel-interfaces-and-libcamera) for remaining gaps |

Record exact software versions, physical actions, device nodes, and results. Mere device enumeration, successful driver probe, or passing non-streaming `v4l2-compliance` checks cannot substitute for the functional acceptance criteria above.

Camera migration must verify both fresh installs and upgrades from older releases. A fresh install must not include `piano-camerad`, `v4l2loopback`, or project control sockets; on upgrade from an older release, uninstall those components, then open both cameras in a standard application via the distro's libcamera and PipeWire. Preview, photo capture, video recording with audio, and both auto and manual controls must all work; repeat the checks after stop/restart streaming and after suspend/resume.

## CI Checks

GitHub Actions **Host checks** runs `./build.sh check --group ci` on PRs targeting `main` and on pushes to `main`. Using pinned public sources, it verifies boot fallback, Fastboot, NVRAM power-loss recovery, runtime variables, full system reboot, camera runtime, and native packaging. Checks do not use release signing keys, and any skipped selected test fails the run. Changes limited to tests do not trigger product builds.

**Build products** runs automatically when relevant product source files change on `main`, covering `uefi`, `linux`, and all four distributions. Before building, it uses the CI host's `kernel-install` in a temporary directory to verify boot entry updates and preservation of previous entries on failure, then uses an offline build container to verify signed APT/pacman repository publishing as well as preservation of administrator configuration across pacman upgrades and removals. Neither test suite uses release signing keys, and any skipped test blocks the build. Neither workflow connects to physical hardware. Once triggered, the build target selector inspects both old and new paths for renames, and moving a file out of a product directory is treated as removing a build input.

Before selecting build targets, the patch series check runs; it validates product targets, patch origins, removal conditions, file digests, and duplicate references across all series without requiring full Git history. Run it standalone with `./build.sh check --pattern test_check_patch_series.py`; see [Patch Management](patches.md#package-patches-and-product-targets) for the rules.
