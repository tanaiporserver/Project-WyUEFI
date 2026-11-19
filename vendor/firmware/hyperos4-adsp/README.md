# Piano ADSP — HyperOS 4

Extracted from `/image` on the `modem` partition of the official full OTA
`OS4.0.6.0.XPYCNXM` (vendor tag `OS4.0.6.0.XPYCN`). All 461 extracted files
match `/vendor/firmware_mnt/image` byte-for-byte on a device updated to the
same version.

Contains the full ADSP firmware, ADSP-DTB, and three service descriptor files,
preserving original bytes and filenames. `manifest.json` records the size and
SHA256 of each file. At build time, this bundle replaces the older upstream
ADSP set as a unit without modifying other device firmware or writing to
Android firmware partitions.

The paired MCA PD protocol is version 2: HyperOS 4 inserts a property at
`0x2100b`, shifting subsequent property IDs for PD capability queries, PPS
selection, and fixed-voltage requests up by 1. `piano-board-identity.dtso`
declares the protocol version in use, and the kernel `piano_mca` driver
translates request IDs centrally; do not mix this firmware set with charging
drivers that only support the older protocol.

On-device testing confirms that the updated driver reads valid PDOs
(voltage/current profiles advertised by the charger), restoring input current
over the same 5 V connection from ~0.5 A to 1.49 A (compared to ~1.59 A on
stock OS with the same cable). This is a standard PD charging fix and does not
indicate that Xiaomi high-voltage fast charging has been verified.

MiPPS authentication in the default kernel is implemented in
[`0027-power-piano-mipps-authentication.patch`](../../../patches/linux/7.3/0027-power-piano-mipps-authentication.patch).
It performs the HMAC-SHA256 challenge-response within the existing charging
workqueue and reports the result through the standard `power_supply`
`authentic` property, without requiring a separate authentication daemon. When
the firmware omits identity reporting, the ID is recovered and verified solely
from a real challenge response; if authentication fails, charging falls back to
standard PD/PPS. Authentication and voltage requests share the same firmware
protocol version translation and do not alter charging current or thermal
limits. See the [kernel notes](../../../docs/devel/linux/kernel.md#usb-type-c)
for command ordering, provenance, and limitations.

These files are proprietary Qualcomm/Xiaomi firmware and are not covered by
SunUEFI's source license. No additional redistribution license has been
granted; all rights remain with the original rights holders, and the files are
intended solely for operating hardware on this device.
