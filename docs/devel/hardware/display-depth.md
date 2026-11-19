# Display Bit Depth and HDR

## Current Implementation

Piano's default kernel uses RGB101010 with DSC 1.1: **10 bpc** input, **8 bpp** compressed transport, and `800×24` slices. Compressed bit rate does not equal panel bit depth. Both the BOE and CSOT final stock Device Trees use the same 10-bit transport configuration, and the driver selects the initialization sequence per panel.

Standard DRM/KMS interfaces include:

| Interface | Capability |
| --- | --- |
| `max bpc` | Fixed at `10..10`, matching the current panel and DSC configuration |
| `GAMMA_LUT` | Uniform 65536 entries, mapped to GCv2's 1024 main sample points and 256 dark-region extension points |
| `DEGAMMA_LUT` | 1025 uniform samples, 16-bit per channel, mapped to the IGCv5 high-precision curve |
| `CTM` | Existing PCC color transformation matrix |
| `EDID`, `Colorspace`, `HDR_OUTPUT_METADATA` | Synthesized capability description for the fixed internal panel, BT.2020 RGB, and PQ output conversion; requires matching calibration firmware |

GC and IGC are submitted in the same REGDMA buffer to both active DSPPs and take effect together upon completion. Identity, non-linear, and bypass restoration for the dark-region extension have been verified on hardware: a full curve takes 2532 words, bypass takes 8 words, and both DSPPs report completion. See the [GCv2/IGCv5 backend](gcv2-night-light.md) for implementation details and resource management.

KDE 6.6 reports color depth as automatic 10-bit with an allowed range of `10..10`, so no bit-depth selector is shown; GNOME 51 also lacks a standalone 8/10/12-bit selector. KWin currently uses an `AR30` 10-bit primary plane. Changing the transport bit depth also requires matching panel commands and DSC PPS, not just widening the `max bpc` property range.

The fixed internal panel receives PQ/BT.2020 output from the compositor through standard properties, and the DPU converts it to the panel's native encoding rather than sending PQ data directly to the DDIC. Stock KWin can toggle HDR and wide color gamut; the compositor handles mixed content and tone mapping, while the driver handles panel signal conversion. Standard SDR continues to use the existing path by default. EDR is a separate compositor policy that uses backlight headroom and is not equivalent to this HDR output mode.

## Resolutions and Refresh Rates

On the same device, stock `DisplayManager` and SurfaceFlinger enumerate seven modes, all at `2136×3200` (`3200×2136` in landscape), with refresh rates of 30, 48, 50, 60, 90, 120, and 144 Hz. In the final BOE and CSOT Device Trees, each DSI link has a timing width of 1600 and height of 2136, with dual-DSI combining to the full width. The Linux panel mode table matches these dimensions and refresh rates, defaulting to 120 Hz.

The kernel exposes 2560×1708, 2400×1602, 1920×1280, 1600×1068, and 1280×854 source modes, plus 640×480 for EDID compatibility, each preserving all seven refresh rates. The DSI bridge restores the native timing at the same refresh rate in `adjusted_mode`, and the DPU scales plane and cursor destination coordinates to the full panel before SSPP allocation and dual-pipe cropping. DSC slices and panel commands remain unchanged.

On Linux 7.3.3, SteamOS/KDE has switched through 2400×1602@120/60, 1280×854@60, 2560×1708@120, and 1600×1068@120 before returning to 3200×2136@120. Mesa's [buffer height alignment fix](../linux/steamos.md#ffmpeg-and-graphics-runtime) preserves the mode dimensions while padding the tail space required for GPU import. The physical display remains full-screen; in 1600×1068 mode, kernel readback confirms both halves scaling `800×1068 → 1600×2136`, and the cursor scales to physical coordinates as well. Other combinations and post-switch touch positioning still need to be checked case by case.

The standard `scaling mode` property supports `None` and `Full`; lower resolutions require `Full`, and requests exceeding native dimensions are rejected. Full-screen mapping preserves the 1:1 correspondence of absolute touch coordinates without introducing letterbox coordinate conversion. KDE selects full-screen scaling by default via KWin's existing `KWIN_DRM_FORCE_SCALING_MODE=FULL`, while Gaming Mode uses the kernel default. Desktop logical scaling continues to apply independently.

Dual SSPPs each scale half of the frame, so bilinear interpolation along the internal seam needs the adjacent source pixel from the other half. On Piano's QSEED 3.4, linear RGB planes use a horizontal preload value of 7 when upscaling horizontally without vertical downscaling, and read one extra source pixel only at the internal right boundary; the rightmost physical edge of the frame does not extend its fetch range. This configuration covers non-integer upscaling rather than being limited to 2x scaling at 1600×1068. Native resolution, downscaling, rotation, compressed buffers, and YUV planes remain on the original path.

