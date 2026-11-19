# P81C Stylus Firmware and Stock OTA Protocol

P81C is the stylus-side firmware for the Xiaomi Focus Pen Pro. It runs on the microcontroller inside the stylus and sits in a different layer from the tablet's Novatek touch firmware, the Linux kernel, and the Android Bluetooth service. Archiving this firmware helps analyze the stylus's pressure, squeeze, haptic vibration, Bluetooth, and charging behavior; placing it into Linux's `/lib/firmware` will not automatically improve any of these features.

This document records `P81C_0.0.32` and the stock Android implementation on the same device. No upgrade, erase, or reboot commands were sent to the stylus, and the stock upgrade process was not modified. The original ZIP, IMG, and checksum information are archived in [`vendor/piano-pen/p81c/0.0.32/`](../../../vendor/piano-pen/p81c/0.0.32/). For stylus coordinate and pressure handling, see [Piano Stylus Protocol](pen-protocol.md).

## Samples and Analysis Sources

| Source | Purpose |
| --- | --- |
| `20Kf_XW_P81C_Pen_FW_User_V0.0.32.zip` from Android cache | Stock download package; MD5 is `25fc9fc49b0b333ef5c28852d237bb97` |
| `XW_P81C_Pen_FW_User_V0.0.32.img` inside the ZIP | Stylus upgrade image, 710,312 bytes |
| `BluetoothExtension.apk` from the same device | Stock download, model selection, GATT chunking, and daily control implementation; SHA-256 is `3b1cae2a9f6226701584982d33a5b46065d2a5cbbf3d63b44e3d0fade337f3bf` |
| `StylusOtaService` logs | Remote version is `0.0.32`; ZIP verification passed and extraction completed |
| Renesas DA1469x official SDK documentation | Reference for the SUOTA service, image header, and boot flow; does not replace on-device records |

Decompiled APK output is stored in the local internal notes directory. Obfuscated class names serve only as reference markers and must not be treated as stable APIs; when decompiling other ROMs, locate code by method signatures, UUIDs, and call graphs.

## Model Selection and Download

P81C has a PnP ID of VID `0x0022` and PID `0x5081`. `oobhelper/a.java:116` reads the VID and PID from the Device Information PnP ID characteristic `2A50`, and the upgrade branch at `oobhelper/h.java:126` explicitly routes PID 20609 (`0x5081`) to `MiuiMippOtaManager`, i.e., obfuscated class `p085y0.h`. This class reads `.img` files and uses the SUOTA service.

The same APK also contains `MiuiMippOtaManagerV2` (`p085y0.l`), which uses `FED0/FED1/FED2` and an `A5` packet header, with its file selector reading `.bin`. That path is for other PIDs. **Do not use this protocol for P81C just because its name includes V2.** Another DFU branch in the APK using the `8EC90001` control characteristic must not be mixed with it either.

Downloads are handled by `com/android/bluetooth/ble/app/mipencil/ota/StylusOtaService.java`:

1. The current version comes from the Device Information Firmware Revision characteristic `2A28`, parsed at `oobhelper/a.java:114`.
2. `p076v0/c.java:348` sets the product to `p81c` and the device to `p81c.pen`; `:421` requests update metadata using module `p81c.pen.full`.
3. The cache directory in `StylusOtaService.java:41` is `/data/user_de/0/com.xiaomi.bluetooth/files/`. The callback at `mipencil/ota/a.java:30` computes the ZIP's MD5, compares only the first 10 hex characters against the server's `md5` field, and then extracts the archive.
4. The extraction directory is returned to the upgrade manager via IPC. This download check and the stylus's post-transfer verification are separate stages; a successful ZIP download must not be treated as a successful upgrade.

