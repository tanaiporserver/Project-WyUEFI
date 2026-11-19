# Integrating Stylus Support in the Touch Process

`piano-pen-owner.c` is driven by the same poll loop as the existing `piano-touch-view`.
It does not open the THP FIFO; instead, it only reads the `hidraw` device matching Bluetooth VID `0x22` and PID `0x5081`. The 15-byte Report5 enters the core stamped with `CLOCK_BOOTTIME` at read time; NTP1 uses the kernel's same clock source, strips the 257-byte transport header, and feeds the raw type29 payload into `piano-pen-frame`'s dual-checksum validation and the core. Disconnection, stream epoch changes, and process exit release the input state.

Stylus support is disabled by default. Draft on-device usage appends the following flags after the existing `input` command and finger arguments:

```sh
--pen-ini ACTUAL.ini --pen-pressure-max-age-ms 100 --pen-input
```

When multiple devices of the same model are present, select the stylus explicitly with `--pen-hidraw /dev/hidrawN`.
`--pen-json` outputs decoded native portrait values, with unknown fields set to `null`.
The owner does not manage Bluetooth connections, stock initialization, or display/scan state changes; those are handled by the BlueZ companion process in the existing touch service.

The stylus reports through a dedicated standard tablet-tool `uinput` device. Landscape coordinates are mapped as `X=portraitY` and `Y=213599-portraitX`; tilt values are mapped as `tiltX=portraitTiltY` and `tiltY=-portraitTiltX`.
The 239×163 mm panel corresponds to X/Y resolutions of 1339/1310 native units/mm.
`BTN_TOUCH` is the first event in every SYN frame. Report2 `02 6e`/`02 00` maps to `BTN_STYLUS` press/release and is reported only while the tool is in proximity; releasing the tool also releases this button. The dedicated HID P81C mapping also suppresses duplicate F19 keyboard output.
Unknown reports do not generate barrel-swipe events or character shortcuts.

Valid coordinates indicate proximity entry; fresh pressure greater than zero indicates contact, while zero pressure indicates hover. Missing or stale pressure releases contact according to the explicit timeout policy without fabricating zero pressure or unknown tilt values.
When a valid raw frame is received but the solver no longer yields coordinates, the tool leaves proximity. The 100 ms limit is a caller-side policy rather than a stock timeout parameter. Receiving no valid frames beyond the same threshold also releases tool state. Value or checksum failures are only counted and never produce fallback coordinates.

Type29 contains only a single finger-matrix quarter and cannot be fed directly into type3's contiguous finger-matrix decoder.
The current integration skips that path and releases stale finger tracking state whenever a valid type29 frame arrives.
Full quarter bridging required for simultaneous stylus and finger input still needs to be implemented.

For building, apply `tools/patches/piano-touch-view-pen-owner.patch` to the derived copy of `piano-touch-view` after the observability and raw-capture patches. C sources include touch-view, owner, and frame; C++20 sources include core and decoder. Link statically with the C++ linker from the same ARM64 GNU sysroot rather than the host x86 default `ld`. The existing Makefile archive already includes the owner C object; the touch-view object can be linked against that archive and `-lm` using `clang++` or cross `g++`.
