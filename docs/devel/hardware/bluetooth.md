# Piano Bluetooth Configuration

During UEFI boot, the firmware reads the Bluetooth address read-only from the stock `persist` partition and passes it to Linux via the standard Device Tree `local-bd-address` property. The HCI core and QCA driver handle controller configuration, so upstream BlueZ works out of the box without any project-specific address initialization helper. Public images and the ESP do not store any individual tablet's MAC address, and the installer does not modify device-specific addresses.

## Board-Specific NVM and Connection Range

The WCN7861 uses the p81 board-specific `brhbtnv20.b2e`, giving normal connection range for Bluetooth keyboards, mice, and other peripherals. [`piano-bluetooth-nvm.dtso`](../../../linux/dts/piano-bluetooth-nvm.dtso) is integrated into the shared Device Tree build and overrides only the NVM, leaving the rampatch as `brhbtfw20.tlv`.

During controller initialization, the hardware reports a board ID of `0x0000`, so the driver cannot automatically select the board-specific file. The generic `brhbtnv20.bin` lacks p81 RF front-end calibration, limiting peripheral connections to roughly 20 cm. The board-specific file is shipped in the shared firmware package and matches the file of the same name in the stock `bluetooth_a` partition; this fix does not change how the local Bluetooth address is sourced.

## Address Source and Kernel Interface

On the same device, stock Android's `BluetoothAddress::GetLocalAddress` reads a 6-byte address from `/mnt/vendor/persist/bluetooth/.bt_nv.bin` and reverses its byte order. The verified stock file uses a raw 6-byte format with no extra record header; Android's AIDL implementation relies on this QTI HIDL library. This analysis is based on stock `OS3.0.309.0.WPYCNXM` binaries and `persist` files, without assuming the chip exposes a directly readable OTP address.

When the controller still uses its firmware default address, Linux `hci_qca.c` / `btqca.c` expects the bootloader to provide `local-bd-address`. The HCI core reads this property and invokes the existing `qca_set_bdaddr()`. It is a standard 6-byte, least-significant-byte-first property; see the [generic Bluetooth Device Tree binding](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/net/bluetooth/bluetooth-controller.yaml) and the [WCN7850 binding](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/net/bluetooth/qcom,wcn7850-bt.yaml). The stock file stores the bytes in human-readable address order and is reversed once when passed to the Device Tree or MGMT.

The QCA driver has no NVMEM/OTP provider capable of reading this ext4 file. Adding `nvmem-cells` alone is not viable, and the kernel should not parse an Android filesystem; instead, the boot firmware translates board identity into the existing Device Tree property. `qcom,local-bd-address-broken` is a byte-order compatibility quirk and cannot substitute for an address source.

## Firmware Initialization and OS Handoff

Once the UFS filesystem is ready, [`PianoBluetoothAddress.c`](../../../uefi/components/board-identity/PianoBluetoothAddress.c) locates the unique stock `persist` volume and opens `bluetooth/.bt_nv.bin` in read-only mode. The file must be exactly 6 bytes long, and the address cannot be all zeros, all `FF`, multicast, or locally administered. The file handle is closed immediately after reading, and the data stays only in firmware memory for the current boot without modifying the stock file, NVM, or partition. Both Bluetooth and [RTC offset](sleep.md#rtc-and-stock-time) enforce read-only access via the file open mode; under standard flashing support, the partition's block device remains writable without affecting either read path.

If the stock file is missing or invalid, the firmware queries Qualcomm ChipInfo for the SoC model and chip serial number, derives a stable address using the same SHA256 derivation rule as the existing implementation, and marks it as locally generated. Neither `machine-id`, partition UUIDs, nor random numbers are used, so replacing the rootfs does not change this identity. A 32-bit chip serial number does not guarantee global uniqueness; if both sources are unavailable, the firmware logs an error rather than assigning a hardcoded address to all devices, and does not block other hardware or OS boot.

Standard EFI Device Tree fixups, Linux EFI boot sessions, and the legacy BOOTv2 path all share this same address. Fixup targets only the single onboard WCN7850/WCN7861 node: it preserves any valid address already supplied by the caller and populates `local-bd-address` only when empty or missing. Matching firmware must be updated together with the BSP that removes the userspace address initializer; older firmware does not automatically gain this handoff capability.

For Bluetooth stylus pairing, pressure-sensitive drawing, and squeeze feedback implementation, see [Stylus Protocol](pen-protocol.md).
