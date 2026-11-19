# UEFI Physical White-Screen Diagnostics

The white screen when entering UEFI via Combined BOOT has been fixed: after deferring the CESTA/media and DCD boot-time initialization in stock ClockDxe, the device displays and responds to SimpleInit and can enter the TianoCore setup page. The investigation process and scope of changes are preserved below.

Diagnostics run on the same production core without disabling UFS or USB or replacing the GOP. When diagnostics are explicitly enabled, colored backgrounds and stage names are drawn through the same GOP used by the boot log, holding each checkpoint for three seconds:

| Stage | Background | Purpose |
| --- | --- | --- |
| BEFORE DRIVERS / BEFORE CLOCK | Blue | Before stock driver base layer and ClockDxe start |
| AFTER CLOCK | Green | After ClockDxe returns |
| AFTER DRIVERS | Orange | After the entire base layer returns |
| AFTER UFS | Purple | After storage initialization |
| BEFORE SIMPLEINIT | Cyan-blue | Before invoking the menu |

Each draw logs `PIANO_DISPLAY_CHECKPOINT` with the stage, GOP address, framebuffer address, mode, and return status. A successful draw only proves that the GOP accepted the operation; confirming whether the frame actually scans out to the screen still requires observing the physical panel.

```sh
SUNUEFI_DISPLAY_DIAGNOSTIC=1 ./build.sh uefi
```

This flag is off by default; the build manifest records `display_diagnostic` and the compiler flags. Normal builds add no color blocks or delays and do not alter the feature set. Testing selects the menu boot route (`target=1`) without auto-booting Linux; stock Android passthrough and Recovery behavior remain unchanged.

If the color blocks disappear before entering SimpleInit, narrow down the last visible driver stage first; if all checkpoints are visible and the display only drops after the menu takes over, then inspect SimpleInit's GOP selection, mode, and refresh paths. RAM screenshots cannot substitute for physical panel observation.

## 2026-10-12 Results

When run on the same device through the Combined BOOT menu route, the physical screen displayed the blue `BEFORE DRIVERS` and `BEFORE CLOCK` checkpoints, then turned gray; the green `AFTER CLOCK` and all subsequent checkpoints were not visible. After returning to Android via stock Fastboot `continue`, the preserved logs showed that SimpleInit had selected the 3200×2136 GOP at `0xFC800000`, continuously rendered the menu, and received physical key events.

Therefore, the failure in this run occurred during ClockDxe's StartImage window, prior to SimpleInit; firmware execution still continued to the GUI, so ClockDxe did not hang or block the menu from running. Earlier log entries were overwritten by heavy UFS read logging, so these results could not yet pinpoint a specific register write or power operation inside ClockDxe. A valid software menu screenshot also does not prove that the physical display link is still working.

The FD digest for this test was `db047de7f642c70dfa1c7a459d4913c9a56117ecc88f8508f0c2a18097c69687` with build ID `de7b9504-a576-4406-83f8-3f6290fda743`. After recovery, the boot preference was restored to Android.

## Ruled-Out Standalone Change

In stock `ClockDxe.efi`, the initialization routine at `0xc540` first calls `0xc420` to request temporary clocks for each CESTA instance before checking the auto-init flag at `+0xd8`. Previously, only the display instance's auto-init flag was cleared, so four display clocks were still requested through the normal EnableClock path, touching parent clocks and power states; skipping CESTA table configuration does not mean the display link went untouched.

A draft patch on a derived driver copy changed the request count at `+0xd0` of display instance `0x3e4b0` from 4 to 0, deferring boot-time requests for `gcc_disp_ahb_clk`, non-GDSC AHB, and RSCC AHB/VSYNC. Its name table, runtime protocols, and other instances were kept intact so drivers that actually need these resources can still request them through the standard interface. It did not disable ClockDxe, UFS, or USB, nor write speculative values to display registers.

