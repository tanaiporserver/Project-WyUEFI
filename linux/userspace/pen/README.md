# Piano Stylus Core

This directory contains stylus BlueZ initialization, offline decoding tools, and manual diagnostic code. In production, stylus and finger input are reported directly by the kernel.

Calibration data is installed to the standard firmware directory via the device configuration package. `pen-control.py` only performs BlueZ FE11/FE12 initialization; release builds require a matching kernel stylus driver and calibration data. For the input pipeline, calibration, limitations between the two stylus models, and licensing rationale, see [Kernel Input and Licensing Boundaries](../../../docs/devel/hardware/pen-protocol.md#kernel-input-and-licensing-boundaries).

Focus Pen Pro has completed 120 Hz pressure-sensitive drawing on the new integer kernel decoder, with physical evdev pressure spanning 0..16383 and live dual-axis tilt variation. Weak hover, tilt accuracy, full four-edge behavior, and palm rejection still require on-device verification. For the standard stylus and refresh-rate scope, see [Kernel Input Notes](../../../docs/devel/hardware/pen-protocol.md#kernel-input-and-licensing-boundaries). For squeeze, double-tap, and barrel slide, see [Barrel Gestures](#barrel-gestures).

## Standard Focus Pen

The standard Xiaomi Focus Pen (M80p, Bluetooth `0022:4d80`) and Focus Pen Pro (P81c, `0022:5081`) share a single THP reader process. Upon connection, scan mode, calibration, and input devices are switched according to the actual HID identity: the standard stylus uses mode 1 with a pressure range of 0..8191, while the Pro uses mode 3 with a pressure range of 0..16383. Before switching models, the old tool is released and a standard tablet-tool with the appropriate pressure range is recreated; disconnection and data timeouts still release contact state.

Hover, positioning, and pressure-sensitive writing have been verified on the standard stylus; see [Known Issues](../../../docs/user/known-issues.md#stylus) for refresh-rate limitations. The BOE/CSOT configurations preserve the coordinate deltas and tilt calibration from this device's stock vendor1 settings without using Sheng's screen mapping or calibration constants. Run `piano-pen-offline --vendor 1` to inspect this parameter set; the default `--vendor 3` keeps the offline interface for the Pro.

Report2 `0x4e` / `0x4b` on the standard stylus map to `BTN_STYLUS` / `BTN_STYLUS2` respectively and are only reported while the stylus is in proximity. The kernel HID fixup prevents them from also acting as global Page Up/Page Down keys while preserving the dedicated Focus button and raw hidraw pressure data. The standard stylus never receives Pro squeeze, vibration, or radio configuration commands.

## Open-Source Implementation

`piano-pen-decoder.hpp/.cpp` is derived from `ianchb/xiaomi-sheng-thp` at pinned commit `34046210932d654a4c0df0121ecc31c008f8148c`, retaining the Apache-2.0 license. `piano-pen-core.h/.cpp` exposes a C API so the existing touch reader process can call into it. See [OPEN_CORE_ORIGIN.txt](OPEN_CORE_ORIGIN.txt) for provenance and modification scope, and [Apache-2.0.txt](Apache-2.0.txt) for the full license.

The core takes raw payloads stripped of the stream header and the 257-byte transport wrapper, reusing the two-layer raw checksum, geometry checks, and four-packet bitmap from `piano-pen-frame.c`. A frame is marked complete only when all four main touch packets are present; type17 is never rewritten as type29. After axis separation, coordinate calculation, tilt correction, and filtering on the matrix, the decoder returns results with explicit validity flags. Unknown pressure, coordinates, or tilt must never be zero-filled and reported.

Calibration is loaded from the caller-supplied stock ini file with no embedded Sheng position tables or dimensions. Piano's 40/60 axes are extracted from the two 2400-point mappings, and pitch is computed with the stock integer formula. P81c's vendor3 selects the default `stylus` parameters rather than the `stylus_3` slot. Output is declared in portrait orientation: X `0..213599`, Y `0..319999`, pressure range selected by stylus model, tilt ±60; coordinates are not swapped and are not divided by report factor10.

**Touch calibration must be selected by touch LCDid / firmware, not by display panel name.** On the current device, the touch LCDid is 1, which uses BOE touch firmware and the BOE ini even when the display pipeline identifies as CSOT. The two ini files have different position tables and must both be retained.

`piano_pen_core_report5()` accepts 15-byte BLE Report5 packets. The two bytes immediately following the Report ID form the little-endian pressure value; substitute reports must not be synthesized from raw29 metadata. BLE timestamps use `CLOCK_BOOTTIME`, matching `ktime_get_boottime()` in the NTP driver; do not mix with `CLOCK_MONOTONIC` across suspend. The pressure max-age is explicitly supplied by the caller and does not claim to reproduce a stock timeout parameter.

## Offline Tool

`piano-pen-offline.cpp` only reads external ini and capture files and emits diagnostic JSON; it performs no FIFO, Bluetooth configuration, or uinput operations.

Build from the repository root to output under `build/pen-core/`; for cross-compilation, specify `CC`, `CXX`, `AR`, and toolchain flags:

```sh
make -C linux/userspace/pen
build/pen-core/piano-pen-offline --ini ACTUAL.ini --config
build/pen-core/piano-pen-offline --ini ACTUAL.ini --pressure-max-age-ns N --events ACTUAL.tsv
```

Each line in the event file uses the same BOOTTIME clock base: `<timestamp> report5|report2|raw29 <capture-file>`, or `<timestamp> reset -`. `report2` is a two-byte barrel-gesture report, outputting its raw value and gesture name. Unknown fields are emitted as `null`. The tool has been verified against BOE/CSOT configurations, 651 stock pressure reports, and Linux type29/Report5 sequences. The latter yielded 1,177 frames with valid coordinates and tilt, 896 of which also had valid pressure; no synthetic raw29 test frames were constructed. Screen-protector / hover-specific branches, full palm interference, and edge behavior still require on-device verification.

## Userspace Stylus Input for Reference Comparison

`piano-pen-owner.c/.h` connects read-only Bluetooth HID pressure and type29 frames to a standard tablet-tool input device so developers can compare decoding results in an isolated environment. Calibration and pressure max-age must be supplied explicitly; everyday writing uses the kernel path—do not launch the virtual stylus while physical stylus input is active.

```sh
piano-touch-view input --seconds 0 \
  --pen-ini /path/to/ACTUAL.ini --pen-pressure-max-age-ms 100 --pen-input
```

100 ms is a caller-chosen validity window, not a stock parameter. Add `--pen-json` for diagnostic output; unknown pressure is never faked as 0, and tool and button state are released on exit, stream interruption, or epoch change. Input preserves pressure 0..16383; landscape coordinates are transformed from portrait output as `X=portraitY` and `Y=213599-portraitX`, with tilt transformed accordingly. On-device verified positioning and stroke-width variation came from this standard input path.

This path does not configure stylus scan mode or display mode. Calibration selection at startup and radio/magnetic-dock state are still coordinated by existing hardware bring-up logic; successful compilation alone does not satisfy those prerequisites.

KDE identifies uinput devices by their `/devices/virtual/` path, so the Drawing Tablet settings page displays a userspace-driver notice without disabling configuration. The stylus is handed to KWin through standard libinput / tablet-tool; pressure curves and display mapping remain managed by desktop settings rather than a separate curve in the decoder process. Pressure-curve adjustments on the device are saved to standard `kcminputrc` and can be reverted; bus type is not altered and hardware paths are not spoofed to hide the notice.

Palm rejection is performed inside finger decoding; see the [Services Source Map](../../../docs/devel/linux/services.md) for the new-kernel implementation, while older kernels and offline diagnostics use [`piano-touch-view-palm-rejection.patch`](../../../tools/patches/piano-touch-view-palm-rejection.patch). For each contact, the connected cells around the peak at or above the touch threshold (default 200) are counted; once a contact reaches 70 cells, it is reported as `MT_TOOL_PALM`. A single palm region occupies only one slot, never reverts to a finger before lift, and never asserts `BTN_TOUCH`; libinput does not forward it to applications, and an active touch is cancelled if a finger turns into a palm. The threshold 70 comes from `palm_area_num_level2` in the stock touch configuration `/odm/firmware/piano_nova_boe_thp_config.ini` (the CSOT variant uses the same value). Stock firmware counts cells on the same 60×40 sensing matrix, where each cell is about 16 mm² and 70 cells is about 1140 mm² (equivalent to a 38 mm diameter circle). The same stock group also defines `palm_neg_area_num_level2=30` (negative-value cell count) and a larger `palm_area_num_level1=110`; this implementation does not count negative cells and only uses the 70-cell threshold, reusing the finger threshold of 200 rather than reproducing the stock area heuristic. When weak signal splits a palm into multiple small islands, it can be misclassified as multiple fingers. Because classification uses cell count, two fingers pressed together into a narrow elongated region still register as two fingers. Contact size estimates the major and minor axes of an ellipse and reports them via `ABS_MT_TOUCH_MAJOR` / `ABS_MT_TOUCH_MINOR` in units of 13/mm (panel dimensions 239×163 mm). When a checksum-validated type29 stylus frame arrives, any held contacts are lifted as palms so a resting hand does not turn into a tap as the stylus approaches. The BSP's `79-piano-pen-touch-group.rules` places the touchscreen and stylus in the same libinput device group, so libinput's built-in touch arbitration ignores new touches near the stylus while it is in proximity. When tuning on the tablet, inspect `ABS_MT_TOUCH_MAJOR`, `ABS_MT_TOUCH_MINOR`, and `ABS_MT_TOOL_TYPE` for fingers and palms with `evtest`; multiplying the major and minor axes and dividing by 3500 approximates the cell count to guide palm-area threshold tuning.

## Stock Protocol Findings

The stock pressure producer is `bt_main_thread` in `com.android.bluetooth`, which receives Report5 through native `libbluetooth_jni.so`, zero-extends each raw byte to int32, and calls `setModeLongValue(0,1088,15,values)`. The touch service then builds common7 / `0x440` and passes it straight to the algorithm, bypassing the kernel common queue. Android's dedicated branch skips normal UHID forwarding, which explains why hidraw reported 0 packets during the same capture; do not assume Linux BlueZ routes packets the same way.

Squeeze on Android has been captured as Report2 `02 6e` / `02 00`, corresponding to `KEY_F19` DOWN/UP and `MSC_SCAN=0007006e`. Report6 is separate IMU/attitude data, not pressure or squeeze. Linux receives the same squeeze Report2 packets along with 21-byte Report6 attitude reports, confirming that the Bluetooth HID channel is delivering data. Report2 carries squeeze, double-tap, and barrel slide; see [Barrel Gestures](#barrel-gestures) for handling. Report6 is not translated into any input events.

## Stock Radio Initialization

[`pen-control.py`](../../bsp/common/usr/lib/piano/pen-control.py) uses system BlueZ to execute the stock control sequence: reading stylus info, two rounds of ID assignment and parameter/frequency/voltage configuration, sending the timestamp and magnetic-dock state, and finally configuring squeeze and screen state. Each step awaits a response; IDs come from the stylus response and the host address comes from the active Bluetooth adapter—no pairing keys or device addresses are embedded.

Radio parameters are stored in [`p81c-radio.json`](../../bsp/common/usr/share/piano/pen/p81c-radio.json), derived from what the stock service sends on this device running `OS3.0.309.0.WPYCNXM`, separate from the BOE/CSOT ini files used for decoding. That stock build uses a runtime `TIME_STAMP` of 1745566508 and selects the standard parameter table; the display's 144 Hz refresh rate must not be passed directly as the stylus frequency parameter.

After switching between operating systems, the stylus may have forgotten the pairing key retained by Linux. Older BlueZ versions (such as 5.79 on SteamOS) may return only a generic connection failure, so the daemon also listens for authentication-failure events on the standard kernel Bluetooth management socket. It clears the stale pairing and re-pairs only when the failing address matches the stylus identified by the magnetic charger; ordinary timeouts do not clear pairings and other devices are unaffected. Automatic re-pairing after authentication failure and radio initialization have been verified on hardware, with the stylus accepting level-3 squeeze haptic feedback.

Full initialization has received every expected response on Linux. An earlier 144 Hz environment still produced no stylus matrix or pressure; a subsequent 120 Hz cold-boot environment received both type29 and Report5 and completed standard stylus drawing. Display/touch scan coordination still needs further comparison, so this does not prove that 144 Hz hardware prohibits stylus input. The radio initialization tool does not read the THP stream, change the display mode, create input devices, or run as a new standalone background service.

On devices with the draft magnetic-dock and activity-state interfaces installed, invoke it explicitly:

```sh
sudo /usr/lib/piano/pen-control.py --address YOUR_PEN_ADDRESS \
  --profile /usr/share/piano/pen/p81c-radio.json \
  --stationary-device /proc/nvt_thp_pen_stationary
```

Running it requires Python GI, BlueZ, and an already-connected stylus. The magnetic-dock and stationary interfaces are optional: when the magnetic switch is absent, no guessed dock state is sent, while the stylus radio and squeeze-feedback parameters are still initialized. Draft modules are used during ad-hoc comparison; the default release kernel does not yet include these two interfaces.

## HID Reports and Desktop Behavior

The P81C's 137-byte HID Report Map places private Report5 data under Keyboard Usage Page `0x57`, which generic Linux HID maps to `KEY_KPPLUS`. The pressure packet's four 16-bit fields and six 8-bit fields are not ten plus keys. The default kernel's [input patch](../../../patches/linux/7.3/0003-input-wireless.patch) extends the existing `hid-xiaomi` driver to skip this bogus keyboard mapping based on Bluetooth VID/PID, Report ID, and field layout while keeping raw hidraw data available to the stylus input handler. It does not replace the full descriptor, globally block `+`, or affect the keyboard cover.

`piano-touch.service` initializes over BlueZ and tracks connections without reading THP or generating keyboard/mouse input. It sends configuration once the stylus is connected and `ServicesResolved` becomes ready, and waits for reconnection after disconnect. Magnetic-dock state is read from the standard switch; BLE initialization still completes when the switch is absent.

While disconnected, the companion process enumerates powered BlueZ adapters and attempts to connect to a paired, trusted Focus Pen or Pro. Once the SC96231 kernel driver supplies the exact stylus address over the magnetic link, the daemon prioritizes that address; if BlueZ reports an authentication failure, it clears only that stylus's stale pairing and re-pairs via the standard Agent1/Device1 interfaces. The pairing agent never registers as the system default agent and rejects other devices. Automatic stale-pairing removal, re-pairing, GATT control service recovery, and initialization have been verified after alternating between stock Android and Linux.

Discovery scans run for 30 seconds followed by a 90-second interval; magnetic-dock state changes trigger an early retry, and device connections and service appearances are handled immediately via D-Bus signals. The daemon never enables Bluetooth if the user turned it off, nor does it stop scans started by other processes. Magnetic-dock power delivery, address retrieval, and battery readout are handled by the kernel `sc96231` driver and MCA regulator without userspace GPIO or I²C commands.

Pressure HID devices are attached immediately via inotify creation events, keeping a 1-second poll as a fallback when inotify is unavailable. Monotonic timestamps are logged at initialization start, radio-parameter completion, pressure-device open, and first Report5 receipt to help diagnose cases where hover works before clicking becomes active. Command response latency still depends on the stylus connection parameters and magnetic-dock state, so total connection latency is not guaranteed to be zero.

Tip position, pressure, and hover remain exported through the standard tablet-tool interface. Cursor appearance is controlled by the desktop environment and applications, not by disguising the stylus as a touchscreen. `piano-pen-canvas` hides the cursor for stylus events inside its own canvas and restores the normal cursor on mouse/touchpad events without altering global desktop settings outside the canvas. Stylus cursors in other applications follow their own settings.

## Barrel Gestures

Squeeze, barrel double-tap, and barrel swipe up/down are recognized by the stylus itself and sent over Report2 as two bytes `02 value`, where each new value replaces the previous one and every gesture ends with `02 00`. The stylus firmware only sends the values listed below; see the [Barrel Gestures Protocol](../../../docs/devel/hardware/pen-protocol.md#barrel-gestures) for evidence from firmware and stock Android.

| Report2 | Gesture | Linux Output | Stock Behavior |
| --- | --- | --- | --- |
| `02 6e` … `02 00` | Squeeze (held while pressed) | tablet-tool node: short press clicks at the tip position; holding > 600 ms asserts `BTN_STYLUS` | Opens the quick menu at the stylus position by default |
| `02 58`, `02 00` | Barrel double-tap | `BTN_STYLUS2` press and release on the tablet-tool node | Delivered to stock drawing, note-taking, and other apps supporting barrel gestures |
| `02 61`, `02 00` | Swipe up | `KEY_PAGEUP` on the stylus keyboard node | Reading and office apps receive Page Up; short-video apps jump to the previous clip |
| `02 5b`, `02 00` | Swipe down | `KEY_PAGEDOWN` on the stylus keyboard node | Reading and office apps receive Page Down; short-video apps jump to the next clip |

Swipes are mapped directly to Page Up / Page Down by the kernel `hid-xiaomi` [`0029` patch](../../../patches/linux/7.3/0029-hid-xiaomi-p81c-body-gestures.patch) and delivered to the focused window just like keyboard paging keys; this does not depend on the tools here and works on other distributions running the same kernel. Generic HID would otherwise map double-tap and swipes to keypad Enter, `9`, and `3`; the patch removes those mappings and stops emitting F19 on squeeze.

The kernel merges squeeze and double-tap inside the HID/touch drivers and reports tool buttons only while the stylus is in proximity, releasing them on out-of-proximity, disconnect, or data timeout. A short squeeze clicks, while holding for 600 ms triggers `BTN_STYLUS`. Applications without dedicated stylus support typically treat `BTN_STYLUS` as middle-click and `BTN_STYLUS2` as right-click; drawing applications can bind them to tools such as eraser or color picker. If a double-tap or swipe interrupts a squeeze, the squeeze ends without registering a click.

The stylus suppresses gestures while pressure is applied to the tip and resumes recognition shortly after pausing. Enable toggles for double-tap and swipe are stored on the stylus, and `pen-control.py` applies `double_tap` and `slide` from the configuration on every connection; see [Stylus Protocol Notes](../../../docs/devel/hardware/pen-protocol.md) for how to change them. Stock Android sends an extra vibration command from the tablet on double-tap and swipe; Linux does not send it, so those two gestures produce no vibration on the stylus.

The report formats for double-tap and swipe are derived from the stylus firmware and stock system and have not yet been verified with on-tablet test logs. When verifying on the tablet, `sudo libinput debug-events --show-keycodes` should show `KEY_PAGEUP` / `KEY_PAGEDOWN` on the stylus keyboard device and `BTN_STYLUS2` on `Piano Focus Pen Pro`; `sudo evtest` can inspect both devices individually. See the protocol page for commands to inspect raw Report2 packets.

## Build and Calibration Packaging Entry Points

`tools/build_release_helpers.py` and `tools/build_piano_runtime_helpers.py` share `build_pen_core()`, invoking this directory's Makefile from a source copy. Runtime artifacts include `/usr/bin/piano-pen-offline`, `/usr/bin/piano-pen-canvas`, the Apache-2.0 license, and provenance notes. The build manifest records the C/C++ sources, headers, Makefile, toolchain, and target static C++ library; the offline tool is a statically linked ARM64 binary and does not require stock ini files at build time. The Bluetooth daemon and service are installed by `piano-device-config` and are not part of this diagnostic-tool build.

`libpiano-pen-core.a` bundles the C-compiled owner/frame units and C++-compiled core/decoder units and is statically linked into regular builds of `piano-touch-view`; the static archive itself is not installed to the system. The touch sources and entry wrappers keep a C ABI, and final linking uses the C++ linker from the same target sysroot. The rootfs installs the binaries via the existing `runtime.stage()` while preserving provenance records for the patches, owner, and core sources.

Native ELF binaries are never placed in `Architecture: all` configuration packages. The BSP maintains numeric calibration data with provenance and SHA256 digests, creating relative symlinks at `/usr/lib/firmware/novatek/piano-{boe,csot}-pen.ini` pointing to existing data for the standard firmware loader. The runtime builder checks for a matching kernel, exported notifier symbols, and calibration files in the target root; on new kernels, missing any of these raises an error. Both build entry points write kernel and calibration digests to the same manifest, which is re-verified at install time. `--sysroot` selects only the build environment; calibration is checked there by default, while cross-distribution builds pass `--firmware-root` to point at a target rootfs that already has calibration installed from the same BSP.

## Retained Stock Algorithm Worker

`prepare_alg_gnu.py` and `piano-pen-worker.c` remain available for comparing against a caller-supplied pinned stock ALG without mixing code with stock library instructions. Their configuration and initialization have been executed successfully in isolation. `--decode29` reconstructs the HAL frame prefix; `--prepare29` additionally requires a 1032-byte common7 / `0x440` buffer and invokes the pressure and matrix preparation interfaces. It does not emit coordinates, nor can a prefix lacking SC state be passed directly to the full `parse_data_package` or slot7.

The primary implementation path is the standalone open-source core described above, which does not require distributing stock ALG/HAL binaries. For complete field layouts, provenance, and remaining hardware issues, see [Stylus Protocol Development Notes](../../../docs/devel/hardware/pen-protocol.md).
