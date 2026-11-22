## Firmware Infos

- **Device:** nubia NP05J (REDMAGIC Astra)
- **Slot / Build:** Slot `_b`, Android 16 `20260509.003614` / `BOOT.MXF.2.5.1` (`PakalaLA9`)

## Patches / Fixes

Regenerate all `.patched.efi` binaries from the unmodified slot B stock `.efi` files with:

```bash
python3 Binaries/nubia_np05j/build_pakala_patches.py
```

### ButtonsDxe:

- **Reason:** Make the Power button act as Enter in UEFI Boot Manager and Shell.
- **Patch:** `0x036B8` remaps the Qcom Power key scan code (`0x102`) to `CHAR_CARRIAGE_RETURN` (`0x000D`).

### PmicDxe:

> [!IMPORTANT]
> Must be paired with the `SPMIDxe` patch.

- **Reason:** Prevent synchronous external aborts on locked SPMI channels during PMIC re-initialization after `abl` handoff.
- **Patch Nr. 1:** `0x13294` (Minimal PMIC Init, called from `0x16F0`) returns `0` immediately.
- **Patch Nr. 2:** `0x04138` and `0x04140` (two PMIC Init calls in `0x04124`) replaced with `NOP`.
- **Patch Nr. 3:** `0x1E148` (PMIC Peripheral Disable call in `0x1E138` loop) replaced with `mov w0, wzr`.

### QcomChargerDxe:

- **Reason:** Prevent crashes during charger protocol initialization in UEFI.
- **Patch Nr. 1:** `0x09BC8`, `0x09BEC`, `0x09C10` (`blr x8` protocol calls) replaced with `mov x0, xzr`.
- **Patch Nr. 2:** `0x0C2B8` (charger protocol init helper called via `0x08FA0`) returns `0` immediately.

### SPMIDxe:

> [!IMPORTANT]
> Must be paired with the `PmicDxe` patch.

- **Reason:** Prevent SPMI PIC re-initialization faults during UEFI boot.
- **Patch:** `0x02DA8` (SPMI PIC init) returns `0` immediately.

### TzDxeLA:

- **Reason:** Prevent TZ applet re-initialization crash at EL1.
- **Patch:** `0x0F5B0` global TZ Applet initialized flag changed from `FALSE` (`0x00`) to `TRUE` (`0x01`).

### UFSDxe:

- **Reason:** Enable internal UFS storage enumeration in UEFI.
- **Patch Nr. 1:** `0x0A998` replaces UFS sleep/ready check with Active wakeup (`mov w1, #1; bl 0x0C0D0`).
- **Patch Nr. 2:** `0x1191C` branches to `.text` cave `0x1FF00` to clear `[x0, #0x4A1]` and invoke UFS link wakeup (`0x106D0`) before gear check (`0x11284`).

### UsbConfigDxe:

- **Reason:** Keep USB working across `ExitBootServices` and enable automatic Type-C Dual-Role (Host / Client) switching in UEFI via the built-in 100ms `HandlePortPartnerXtach` polling loop.
- **Patch Nr. 1:** `0x04BB0` (`cbz w8, 0x4BD4` -> `b 0x4BD4`) skips USB deinit during `ExitBootServices`.
- **Patch Nr. 2:** `0x0E456` (`0x00` -> `0x01`) enables `StartControllerOnAttach` in `HandlePortPartnerXtach`.
- **Patch Nr. 3:** `0x04948` (`strb wzr, [x8, #0x794]` -> `nop`) keeps Dual-Role armed across plug/unplug cycles so Detach stops Host and subsequent Attach re-evaluates DFP (Host) vs UFP (Client).
- **Patch Nr. 4:** `0x0495C` (`cmp w8, #2` -> `cmp w21, #2`) checks the new role (`1` = Host, `4` = Device) when deciding whether to invoke `EnableVbus`.
- **Patch Nr. 5:** `0x04A54` (`tbnz w22, #0, 0x4A6C` -> `nop`) allows `HandlePortPartnerXtach` to auto-start both Host (`w21 = 1`) and Client (`w21 = 4`) via `UsbStartController`.

### UsbPwrCtrlDxe:

- **Reason:** Avoid PMIC charger crashes when starting USB Device Mode.
- **Patch:** `0x0819C` changes Port 0 charger port type detection from PMIC (`2`) to fixed SDP (`6`).
