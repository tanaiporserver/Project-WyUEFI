# P81C 0.0.32 Firmware Binary Structure and Debugging Entry Points

This analysis examines `XW_P81C_Pen_FW_User_V0.0.32.img` (710,312 bytes; SHA-256 `7d05be949d58e937164782844216ad34cb669bc8cd1619ff29c49eea62902606`), cached by the stock Android upgrade process. The single IMG inside the original ZIP is byte-for-byte identical to the cached extracted file. This analysis did not connect to the stylus, send OTA commands, or modify the firmware.

## The Container Is Not an Android boot.img

It is a Dialog / Renesas SUOTA `Qq` single image. The header fields and verification results are listed below. Multi-byte values are interpreted as little-endian; `AA 22` and `AA 44` are byte-sequence markers rather than renamed little-endian integer names.

| IMG Offset | Length | Contents |
| --- | --- | --- |
| `0x00` | 2 | `51 71`, ASCII `Qq` |
| `0x02` | 4 | Payload length `0xAD2A8` = 709,288 bytes |
| `0x06` | 4 | Payload CRC32 `0xDCD50A1C`; matches zlib CRC32 over `0x400..EOF` exactly |
| `0x0A` | 16 | ASCII `P81C_0.0.32` + NUL padding |
| `0x1A` | 4 | Timestamp `0x69412DA0`, 2025-12-16 10:00:00 UTC |
| `0x1E` | 4 | Vector table / application payload offset `0x400` |
| `0x22` | 4 | Security section marker `AA 22`, following length 0 |
| `0x26` | 4 | Management section marker `AA 44`, following length 0 |
| `0x2A..0x3FF` | 982 | `FF` padding |

The timestamp in the header is the firmware packaging date, not the test capture or upgrade time. CRC32 only verifies the integrity of the saved data and does not serve as signature verification. This file has no non-empty security or management sections, so the payload can be read and disassembled directly; this does not reveal the OTP security configuration of the stylus chip, nor does it imply that the upgrade process permits arbitrary packet modification.

## MCU and Code Addresses

It can be confirmed to contain Arm Thumb / Cortex-M code. The `Dialog BLE`, `DA14`, and `charger_da14697` strings, `0x200xxxxx` RAM accesses, `0x500xxxxx` register accesses, and startup table layout all point to the Dialog / Renesas **DA1469x, Cortex-M33** SDK. The exact part number (DA14691/95/97/99) cannot be determined from this binary alone, as the image bundles drivers for multiple draft hardware variants.