The derived driver differed from the original by seven bytes in total, with SHA256 `98d76405ef517901ea593d5207afb2daf39c520c35b28f4e776271111200a24d`. Re-running on the same device still showed only `BEFORE CLOCK` and no green `AFTER CLOCK`. This change was reverted from the default source tree: deferring those four requests alone is insufficient to fix the display and cannot be treated as the root cause. Checkpoint results are now stored in production memory and periodically re-emitted by the existing GUI pump to avoid being overwritten by UFS logs; default non-diagnostic builds omit this output.

## ClockDxe Internal Diagnostics

`SUNUEFI_DISPLAY_DIAGNOSTIC=2` draws checkpoints before and after eight fixed call sites covering module initialization, core initialization, power resource registration, display module address fixup, display source registration, CESTA, and CPU initialization. The two display-module call sites only observe the display module; all other modules are invoked as-is without added delays.

Before LoadImage, the diagnostic code patches its owned temporary file buffer: it places trampoline instructions in verified zero-padded executable space within the original binary, validates each original BL target and relocation entry, and redirects calls to wrapper functions in the production code. Each wrapper forwards all eight integer arguments verbatim, invokes the original target, and returns its result unchanged. It does not modify extracted source files, write to live driver code, replace the clock protocol, or access hardware registers directly. After loading, it locates the original calls using the base address provided by LoadedImage, and the build manifest records `clock_init_trace`.

This instrumentation extends initialization time and is only for debugging; default release builds with `SUNUEFI_DISPLAY_DIAGNOSTIC=0` contain none of these trampolines or pauses. On-device findings are still cross-checked using the last visible checkpoint and preserved logs.

The first round of internal diagnostics preserved all 22 stages and proceeded into SimpleInit; physical observation only recalled that the last screen may have been green, without a reliable stage name, so the failure could not yet be narrowed down to a single call. Successful software rendering does not guarantee that those stages actually appeared on the physical screen.

Large two-digit numbers and large-font stage names were added to the diagnostic screens, with a 3200×2136 preview generated using the C drawing code. Each stage also performs read-only sampling of the verified GCC registers `0x127004` and `0x127008`, reading each twice to verify stability and re-emitting the status alongside the AHB/AXI values. It does not read DISPCC or DPU registers whose power-domain access conditions are unproven, nor does it modify any clock registers. The next round cross-references the last visible number against these records.

## Fix and On-Device Results

The large-numbered diagnostic run stopped first at purple `14 BEFORE CESTA`. After deferring only the CESTA/media boot call, the subsequent CPU stages became visible, but the menu remained a black screen with the backlight on; after also deferring the DCD boot dependency block, the SimpleInit menu rendered and responded to input, and then entered the TianoCore setup page. This confirms the white screen occurs during stock clock initialization, prior to SimpleInit, rather than being caused by menu rendering itself. The exact individual PLL or register operation inside those initialization blocks has not yet been isolated.

Release builds derive ClockDxe in a working copy via `tools/piano_inherited_clock.py`:

* RVA `0xc784`: replaces the boot-time `BL` call to `0xc540` with `MOV w0,#0`, deferring the entire CESTA/media auto-initialization sequence.
* RVA `0x3ead0`: changes the DCD boot dependency switch from 1 to 0.

This changes five bytes in total relative to the pinned original, yielding derived SHA256 `abb63e6cc5d96dc2c3256f5a09ba0a30216150fa7aea595743d84ef067686f60`. CPU/SCMI initialization, clock protocol callbacks, resource descriptors, and subsequent explicit request interfaces are all preserved. Neither UFS nor USB is disabled, no speculative display register writes are added, and the safe-exit rules remain intact.

On-device observations come from the same production core with stage diagnostics enabled. The release image with diagnostics disabled has been built locally and contains no colored stage pauses. Getting the menu to display does not mean USB is fixed: during this test, cold boot failed to enumerate UEFI USB devices even before entering the setup page, which still requires separate investigation.
