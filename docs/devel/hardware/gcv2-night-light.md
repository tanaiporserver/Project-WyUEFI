# GCv2/IGCv5 and Standard Color Interfaces

The default kernel submits hardware color lookup tables through Qualcomm REGDMA, exposing standard `GAMMA_LUT` and `DEGAMMA_LUT` alongside the existing `CTM`. GNOME Night Light works; for HDR signal conversion on the fixed internal panel, see [Display Bit Depth and HDR](display-depth.md#standard-hdr-output). 12-bit FRC is still not implemented.

## Hardware Resources

The implementation cross-references the [pinned MiCode display source](https://github.com/MiCode/vendor_opensource_display-drivers/tree/aa06fd1757c28dce96fbe4e04d4530ec21b52aac) and the final stock Device Tree: REGDMA3 has MMIO at `0x0af80000/0x7000`, XIN 7, and GCv2 at offset `0x17c0` relative to DSPP. The boot Device Tree provides the named resource via [piano-regdma-resource.dtso](../../../linux/dts/piano-regdma-resource.dtso).

The driver allocates a 64 KiB GEM buffer with an IOVA in the normal DPU GPUVM to hold the combined GC, IGC, and Gamut v4.3 state, rather than treating a CPU physical address as a DMA address. LUT commands and the final descriptor share the mapping, with the descriptor aligned to the command length. CTL only uses instances present in the catalog, and register ranges are bounds-checked before access.

## Curve Submission

- GCv2 exposes a uniform 65536-entry `GAMMA_LUT`, mapped to 1024 main sample points covering the full input range plus 256 dark-region extension points. The extension points have an input step of approximately `1/65536`, while output remains 10-bit. Both sample sets are written sequentially through the same HW-index port before swapping tables and enabling high-precision mode; legacy GC continues to use the 1024-entry interface.
- IGCv5 uses a standard 1025-entry uniformly sampled Degamma curve, keeping 16-bit values per channel, mapped to the stock 257 regular points and 128 dark-region points and written through LUTBUS. The high-precision extension is enabled, and Linux 7.3 further enables IGC internal dither to match the stock configuration; this does not change DSC transport bit depth.
- Both curve types are combined into a single complete color-state batch and submitted to both currently allocated DSPPs. The corresponding flush bits are updated only after both complete; disabling a curve uses the bypass state.
- On DMA queue failure, existing lifetime and teardown rules remain in effect so memory that may still be in use by hardware is never freed.

The IGCv5 implementation lives in the [display patch](../../../patches/linux/7.3/0002-display.patch), and the GCv2 dark-region extension lives in [`0052`](../../../patches/linux/7.3/0052-dpu-gcv2-dark-range.patch). On Linux 7.3.3 hardware, identity, non-linear, and bypass restoration for the dark-region extension have been verified: the dual-DSPP mask is `0x60000`, and hardware completion `status=0x8` is read back every time. A full combined command is 2532 words with the final descriptor aligned to 10240 bytes, still fitting inside the 16 KiB buffer; bypassing both LUTs takes only 8 words. Physical dark-tone gradation and color accuracy still require independent verification.

## Desktop Compatibility

Mutter's empty-Gamma restoration previously triggered a null-array access; the fix lives in [0001-kms-empty-gamma-bypass.patch](../../../linux/desktops/gnome/patches/mutter/0001-kms-empty-gamma-bypass.patch) and is included in both Debian and Nyarch builds.

Without display calibration firmware, the internal panel still has no EDID; GNOME's color configuration uses the colord device to create an uncalibrated default sRGB profile while preserving custom user profiles. When matching calibration firmware is present, a synthesized EDID and standard HDR properties are provided; see [Display Bit Depth and HDR](display-depth.md) for bit-depth and stock processing boundaries.
