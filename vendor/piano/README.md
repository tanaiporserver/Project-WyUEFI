# Piano Static Product Inputs

This directory stores the 13 stock Foundation PE/raw DEPEX binaries loaded at product runtime, 1 early DXE driver (EnvDxeEnhanced), the sanitized board DTB, and the configuration DTB extracted from xbl_config. The manifest uses relative paths, lengths, and SHA256 hashes, preserving original provenance and derivation notes. Product inputs exclude GPT snapshots, full firmware dumps, 14 MiB test-region backups, logs, personal paths, and runtime seeds.

[`backups/`](backups/) separately stores local partition backups; it is not included in the product manifest and does not participate in builds or packaging.

The board description comes from a 16 GiB piano/SM8750P test unit. DTB sanitization removes only identity, seeds, boot arguments, and snapshot addresses from `root`/`chosen`, leaving hardware nodes and reserved regions unchanged; it neither proves suitability for every hardware/SKU revision nor grants live memory ownership.

Historical GPT captures serve only as test material and are not product build inputs. The product reads the current device's GPT instead of comparing against historical full-table snapshots; read-only protection for critical stock partitions remains active.

`tools/piano_vendor_inputs.py` pins the manifest identity and verifies the referenced firmware files, ARM64 PEs, DEPEX sections, and DTBs. ClockDxe retains its original bytes, and the existing strict derivation tool generates the product-compatible copy.

`early-dxe/EnvDxeEnhanced/` preserves the original PE, DEPEX, and stock binary INF, while `provenance.json` records the stock `uefi_a.img` identity, file GUID, length, and SHA256. After verification during product preparation, it is copied to the Stage0 path of the build copy and continues to execute automatically via FV/DXE APRIORI without being added to Foundation's runtime load table.

Public redistribution licensing for stock binaries and derived materials has not yet been cleared; this directory does not claim that permission to distribute open binaries has been obtained.
