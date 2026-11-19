# Upstream Firmware Patches

Standard Git patches are pinned to the upstream commits specified in `series.json` with recorded SHA256 hashes. They hold the integration changes for Mu Basecore, Mu Silicium library bindings, and SimpleInit; `mu-basecore/cfb9372/` contains the read-only USB mass storage patch used by USB host mode. Production builds apply these patches first, after which the preparation tool copies this project's components and verifies the installed source tree.

`apply_firmware_patches.py` accepts only a cleanly applicable or already fully applied state; unknown modifications report a conflict and never reset the working tree automatically. All three patch sets have been applied to their respective clean baseline trees and verified against the existing build sources.

Stock PE binaries are not included in these patches; they reside under `vendor/piano`. See `patches/linux` for Linux kernel patches. Generated platform directories continue to be assembled from official templates and components rather than maintained by manual edits.