The public download source is the [stock ZIP on Xiaomi's OTA CDN](https://cdn.cnbj1.fds.api.mi-img.com/ota-packages/20Kf_XW_P81C_Pen_FW_User_V0.0.32.zip). Research records do not store account identifiers, device addresses, tokens, or Bluetooth pairing keys.

## P81C SUOTA Endpoints

All UUIDs below come from the constant table at `p085y0/h.java:106`, with their roles confirmed by its read/write methods and the callbacks in `oobhelper/a.java`.

| Purpose | UUID | Data |
| --- | --- | --- |
| SUOTA service | `0000fef5-0000-1000-8000-00805f9b34fb` | GATT service |
| `MEM_DEV` | `8082caa8-41a6-4021-91c6-56f9b954cc34` | 4-byte little-endian integer; starts transfer, finalizes, and reboots |
| `GPIO_MAP` | `724249f0-5ec3-4b5f-8804-42345af08651` | 4-byte little-endian integer |
| `PATCH_LEN` | `9d84b9a3-000c-49d8-9183-855b673fda31` | 2-byte little-endian block length |
| `PATCH_DATA` | `457871e8-d516-4ca1-9116-57d0b17b9cb2` | Upgrade image data, Write Without Response |
| `STATUS` | `5f78df94-798c-46f5-990a-b3eb6a065c88` | Single-byte notification controlling transfer progress |
| `PATCH_DATA_CHAR_SIZE` | `42c3dfdd-77be-4d9c-8454-8f875267fb3b` | 2-byte little-endian characteristic capacity |
| `STATUS` CCCD | `00002902-0000-1000-8000-00805f9b34fb` | Write `01 00` to enable notifications |

The stock client uses GATT chunked transfers. There is no record of this P81C upgrade using L2CAP CoC; optional transport paths from other Renesas SDKs cannot be copied over directly.

## Chunking, Flow Control, and Finalization

`p085y0/h.java:438` reads the entire IMG into an array and appends one trailing byte: the XOR of all bytes in the original IMG. Although the method at `:141` labels its log tag `crc`, the implementation is **XOR-8**, not CRC32. For this sample, the XOR value is `0x7C`, making the total over-the-air payload **710,313 bytes**. This is a separate layer of verification independent of the payload CRC32 in the IMG header.

After reading `PATCH_DATA_CHAR_SIZE`, the client requests an ATT MTU of `capacity + 3`; once the MTU callback arrives, the chunk size is `min(capacity, MTU - 3)` (`p085y0/h.java:333`). The initial logic groups eight chunks into one block, with the final block shortened to the remaining bytes. The write-complete callback on the data characteristic only advances to the next chunk within a block, while the stylus's `STATUS` notification advances to the next block; both callbacks must be tracked.

| Stage | Stock Behavior | Source Location |
| --- | --- | --- |
| Prepare | Set high connection priority and enable `STATUS` notifications | `p085y0/h.java:272`, `:245` |
| Start | Write `0x13000000` to `MEM_DEV` (`00 00 00 13` on the wire) | `p085y0/h.java:525` |
| Initial handshake | Wait for both the `MEM_DEV` write callback and `STATUS=0x10`, then write `GPIO_MAP=0x05060500` | `p085y0/h.java:290`, `:518`; `oobhelper/a.java:50`, `:209` |
| Block length | Write the current block length to `PATCH_LEN`; shortened for the final block | `p085y0/h.java:507` |
| Data | Write chunks to `PATCH_DATA`; write callbacks advance within a block, and `STATUS=0x02` is awaited between blocks | `p085y0/h.java:361`; `oobhelper/a.java:64`, `:227` |
| Finalize | Write `0xFE000000` (`00 00 00 FE`) to `MEM_DEV` and wait for follow-up notifications | `p085y0/h.java:418` |
| Complete | Restore balanced connection priority, close the input stream, and report completion to the Android UI | `p085y0/h.java:261`; `MiuiBleOobHelperService.java:3411` |
| Reboot trigger | Can write `0xFD000000` (`00 00 00 FD`) to `MEM_DEV` | `p085y0/h.java:426` |

`oobhelper/a.java` explicitly maps only `0x10` to handshake step 3 and `0x02` to transfer step 5. Other status codes are not decoded in detail inside that callback. Future Linux tooling should preserve raw values for unknown status codes rather than treating every notification as success.

The stock `H3()` completion path also decides whether to queue a stylus reboot based on discovered services. The presence of `sendRebootSignal()` does not mean a reboot was issued during this upgrade; always re-read `2A28` afterward to verify the stylus's running version.

These endpoints and the roles of `0x13` / `0xFE` / `0xFD` also match the [Renesas DA1469x SUOTA Service Description](https://lpccs-docs.renesas.com/um-b-092-da1469x_software_platform_reference/User_guides/User_guides.html#suota-service-description). According to that documentation, block length provides flow control, the image is checked after the entire transfer finishes, and the bootloader handles final verification and activation on the next reboot. Whether P81C enables all security options from that SDK must be verified separately against the stylus's configuration.

## Image Header and On-Stylus Implementation

This sample uses a `Qq` image header: the first `0x400` bytes of the IMG form the header, followed by a `709,288`-byte payload. The payload CRC32 is `0xDCD50A1C`, which matches computing CRC32 over `IMG[0x400:]`. The header version is `P81C_0.0.32`, the timestamp corresponds to `2025-12-16 10:00 UTC`, and the vector table offset is `0x400`.

The header, Cortex-M vector table, Dialog/Renesas BLE strings, and register accesses all point to the **DA1469x family**. At this stage, the `DA14697` string in logs is not treated as definitive proof of the exact silicon part number, nor are all drivers present in the image assumed to reflect this stylus's bill of materials. The image header format and boot flow can be cross-referenced with the [Renesas DA1469x Image and Booting Guide](https://lpccs-docs.renesas.com/um-b-092-da1469x_software_platform_reference/User_guides/User_guides.html#booting).

Located strings and Thumb code references provide the following analysis entry points. Here, "file offset" is the position within the IMG, and "code address" is calculated from the mapping after stripping the `0x400` header; without a symbol table, parameters and callers still need to be verified function by function.

| Clue | IMG File Offset | Referenced Code Address | Relevance to Linux Debugging |
| --- | --- | --- | --- |
| `AW86224` haptic driver logs | `0x9A498` | `0x34980` | Trace motor initialization, gain, and playback; corresponds to FE11 haptic requests |
| `CDV2624` driver logs | `0x99B50` | `0x32898` | Another haptic driver exists in the same image; runtime selection must be verified instead of assuming both are active |
| Touch double-tap | `0xA5058` | `0x3E8CC` | Distinguish barrel touch detection, double-tap configuration, and key events sent to the host |
| MIPP haptic gain | `0xA5CA4` | `0x41C44` | Entry point for tracing squeeze feedback intensity and on-stylus waveforms/gain |
| Default MIPP 60 Hz | `0xA4458` | `0x3AD1C` | Indicates an on-stylus default parameter; does not directly explain why the tablet receives no coordinates at 144 Hz |
| Pressure processing library | `0xA4350` | `0x3AD2C` | Cross-reference with raw pressure in Bluetooth Report 5 and its calibration path |

## Keep Daily Control Separate from the Upgrade Protocol

Daily control uses the **FE10 service, FE11 write characteristic, and FE12 notify characteristic**, all sharing the suffix `aa6c-462a-964a-7f2ed5b3e512`. Commands generally follow `opcode, payload_length, payload`. The table below is derived from packet construction and response parsing in `MiuiBleOobHelperService` on the same device, not guessed from the Renesas SDK.

| Request | Meaning | Response | Reference in Pinned APK |
| --- | --- | --- | --- |
| `01 00` | Query stylus capability bits | `81 02 feature0 feature1` | `MiuiBleOobHelperService.java:1074` |
| `02 00` | Query operating status of each IC | `82 02 state0 state1` | Same file `:1191` |
| `51 01 state` | Report magnetic attachment state; `0` = attached, `1` = detached | `D1 00` | Same file `:3273`, `:1383` |
| `52 00` | Query active/idle state | `D2 01 state` | Same file `:1420` |
| `5A 01 level` | Set squeeze feedback intensity, levels 0–5 | `DA 01 status` (`1` = success, `0` = failure) | Same file `:2613`, `:1580` |
| `5C 04 down_hi down_lo up_hi up_lo` | Set squeeze press and release thresholds as big-endian u16 values. Stock packet builder clamps `down` to 1–900 and `up` to 0–900; unit is not yet confirmed | `DC 01 status` | Same file `:2661`, `:1482` |
| `5E 02 type value` | Haptic control type and value; not a squeeze event | `DE 00` | Same file `:2894`, `:1522` |
| `5F 01 enabled` | Enable/disable double-tap | `DF 01 status` | Same file `:637`, `:1502` |
| `59 02 pen_type level` | Set writing feedback; pen type and intensity encoded separately | `D9 01 status` | Same file `:2923`, `:1562` |
| `5B 01 enabled` | Enable/disable accelerometer/gyroscope channel | `DB 01 status` | Same file `:644`, `:1547` |
| `61 01 enabled` | Notify screen state | `E1 00` | Same file `:654`, `:1603` |
| `62 01 state` | Notify reporting / lift-and-move state | `E2 00` | Same file `:656`, `:1608` |
| `63 01 enabled` | Enable/disable barrel slide gesture; enabled by default on stylus boot | `E3 01 status` (`1` = success) | `SET_SLIDE_STATE_ENABLE_CMD`, `STYLUS_SLIDE_REPORT`; stylus firmware handler VA `0x41600` |
| `64 01 enabled` | Master switch for barrel gestures; enabled by default on stylus boot | Stylus firmware replies `E4 01 01` | Stylus firmware handler VA `0x415C4`; not observed being sent by the stock client |
| `6F 01 x` | Reset stylus | Replies `EF 00`, then resets after a delay | Stylus firmware handler VA `0x413B0`; do not send |

`feature0.bit0` indicates wireless charging support; `feature1.bit0` indicates haptics, and `feature1.bit1` indicates high-frequency pressure support. `state0.bit0` is the charger IC, `.bit2` is the wireless charging IC, `.bit5` is the pressure IC, and `.bit6` is the touch IC operating state. These values are reported by the stylus and must not be spoofed as supported via configuration files.

**`DA 01 01` only confirms that the intensity setting was accepted; it is neither a squeeze event nor proof that the motor vibrated.** When stock `oobhelper/h.java:40` receives IPC message 3006, it queues the double-tap, feedback intensity, and two squeeze threshold commands from Android user settings. After a stylus firmware upgrade or reconnection, Linux should likewise reinitialize these daily parameters, and then observe squeeze input and motor output separately.

[`pen-control.py`](../../../linux/bsp/common/usr/lib/piano/pen-control.py) already handles initialization for wireless parameters, double-tap, squeeze feedback, and thresholds. There is no need to flash stylus firmware just to configure feedback intensity. If a user reports that squeezing produces no vibration, first check whether initialization completed, which FE12 response was received, and what firmware version is running, then compare against the same operation on stock Android. Command `62` cannot be used to pretend to "enable pressure"; whenever toggling it affects button or reporting behavior, verify it together with the HID input path.

A firmware upgrade can also change the HID descriptor. Stock `oobhelper/i.java:837` reads the HID descriptor version from advertising data and compares it against the local constant `3`; if the version is newer, it clears `persist.bluetooth.spechid` so the stale local descriptor is no longer used. Linux should similarly rely on the Report Map currently returned by the stylus. If spurious key events appear on the stylus, touchscreen, or keyboard after an upgrade, first identify which input device emitted the event, then verify the Report ID, length, fields, and descriptor—do not mask a protocol parsing bug by globally blocking the `+` key.

## Responsibilities by Layer

| Feature or Issue | Stylus Firmware | Tablet Touch / Linux Driver | Host Userspace |
| --- | --- | --- | --- |
| Tip coordinates and tilt | Transmits stylus signal | Receives raw Novatek type 29 matrix, computes coordinates, and emits standard input events | libinput / drawing apps consume input |
| Pressure | Tip pressure IC; Bluetooth Report 5 | Fuses with coordinate timing and clears stale pressure | Applications process pressure curves |
| Squeeze and double-tap | Barrel sensing and HID / control reports | Parses reports according to the HID descriptor | Binds desktop actions; configures feedback via FE11 |
| Vibration | Motor driver and waveform/gain execution | Must not be exposed as a virtual tablet vibration motor | Uses the stylus control protocol and verifies physical feedback |
| Bluetooth wake and reconnect | Advertising, connection, and sleep policies | Bluetooth controller and standard BlueZ/HID support | Follows magnetic attachment / screen state; avoid hardcoded addresses or deleting pairings to force reconnects |
| Magnetic wireless charging | Stylus-side reception, temperature, and battery state | Tablet-side charging transmitter, Hall sensor, and power supply driver | Exposes status through standard power/battery interfaces |
| No stylus coordinates at 144 Hz | Transmit timing worth investigating | Panel refresh vs. touch scan / stylus sync is the primary comparison point | Forwards stock configuration accurately; do not treat desktop refresh rate as stylus scan rate |

The presence of a "default 60 Hz" log inside the stylus is neither proof that it only supports 60 Hz nor proof that changing that single constant will fix the 144 Hz issue. The working chain on stock Android on the same device must be compared end to end: panel mode, stylus protocol timing, touch scan mode, FE11 configuration, and whether Report 5 / type 29 packets arrive. Modifying only the stylus firmware may fail to change tablet scanning while introducing upgrade risk.

The most valuable next debugging step is to keep the stock firmware untouched, record the `2A28` version, FE11 requests / FE12 responses, and squeeze/pressure input on newer versions, and then fold the necessary initialization and input parsing into the existing BSP. Research modifying the stylus firmware only after pinpointing an actual stylus-side algorithm or protocol defect—not by building a Linux OTA flasher first.
