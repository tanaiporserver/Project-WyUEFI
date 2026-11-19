# SM8750 Dual-DSI and Refresh Rate Switching

Piano drives a single panel over two DSI links, defaulting to 120 Hz. 30/48/50/60/120 Hz use seamless switching, while 90/144 Hz perform a full display reset due to different horizontal timings. Refresh rate switching, 144 Hz display output, and stylus input all work; a full reset is a normal switching path, not a failure.

## Seamless Switching

Both the stock BOE and CSOT Device Trees provide dynamic refresh rates based on the vertical front porch. `0035-panel-piano-low-refresh-rates.patch` adds 30, 48, and 50 Hz, reusing the 120 Hz clock and horizontal timings just like 60 Hz, and lowering the refresh rate by extending per-frame blanking time.

`0037-drm-piano-seamless-vfp.patch` recognizes modes in the same timing family during a standard atomic modeset: it updates the double-buffered DSI timings and the DPU interface frame period, committing at the frame boundary. Panel power, DSC, and touch firmware remain running, and normal Gamma updates can be committed at the same time. Changing horizontal timings, link clock, or CTM routing still performs a full reset.

The device has repeatedly completed seamless switches across the fixed 30/48/50/60/120 Hz modes while keeping the touch firmware epoch unchanged; steady-state frame interrupt frequency matches the requested rate with no new underruns. Automatic rate selection based on video frame rate or idle content still requires desktop and driver coordination.

## Display Off and Resume

The following fixes together address the earlier black screen, gray screen, and colored shutdown artifacts:

| Patch | Purpose |
| --- | --- |
| `0024-dsi-pll-defer-unprogrammed-clock.patch` | Avoids enabling an unprogrammed PLL prematurely when registering clock providers |
| `0025-dsi-sm8750-restore-bonded-slave.patch` | Saves and restores the divider state of both master and slave PHYs |
| `0026-dpu-video-stop-order.patch` | Flushes the DPU pipeline before stopping video timing so cleanup completes on a frame boundary |
| `0034-dsi-phy-runtime-interface-clock.patch` | Manages the PHY interface clock through runtime PM |
| `0036-backlight-ktz8866-wait-disable-ramp.patch` | Waits for both backlight chips to finish ramping down before turning off the panel and video link |
| `0038-panel-piano-vendor-refresh-order.patch` | Preserves the stock command order: B2→B3 for CSOT and B3→B2 for BOE |

The PLL registration, DPU stop-order, and PHY interface clock patches are based on Junhao Xie's [67b3cbd4bab](https://github.com/BigfootACA/linux/commit/67b3cbd4bab5687c3aa3c9fe5a14c37433e28bbf), [8f353c130fee](https://github.com/BigfootACA/linux/commit/8f353c130fee0fcf09afbe10e7fc58f901931418), and [b95037aef0a0](https://github.com/BigfootACA/linux/commit/b95037aef0a0bf5cd612e7517a67644887e9fd5d), preserving original authorship.

Normal boot, display off/resume at 120 Hz, and wake via RTC and the power button have all been verified on hardware. The system remains usable after display resume with touch and networking restored; this does not imply that standby power draw matches stock levels. For other driver work related to system suspend, see [Power Management](power-button.md).

During shutdown and reboot, the display controller no longer submits color data to an already-disabled output. Device links ensure it turns off the panel and backlight before stopping the I²C bus used by the backlight. On Debian hardware, register readbacks confirm both backlight chips power down on screen-off and recover on screen-on; rebooting cleanly from a lit screen no longer logs atomic display shutdown failures or suspended backlight bus errors.

## 144 Hz and Timing Differences

90/144 Hz use different horizontal timings from 120 Hz and cannot directly reuse the VFP update path above. Earlier display corruption and stylus input issues are resolved; stock command ordering, dual-link state restoration, and touch scan coordination remain in place. Historical faults are kept as implementation context and are no longer listed as current usage limitations.

See [Stock Refresh Rate Analysis](oem-refresh.md) for the stock switching flow, and [Color Processing](display-depth.md) for display bit depth, HDR, and FRC boundaries.
