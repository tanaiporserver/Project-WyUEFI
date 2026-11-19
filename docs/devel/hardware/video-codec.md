# Piano Video Codec

Video encoding and decoding use Linux's Qualcomm Iris driver and the standard V4L2 interface. FFmpeg patches live in [`linux/bsp/patches/ffmpeg/`](../../../linux/bsp/patches/ffmpeg/), and all four distro builds install full patched packages.

## Packages and Builds

| Distro | FFmpeg | Build Entry Point |
| --- | --- | --- |
| Debian sid | 9.0.2 (Debian archive version `7:9.0.2-6`), keeping Debian packaging configuration | `tools/build_ffmpeg_packages.py` |
| SteamOS | 8.1.1, keeping Valve 8.x library ABI and build options | `tools/build_arch_ffmpeg.py` |
| Nyarch | 9.0.2, keeping current Arch library ABI | `tools/build_arch_ffmpeg.py` |

The Arch builder reads build options, dependencies, and version metadata from the target system's installed package, retains the full codec feature set, and rebuilds pacman packages. It neither downgrades FFmpeg 9 to 7 nor overwrites system packages with stripped debug binaries. Package and source provenance are recorded in `packages/ffmpeg/SOURCE.json` under the corresponding distro build directory.

Debian packages are built in a sid ARM64 environment; on other hosts, supply a rootfs matching the target ABI, which the builder copies into an isolated environment:

```sh
sudo ./build.sh ffmpeg --sysroot build/distros/debian-sid/rootfs --output build/ffmpeg --jobs 8
```

The builder verifies signed sid `Sources`, `.dsc`, and all archive hashes, uses the unmodified Debian `debian/rules`, and builds both the standard and extra codec variants. The runtime package set includes the standalone `ffplay` and the seven FFmpeg 9 library packages, omitting the removed `libpostproc`. Debian's orig tarball is identical to the official FFmpeg 9 tarball used by Nyarch and reuses `patches/ffmpeg/9.0.2/series.json` rather than duplicating a Debian-specific patch set. The Debian packaging version is recorded separately in `debian.json` in the same directory.

To prepare the shared source tree standalone:

```sh
python tools/prepare_ffmpeg_source.py \
  --series patches/ffmpeg/9.0.2/series.json \
  --output build/ffmpeg-source-9
```

Source archives and patches are pinned by SHA256, fuzzy patch application is disabled, and `upstream/` is never modified. FFmpeg 8 and 9 use the official 8.1.1 and 9.0.2 archives respectively. Upstream 9.0.2 and Debian's patch series do not yet include these V4L2 fixes, so the standard distro packages continue to be rebuilt.

The SteamOS and Nyarch package caches check version, patch series, and binary package hashes together. Modifying the patch series triggers a full rebuild; reusing the cache reinstalls the cached package rather than leaving a version overwritten by the base repositories.

Debian's `SOURCE.json` binds the signed sid archive identity, target installed runtime library ABI, original `debian/rules` and `debian/control`, patches, and binary hashes together. Any source or ABI change invalidates cached packages; `--resume` in the same directory verifies these identities before reusing a complete `.changes` build or resuming incomplete targets, without re-downloading hash-verified archives or reconfiguring already-complete packages.

## Qt Recording

On Debian, when Qt Multimedia records 8-bit H.264 into MP4 or MOV, it tries Qt's existing hardware encoders first and then falls back to V4L2. If the encoder device cannot be opened, it falls back to the original software encoder. Qt selects the H.264 level from resolution, frame rate, and bitrate—for example, requesting Level 5.2 for 3840×2160 at 60 fps. The project's FFmpeg packages pass this level through to the V4L2 driver.

The Qt changes are delivered as native Debian packages, requiring no custom plugin paths for applications. The APT repository hosts matching ARM64 runtime, development, and example packages for Qt Multimedia; version-independent documentation continues to install from official Debian repositories.

With these Qt packages and the signed-source FFmpeg packages installed on the tablet, the system's Plasma Camera records directly from the real rear camera and built-in microphone, without running legacy camera services or loading a virtual camera. When the camera captures at the selected profile, Plasma Camera preserves actual capture timestamps instead of resampling frames on a separate clock. A 65-second rear-camera recording produces an MP4 decoding to 3921 frames at 3840×2160, encoded as H.264 Level 5.2 at ~21.8 Mbit/s, with a maximum frame interval of ~16.67 ms. The front camera also records 1920×1080 at 30 fps with audio through Iris at Level 4.0. Audio and video track end times can differ by ~0.4 s; A/V sync must still be verified against a shared audiovisual event rather than treated as passing simply because both tracks decode cleanly.

## Decode Flow Fixes

