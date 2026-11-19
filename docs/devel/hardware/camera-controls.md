# Camera Formats and Controls

The camera service exposes two V4L2 cameras (front and rear) to applications. Normal mode uses the CAMSS hardware image signal processor (ISP) to output NV12; full-resolution RAW10 on the rear camera uses software Quad CFA conversion. The service only powers on the sensor and ISP when an application starts streaming, and stops DMA when streaming stops. Auto-exposure, auto white balance, and rear autofocus (3A) are driven by the frame loop in the same service. Because there is not yet a libcamera TFE pipeline and IPA algorithm module that operates without reading image data, the legacy service along with its loopback devices and control socket remain required for applications to use the cameras.

On SM8750 TFE980, the PIX sink accepts four RAW10 Bayer patterns, while its source and corresponding capture node output only NV12. Raw RAW10 capture uses RDI; PIX must not be advertised as a RAW10 capture device merely because its sink accepts Bayer input. The [format patch](../../../patches/linux/7.3/0114-camss-pix-capture-formats.patch) defines the sink and capture formats separately and places the default SM8750 sink format among the supported formats, instead of sharing a capture enumeration that mixes RAW10 and NV12.

NV12 capture and cropping share the same dimension alignment: width must be a multiple of 16, and height and crop origin must align to full chroma samples. The minimum downscale size is rounded up to an even number at a 16× downscale ratio; with a 4080×3060 rear-camera input, the minimum scaled size read back on the tablet is 256×192. DMA base addresses and PIX line strides are validated against TFE980's 256-byte address unit.

## Kernel Interfaces and libcamera

The production camera driver comes from the [Linux patch series](../../../patches/linux/7.3/series.json), not unmodified mainline CAMSS. Images, controls, and frame events use V4L2, while the color correction matrix still uses a local private control. TFE's `vfe_isp_params` is internal kernel state, not a parameter buffer interface that userspace can queue.

CAMSS writes only a single DMA base address per image plane to hardware. [Buffer validation](../../../patches/linux/7.3/0135-media-camss-validate-capture-dma-ranges.patch) verifies before programming that the mapped contiguous address range covers the negotiated `sizeimage`. A contiguous range may span multiple adjacent DMA segments rather than requiring the entire buffer to be contiguous; if the range is insufficient, Videobuf2 unmaps it and the buffer is never handed to hardware.
Connecting directly to the front and rear capture nodes on the tablet, both 1920×1440 NV12 streams passed MMAP and DMA-BUF capture as well as stream-off/restart; rear 4080×3060 RAW10 also passed capture and restart.

### Existing Service and Standard Replacement

The existing service still provides the camera functionality required by applications. The libcamera prototype can already capture frames directly from hardware, but cannot yet replace the controls and still-capture workflow provided by the service.

Both GNOME and KDE on Debian share the same set of camera packages. libcamera is built first, then PipeWire and Plasma Camera are linked against that version of the library; the release manifest verifies their archive versions and SHA-256 checksums. They can be rebuilt individually using an ARM64 sid build root configured with only official signed Debian repositories:

```sh
sudo ./build.sh libcamera --sysroot "$SID_ARM64_ROOT" --output build/libcamera
sudo ./build.sh pipewire --rootfs "$SID_ARM64_ROOT" --libcamera-packages build/libcamera \
  --output build/pipewire-libcamera
sudo ./build.sh plasma-camera --sysroot "$SID_ARM64_ROOT" --libcamera-packages build/libcamera
```

The build replays source code according to the [pinned libcamera version](../../../config/libcamera.json) and [CAMSS patch series](../../../patches/libcamera/0.8.0/series.json), keeping Debian's package split and IPA signing. Seven ARM64 `.deb` packages are placed under `runtime/` in the output directory, while the source package and Debian build logs go into `source/`. PipeWire compiles `libspa-0.2-libcamera` against this set of development packages and verifies which library the plugin actually links against.

Packaging itself does not replace the existing camera service. The 0.8 IPA modules, signatures, and proxies are installed by `libcamera-ipa-0.8` into a separate directory without overwriting Debian 0.7's `libcamera-ipa`. The final image explicitly installs the matching IPA version and standard camera plugins, and KDE also installs the relinked Plasma Camera.