On-device verification on Debian 13/KDE confirms the center text seam disappears at 1920×1280@120 and 2560×1708@120. Desktop mode switching and screen-off/on cycles at 60/120 Hz pass for 3200×2136, 2560×1708, 1920×1280, 1600×1068, and 640×480. With current Mesa 26.1.6, 2400×1602 and 1280×854 are rejected by KWin's test-buffer check, so desktop switching cannot be considered passing for them; both have passed left-half scaling readback through the direct DRM path. Readback for non-integer ratios still shows under 4/255 per-channel difference against an ideal bilinear reference, while native and 2x-scaled control readbacks remain unchanged. Concurrent readback of the right half duplicates the left half and therefore cannot be used to judge actual right-side display output.

## Stock Color Processing

The pinned [MiCode display driver](https://github.com/MiCode/vendor_opensource_display-drivers/tree/aa06fd1757c28dce96fbe4e04d4530ec21b52aac) and on-device Android DRM state show:

- Stock SDR/P3 modes use IGCv5, GCv2, and Gamut v4, with high-precision extensions enabled on both IGC and GC. On-device runtime state has IGC internal dither enabled, while QDCM profiles such as `expert_*`, `hal_*`, and `video*` disable it; see [12-bit and Dithering](#12-bit-and-dithering).
- IGC's high-precision format consists of 257 regular points plus 128 dark-region extension points and must not be interpreted as 385 uniform points. The stock QDCM sRGB table matches a regular step of `1/256` and a dark-region step of `1/1024`; an independent Gamma 2.2 table deviates by at most one 16-bit code value. The default backend exposes a uniform 1025-point interface to userspace and selects the two sample sets required by hardware.
- GC's high-precision format consists of 1024 main sample points plus 256 dark-region extension points. The stock Gamma 2.2 main table spans `0..1`, while the dark-region table spans roughly `0..255/65536` with output code values `0..82`. In both SDR and active HDR playback, both GC control words read `0x5` and both table sets are identical; Linux supplies both sample sets through a standard uniform LUT without treating the dark-region extension as FRC.
- Stock DSI `hdr_properties` describes static color primaries and luminance capabilities; it is not equivalent to HDMI/DP dynamic `HDR_OUTPUT_METADATA`, nor does it prove that the DDIC accepts PQ pixels directly.
- When stock HyperOS 4 plays HDR10 video on the same device, the video layer is `BT2020_ITU_PQ`, display output remains `DISPLAY_P3`, and the HDR render intent is `0x113`. Both video planes use Gamut v6 3D LUTs, while the full display continues using IGCv5, GCv2, and Gamut v4. Porting requires converting video content to panel output rather than feeding PQ data directly into the existing panel link.

Reference source: [`reg_dmav2_setup_dspp_igcv5`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/sde/sde_hw_reg_dma_v1_color_proc.c#L5366), [`dsi_panel_parse_hdr_config`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/dsi/dsi_panel.c#L4061).

HyperOS 4 calibration configs live at `/odm/etc/display/qdcm_calib_data_xiaomi_p81_35_02_0b_vid_mode_dualdsi_dsc_panel.json` and the corresponding `p81_42_02_0a` file. Hex payloads inside the JSON must have their byte-pair order reversed before the embedded curves and XML can be read:

- `native_hdr` targets an output Gamma of 2.2 and includes P3 panel calibration, 17-point tone mapping, real-time brightness compensation, and content statistics; in the CSOT config, full-screen peak is around 844.5 nit and SDR reference peak is 400 nit. These are configuration values, not physical luminance measurements.
- HDR modes and intent-1 `standard_*` modes configure LUMA dither with `temporalEnable=0`; config fields cannot substitute for live playback register state, nor can this field alone establish panel FRC.
- The [high-precision backend in the display patch](../../../patches/linux/7.3/0002-display.patch) exposes a uniform 1025-entry `DEGAMMA_LUT`, from which the driver picks both hardware sample sets. The combined DMA buffer is 64 KiB, holding GC, IGC, and Gamut state for both pipes; identity, non-linear, and bypass restoration have been verified on hardware.
- Linux 7.3's [`0016` patch](../../../patches/linux/7.3/0016-drm-igcv5-high-precision-dither.patch) matches the stock IGC control word by enabling high-precision internal dithering at strength 4 whenever a Degamma LUT is present, while keeping an empty LUT in bypass. It does not change 10-bit DSC and does not provide panel FRC control.

### HDR Brightness and Backlight

Stock uses two KTZ8866 backlight chips. System requests in `0..4095` first go through normal/HBM range mapping, then through the `mi_bl_level_remap` curve, and are finally written to each chip's 11-bit brightness register. System request values must not be treated directly as chip code values or luminance.

In the 2026-10-20 HDR10 playback comparison, minimum brightness requested 17 from the system and both chips read back 374; maximum brightness requested 4095 and both chips read back 2047. Holding minimum brightness visually confirmed physical screen dimming. The stock `actual_brightness` callback always returns 0 and cannot be used to determine whether the screen is off; register readbacks are also not colorimeter-measured nits.

While paused on the same HDR video buffer, a "max → min → max" comparison showed that full-screen IGC, GC, PCC, and Gamut tables stayed constant, whereas the Gamut v6 tables on both video planes changed with brightness and returned to the first table at the end. The compositor-reported video white point also changed from 800 to 4.5 and back to 800 nit; this is a software luminance descriptor rather than a measured value. Stock HDR processing includes brightness-dependent video tone mapping, so porting must handle both backlight and content mapping.

Media players can override system settings via window brightness. When debugging, check `mWindowManagerBrightnessOverride` first before assuming system dimming commands apply; during visual checks, hold a single level while observing to avoid asking for confirmation after it auto-reverts.

Related implementation: [`dsi_panel_set_backlight`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/dsi/dsi_panel.c), [`mi_backlight_ktz8866.c`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/mi_disp/mi_backlight_ktz8866.c), [`sde_backlight_device_get_brightness`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/sde/sde_connector.c).

## 12-bit and Dithering

[Xiaomi's official specs](https://www.mi.com/global/product/xiaomi-pad-8-pro/) claim 12-bit color depth. The stock link is 10-bit transport; the extra two bits can only come from internal panel FRC or SoC-side dithering. The sections below list what has been ruled out and what remains unread.

Qualcomm's standard pingpong dither cannot be enabled as a substitute. Stock [`_sde_encoder_setup_dither`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/sde/sde_encoder.c#L4002) disables this block under DSC10/30bpp configurations, and its bit-depth table only covers 6/7/8-bit outputs. IGC's 16-bit LUT, internal dither, and panel 12-bit FRC are also distinct concepts.

During both SDR and active HDR10 playback on stock HyperOS 4 on the same device, both pingpong dither enable registers (SDE offsets `0x6a0e0`/`0x6b0e0`) and both DSPP dither enable registers (`0x5582c`/`0x5782c`) read back 0. During HDR playback, the `BT2020_ITU_PQ` layer and `renderIntent=0x113` were confirmed simultaneously; neither property blobs nor static configs can substitute for this hardware readback.

Stock does not use a higher bit-depth link: the CSOT node `qcom,mdss_dsi_p81_35_02_0b_dualdsi_dsc_vid` specifies `qcom,mdss-dsi-bpp = 30` and `qcom,mdss-dsc-bit-per-component = 10`, and public DSC latency calculations are based on hardware configurations up to 10 bpc. On Linux, the initialization commands for both panels match the stock 120 Hz timing group command for command: 97 commands for CSOT and 104 for BOE, including their respective B2/B3 sequences. Identical initialization alone does not prove the effective bit depth or switching mechanism of internal FRC.

The DSPP dither register locations come from the final stock Device Tree's `qcom,sde-dspp-dither = <0x82c 0x10007>` and DSPP0/DSPP1 base addresses `0x55000`/`0x57000`. Both post-blend blocks are disabled and cannot be the source of stock's two bits of FRC. How DDIC internal FRC is controlled has still not been confirmed from public source code or registers.

AIQE is not enabled by the stock driver either. At boot on stock HyperOS 4 on the same device, the SSIP fuse configuration reads `disable=1` and `polarity=0`, causing the driver to set `ssip_allowed=0` and skip installing AIQE operations and DRM properties; the active internal display's property snapshot matches this. The AIQE dither address in the Device Tree is not evidence that it implements FRC. For the decision path, see stock [`sde_hw_check_ssip_fuse`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/sde/sde_hw_catalog.c#L6327), [`dspp_aiqe`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/sde/sde_hw_dspp.c#L479), and [`_dspp_aiqe_install_property`](https://github.com/MiCode/vendor_opensource_display-drivers/blob/aa06fd1757c28dce96fbe4e04d4530ec21b52aac/msm/sde/sde_color_processing_aiqe.c#L40).

Stock configs only request post-blend dithering in a subset of modes. Under `/odm/etc/display/`, CSOT's `qdcm_calib_data_xiaomi_p81_35_02_0b_vid_mode_dualdsi_dsc_panel.json` and BOE's corresponding `p81_42_02_0a` file each contain 35 modes with identical dithering fields entry for entry:

| Mode (render intent) | `PostBlendDither` | `PostBlendPaDither` | IGC Internal Dither |
| --- | --- | --- | --- |
| `native_hdr`, `native_p3`, `native_srgb` (275, i.e. `0x113` during HDR10 playback) | On | Off | On, strength 4 |
| `standard_hdr`, `standard_p3`, `standard_srgb` (1, Android's ENHANCE) | On | Off | On, strength 4 |
| `GM1`, `GM2`, `GM3`, `GM11`, `GM22`, `GM33` (263–265), `smart_MC`, `smart_MC1` (258) | Off | Off | On, strength 4 |
| `expert_*` (267–269), `hal_*` (256, 257), `video*` (259–262), `Native_D65_*` (273), `standard_native` (0) | Off | Off | Off |

Every mode with `PostBlendDither` enabled in config uses the exact same settings: `ditherMode` set to `LUMA`, `strengthR`/`strengthG`/`strengthB` all set to 6, `temporalEnable=0`, and the 4×4 matrix `[15,7,13,5,3,11,1,9,12,4,14,6,0,8,2,10]`. In actual SDR and HDR playback these post-blend blocks remain off, so Linux does not enable them either; IGC internal dither is enabled at strength 4 by the 7.3 `0016` patch whenever a Degamma LUT is present.

## Standard HDR Output

The panel driver synthesizes EDID 1.4, a CTA HDR data block, and DisplayID native timing. This describes the capabilities of the full DPU/fixed internal panel path rather than an EDID actually supplied by the screen. The 10-bit input, P3-D65 primaries, Gamma 2.2, and luminance figures come from the corresponding stock config; because the native 120 Hz pixel clock exceeds base EDID's 655.35 MHz ceiling, it is described in full via DisplayID type VII. Lower-resolution inputs in the base block are handled by full-screen DPU scaling without changing DSI/DSC timing. KDE's default display configuration matches both the legacy EDID-less entry and the synthesized identity, keeping 3200×2136@120 with 2x scaling.

Matching `qcom/sm8750/piano-{boe,csot}-display.bin` files supply the conversion data. See [display calibration firmware](../../../vendor/firmware/piano-display/README.md) for generation sources and methods, and [local firmware package](../../../linux/bsp/README.md#local-firmware-packages) for standalone installation. If the firmware is missing, malformed, or does not match the hardware blocks, HDR properties are not attached, no HDR EDID is generated, and standard SDR continues to boot. Capability properties are created before DRM device registration.

There are three output states: standard SDR bypass, BT.2020 SDR, and PQ HDR. The latter two apply IGCv5 linearization, Gamut v4.3 17³ color gamut and panel calibration, and the stock GCv2 curve in sequence, broadcasting the same batch of REGDMA commands to both DSPPs. While conversion is active, an empty or identity `GAMMA_LUT` does not alter pixels and remains accepted. `CTM`, non-identity `GAMMA_LUT`, and `DEGAMMA_LUT` share hardware with output conversion, so atomic check rejects those combinations and the compositor must perform them on the GPU; commit failure does not automatically trigger a GPU fallback. This stays compatible with the identity Gamma table submitted by GNOME at its default white point, while non-identity adjustments such as Night Light and ICC VCGT still require the compositor to provide its own path. Standard LUT and Night Light interfaces for normal SDR are unchanged. Driver exit and DMA errors continue to follow the [REGDMA resource retention rules](gcv2-night-light.md).

When switching SDR wide color gamut on the internal panel without changing timing, only the two color tables are updated without turning off the panel or backlight. Across two consecutive round-trips on Debian hardware, both color DMA transfers completed and neither the display controller nor either backlight chip powered down. HDR toggling, refresh-rate switching, and resume after display-off still follow the existing path.

On SteamOS, KWin completes HDR/wide-color-gamut toggling, DPMS screen-off recovery, and one RTC wake after `deep` suspend through standard properties. The boot ID stays identical before and after resume, the suspend success count increments by one, ADSP/CDSP remain running, and front camera capture and microphone recording recover. In PQ mode, the Gamut, IGC, and GC control words on both pipes read back `0x1`, `0x10114`, and `0x5` respectively; once disabled, all enter bypass, with REGDMA completion status `0x8` and zero error status. Backlight rises to 2047 in HDR; a generic KWin 6.6.5 fix invalidates the brightness cache when exiting HDR, and hardware backlight completes the `1535 → 2047 → 1535` transition. The patch lives in `patches/kwin/6.6.5/` and does not alter HDR capability detection or color processing interfaces. The four-level PQ test pattern shows visible brightness steps. This verification does not mean per-unit peak luminance, black level, or color accuracy has been measured, nor does it mean 12-bit FRC is implemented. BOE data has been generated from the corresponding stock config, with no on-device test results on that panel yet.

[`tools/android/piano_drm_snapshot.c`](../../../tools/android/piano_drm_snapshot.c) exports DRM properties and LUT blobs read-only on both Android and Linux. Compare SDR and HDR playback state under the same scene settings while confirming the active CRTC, HDR layer, and power state; blob presence or DMA completion alone does not prove physical HDR output.
