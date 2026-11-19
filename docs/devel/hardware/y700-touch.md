# Y700 Gen 4 UEFI Touch

The Y700 Gen 4 (TB322FC, CSOT 3K panel, Lenovo Legion Y700 Gen 4 tablet) bit-bangs SPI over GPIO during UEFI to read the Novatek NT36536 touch controller and publishes the Absolute Pointer protocol for the built-in menu. This implementation covers single-finger input in the UEFI menu only; Windows and Linux touch input are handled by their respective OS drivers and do not reuse this firmware download or packet-reading path.

## Build Dependencies

The files listed on this page cannot be compiled standalone. `Y700Touch.c` includes the build-time-generated `Y700TouchConfig.h`, which `tools/y700/prepare_touch.py` generates from `vendor/y700/dtb/y700-live.dtb` and `vendor/y700/touch/novatek_ts_csot_3k_fw.bin`. The same script also adds `Y700Touch.c` to `[Sources]` in `ProductCore.inf`. The Y700 platform package (DSC/FDF, the `SUNUEFI_Y700` flag, and the `Y700TouchStart`/`Y700TouchStop`/`Y700TouchReplay` calls in `PianoProductCore.c`) and these two vendor files have not yet been merged into this repository; until they are in place, checking out the files mentioned here will not produce a buildable firmware image.

Before generating the config, `prepare_touch.py` verifies the Device Tree entries for SPI controller `0xA90000`, CS0, Mode 0, max 10 MHz, pin multiplexing for GPIO 48/49/50/51, and the interrupt descriptor for GPIO 162, as well as the full firmware SHA256 `31382287ce3c5842f6df299589233459056043878deb1d27f7ef6b7f104ce271` (253952 bytes). Any mismatch against the on-device capture halts the build without generating the config.

## Hardware and Protocol Sources

The stock Android final Device Tree specifies SPI controller `0xA90000`. MISO, MOSI, CLK, and CS are GPIO 48, 49, 50, and 51 respectively, and the interrupt is GPIO 162. The current implementation does not request host DMA (Direct Memory Access) and does not call GENI/GPI binary interfaces from other devices; it drives only these four SPI pins and one interrupt pin.

The driver protocol is based on Lenovo's public Novatek `NT36XXX_SPI` source code; the actual register addresses and revision differences for this device were extracted from the ELF symbols and disassembly of the stock `nvt_36xxx.ko` module from the same unit. The older NT36523 and NT36532 memory maps in the public source do not match this device and cannot be substituted directly: this device's event buffer is at `0x130900`, not `0x125800` as in the older tables.

## Initialization

`Y700Touch.c` initializes in `Y700TouchStart` in the following order; if any step fails, `Y700TouchStop` is called to restore any already-acquired resources:

1. Read the serial engine status to confirm the corresponding engine is idle; save the mux, output, and ownership state of the four SPI pins, then switch them to GPIO mode 0.
2. Configure rising-edge hardware latching on GPIO 162: retain raw status capture, disable the TLMM (Top Level Mode Multiplexer, Qualcomm GPIO control block) summary output and PDC wake output, and leave CPU interrupts disabled. On `sun`, `intr_ack_high` defaults to false, so status is cleared by writing 0 rather than writing 1.
3. Verify the full chip trim (`15 0e 00 23 65 03`) and cascade configuration, then read the firmware info header and PID `0x6097`; any other valid model PID is rejected.
4. Follow the stock sequence to write the four firmware sections into the touch controller SRAM while preserving the stock 0x3c00-byte per-chip-select transfer boundary, wait for internal cascade copy completion, read back both info headers, and verify GCM CRC status byte1 bit3, ILM/DLM result CRCs, and the final PID. The fixed firmware is reloaded on every boot to avoid inheriting the controller's deep-sleep state after Android suspend. This path never writes to touch flash.
5. Send the normal report command `76 00` separately and log its ACK; a non-zero ACK does not block subsequent input. During post-boot calibration, the controller may not yet acknowledge this command, so treating it as a hard requirement would falsely disable input.

