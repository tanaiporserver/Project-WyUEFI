# Local Stock Partition Backups

`persist-20261025.img` is a full 32 MiB backup of the `persist` partition from this project's development tablet, saved before SunUEFI wrote any project files to that partition. During backup, ext4 writes were frozen, the partition was copied, and the SHA-256 digests of the partition, the on-device copy, and the host-side image were compared before restoring normal mounting.

See [`manifest.json`](manifest.json) for file size and digest details. This directory is not a product input and is not bundled into UEFI, Android modules, or distribution installer packages. The image is intended for restoring the original device and is not a generic `persist` image for other tablets.
