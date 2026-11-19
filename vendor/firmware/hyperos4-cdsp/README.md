# Piano CDSP — HyperOS 4

Extracted from `/image` on the `modem` partition of the official full OTA
`OS4.0.6.0.XPYCNXM`, matching `/vendor/firmware_mnt/image` on Android. Contains
the CDSP firmware, CDSP-DTB, and `cdspr.jsn` service descriptor, preserving
original bytes and filenames; the size and SHA256 of each file are recorded in
`manifest.json`.

Linux uses the standard Qualcomm remoteproc, RPMh power domains, and FastRPC
interfaces. The accompanying `piano-dsp-resources.dtso` provides the CX, MXC,
and NSP power domains required by SM8750 and wires the ADSP/CDSP XO clocks to
the mainline RPMh clock provider. Firmware is loaded into Linux's reserved
memory and is never written to Android firmware partitions.

The firmware currently loads and passes signature authentication, but CDSP
never signals boot readiness; remoteproc times out and compute services are not
yet available. Shared sync memory permissions have been aligned with the stock
driver, and further investigation is still needed to identify the resource or
communication condition the firmware is waiting on during boot.

These files are proprietary Qualcomm/Xiaomi firmware and are not covered by
SunUEFI's source license. No additional redistribution license has been
granted; all rights remain with the original rights holders, and the files are
intended solely for operating hardware on this device.
