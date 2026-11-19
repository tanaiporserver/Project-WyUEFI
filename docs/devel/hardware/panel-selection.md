# Panel Selection Within a Single Firmware Image

The BOE and CSOT panels on `piano` share the same NT36532 kernel driver, but require different initialization commands. Each device must use the panel identified by the stock ABL rather than assuming the development unit's panel vendor as the default for all devices.

Early in boot, UEFI reads `/chosen/bootargs` from the stock DTB, saves the match result, and patches the panel node's `compatible` property on a newly allocated, expanded copy of the DTB during Linux handoff. The input DTB, CPU, power supply, DSI endpoints, and all other properties remain unchanged. The stock DTB is not used directly as the complete Device Tree for mainline Linux.

| ABL `msm_drm.dsi_display0` Identifier | Linux Panel Configuration |
| --- | --- |
| `qcom,mdss_dsi_p81_42_02_0a_dualdsi_dsc_vid` | `xiaomi,piano-boe-nt36532` |
| `qcom,mdss_dsi_p81_35_02_0b_dualdsi_dsc_vid` | `xiaomi,piano-csot-nt36532` |

If the identifier is missing, unknown, or duplicated, UEFI does not guess the vendor; it preserves the input DTB configuration and logs `PIANO_PANEL_SELECT`. Any new panel must be added to the table only after confirming its reliable stock identifier and corresponding driver configuration. The log also records the length and CRC of the stock parameter so the read source can be verified.

`--panel-vendor auto` in `./build.sh package` preserves the configuration from the reference DTB, deferring panel selection to UEFI Linux handoff. The packaging tool keeps explicit `boe` / `csot` options for diagnostics; this does not imply support for identifying other devices.

On 2026-10-08, after a normal reboot from BOOT with a new ESP and `PIANOROOT` booting 7.2.9, `/proc/device-tree` on real hardware read back `xiaomi,piano-csot-nt36532` while the ESP input DTB remained BOE, confirming CSOT auto-selection end to end. BOE hardware has not yet been tested on device. Targeted host checks also confirmed that only the target panel `compatible` in the handoff copy is modified, leaving the original input intact.

The development unit displayed output both previously under the BOE configuration and after switching to CSOT. Both share the NT36532 driver and primary display timings, but their vendor initialization commands and ESD configurations differ; getting an image on screen does not prove the two configurations are equivalent. Auto-selection aligns the configuration with stock hardware detection and is not claimed as a confirmed fix for display bugs. UEFI physical white-screen issues, refresh-rate switching, and HDR still need separate investigation.

The implementation lives in `uefi/handoff/early-memory/PianoColdBootObjects.c` and `uefi/core/PianoPanelSelection.c`; both the Raw Linux and EFI Linux boot paths use the same selection function. The Linux driver is `drivers/gpu/drm/panel/panel-novatek-nt36532.c`.

The Device Tree is the hardware description firmware passes to the kernel; see the [Linux Device Tree usage model documentation](https://docs.kernel.org/devicetree/usage-model.html) for build-time and runtime modification details.
