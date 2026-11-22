# vendor/np05j — nubia NP05J (REDMAGIC Astra / SM8750P) Firmware Inputs

- `dtb/np05j.dtb`: Sanitized board DTB (no `/chosen` snapshot properties).
- `dtb/xbl-config.dtb`: Slot B XBL config FDT (offset `0x7fa4` in `xbl_config_b.img`) containing `/usb0/ss_phy_cfg`.
- `early-dxe/EnvDxeEnhanced/`: Stock `EnvDxeEnhanced` APRIORI driver + `provenance.json`.
- `native/`: 16 unmodified stock Foundation PE/DEPEX modules mapped to Project-WyUEFI `NATIVE_NAMES`.
- `manifest.json`: Verified SHA-256 catalog consumed by `tools/np05j_vendor_inputs.py`.
- `pakala-dxe/`: Complete production `Binaries/nubia_np05j/` tree (with 8 `.patched.efi` binaries + `build_pakala_patches.py` + `RawFiles/`) for Phase 1 Hybrid build.