The [Renesas DA1469x datasheet](https://www.renesas.com/en/document/dst/da1469x-datasheet?r=1606281) states that the application CPU in this family is a Cortex-M33. The [SDK booting and memory layout documentation](https://lpccs-docs.renesas.com/um-b-092-da1469x_software_platform_reference/User_guides/User_guides.html#booting) explains that applications execute via QSPI remap and that startup code initializes RAM from copy/zero tables. This explains why both low-address Flash functions and `0x200xxxxx` RAM functions appear in the same vector table.

The file does not contain a full Product Header, QSPI active/upgrade slot addresses, or the chip Boot ROM. When loading it into a disassembler, strip the `0x400`-byte header and map the body to **application remapped VA 0**; do not treat file offsets as runtime addresses, and do not assume VA 0 is the chip's physical QSPI address.

| Meaning | File Offset | Runtime Address / Value |
| --- | --- | --- |
| Vector table | `0x400` | VA `0x00000000` |
| Initial stack pointer | First word at `0x400` | `0x20045888` |
| Reset vector | Word at `0x404` | `0x00000201` (LSB indicates Thumb) |
| `Reset_Handler` instructions | `0x600` | VA `0x00000200` |
| NMI vector | Word at `0x408` | `0x200007D9`, source code file offset `0xA8940` |
| HardFault vector | Word at `0x40C` | `0x200007F1`, source code file offset `0xA8958` |

The bytes of `Reset_Handler` have been disassembled as Thumb/M-class code. VA `0x20A`/`0x20C` load the copy table bounds `0xA83B4..0xA83D8` from the literal pool, and VA `0x212..0x244` perform copies in 3-word entries. VA `0x246`/`0x248` load the zero table `0xA83D8..0xA83E8` and zero those regions, then call VA `0xA7C` and `0x420`. VA `0x290..0x2A7` is a literal pool and must not be interpreted as instructions.

| ROM Source File Offset | App Source VA | RAM Target VA | Length |
| --- | --- | --- | --- |
| `0xA87E8` | `0xA83E8` | `0x20000680` | `0x3834` |
| `0xAC01C` | `0xABC1C` | `0x20045888` | `0x314` |
| `0xAC330` | `0xABF30` | `0x20068800` | `0x1378` |

The zero table clears `0x7B10` bytes at `0x20069B78` and `0x417C8` bytes at `0x20003EB8`. These addresses are derived from the image's startup instructions and table data; the tool does not hardcode the 0.0.32 copy table location.

## Feature Clues for Linux Debugging

In the table below, presence only means the image contains corresponding code and log strings; it does not prove that the current stylus actually has all of these chips installed. The pointer reference column gives the IMG offset of the literal/data word holding the string VA; only explicitly noted disassembly entry points have local control-flow confirmation.

| Feature | IMG String Offset | String App VA | Pointer Ref IMG Offset | Purpose |
| --- | --- | --- | --- | --- |
| AW86224 haptics driver | `0x9A498` | `0x9A098` | `0x34980` | Distinguish haptic IC and gain routing from CDV2624 |
| CDV2624 haptics driver | `0x99B50` | `0x99750` | `0x32898` | Alternative draft hardware variant in the same image |
| Squeeze / touch double-tap logs | `0xA5058` | `0xA4C58` | `0x3E8CC` | Trace the Touch state machine, double-tap thresholds, and enablement conditions |
| MIPP haptic gain | `0xA5CA4` | `0xA58A4` | `0x41C44` | Corresponds to the Android `5A 01 level` command |
| Default MIPP 60 Hz configuration | `0xA4458` | `0xA4058` | `0x3AD1C` | Compare against scan/refresh config sent by the stock tablet to the stylus; do not treat the logged 60 Hz as the stylus's maximum sampling rate |
| Pressure library init and version | `0xA4350` | `0xA3F50` | `0x3AD2C` | Inspect pressure calibration, zero point, and nib sensor path |
| BL7488D scan enable | `0x8EFE8` | `0x8EBE8` | `0x286F4` | Draft squeeze-touch controller |
| BL6486 scan enable | `0x98784` | `0x98384` | `0x2A9A8` | Alternative draft touch controller |

Other directly locatable components include: `icm42670p` / `bmi325` inertial sensors, the `app_air_mouse` air-mouse state machine, `sidespin` barrel-roll behavior, `f_psensor_cal` pressure calibration, `app_bms` charging state, and the `cps4520` power driver. `TouchPencil` (IMG `0x8EA19`) and `DarwinPen` (IMG `0x981C9`) inside the image reside in the associated controller data regions; their contents and architectures have not yet been fully analyzed and must not be treated as Cortex-M33 MCU function names. Mixed Flash regions contain constants, strings, and data for other chips, rather than a single contiguous executable code block.

A 29-entry command dispatch table was found at IMG `0xA5BBC..0xA5CA3`. Each entry has the layout `[id:u8, payload_len:u8, reserved:u16=0, handler:u32]`, with monotonically increasing command IDs and Thumb pointer handlers. The entry at IMG **`0xA5C4C`** is `5A 01 00 00 0D 18 04 00`, directly mapping `0x5A` with a 1-byte payload to the gain handler `0x4180D` rather than relying on log-based guesswork. Other entries remain tentative and should not be sent to the stylus merely because they appear in the table.

Haptic gain has local code analysis beyond string references:

- Handler VA **`0x4180C`** (IMG **`0x41C0C`**) reads the first byte from the payload pointed to by the third argument.
- VA `0x4182C` loads that byte into `r1`, `0x4182E` sets `r0=6`, and `0x41830` calls VA **`0x4253C`**.
- The helper at VA `0x4253C` builds an internal message whose first byte is 5 and second byte is that payload value, then calls VA `0x23224`. Here, 5 is an internal message type, not a gain upper limit.
- VA `0x41834..0x4183A` prints the corresponding haptic gain log. This proves the path processes the gain value, though final chip register writes, quantization curves, and clamping rules have not been traced completely. The Android `0..5` range comes from the stock client and should not be inferred from this partial code alone.

The recommended next step for Linux is to confirm the stylus hardware variant and MIPP messages via stock GATT/input logs before performing detailed control-flow analysis on the corresponding functions. Current Linux pressure and position support does not require writing the OTA image to the tablet filesystem, and this firmware should not be treated as a runtime auto-upgrade dependency.

## Reproducing the Analysis

```sh
python3 tools/dev/inspect_p81c_firmware.py \
  vendor/piano-pen/p81c/0.0.32/XW_P81C_Pen_FW_User_V0.0.32.img \
  --output build/p81c-inspection/manifest.json \
  --export-dir build/p81c-inspection/disassembler-input
```

The tool also accepts the original single-IMG ZIP and uses only the Python standard library. It validates the container bounds and payload CRC32, reads the vector table, recognizes the observed SDK startup instruction pattern, exports the RAM initialization mappings, and lists offsets and pointer references for selected feature logs. `body-remapped-00000000.bin` can be imported into Ghidra/IDA/radare2 as Cortex-M33 little-endian Thumb; each `ram_initialization_*.bin` must be mapped to its `runtime_va` from the manifest, otherwise RAM functions such as NMI/HardFault will not resolve correctly.

Test outputs include `binary-analysis.json`, `zip-analysis.json`, and `selected-thumb-disassembly.txt`. The exports are strictly for static analysis; the tool has no device connection, flashing, signature-bypass, or OTA upgrade capability. Inferred function roles are based on partial disassembly and strings, without original source code or full symbol recovery.
