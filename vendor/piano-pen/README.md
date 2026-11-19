# Xiaomi Focus Pen Pro Stock Firmware

`p81c/0.0.32/` stores the full ZIP retrieved from the stock Android OTA cache along with the extracted IMG. Both files have been verified to match; provenance, version, and checksums are recorded in `manifest.json`, with the checksum file in `SHA256SUMS`.

This is the firmware for the stylus itself, distinct from the Novatek NT36532 touch controller firmware on the tablet. This project does not automatically flash it on Linux boot. The raw firmware bytes are provided by the vendor, and the project grants no separate open-source license for them.

For container and binary analysis, see [Firmware Analysis](../../docs/devel/hardware/p81c-firmware.md); for the stock upgrade and daily haptic protocols, see [P81C Protocol](../../docs/devel/hardware/p81c-ota-protocol.md).