| Feature | Existing Service (Legacy Path) | Standard Hardware Path (Replacement Status) |
| --- | --- | --- |
| Front/rear preview and video recording | CAMSS PIX outputs NV12; the service exposes it to applications via two loopback cameras | Normal Debian desktop sessions already capture frames from both physical cameras via the production libcamera and PipeWire packages; hardware statistics, per-frame controls, and photo/video acceptance testing are still needed |
| Resolution and frame rate negotiation | The service handles `S_FMT` / `S_PARM` on loopback and configures the sensor, hardware scaling, and cropping | After installing the Debian packages, unprivileged users capture 640×480, 1280×720, 1920×1080, and 1024×600 NV12 from both front and rear cameras via GStreamer; rear native 3840×2160 / 60 fps produces continuous frame sequences and switches back to 720p after stream-off |
| Auto-exposure, white balance, rear autofocus | The service samples NV12 frames to compute AE / AWB and sharpness | TFE980 statistics configuration, metadata DMA queuing, completion handling, and an IPA that consumes only hardware statistics are not yet implemented; see the [next section](#tfe980-public-documentation-and-metadata) for documentation gaps |
| Manual controls and per-frame parameters | Control socket writes standard sensor / PIX controls; CCM uses a private control | Low-level exposure, gain, white balance, and similar controls are retained; standard per-frame ISP parameters, effective-frame association, and libcamera control wiring are not yet implemented |
| Lens and flash photography | Lens and dual-flash ancillary links are verified; the service provides a forced-flash still-capture workflow | Standard lens / flash controls and associations are in place; hardware AF statistics, lens calibration, settling time after movement, and a libcamera flash workflow are not yet implemented |
| Rear full-resolution | RDI captures Quad CFA and converts it to NV12 using the existing software converter | TFE currently does not implement full-resolution remosaic; the hardware path uses binned modes first and does not add software fallbacks or upscale binned frames to impersonate full resolution |

libcamera reads the supported size range from the NV12 capture node, preserving native sensor modes first while also advertising common scaled sizes used by applications. Both scaling and cropping are performed by the TFE: the sensor maintains its native aspect ratio, and the image is downscaled before center cropping without stretching. Output dimensions, line stride, and buffer size follow the values read back from the capture node.

libcamera advertises `FrameDurationLimits` once the sensor mode is determined. Before the rear camera starts streaming, V4L2 vertical blanking is set and read back; the front camera uses the fixed frame interval reported by its driver. GStreamer selects a frame rate from the intersection of application requirements and camera capabilities, falling back to the mode default when unspecified.

After upgrading the Debian tablet from the signed APT repository, the system GStreamer plugin was verified on hardware to stream continuously at both 30 and 60 fps for rear 4K and 720p. Front 720p supports only 30 fps: offering both 60 and 30 selects 30, while requiring 60 fails cleanly and allows subsequent reopening. When Plasma Camera records 4K30, the rear camera captures at 30 fps directly instead of dropping frames from 60 fps. PipeWire does not yet forward application-requested frame rates to libcamera.

Therefore, `piano-camerad`, loopback, and the control socket cannot be removed yet—doing so would strip applications of processed video output, 3A, and the existing still-capture entry point. The WirePlumber configuration both hides the CAMSS nodes claimed by the service and disables the libcamera monitor so the two paths do not contend for the sensors; this configuration must remain until the standard replacement implements and verifies these features.

While the front camera starts streaming synchronously, libcamera's camera manager thread cannot service requests for an already streaming rear camera. When starting both cameras together, `cam` can complete stream-on for both cameras first before queuing the initial batch of requests. On the tablet, four buffers per stream are sufficient to deliver 120 consecutive rear frames and 60 consecutive front frames, with stream-off and restart also passing. Kernel frame sequence numbers show that the rear camera produced seven initial frames before the first requests were queued, with continuous sequence numbers thereafter. This startup order does not change the default buffer count, nor does it solve seamlessly opening a second camera while the first is already recording.

The video queues do not support Media Requests; controls are written individually and trigger register updates immediately. Sensor control latency and the exact frame on which TFE parameters take effect still need to be measured—a successful write cannot be assumed to apply to a specific frame.

The existing `frame_stats()` samples Y/UV from NV12 frames and converts those samples for AE/AWB; AF likewise reads image sharpness directly. Moving those functions into an IPA would not satisfy the goal of having the IPA process only hardware statistics. The statistics write master names present in the driver are not enough to infer computation block configuration, statistics record layout, or DMA buffer sizes; statistics DMA queuing cannot begin based on names alone.

Upstream [libcamera v0.8.0 simple pipeline](https://github.com/libcamera-org/libcamera/blob/v0.8.0/src/libcamera/pipeline/simple/simple.cpp) uses the software ISP for CAMSS, and even when selecting the hardware PIX path it lacks TFE 3A parameter and statistics handling; [upstream master](https://gitlab.freedesktop.org/camera/libcamera/-/tree/da6a6ed33ed0ab305f10b6f8b44736150d39cef4/src/libcamera/pipeline) likewise has no TFE pipeline or IPA. The [CAMSS pipeline v3 proposal](https://lists.libcamera.org/pipermail/libcamera-devel/2026-November/063337.html) adds per-camera CSID/VFE allocation, but its hardware ISP portion targets QRB2210 OPE rather than SM8750 TFE980. The [OPE implementation](https://lists.libcamera.org/pipermail/libcamera-devel/2026-November/063344.html) still uses `SwStatsCpu` to read raw images and reuses the softISP IPA, so it cannot serve as a zero-image-read TFE solution.

libcamera's [sensor driver requirements](https://github.com/libcamera-org/libcamera/blob/v0.8.0/Documentation/sensor_driver_requirements.rst) also require RAW sensors to expose `HBLANK`, `VBLANK`, and `PIXEL_RATE`, and to express exposure in image lines. OV32D40 currently does not fit this model, and previously disproven blanking values must not be reinserted just to pass initialization. Upstream also lacks gain helpers and measured control delays for both sensors; default delays cannot substitute for per-mode measurements.

The OV32D40 [Device Tree binding](../../../patches/linux/7.3/0115-media-ov32d40-binding.patch) uses the generic video-device and CSI-2 endpoint schemas to describe the existing 4-lane D-PHY, 19.2 MHz MCLK, three power supplies, active-low reset, and one optional power domain. The binding YAML and example pass `dt_binding_check`; after fixing legacy boolean properties and an invalid empty `reg`, the OV32D40 node in the complete merged DT passes targeted validation. The base Piano skeleton does not contain full camera nodes and cannot substitute for this check.

The SM8750 [CAMSS binding](../../../patches/linux/7.3/0116-media-camss-bayer-clock-binding.patch) also describes the Bayer, Bayer fast AHB, and Bayer CAMNOC clocks for each full TFE, totaling 44 clock resources; these are hardware clocks used by the PIX driver and must not be removed merely to match an older schema. On the capture side, only the `ahb` and `hf_mnoc` interconnects used by the driver are described, without placing standalone ICP resources inside CAMSS. The [bandwidth overlay](../../../linux/dts/piano-camera-interconnects.dtso) uses the existing SM8750 providers to add the active APPSS→CAMERA_CFG bandwidth path and the CAMNOC_HF→EBI image path; runtime resume submits the driver's bandwidth votes and suspend clears them. Both paths and all 44 clocks pass targeted schema checks on the complete merged node, though bandwidth voting still requires on-device verification. When properties are missing, the interconnect core returns NULL and skips constraints, which must not be used as an excuse to make hardware bandwidth paths optional.

Processing and on-chip bus clocks for a full TFE980 are selected based on D-PHY link frequency, active lane count, and input pixel bit depth; when link information is missing, the original pixel-rate estimate is retained, and other VFEs are unchanged. This avoids requiring sensors to impersonate array pixel rate using CSI throughput. On the tablet, single-stream rear 4K60 selected a 400 MHz bus clock (versus 300 MHz in the old driver), while TFE remained at 360.28 MHz. Concurrent front and rear capture also passed; this does not equate to long-duration audio/video recording acceptance testing.

Both front and rear sensors point their `power-domains` to `CAM_CC_TITAN_TOP_GDSC`. Public camera DTS files note that this reference ensures the CAMBIST MCLK branch operates rather than indicating that the sensor itself resides in an on-chip power domain; the I2C core attaches the domain before probe, and runtime PM manages it around sensor callbacks. The [sensor overlay](../../../linux/dts/piano-camera-sensors.dtso) also models the Titan dependency on the MCLK provider side: CAMBIST attaches Titan in addition to its existing MMCX and MX domains, and generic clock runtime PM holds and releases these domains on prepare/unprepare. The corresponding clock binding, example, and node validations pass, but standalone operation on the provider side still needs on-device verification; sensor references are kept until probe, stream-off, and resume are verified. The S5KJN1 schema still rejects this legacy reference while the OV32D40 schema permits it; the former is not relaxed to mask the incomplete migration.

### TFE980 Public Documentation and Metadata

Both MiCode's [Piano camera-kernel](https://github.com/MiCode/vendor_qcom_opensource_camera-kernel/blob/piano-w-oss/drivers/cam_isp/isp_hw_mgr/isp_hw/vfe_hw/vfe17x/cam_tfe980.h) and Qualcomm's official [camera-driver](https://github.com/qualcomm-linux/camera-driver/blob/camera-kernel.qclinux.0.0/camera/drivers/cam_isp/isp_hw_mgr/isp_hw/vfe_hw/vfe17x/cam_tfe980.h) publish `cam_tfe980.h` under GPL-2.0-only. `tfe980_bus_hw_info` describes the registers, formats, and completion groups for statistics write masters 10–18; `cam_vfe_bus_ver3.c` in the same source tree provides the frame-mode, stride, address, and context programming sequences, which can serve as the basis for implementing statistics DMA.

| TFE980 Output | Write Master | `cfg` / `image_addr` Offset | Transfer Format |
| --- | --- | --- | --- |
| AEC BE | 10 | `0x1700` / `0x1704` | PLAIN64 |
| AEC BHIST | 11 | `0x1800` / `0x1804` | PLAIN64 |
| Tintless BG | 12 | `0x1900` / `0x1904` | PLAIN64 |
| AWB BG | 13 | `0x1a00` / `0x1a04` | PLAIN64 |
| AWB BFW | 14 | `0x1b00` / `0x1b04` | PLAIN64 |
| AF BHIST | 15 | `0x1c00` / `0x1c04` | PLAIN64 |
| ALSC BG | 16 | `0x1d00` / `0x1d04` | PLAIN64 |
| Bayer RS | 17 | `0x1e00` / `0x1e04` | PLAIN32 |
| TMC BHIST | 18 | `0x1f00` / `0x1f04` | PLAIN64 |

These outputs belong to completion group 2, where `comp_done_mask[2] = 0x70` corresponds to three contexts; the MiCode table also lists an aggregated completion interrupt `mc_comp_done_mask[2] = BIT(25)`, which is absent from the Qualcomm version linked above. CAF for autofocus uses a separate write master 22 with `cfg` / `image_addr` at `0x2300` / `0x2304`, and completion group 4 with completion bit `0x100`. The two completion groups must be handled separately—SM8650's statistics interrupt handling cannot be copied verbatim.

Neither public kernel source tree contains complete configuration or output record definitions for the statistics computation blocks; IQ register commands are generated in userspace and submitted via `CAM_ISP_IQ_BL` and the Command Data Manager (CDM). `PLAIN64` only specifies transfer packing, not statistics semantics or allocation size. Frame-based mode on statistics write masters sets width and height to zero, and the public code does not cap writes using the allocated buffer size; the driver must bound the computation block output first to guarantee DMA cannot overrun the buffer.

#### Stock Statistics Layout and Sizes

The findings below come from static analysis of the CamX frontend, platform, core, CAF parser, and buffer-management libraries in `OS3.0.309.0.WPYCNXM`, solely for independent implementation of the hardware interface. Those libraries are neither executed nor packaged, nor are their algorithms or tuning imported. Stock statistics DMA captures and final allocation records from the same device have not yet been collected, so the sizes in the table should not be treated as verified across all hardware modes.

| Statistics Output | Stock Parsed Layout | Size Basis and Gaps |
| --- | --- | --- |
| BG / BE | 80 bytes per region, little-endian; R, Gr, Gb, B, and Y each have normal and saturated accumulation records. Each entry is 64 bits: `sum` in the low 34 bits, `count` in the high 16 bits | Stock AEC BE, AWB BG, Tintless BG, and ALSC BG share a 64×48 configuration cap (245,760 bytes for a full grid); Blob allocation capacity is 327,680 bytes. Sampling semantics, dual-TFE splitting, and on-device data verification are still missing |
| AWB BFW | Also 80 bytes of color statistics per region, including an ROI index, up to 16 regions | 16 full regions total 1,280 bytes; stock format 23 uses ten 64-bit words per row across 16 rows. This is not focus sharpness statistics |
| CAF normal grid | 16 bytes per filter: `sum` in bits 0–36 and `count` in bits 39–61 of the first 64-bit word; `sharpness` in the low 40 bits of the second word | Filter count and grid cap are determined by the same configuration mode (see table below); frontend-adjusted counts and live payloads still need on-device verification |
| BHIST | Stock parser reads little-endian `u32` entries and takes the low 25 bits; valid entry count comes from the per-frame configuration | All three engines use 1,024 entries (4,096 bytes) in single-channel mode; AEC also has a 4-channel R/G/B/Y mode with 512 entries per channel (8,192 bytes total). All three allocate 8,192-byte Blobs; sampling and binning semantics still need verification |

BG / BE configuration checks first reject zero region counts, grids exceeding 64×48, and ROIs outside the input bounds, then adjust the ROI and region counts for even per-region widths and heights of 16–512 pixels. The final region counts are written to registers using 6-bit minus-one encoding; while the bit width can represent 64×64, stock configuration does not permit that grid. The driver should separately bound configuration, compute payload length, and allocate buffers—maximum grid dimensions must not be reverse-inferred from allocation capacity.

BHIST channel selection, valid entry count, and initialization command count are also distinct concepts. In the stock constructor arguments `(2048, 4)` / `(1024, 1)`, the second argument sizes the space needed for DMI (on-chip statistics memory access) initialization commands, not the number of output channels. Single-channel mode selects R, B, G, or Y; only AEC accepts 4-channel mode, which is parsed into four R, G, B, Y segments for the algorithm. AF and TMC reject 4-channel mode. ROIs align to 2×2 cells, and additional configurations exist for multi-region weights, green-channel sampling selection, and non-uniform binning; because these behaviors are not fully verified, a 1,024-entry output cannot be directly interpreted as the Bayer-quad luma counts expected by public interfaces.

Stock CAF hardware capabilities pass through MCTFE metadata to AF, which then selects the format tag 2 parser. The tag identifies only the record version and cannot substitute for the applied filter mode. The normal-grid configuration function and parser share the same region counts:

| Normal Grid Mode | Filter Order | Bytes per Region | Max Grid | Max Payload |
| --- | --- | --- | --- | --- |
| 6-filter | H11, V1, H12, V2, H21, H22 | 96 | 32×24 | 73,728 bytes |
| 2-filter | H11, V1 | 32 | 48×48 | 73,728 bytes |

Stock TFE980 capability descriptors select HAF configuration and the format tag 2 parsing path. This path uses the normal grids above and does not read `windowType` from the separate BAF configuration path. The fixed / floating / flexible branches in the library operate on differently laid out BAF objects; their 48×48 fields cannot be combined with HAF's 96-byte 6-filter records, nor can they be taken as proof that extra data is appended to the end of the same CAF buffer. The driver must still independently bound the enabled HAF ROI type, final grid, and output length; 73,728 bytes applies only to the normal grid modes in the table. Color accumulations from BG / BFW cannot replace CAF sharpness.

The normal-grid filter mode passes from AF configuration into the frontend and is ultimately written to bit 5 of HAF top register `0x9370`; the two region counts are converted and written to bits 16–21 of `0x9650` / `0x9658` using minus-one encoding. Because the frontend also adjusts the grid for cropping, subsampling, and minimum region size, metadata must carry the counts and mode actually applied for that frame rather than returning only the requested values. ROI type and filter mode are separate fields; validation branches for floating / flexible ROIs cannot serve as proof of total payload length for 6-filter mode.

In stock SM8750's static `UsecaseZSL` graph, the BG, BHIST, and CAF links use Blob format 6; MCTFE's `FinalizeBufferDimensions()` overrides initial sizes using hardware capabilities. Normal Blobs compute `width × height` bytes and round up to 4,096 bytes; color space `0x1000` takes a separate 16× branch. Internal non-HAL ports initialize with color space `0x101`, and the default runtime-negotiated value maps to `0x201`, neither of which takes that branch.

Format initialization clears explicit total length and plane-size override counts, then `GetTotalSize()` computes the single-frame size. The buffer manager retains this format; non-shared pool allocations multiply that size by a separate batch frame count and pass it to `CSLAlloc()`. Under this normal Blob path, single-frame allocations for BG / BE, BHIST, and CAF are therefore 327,680, 8,192, and 73,728 bytes respectively, all already page-aligned. The initial `0x4000` / `0xc00` values in the link definitions are not final allocation sizes; shared pools, actual batch frame counts, and runtime graphs still need to be captured from stock. Computation block output caps and statistics effective frames also require independent verification—DMA cannot be enabled merely because an allocation appears large enough.

Linux 7.3's `v4l2-isp` includes generic buffer/block headers and statistics wrappers, but the header `type` is explicitly driver-defined and there is no generic Bayer grid, BHIST, or BF payload. [Mainline Linux](https://github.com/torvalds/linux/tree/aacb7797504183a90d66b19b1efb555cfe9b3eec/drivers/media/platform/qcom/camss) and [media next](https://git.linuxtv.org/media.git/tree/drivers/media/platform/qcom/camss?id=bf6c63ecc20a54aaac7a2fcbd6adaf6498388825) still lack a TFE980 statistics backend or payload definitions. The existing [GENERIC metadata format](https://docs.kernel.org/userspace-api/media/v4l/metafmt-generic.html) describes storage of CSI-2 line data rather than statistics semantics; stuffing custom TFE data into it would still create a project-private ABI. Merged formats from other ISPs likewise cannot be used to masquerade as TFE data.

Qualcomm's [IFE BG statistics proposal](https://lore.kernel.org/linux-media/20261006223953.3382273-21-gjorgji.rosikopulos@oss.qualcomm.com/) uses a 64×48 grid with four-color `u32` accumulators and is unsuitable for directly carrying TFE980's five-channel 34-bit sums; data must not be truncated to fit that structure. The [CAMSS extensible BHIST proposal](https://lore.kernel.org/linux-media/20261005-linux-mainline-7-2-sm8650-hdk-rfc-0-v1-58-ed985aefd611@kernel.org/) defines 1,024-bin Bayer-quad luma statistics, and matching bin count alone does not prove identical channel and sampling semantics on TFE. Neither public interface has been merged yet; adopting or extending them requires alignment and review with upstream design rather than allocating project-local blocks or fourccs and claiming standard interface compliance.

An independent implementation still needs complete computation registers, permitted configurations, maximum outputs, and failure-stop procedures for AEC BE/BHIST, AWB BG, and CAF. Register commands, buffer sizes/strides, statistics completion order, and statistics samples from matching stock modes are needed for cross-checking; ordinary Linux control enumeration and idle register readbacks cannot supply those definitions. Qualcomm's [libcamx-dev packaging notice](https://github.com/qualcomm-linux/pkg-camx-dev/blob/ceee1ff870a96da9ea4fc2c7ba46b952c6e034da/debian/copyright) explicitly separates BSD packaging scripts from proprietary CamX payloads—public availability for download does not grant a license to import implementations or tuning.

### Media Graph and Concurrency

Currently, the production setup routes the rear camera through `s5kjn1 → msm_csiphy1 → msm_csid0 → msm_vfe0_pix → msm_vfe0_video3` and the front camera through `ov32d40 → msm_csiphy4 → msm_csid1 → msm_vfe1_pix → msm_vfe1_video3`. CSID sink is pad 0 and IPP is source pad 4; VFE sink and source are pads 0 and 1 respectively. Device node numbering depends on probe order and is not a configuration constant. Raw capture uses CSID source pad 1 and the corresponding VFE RDI0.

`MEDIA_IOC_G_TOPOLOGY` on the previous production kernel confirmed three enabled, immutable ancillary links from the rear camera to the lens and two flash channels, matching `lens-focus` and `flash-leds` in the runtime Device Tree. After the GT9764BA driver took over the lens, this association is preserved, though stream-on, focusing, and parking after close still need on-device verification.

The service discovers `MEDIA_ENT_F_LENS` by following enabled ancillary links from the sensor rather than matching by lens driver name. The front camera has no such link and remains fixed-focus; if an ancillary link points to a missing device or a single sensor links to multiple lenses, the service logs an error and disables autofocus for that camera without guessing a device or affecting the other camera.

The stock `OS3.0.309.0.WPYCNXM` rear camera module descriptor and actuator library identify the motor as GT9764BA, initialized sequentially with `0x02 = 0`, `0x02 = 2`, `0x06 = 0x40`, and `0x07 = 0x73`. The [vendor product table](https://www.giantec-semi.com/yqmd/164) lists GT9764BA as bidirectional and GT9769 as unidirectional; the two cannot be treated as fully compatible simply because their position registers match. The [GT9764BA driver](../../../patches/linux/7.3/0126-media-gt9764ba-lens.patch) and [binding](../../../patches/linux/7.3/0125-media-gt9764ba-binding.patch) provide a standard V4L2 lens entity, `V4L2_CID_FOCUS_ABSOLUTE`, and runtime PM, and the Device Tree no longer uses `giantec,gt9769` to match the DW9768 driver.

GT9764BA uses a 0–1023 DAC range, with a default value and power-down parking target of 512. [MiCode's GPL GT9764 driver](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/a369d30bd5df3fcfb4304ee9e7ae10fa4f11899a/drivers/misc/mediatek/lens/vcm/v4l2/gt9764/gt9764.c) uses the same midpoint. Parking steps move at most 50 counts per step with a 10 ms wait; this timing comes from the stock `GetParkLens` callback and its I²C command construction call chain. If a parking write fails during runtime suspend, power is kept enabled so it can be retried. On unbind, the driver still releases its regulator enable reference even if parking fails. The driver tracks the last successfully written position and restores the user setting using the same step size after power resumes. The 10 ms delay is a parking step wait, not a measured optical settling time; post-move settling time and focus calibration are not yet implemented. The stock value `0x73` exceeds the 6-bit timing range of the DW9768 binding; the driver uses the stock GT9764BA register value directly rather than applying the DW9768 formula.

SM8750 exposes only same-index CSID→VFE links, matching Gen4 register updates, frame timing, and completion callbacks; CSIPHY selection and graphs for other SoCs remain unchanged. All four CSID source pads share format normalization, and IPP pad 4 no longer accepts arbitrary media bus codes or dimensions. Concurrent front/rear operation, staggered stream-off, mode switching, and failure recovery still need on-device verification; verifying reference counting on the shared privacy LED does not prove that the two image, statistics, and control streams do not interfere with each other.

On the previous 7.3.3 production kernel, both PIX nodes passed 48 non-streaming `v4l2-compliance 1.30.1` checks each with 0 warnings, and Requests reported `Not Supported`. This reflects only ioctl behavior on that baseline and does not cover post-format-fix streaming, 3A, flash synchronization, or libcamera.

### Tuning Sources and Full-Resolution Mode

The MIT license on the camera source code does not automatically cover external tuning data. Comments on `cam_cfgs[].ccm` note that it is derived from the mean of stock tuning matrices, without an independent source or redistribution license record; CamX origins of TFE register programming also need per-item verification of public source and licensing. New libcamera tuning must use clearly licensed public data or newly measured data—OEM tuning or proprietary Chromatix cannot be imported, nor can existing constants simply be repackaged under a YAML filename.

Rear 8160×6120 outputs Quad CFA, while the current TFE configuration selects only standard 2×2 Bayer order and standard demosaic with BDS2 disabled, without implementing full-resolution remosaic. While the source shows that the current path does not support this mode, that neither proves the silicon permanently lacks the capability nor justifies claiming a hardware width limit solely from the 7008-pixel value in comments. The hardware path should start with binned modes (rear 4080×3060, front 3264×2448); full-resolution support requires separate public hardware documentation and verification, without adding software debayer, scaling, or color conversion fallbacks, and without upscaling binned images and labeling them as full resolution. The legacy service's software full-resolution path is retained until a working replacement exists, and is not counted as TFE full-resolution support.

## Resolutions and Frame Rates

Default output is 1920×1440 at 30 fps. Applications select dimensions and frame rates using standard `VIDIOC_S_FMT` and `VIDIOC_S_PARM` without changing service configuration.

| Camera | Sensor Mode | Nominal Sensor Frame Rate |
| --- | --- | --- |
| Rear S5KJN1 | 4080×3060, 4080×2296 | 30 fps |
| Rear S5KJN1 | 3840×2160 | 60 fps |
| Rear S5KJN1 | 8160×6120 | 10 fps |
| Front OV32D40 | 3264×2448, 3264×1836 | 30 fps |
| Front OV32D40 | 6528×4896 | 10 fps |

The rear camera rejects changing the active sensor mode while streaming, while still allowing `TRY_FMT` or re-setting the current mode. Switching modes while stopped updates only control ranges; frame length and exposure are applied after the new mode registers are written. After stopping and switching from 4080×3060 to 3840×2160 on the tablet, 180 consecutive frames were captured at 59.98 fps measured by CSID timestamps; this does not include encoding, audio recording, or full-resolution still capture.

Rear 8160×6120 preserves full-resolution Quad CFA samples and outputs NV12 through RDI capture, QPPG conversion, and software color processing. The sensor's 10 fps rate does not equal software output throughput: the tablet currently produces one full-resolution image roughly every 2.4 seconds (~0.4 fps). Working memory is approximately 953 MiB; while the conversion thread is busy, incoming frames are dropped instead of queuing stale frames. The new rear modes use stock static ODM register tables from the same device, with provenance recorded in the kernel patches. The 19.2 MHz input clock and 857.6 MHz CSI link are configured by the shared Device Tree, while the legacy 24 MHz sensor tables remain for Device Trees using that clock.

Output dimensions may be smaller than the sensor mode. Width aligns to 16 pixels and height to 2 pixels; in normal mode, the ISP scales proportionally first and then center-crops mismatched aspect ratios without stretching. Rear full-resolution completes Quad CFA conversion first, then center-crops and scales via libyuv; smaller sensor modes are never upscaled to impersonate full resolution. Very small outputs hit the ISP's 16× maximum downscale limit and are cropped further in field of view. 60 fps is advertised only for rear outputs up to 3840×2160, and full-resolution modes on both front and rear cameras advertise only 10 fps.

All three front-camera register tables match the stock static configuration from the same device, and the driver reports nominal 10 / 30 fps via V4L2 pad frame intervals. On the tablet, 40 frames per mode were measured using CSID hardware timestamps: both 3264-wide modes had a frame period of 33.3324 ms, and 6528×4896 had 99.9992 ms. Testing used 1920×1440 PIX output to verify actual sensor pacing and did not evaluate full-resolution photo output or image quality.

Upstream `v4l2loopback` exposes only a single format to readers once the producer sets it. The shared [v4l2loopback patches](../../../patches/v4l2loopback/0.15.4/series.json) and [camera source](../../../linux/userspace/camera/piano-camerad.c) add producer negotiation on the same device:

1. Before writing the first frame, the service sequentially sets each mode's maximum dimensions and frame interval and subscribes to `SOURCE_CHANGE`, registering the combinations supported by the producer.
2. Applications use standard frame-size/frame-interval enumeration and `S_FMT` / `S_PARM`. The driver notifies the service via `SOURCE_CHANGE` and waits for producer confirmation.
3. The service stops CAMSS, stops its OUTPUT queue, releases old mappings and buffers, accepts the requested dimensions and frame rate, rebuilds the MMAP queue, and publishes a complete black frame via `QBUF`. Once dimensions, frame interval, and a complete frame all match, the driver completes application negotiation; after the application calls `STREAMON`, it still waits for a fresh sensor frame and cannot read the black negotiation frame.
4. Existing reader usage-count events still drive actual stream-on and stream-off; dimensions returned by the raw sensor propagate through PHY, CSID, and ISP configuration.

This is a project extension to loopback, not an upstream dynamic-sizing feature. Unsubscribed producers retain original behavior. Mode switching is blocked while a reader still holds mappings, is actively streaming, or another reader owns the format. The producer's own output mappings may enter negotiation, but all of them must be released before reallocation proceeds; if stopping fails, capture buffers and the process are kept alive until the application times out, avoiding unmapping memory that may still be in use by DMA. No extra virtual cameras, format-selection buttons, or auxiliary services were added.

Rear full-resolution RAW10 uses a black level of 64 from the stock static description and a white level of 1023. Conversion first computes `(sample−64)/(1023−64)`, then applies the existing digital gain, white balance, color matrix, and tone curve. This uses a static black level without dynamic dark-level compensation or lens shading correction. Normal PIX mode continues to use the kernel TFE black-level configuration.

## Manual Controls

`piano-camera` installs the camera service and `piano-camera-ctl`. The client reuses the service's active hardware connection; when not streaming, it returns `ENODEV` instead of silently starting the camera. The camera package depends on the device configuration and the kernel metapackage carrying the matching `v4l2loopback`, so kernel upgrades do not require replacing the camera binary.

Production images have the camera service enabled by default. When installing manually, Debian enables the service via packaging; Arch-based systems use `systemctl enable --now piano-camerad.service`. When migrating from older images with manually copied files, follow the [device configuration file ownership checks](../../../linux/bsp/README.md) and do not move `/etc/piano/camera.conf`.

```sh
piano-camera-ctl caps rear
piano-camera-ctl get rear
piano-camera-ctl set rear ae manual
piano-camera-ctl set rear exposure-time-ns 500000
piano-camera-ctl set rear ae auto
```

`rear` can be replaced with `front`, though the front camera has no focus actuator. `caps` returns the minimum, maximum, step, default, and flags for the active mode; applications should re-query after switching modes.

| Control | Unit | Hardware Interface |
| --- | --- | --- |
| exposure | Native integration count (lines on the rear camera) | `V4L2_CID_EXPOSURE` |
| exposure-time-ns | Nanoseconds, rounded to a valid exposure line count | Same sensor exposure control |
| analog-gain | Driver-native integer | `V4L2_CID_ANALOGUE_GAIN` |
| digital-gain | Q10 (`1024` = 1×) | TFE `V4L2_CID_DIGITAL_GAIN` |
| red-balance / blue-balance | Q10 | TFE red / blue balance controls |
| focus | Driver-native DAC value | Rear `V4L2_CID_FOCUS_ABSOLUTE` |

AE, AWB, and AF maintain independent auto/manual states. Writing a numeric value requires switching the corresponding domain to `manual` first so auto algorithms do not overwrite it; switching back to `auto` resumes from the current control state. Controls are written one at a time and read back, preserving `errno` on failure; multiple controls do not form a hardware atomic transaction. If a reply is lost, run `get` before deciding whether to retry.

Rear exposure line time is derived from the driver's VT `PIXEL_RATE`, image width, and `HBLANK`, while frame length is set via `VBLANK`. The stock 19.2 MHz table's VT clock is `19200000 × 131 × 4 / (3 × 6)` Hz and is handled separately from the CSI transfer rate; the 857.6 MHz link throughput is not treated as the integration clock.

The front camera retains native integration counts. Stock code writes directly to `0x3500..0x3502` without scaling exposure counts by resolution; the stock FLL for 6528×4896 is 2551 and cannot be treated as 4896 image lines for subtraction. The driver removes inaccurate `HBLANK`/`VBLANK` controls, fixes FLL in the mode tables, and caps exposure at `FLL − 12` so stale blanking values cannot overwrite new mode timing. The CSI-derived `PIXEL_RATE` is retained solely for existing CAMSS clock voting and is not used for integration-time conversion.

The front-camera time interface divides the stock nominal frame interval by the native FLL, matching CamX's integration-time relationship. For 3264×2448 and 3264×1836, one count period is `1e9 / 84600` ns; requesting 500000 ns rounds to the nearest valid 42 periods (~496454 ns). For full resolution, one period is `1e9 / 25510` ns, where the same request rounds to 13 periods (~509604 ns).

A [public driver](https://github.com/HelloVolla/android_kernel_modules_volla_mt6878/blob/bc3a2f54403e608257b3f0780002c8355578ce2b/mtkcam/imgsensor/src-v4l2/common/ov32d40_mipi_raw/ov32d40mipiraw_Sensor.c#L139-L159) for the same sensor uses the same native HTS / FLL at 100 MHz. The resulting 11.82 / 39.20 µs periods match the frame periods measured on Piano, though input clocks and PLL configurations from other platforms cannot be copied blindly. Nor does CSI transmission duration prove how many image lines a native count corresponds to. Uncalibrated conversions to ISO, Kelvin, physical focal length, or focus distance are not exposed.

The control socket is `/run/piano-camerad/control.sock` with `root:video 0660` permissions. It introduces no additional daemon and does not replace application V4L2 format negotiation.

## Frame Timestamps and Errors

PipeWire's V4L2 capture uses MMAP copies for devices that cannot export DMA-BUF. Copied SPA buffers are managed separately from kernel capture indices: frames still queued, handed to an application, or carrying a non-zero busy reference cannot be overwritten, while kernel buffers are returned immediately after copying. When an application temporarily holds all buffers, the camera clock keeps advancing so the processing loop can return frames without altering sensor timestamps. See the [shared PipeWire patch](../../../patches/pipewire/1.6.9/0007-v4l2-copy-buffer-pool.patch) for the fix.

The Debian build folds this fix into the existing `libspa-0.2-modules` package. `./build.sh pipewire` builds the daemon, libraries, and plugins together from the same Debian source version. Nyarch and SteamOS keep their target distributions' original daemon, audio, and Bluetooth libraries while rebuilding the GStreamer plugin and `pipewire-v4l2-source`. The standalone capture package installs the SPA library and standard `context.spa-libs` configuration using a dedicated source factory name, because PipeWire selects the library from the first matching rule and existing `api.v4l2.*` rules are not overridden by later rules of the same name. WirePlumber enumerates devices from that library and hands the dedicated factory to PipeWire so both processes use the same fix. Debian, Nyarch, and SteamOS share the same source and patches. Front and rear video recording with audio has been verified for start, stop, reopen, and complete decoding with the production Debian packages.

Plasma Camera on Debian reads the physical cameras directly through libcamera. Recording switches to a `VideoRecording` stream, preferring the target size and allowing lower tiers to downscale from a larger input of the same aspect ratio. Each completed NV12 frame is submitted to Qt with its capture timestamp, copying frame data only once into a Qt buffer; stopping recording restores the original preview size. Frames are never repeated via timers, nor are small resolutions upscaled to 4K. On hardware with the legacy service and virtual cameras disabled, front and rear photo capture, camera switching, and close/reopen passed. Paired with the experimental Qt encoding backend, the front camera completed 1080p recording with audio, and the rear camera recorded a short clip of 184 consecutive 4K frames (~60 fps) that decoded cleanly; encoder profiles/levels, long-term A/V sync, and native Qt packaging require separate acceptance testing.

FFmpeg 8.1.1 and 9.0.2 share the [capture payload bounds](../../../patches/ffmpeg/9.0.2/0008-v4l2m2m-bound-capture-payload.patch) and [encoder buffer reservation](../../../patches/ffmpeg/9.0.2/0009-v4l2m2m-reserve-capture-buffers.patch) fixes. When the muxer holds encoded packets while waiting for another track, FFmpeg copies the compressed bitstream and immediately returns scarce CAPTURE buffers without changing default buffer counts. Recording for 8 seconds on the tablet with the real microphone using the minimally patched build delivered all 433 frames fed to Iris, all of which wrote and decoded cleanly with ~8 seconds of audio and video and no encoder buffer exhaustion. Existing applications routing through loopback still drop frames; enlarging the shared queue cannot guarantee that frames held by an application are not reused by the writer. Bypassing loopback to connect CAMSS PIX directly to Iris on the same device produced a 60-second MP4 decoding to 3,600 H.264 frames, with a maximum video frame interval of 16.673 ms and a complete 60-second microphone track. These results verify the hardware capture and encoding pipeline; regular application integration, A/V sync offset against a single audiovisual event, and still-image quality still require separate measurement.

IPP and RDI0 on full SM8750 CAMSS use physical counters latched by CSID980 and report frame timing through standard V4L2 interfaces.

| Interface | Timestamp Meaning | Sequence Number for the Same Frame |
| --- | --- | --- |
| `V4L2_EVENT_FRAME_SYNC` | `timestamp` is CSI SOF in `CLOCK_MONOTONIC` | `u.frame_sync.frame_sequence` |
| `VIDIOC_DQBUF` | `timestamp` is CSI EOF with `TIMESTAMP_MONOTONIC` and `TSTAMP_SRC_EOF` flags | `sequence` |

Applications pair events and buffers using the sequence numbers in the table; `event.sequence` is the event queue sequence number and cannot be used to match frames. DMA completion, userspace dequeue, and ISP conversion may all occur after EOF. Because `DQBUF`'s `timeval` truncates nanoseconds to microseconds, conservatively bounding EOF takes the upper bound of that microsecond interval—convert to nanoseconds and add 999.

Inside the interrupt handler, the driver reads the physical counter, `ktime_get_ns()`, and the physical counter again, converting the latched value using the midpoint of the two counter reads. The driver requires ARM64 `CNTFRQ_EL0` to be 19.2 MHz and refuses conversion otherwise; it also rejects conversion if the sampling span exceeds 50 µs or the latched value is more than one second older than the sample. Provided the total instantaneous rate difference between `CLOCK_MONOTONIC` and the nominal counter rate stays within ±500 ppm, the conservative upper error bound is 550 µs.

CSI SOF marks the start of the frame packet, whereas integration may begin earlier. The kernel neither subtracts exposure time nor sets `TSTAMP_SRC_SOE`; still capture must still account for the sensor integration model. Likewise, the actual span from SOF to EOF cannot simply be replaced by active line count multiplied by line time.

libcamera's `SensorTimestamp` is defined as the start of exposure of the first row in `CLOCK_BOOTTIME`. Neither CSI SOF nor EOF here can be placed directly into that field: sensor integration timing relative to CSI and a `MONOTONIC`→`BOOTTIME` conversion are still needed, and must be re-verified after resume from suspend. The existing simple pipeline's direct reuse of capture timestamps does not mean TFE timestamp semantics have been adapted.

When `SensorTimestamp` is absent, `libcamerasrc` uses the completed buffer's timestamp to avoid producing invalid timestamps when subtracting the pipeline start time. Rear 3840×2160 recorded to MP4 with audio via GStreamer and Iris, keeping a compressed packet queue between the encoder and muxer so the muxer does not hold camera buffers while waiting for the audio track. The tablet captured 3,600 consecutive frames, all 3,600 recorded frames decoded cleanly, the maximum video frame interval was 16.833 ms, and the built-in microphone track ran ~60 seconds. Long-duration 4K60 recording in camera apps and sync offset against a single audiovisual event still require verification.

SOF/EOF pairs are associated through the existing double-buffered FIFO. Missing markers, discontinuities, receiver errors, or invalid timestamp conversions cause the stream to keep returning `V4L2_BUF_FLAG_ERROR` until streaming is restarted. TFE image-size, CCIF, and constraint errors are delivered via BUS interrupts; error handling and buffer completion share `output_lock`, checking errors before publishing frames. Only active write masters may access BUS registers, and when stopping the last stream, interrupt configuration is masked, cleared, and read back before disabling clocks.

See the [physical frame timing patch](../../../patches/linux/7.3/0048-camss-frame-timestamps.patch) and [TFE BUS fixes](../../../patches/linux/7.3/0049-camss-tfe-bus-errors.patch) for implementation details. The `camss_frame` and `camss_frame_error` tracepoints report raw counters, cross-samples, and buffer indices.

The production kernel does not provide ISP register scripts or arbitrary MMIO read/write entry points. Default TFE settings, scaling, and image controls are programmed by the driver; diagnostics rely on the existing frame tracepoints and error logs. The [removal patch](../../../patches/linux/7.3/0113-camss-remove-register-debug.patch) drops `camss_isp` debugfs and its shared CSID/VFE pointers so the two cameras cannot overwrite each other's debug targets, without altering stream-on, stream-off, or DMA cleanup.

## Flash

The PMIC flash uses standard V4L2 flash controls; the rear-camera Device Tree references both channels via `flash-leds`, and `MEDIA_LNK_FL_ANCILLARY_LINK` connections to both flash subdevs are confirmed. The existing service discovers the flash LEDs through these links and accepts a forced-flash still-capture request over the control socket:

```sh
piano-camera-ctl capture rear --flash --output photo.jpg
```

The service freezes current exposure, white balance, and focus, waits for two sensor frames that genuinely start after the freeze, and then fires the flash with a hardware timeout. Physical `FRAME_SYNC` events and capture buffers with matching sequence numbers supply SOF and EOF; only a frame whose entire exposure window falls within the conservative flash-on window is delivered. Cancellation, timeout, format change, or sensor error turns off the flash, restores the previous control state, and returns no other frame.

Photo dimensions follow the currently negotiated PIX output and do not automatically switch to native full resolution. Flash requests on full-resolution RDI or the front camera return `EOPNOTSUPP`. This path provides long-pulse exposure coverage rather than automatic pre-flash metering or short-pulse motion freeze, and ordinary video applications do not automatically trigger this capture request. The caller receives a read-only sealed image file descriptor and encodes the JPEG itself; the service never accepts a user output path.

libcamera v0.8.0's generic sensor class recognizes only associated lenses and includes no TFE flash photography workflow. Future integration must preserve hardware timeout, fault readback, cancellation, and full-exposure-coverage guarantees, completing standard kernel interface and libcamera control design first instead of using the project socket to impersonate libcamera support. Having source code for the existing flash workflow does not mean optical illumination-to-exposure synchronization has been measured.

See the [implementation notes](camera-privacy-led.md) for the privacy indicator LED driver and PWM coupling; it serves a different purpose from the camera flash.
