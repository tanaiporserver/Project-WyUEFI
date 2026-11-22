# SM8750 (`Pakala`) DXE Patching Catalog & Hardware Constraints

## 1. Common DXE Binary Patch Recipes

> Always verify PAC/BTI instructions (`PACIASP` / `AUTIASP` / `RETAA`) before
> patching, and recalculate the PE32+ checksum afterward.

### `ButtonsDxe.efi` — Map Power Key to Enter

- **Problem:** Qualcomm stock `ButtonsDxe` maps the Power button to a custom
  ScanCode (e.g., `SCAN_SUSPEND` / `0x120` on `dodge` / `0x102` on `annibale`)
  with `UnicodeChar = 0`, preventing selection in UEFI Boot Manager / Shell. On
  modern Qcom platforms, Volume Down is also often routed via PMIC/SDAM with
  Power rather than a standard GPIO.
- **Patch approach:**
  1. Search for `"ButtonsDxe"` / `"Keypress SDAM"` or the callback populating
     `EFI_KEY_DATA` (`Key.ScanCode`, `Key.UnicodeChar`).
  2. Locate the Power button branch/switch emitting the vendor ScanCode.
  3. Patch it to emit `Key.ScanCode = 0x0000` (`SCAN_NULL`) and
     `Key.UnicodeChar = 0x000D` (`CHAR_CARRIAGE_RETURN`).

### `ClockDxe.efi` — Boot `ASSERT` / `ExitBootServices` Hang

- **Problem:**
  1. During DXE load, querying NPA/RPMh or sub-clock nodes already handled by
     `abl` can return an error and trigger `ASSERT` / `CpuDeadLoop()`.
  2. During `ExitBootServices`, `ClockDxe`'s callback may gate critical bus
     clocks (`/dev/icbcfg/boot`) or display clocks, causing Linux/Windows to
     freeze or reboot on handoff.
- **Patch approach:**
  1. **Load crash:** Resolve `ELR - BaseAddress` in Ghidra. For non-fatal
     checks jumping into `CpuDeadLoop`, replace conditional branches
     (`B.NE`/`CBNZ`) with `NOP` (`1F 20 03 D5`) or an unconditional `B` over the
     error block.
  2. **`ExitBootServices` callback:** Search for
     `gEfiEventExitBootServicesGuid` (`27ABF055-B1B8-4C26-8048-748F37BAA2DF`),
     locate the callback registered via `gBS->CreateEventEx`, and patch its
     entry to return `EFI_SUCCESS` immediately (PAC-safe: keep `PACIASP`, then
     `MOV X0, #0; AUTIASP; RET`; non-PAC: `MOV X0, #0; RET`).

### `UsbConfigDxe.efi` — OS Handoff & Host/OTG Role

- **Problem:**
  1. `ExitBootServices` USB deinit code tears down the USB controller so the OS
     loses USB access.
  2. Default phone configuration initializes `USB_PORT_0` in Peripheral/Device
     mode (Fastboot/UMS), preventing XHCI Host/OTG usage with powered USB hubs.
- **Patch approach:**
  - **OS handoff (standard in `dodge`):** Locate `gEfiEventExitBootServicesGuid`
    callback and stub/remove the USB deinit sequence.
  - **Force Host mode (optional, trade-off with UMS):** Search for
    `"UsbConfigInit"` or the primary-port mode variable and force `USB_PORT_0`
    to `USB_HOST_MODE_XHCI`. Note: forcing permanent Host mode disables UEFI USB
    Mass Storage mode; prefer separate builds or a BDS role-switch driver when
    both modes are needed.

### Additional Verified `Pakala` (`BOOT.MXF.2.5.1`) Patches (`dodge` / `annibale`)

When bringing up a new SM8750 device on `BOOT.MXF.2.5.1`, check these modules
against `Binaries/dodge/README.md` and `Binaries/annibale/README.md`:

- **`SPMIDxe.efi` + `PmicDxeLa.efi` (must be paired):**
  - `SPMIDxe`: Remove/stub SPMI PIC / handle re-init that faults after `abl`.
  - `PmicDxeLa`: Remove conflicting SPMI protocol handle / minimal PMIC init /
    PMIC peripheral disable calls and OEM-specific calls (e.g., OnePlus
    `EFI_DeadBattery`).
- **`TzDxeLA.efi`:** Change the global TZ Applet initialized variable from
  `FALSE` to `TRUE` to prevent re-running TZ initialization that crashes at EL1.
- **`UFSDxe.efi`:** Replace UFS Sleep call with UFS Wakeup call and ensure UFS
  Link Wake Up executes before gear check so internal storage enumerates.
- **`QcomChargerDxeLA.efi`:** Remove crashing PMIC register / protocol calls
  during early boot if charger driver is kept.
- **`UsbPwrCtrlDxe.efi`:** If USB Device Mode crashes touching PMIC charger,
  override Port 0 charger port type detection from PMIC (`2`) to fixed SDP (`6`).
- **`UsbMsdDxe.efi`:** Change removable media state to non-removable so UFS
  LUNs appear as fixed drives rather than removable USB drives when exposed via
  Mass Storage.

## 2. DXE Exclusion Criteria

Do **not** include these vendor DXEs in `APRIORI.inc` / `DXE.inc`:

- `VerifiedBootDxe` and redundant pre-milestone TZ/RPMB security drivers (stock
  `abl` already sealed pre-milestone state; re-executing causes access
  violations or BDS hangs).
- Unnecessary OEM apps/diagnostics (OEM charger UIs, factory diagnostics,
  vendor-proprietary apps unrelated to boot).

## 3. SM8750 (`Pakala`) Hardware & Memory Map Constraints

- **Strict Uncached (`EFI_MEMORY_UC`) Boundaries:**
  Early Heap, IMEM (`bootImemPblRegionStart`, `bootImemAuthMetaDataStart`,
  `IMEM_Base` `0x14680000`, `IMEM_Cookie_Base` `0x146AA000`), Shared RAM Manager
  (`shrm.elf` MMIO `0x10d03000`), `SMEM`, `AOP`, `UEFI_Log`, and `PIL_Reserved`
  must be mapped uncached (`UNCACHEABLE` / `UNCACHED_UNBUFFERED_XN` /
  `NS_DEVICE`) in `MemoryMapLib.c`. Misclassifying them as cacheable causes
  immediate memory faults or system panics.
- **Oryon CPU EPSS Domains:**
  Map all three independent Oryon EPSS domains (covered under `APSS_HM`
  `0x16000000` length `0x06000000` in `MemoryMapLib.c`, and required for
  clock/ACPI support):
  - `0x17d91000` — Base Cluster
  - `0x17d92000` — Prime Cluster
  - `0x17d93000` — LLC Bus
- **Display & PMIC Bring-up Order:**
  SM8750 panels rely on PMIC rails configured via DCB. Keep
  `USE_CUSTOM_DISPLAY_DRIVER = 0` (`SimpleFbDxe`) during initial bring-up to
  inherit `abl`'s active framebuffer (`Display_Reserved`), and only enable
  vendor `DisplayDxe` after core DXEs, PMIC, and clocks are stable.