Per the [Linux stateful decoder documentation](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-decoder.html), compressed data is submitted to the OUTPUT queue first, and the CAPTURE queue is configured only after the initial `SOURCE_CHANGE` event arrives. The CAPTURE queue delivers the decoded frames.

When resolution changes mid-stream, all old frames and the final buffer marked `LAST` must be drained before decoding resumes. Existing buffers are reused if their capacity is sufficient; otherwise, only the CAPTURE queue is reallocated while unprocessed data in OUTPUT is preserved. An empty `LAST` buffer marks a transition boundary and must not emit a frame; a `LAST` buffer carrying valid payload must still deliver the final frame.

End-of-file and resolution changes are handled separately. `STOP` is issued only when both queues are streaming, and an EOS notification does not discard unretrieved frames. A non-blocking `DQEVENT` returning `ENOENT` indicates an empty event queue rather than a decode error; other I/O errors are still propagated to the caller.

## Verified Cases and Remaining Issues

Both the FFmpeg 7 patch set and the full Nyarch FFmpeg 9 package have completed 12-frame decode and 12-frame encode of a 640×480 H.264 sample on Iris hardware. This result does not imply that all codecs, resolutions, and players work.

A continuous 640×480 → 1280×720 stream exposed two issues:

1. **Stale frame references blocking reallocation.** Players may retain the previous frame while waiting for the next; stock FFmpeg waited for all old references to be released before allocating new buffers. The default patch uses the driver's advertised `V4L2_BUF_CAP_SUPPORTS_ORPHANED_BUFS` capability to keep old mappings alive until the last reference drops while allocating a new generation of buffers, avoiding premature freeing of memory still used by the player.
2. **Duplicate end markers from Iris.** The driver flagged a normal empty `PSC_LAST` as `ERROR` and skipped the `LAST` state update when handling `ERROR`. The new queue then received another `LAST`, terminating playback early. The kernel patch fixes empty `PSC_LAST` detection and ensures a final frame carrying an error still properly closes out the old queue.

Both the FFmpeg buffer patch and the Iris `PSC_LAST` fix are included in default builds; the kernel changes live in [`0004-media.patch`](../../../patches/linux/7.3/0004-media.patch). Both FFmpeg 7 and 9 completed continuous 640×480 → 1280×720 → 640×480 transitions on hardware with all 36 frames decoded and zero decode errors, and both versions completed 12 frames of H.264 hardware encoding.

SteamOS's full FFmpeg 8.1.1 package reuses the same NV12 row-copy, sequence-header merge, and unstarted-encoder teardown fixes. Input frames only supply the visible height; the driver's aligned height applies solely to destination buffer layout and must not be used to read past the source image. On hardware, 30 frames of 1280×720 H.264 encode and all 30 frames of hardware decode passed. Frame-count checks on raw bitstreams use `-fps_mode passthrough` so output sync logic does not drop frames with identical timestamps.

The same kernel patch waits for interrupt threads to finish before acquiring the core lock, fixing a driver-teardown deadlock observed on hardware. Module unload, reload, and normal reboot have been verified on the device.

Iris V4L2 frame-rate negotiation syncs CAPTURE when OUTPUT is set, while setting CAPTURE alone does not back-propagate to OUTPUT. Encoder frame-rate changes that cannot be applied while streaming return `EBUSY` and preserve the previous value. Negotiated values, out-of-range rejection, and reconfiguration after stream stop have been verified on the tablet.

3840×2160 frames from the rear camera can be encoded to H.264 by Iris and muxed directly into MP4 alongside AAC audio from the built-in microphone. An 8-second test recording decoded cleanly to 423 frames (~53 fps average); the bitstream reported H.264 High Level 5.0. 3840×2160@60 requires at least H.264 Level 5.2. Both FFmpeg patch sets share a change allowing `h264_v4l2m2m -level:v 52` to request Level 5.2 from V4L2 before streaming starts. Omitting the level keeps the driver default; if the driver rejects an explicit value, an error is reported. Encoding a 720p test pattern on the tablet read back the default Level 5.0 and explicit Level 5.2, with 30 frames decoded cleanly for each clip. Synthetic 3840×2160 input also read back 120 frames at 60 fps and Level 5.2. This verifies encoding and muxing only, not camera capture, live frame pacing, or long-duration A/V sync.

FFmpeg's `h264_v4l2m2m` delivers H.264 SPS/PPS with the first packet, whereas muxing directly to Matroska requires those parameters before the first packet. MPEG-TS accepts them in-band and can be remuxed afterward with `-c copy`.

Follow-up verification must confirm all frames before and after transitions are emitted intact rather than relying solely on process exit codes. Other codecs, container playback timing, and automatic hardware-decode selection in media players also need separate testing.