On success, it publishes the Absolute Pointer protocol, a ConsoleIn device path, and a 20 ms periodic timer. The timer checks only the rising-edge latch bit and reads the 108-byte event buffer only when a new edge is observed; it does not read SRAM while idle and does not use instantaneous GPIO high level as a read trigger.

## Packets and Menu Input

Each touch packet is 108 bytes long: the byte sum of the first 65 bytes must equal 0, followed by ten 6-byte slots. Within a slot, the contact ID is the upper 5 bits of the first byte (1..10), the status is the lower 3 bits (1 or 2 indicates touch down), and X/Y are 16-bit big-endian coordinates up to `19040 × 30400`. Checksum failures, recovery markers (`FD`/`FE`), or out-of-range coordinates invalidate the entire frame; bad frames are not used to synthesize lift-up events.

`Y700TouchDecode.h` tracks only a single primary contact until it lifts; when the primary contact lifts, a release event is queued first, and another contact is only treated as a new press on the next frame so a second finger is not interpreted as a drag across the screen. Both press and release events are queued individually, and release retains the last coordinates so it is not dropped when coordinates have not changed. `Push` determines portrait versus landscape from the current GOP (Graphics Output Protocol) resolution and applies a rotation mapping to the native coordinates; this is the UEFI menu's own display transform and is independent of Windows display rotation.

Three consecutive bad frames force a release event to be queued so the menu cannot get stuck in a pressed state. When watchdog markers appear repeatedly, `GetState` triggers a bounded number of firmware reloads only at application-level TPL (Task Priority Level), never downloading firmware inside the timer callback.

## Teardown and Restoration

Before normal OS handoff or reboot, `Y700TouchStop` stops the timer, disconnects and uninstalls the protocols, restores GPIO 162 control and interrupt configuration, verifies the restored values of all four SPI pins by readback, and finally releases the clocks. If any step fails, it sets `Y700TouchRetained` and refuses to proceed with handoff. The ExitBootServices notification only sets a lifecycle flag and performs no cleanup.

## Verification

`tests/native/test_y700_touch_decode.c` uses hand-crafted packets to verify the 65-byte checksum, coordinate bounds (0 and `19040 × 30400`), single-contact movement, release on primary contact switch, empty frames, bad frames, and invalid lengths; `tests/native/test_usb_debug.sh` compiles and runs it. These are host-side decoder checks that do not involve hardware and do not verify the queue or timer paths.

Verifying menu input on-device requires reading the RAM logs: `SUNUEFI_Y700_TOUCH_RESULT` records initialization stage, firmware version, and packet/error-frame counters; `SUNUEFI_Y700_TOUCH_EVENT` records presses and releases; `SUNUEFI_Y700_TOUCH_IRQ` records edge and idle counters. Publishing the protocol successfully and receiving real touch events are separate things. Remaining usable after an idle period should show `edges` and `transfers` staying flat while `idle` keeps increasing during inactivity, followed by `edges` and `events` increasing with `bad`, watchdog, and `recoveries` all at 0 once touch resumes. Continued tapping and swiping in the menu after an idle period has been confirmed on TB322FC; the device capture logs are not in this repository. GPIO SPI throughput and initialization latency must be measured on hardware and cannot be extrapolated from the stock 10 MHz hardware SPI.

## Known Limitations from Review

- UEFI tracks only a single primary contact; multi-touch is not implemented. Multi-touch on Windows is handled by a separate driver.
- Landscape coordinate mapping checks only `HorizontalResolution > VerticalResolution`; orientation must be verified against actual on-device rendering and is not claimed as verified here.
- `PianoTouchInput.h` uses `double` for coordinate scaling inside a UEFI header, which relies on soft-float in ARM64 DXE; this is pre-existing code and was not modified in this change.
