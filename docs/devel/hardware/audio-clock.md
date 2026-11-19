# Piano Microphone Clock

`linux/dts/piano-audio-dmic-clock.dtso` is applied after the full audio overlay, changing `qcom,dmic-sample-rate` on Linux-bound `/soc/codec@7660000` from `4800000` to `2400000`. The mainline driver selects divider 4 accordingly. It modifies only this property and does not alter the stock DTB; routing and gain are managed by the unified UCM, currently set to DMIC2, DEC0, Volume 98 (+14 dB), 48 kHz / S16, and 2-channel PCM.

This is based on the final stock Android DTB from the same device (2026-10-05, SHA256 `8056ae623549f4dbefbf3c2615c134fa670cafa83387235575ab573896636fcc`) and the [MiCode piano-w-oss audio source](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/tree/baeb7389997a6f6074dae31ec26565d6c286986e). All four stock TX DMIC configurations use divider 4. The VA DT sets divider 16, but [VA `hw_params`](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/lpass-cdc/lpass-cdc-va-macro.c#L1414) sets a switching flag when the sample rate exceeds 16 kHz, causing the [divider getter](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/lpass-cdc/lpass-cdc-va-macro.c#L233) to return divider 4.

Both the TX and VA core clock providers in the final stock DTB declare 19.2 MHz; the [stock clock driver](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/audio-ext-clk-up.c#L679) passes this frequency to PRM. Current mainline also requests 19.2 MHz, whereas the previous configuration selected divider 2. What is aligned here is the driver's divider selection—the value `16` in the stock DT cannot be extrapolated directly into a "measured 600 kHz".

On the `g086` test unit, the property reads back as `2400000`; this confirms the configuration is applied, but does not mean the physical DMIC clock has been measured or voice quality verified. It is not yet confirmed that the public source matches the exact build commit of the on-device binaries, and the active mixer/DSP routing and registers during normal stock recording have not been captured. Do not claim microphone quality is fixed until improvement is measured.

## Current Microphone Configuration and Controlled Comparison

On 2026-10-08, known 500/1000/2000 Hz test tones were played through a computer speaker, triggered automatically after PCM capture started; only per-second levels, spectra, and clipping counts were saved—no microphone audio was stored. DMIC2 showed stronger response than DMIC1, consistent with the stock VA routing. During the steady-state segment with DMIC2 and Volume 98 (+14 dB), the three tones measured roughly 18/27/21 dB above the trailing ambient background with no clipping. Because source distance and SPL were uncalibrated, this cannot serve as a full voice quality validation.

These measurements predate the clock-ordering fix: the start of the stream contained roughly 41 full-scale samples, with none thereafter. The high RMS seen in earlier short captures included this startup transient and cannot be treated directly as continuous noise floor. The current UCM selects DMIC2/DEC0 and Volume 98 while keeping the driver-required 2-channel PCM and `MONO`/`AUX0`, without adding virtual entries. Both the full release and the multi-distro BSP share the same tracked `HiFi.conf`. Startup transient and controlled-source results on the subsequent `a8` kernel appear in the next section; normal speech and stock TX/DSP processing remain to be verified. These early statistics are stored in the internal logs directory.

## Stream-Start Transient Analysis and Source Draft

The initial statistics were bucketed in 1-second chunks: with DMIC2 / gain 84, the first chunk had `peak=13362` and `RMS=310`, versus a steady ambient RMS of ~3.8; with gain 98, the first chunk had 41 clipped samples and a mean of ~62, with no clipping afterward. This confirms a large startup spike even at low gain, though chunked data alone cannot pinpoint the exact sample index or attribute it solely to the codec. Fine-grained windowed statistics for the first 100 ms were subsequently collected as root (see below); correlation with hardware enablement and period timing is still pending. No PCM is saved, and no first-packet trimming or software filtering is added as a workaround.

New statistics collected after a normal reboot on build 45 (`startup-45b.json`) show 75 consecutive clipped samples at frames 0–74, with the last occurring at 1.5417 ms (total span of 75 samples: 1.5625 ms). All 48 samples in the first 1 ms are `+32767`; the signal then stays positive DC, with peaks at 2/3/4 ms of 21241/8241/3197, settling to the quantization floor at roughly 12 ms. A log-linear fit to the windowed DC mean across 3–7 ms yields a time constant of ~1.055 ms, corresponding to a single-pole cutoff of ~150.8 Hz—matching the 150 Hz HPF forced by the driver at stream start. This supports the "positive step decaying through the HPF" explanation rather than random garbage in the first packet; however, the first DSP/DMA packet may also carry this pre-existing codec/filter transient, and without hardware timestamps and period boundaries, first-packet effects cannot yet be ruled out.

Based on this, the original RFC cannot be considered sufficient: a stream-close reset does not initialize a cold first open, and PRE mute is immediately cleared by the DAI, adding no effective mute window after DMIC startup. The preferred minimal proper fix is to place the DMIC clock event in the correct DAPM supply order—ahead of DEC/filter initialization—while preserving the existing HPF wait and signal path, combined with PRE mute. If additional unmute/reset settling is still needed, it should run after full DAPM completion and target only newly powered-up DECs according to OEM timing—not via blind waits at seq 7 or dropping frames in software. Only analysis and documentation were updated at that stage; `startup-45b-analysis.json` records the curve fit and these boundaries without altering the RFC or current kernel implementation.

Subsequently, the default release integrated the DMIC clock ordering fix in the [audio patch](../../../patches/linux/7.3/0005-audio.patch): the eight original ADCs are kept, clock events are converted into their own DAPM supplies explicitly depending on `VA_MCLK`, and each supply powers up before and powers down after the DEC MUX. Hardware PGA mute on `PRE_PMU`/`PRE_PMD` is completed while keeping the shared clock reference count, original signal path, and existing HPF wait, with no added reset or extra delays. The preparer reproduced tree `7d2ad26d416bec0d052059727491a37ac7536f0b` at commit `a8c9650eb32038c40a5f5934bb64a813dff0044f`. The new kernel was built in full and deployed along with matching modules and default ESP boot files, rebooting into Linux via normal BOOT. The earlier RFC remains only as an analysis draft.

On-device captures of the first 100 ms under the same DMIC2 / gain 98 configuration reduced the 75 consecutive full-scale samples to 0, with a first-1 ms peak of 33; a separate 20-second controlled-source capture also showed no clipping. The 500/1000/2000 Hz responses from the computer speaker remained at approximately 155/476/219 S16 RMS—with 1000 Hz close to the pre-fix ~478—confirming that the startup spike was not hidden by muting the signal. The trailing ambient background RMS was ~8.04; because SPL and ambient conditions were uncalibrated, before/after background differences cannot be attributed solely to hardware noise reduction. Normal speech and stock TX/DSP processing remain to be verified. Only level/spectrum statistics were saved, with PCM processed in memory and discarded, recorded locally in the internal logs directory and `RESULT-a8.json`.

Before the fix, `PRE_PMU` in `sound/soc/codecs/lpass-va-macro.c:va_macro_enable_dec()` had only an `"Enable TX PGA Mute"` comment without writing PGA mute `BIT4` in `TX_PATH_CTL`; the widget also registered `PRE_PMD`, which the handler did not implement. `POST_PMU` already enabled TX clock `BIT5` and ran the 1/1/6 ms HPF gate settling sequence; `va_macro_digital_mute()` then cleared `BIT4` directly. Here `TX_PATH_CTL = 0x400 + 0x80 * DEC`, HPF cutoff bits live in `TX_PATH_CFG0`, and the gate bit lives in `TX_PATH_SEC2`. This is independent of route index, gain, or extra TLMM power rails.

By contrast, the stock [VA `enable_dec`](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/lpass-cdc/lpass-cdc-va-macro.c#L1031) writes `BIT4` mute on `PRE_PMU`/`PRE_PMD` and pulses `BIT6` reset after disabling the TX clock; [DAI `mute_stream`](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/lpass-cdc/lpass-cdc-va-macro.c#L1491) additionally performs a stream-start `BIT6` reset with a 2 ms wait and delays unmute by 10 ms. The stock dec path also configures a 40 ms DMIC unmute delay and a 300 ms delay before restoring the original HPF cutoff; these values do not equal the transient duration measured on this device.

Normal DAPM ordering also matters: in mainline, DEC is a `MUX_E` (`soc-dapm.c` power-up seq 7) while the DMIC clock event was on `ADC_E` (seq 11), so DEC `POST_PMU` waits ran before DMIC clock `PRE_PMU`; stock enables DMIC first inside `enable_dec`. `soc-pcm.c:__soc_pcm_prepare` invokes DAI unmute only after the complete DAPM `STREAM_START`. Blindly adding delays to DEC `POST_PMU` does not prove DMIC settling was waited on; delays and resets must be placed according to this sequence and the first-100 ms measurements.

The private `codec-candidate.patch` is a minimal RFC for review: it adds `PRE_PMU`/`PRE_PMD` hardware mute and matches the stock `POST_PMD` reset. It was never applied, compiled, or run on hardware; it adds no delays and makes no changes to HPF, gain, routing, or PCM, nor does it claim to eliminate all 41 clipped samples.

Below are the preserved diagnostic notes from earlier DMIC1/0 dB, clock, and stock routing investigations.

## Stock Routing and Previous Configuration

The 2026-10-08 stock idle dump (stored in the internal logs directory) shows sound card `sun-mtp-snd-card`, with all TX/VA CAP controls off, all DMIC muxes set to `ZERO`, and VA Volume at 84; these reflect the idle state, not an active recording route. `extracted/vendor/etc/audio/sku_sun/mixer_paths_sun_mtp.xml` and `resourcemanager_sun_mtp.xml` extracted into the same directory provide the following static configurations:

| Route | Input & Gain | Backend |
| --- | --- | --- |
| `va-mic` / `va-mic-mono` / `va-mic-mono-lpi` | VA DEC0 → DMIC2; initial VA Volume=84 | `CODEC_DMA-LPAIF_VA-TX-0` |
| `va-mic-dmic` / `va-mic-dmic-lpi` | VA DEC0 → DMIC2, DEC1 → DMIC3; initial Volume=84 each | VA |
| `handset-mic` / `voice-rec-mic` | TX DEC2 → DMIC2, Volume=98 | `CODEC_DMA-LPAIF_RXTX-TX-3` |
| `speaker-mic` | TX DEC2 → DMIC0, Volume=85 | TX |
| `handset-mic-unprocessed` | TX DEC2 → DMIC1, Volume=85 | TX |

Both stock VA's `MSM_DMIC` and mainline's `VA_DMIC` correspond to DEC mux value 0. The [stock DMIC enum](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/lpass-cdc/lpass-cdc-va-macro.c#L1614) and local kernel source `sound/soc/codecs/lpass-va-macro.c` share the exact same sequence `ZERO`, `DMIC0`…`DMIC7`, selecting the input at shift 4 in `CFG0`; DMIC2/3 map directly with no numbering offset.

Previously, UCM enabled only VA DEC0/DMIC1 with `Volume=84`. Each mainline Volume step is 1 dB: 84 is 0 dB and 98 is +14 dB, but stock's 98 belongs to the TX handset path and cannot by itself justify adding +14 dB to the VA path. Increasing gain also amplifies the noise floor. WirePlumber currently labels the 2-channel PCM as `[ MONO AUX0 ]`; leaving the second channel unrouted is the existing single-mic design, so an all-zero second channel does not by itself indicate missing dual-mic support.

On 2026-10-08, the default PipeWire source on `g086` was the existing `HiFi__Mic__source` on PCM `hw:Pro,2`, suspended when idle, with `EnumFormat` set to S16LE / 48 kHz / 2-channel `MONO`, `AUX0`, and `Format` empty when hardware is not streaming. `volume`, `softVolumes`, and `channelVolumes` were all `1.0`, dithering was off, and no extra software boost was observed. Because the `q6apm` capture frontend enforces `channels_min=2`, UCM `CaptureChannels` cannot simply be changed to 1; mono support in the VA codec itself does not mean the full PCM supports mono.

Keeping gain 84 and the same PCM/format, one 2-second ambient amplitude measurement was run on each input, processed in memory to report only peak/RMS without saving audio. Channel 0 RMS was −32.33 dBFS on DMIC1 and −32.18 dBFS on DMIC2, both with `peak=32767` and channel 1 all zeros. DMIC2 mux readback was `enum3` before and after the measurement; DMIC1 (`enum2`) and gain 84 were restored afterward. Because ambient silence was unverified and full-scale peaks were present, a 0.14 dB difference proves neither noise-floor nor SNR improvement, nor does it validate normal speech response. Because DMIC2 returned non-zero PCM, it remained the top candidate for the next controlled-source comparison; neither UCM nor the default source was modified and no virtual entries were added. Statistics and parameters are recorded in the local internal logs directory.

In the final stock DTB, `cdc_dmic01_pinctrl` / `cdc_dmic23_pinctrl` select the `p81` active states on LPI `gpio6`/`7` and `gpio8`/`9` with `drive-strength=4`, whereas the upstream audio overlay uses `8` for these four pins. The `qcom,tlmm-pins=<171 172>` / `<174>` properties in the stock nodes are stored as wakeup pins by [msm-cdc-pinctrl](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/msm-cdc-pinctrl.c#L317), and the [wakeup handler](https://github.com/MiCode/vendor_qcom_opensource_audio-kernel/blob/baeb7389997a6f6074dae31ec26565d6c286986e/asoc/codecs/msm-cdc-pinctrl.c#L197) calls `msm_gpio_mpm_wake_set`; the active state switches only LPI pinctrl. These TLMM properties are not microphone power switches and should not be assumed to require adding GPIO outputs for power.

## Previous DMIC1/DMIC2 Comparison Procedure

The following are historical steps from before routing selection and must not be used to restore the current settings. The current default is DMIC2 and Volume 98, which future measurements should restore to; the controlled computer-speaker comparison is already complete and does not need to be repeated for the same question.

First establish a clear test window and sound source; do not start normal speech capture unprompted. Keep DEC0, gain 84, 48 kHz / S16, and 2-channel PCM, outputting only per-phase peak/RMS without saving PCM or audio files. Compare speech level and noise floor between DMIC1 and DMIC2 only after confirming ambient silence and a controlled source at the same distance; 2-second ambient samples cannot substitute for this.

If reading directly via ALSA, first verify that the capture PCM is idle and that the existing HiFi/Mic UCM is enabled. The current UCM `EnableSequence` writes back DMIC1, so do not re-enable Mic after changing the mux. Switch only the VA mux while keeping all other parameters, then restore DMIC1 and gain 84 when done:

```sh
amixer -c 0 cset "name='VA DMIC MUX0'" DMIC2
amixer -c 0 cget "name='VA DMIC MUX0'"
# Read hw:0,2 with a level-only tool during the confirmed window, printing peak/RMS without writing audio files.
amixer -c 0 cset "name='VA DMIC MUX0'" DMIC1
```

Write the route back to UCM and the BSP only if DMIC2's controlled-source response is verified normal and better than the current input. TX 98 belongs to a different backend and processing chain, so raising the current VA gain by +14 dB upfront cannot be treated as an improvement.

Future dynamic captures on stock Android should run in the same window where the user explicitly opens the stock recorder app and selects an `AudioSource`, reading only routing/format/effect metadata without starting recording or collecting audio:

```sh
tinymix -D 0
cat /proc/asound/cards /proc/asound/pcm
for p in /proc/asound/card*/pcm*c/sub*/hw_params; do echo "$p"; cat "$p"; done
dumpsys media.audio_flinger
dumpsys media.audio_policy
```

Full `tinymix` output confirms the final TX/VA path, DMIC, and gain, while active `hw_params` confirms sample rate, format, and channel count; `AudioFlinger`/`AudioPolicy` correlate the recording `AudioSource`, backend, and AGC/NS/AEC effects. Having a route defined in XML does not mean the app actually enables it.
