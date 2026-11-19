# Piano Camera Flash

Torch mode is controlled through the standard LED interface, and the rear camera can also trigger both LED groups via standard V4L2 flash controls. The two groups correspond to controller channels 1+4 and 2+3. Standard camera apps cannot use flash automatically yet: the hardware can emit short strobes, but libcamera does not yet synchronize them with exposure.

## Identity, Wiring, and Limits

The stock final Android Device Tree places the controller on PM8550 at SPMI SID 1, base address `0xee00`. Read-only hardware registers return `type=0x18` and `subtype=0x07`, matching the 4-channel IP supported by the standard driver. The two rear-camera request entries in stock Android do not represent physical wiring: the HyperOS Control Center actually enables all four channels. The stock Device Tree also specifies the two pairings `0x9` (1+4) and `0x6` (2+3). On-device testing confirmed both groups emit light independently, so we use the same dual-channel grouping as the [mainline SM8750 reference board](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sm8750-mtp.dts) without extending the binding or changing power supplies.

Current properties represent the combined current for a group, which the driver splits evenly across its channels. Each group is capped at 100 mA for continuous torch mode and 400 mA / 300 ms for flash strobe; switching to paired groups preserves the original board-level total current limits. The [stock implementation](https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/45fb9bd6ae5ba2942fc1d53e4b6b46ef76992f71/drivers/leds/leds-qti-flash.c#L1710) allows briefly boosting torch current before stepping it down over time; the standard driver does not implement this derating curve.

## Kernel and Device Tree

The shared [flash overlay](../../../linux/dts/piano-camera-flash.dtso) disables the stock flash node and trigger configuration, creating a standard PM8550 `led-controller@ee00` in its place. The S5KJN1 `flash-leds` property references both paired nodes, and `v4l2_async_register_subdev_sensor()` wires up the association without adding userspace devices or daemons. See [qcom,spmi-flash-led.yaml](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/leds/qcom,spmi-flash-led.yaml) for the binding.

The [kernel config](../../../linux/configs/piano-flash.config) enables `LEDS_CLASS_FLASH`, `LEDS_QCOM_FLASH`, `V4L2_FLASH_LED_CLASS`, and `VIDEO_V4L2_SUBDEV_API`. The driver exposes torch brightness, flash brightness, timeout, strobe, and fault controls, and the V4L2 wrapper exposes the corresponding standard controls. Device node numbers vary with probe order, so locate them via the media graph and device name rather than hardcoding `/dev/v4l-subdevN`.

The [platform patch](../../../patches/linux/7.3/0001-platform.patch) fixes the release-loop index and keeps valid 5 mA current steps consistent across LED brightness conversion: encoding into `0..255` rounds up, while the hardware setter continues to round down and preserves the existing current limits, thermal throttling, and fault handling. Zero brightness turns off the LED group. The [safety timer fix](../../../patches/linux/7.3/0137-LOCAL-leds-qcom-flash-fix-safety-timer.patch) ensures the hardware cuts off at the requested duration: the minimum 10 ms corresponds to `0x80`, whereas `0x00` disables the timer for continuous torch operation.

## On-Device Verification Scope

With fixed exposure, white balance, and focus, the rear camera captured both groups turning on individually, turning on together, and turning off. Torch mode was tested at 35 mA per channel, with the controller reporting ramp completion. The old 1+2 pairing emitted no light at the same current, and the chip reported a summary fault (open circuit, short circuit, or power supply not ready). Because the current driver reads only a subset of fault registers, V4L2 `faults=0` does not rule out this class of failure.

Each group emitted 40 ms and 80 ms strobes at 100 mA, and the rear camera captured bright frames followed by turn-off. Before the timer fix, the chip completed ramp-down at roughly 52 ms and 91 ms; with the fixed driver, the same settings measured roughly 42 ms and 81 ms. Register encodings for 10, 20, and 300 ms were also verified with the LEDs off. This confirms only that standard controls trigger short strobes, turn the LEDs off, and program the hardware timer accurately—it does not constitute sign-off for automatic camera flash.

## Capture Flow and Error Handling

The existing camera service discovers both flash channels via the rear camera's ancillary links. Forced-flash capture freezes exposure, white balance, and focus, waits for a real sensor frame after the lock takes effect, and then requests a hardware-timed strobe. Only frames whose entire exposure interval falls inside a conservative lit window are returned; cancellation, timeout, format changes, or sensor errors turn off the flash, restore control state, and discard any other frames.

This flow remains part of the legacy camera service—it is not integrated into libcamera and has not undergone optical exposure-synchronization measurement. For controls, SOF/EOF frame correlation, and existing commands, see [Camera Formats and Controls](camera-controls.md#flash).
