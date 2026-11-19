# Android Storage Expansion

Deleting the dedicated storage partitions and returning space to Android requires expanding the `userdata` range in the GPT partition table, then expanding the F2FS filesystem inside it. Boot Helper performs the second step through SukiSU's early init hook rather than relying on stock automatic expansion. Merely deleting `sunuefi_esp` and `sunuefi_root`, or only changing the partition end boundary, does not automatically increase usable capacity in Android.

## Encryption and Capacity Resizing

On this device, `/data` uses F2FS and is mounted on a device-mapper device named `userdata`. The stock fstab enables both File-Based Encryption (FBE) and metadata encryption; during boot, `vold` loads the existing keys and sets up the decrypted block device before mounting `/data`.

Therefore, resizing the partition does not require disabling FBE, regenerating keys, or formatting Android. Filesystem tools must operate on the decrypted block device set up by `vold`, not treat the physical `userdata` partition directly as plaintext F2FS. The module's read-only checks confirm that the mapping has a single `userdata` backing device starting at offset zero; keys in the device-mapper parameters are never printed or saved.

The F2FS kernel online resize interface can shrink a mounted `/data`, but does not support online expansion. Expanding capacity requires cleanly unmounting the filesystem and running `resize.f2fs`. Rebooting rebuilds the full mapping, but does not guarantee that the stock system will run the resize tool.

## Expansion During Stock Boot

HyperOS's `libfs_mgr.so` contains Xiaomi-added F2FS expansion code, but the presence of code, binaries, or strings does not mean the current `/data` path executes it. HyperOS `OS4.0.5.0.XPYCNXM` skips `resize.f2fs` for paths starting with `/dev/block/dm`, which includes the mapping `vold` sets up for metadata encryption. Boot logs show only filesystem check and mount, with no resize invocation.

Analysis basis from binaries on the same device:

| File | SHA-256 |
| --- | --- |
| `/system/lib64/libfs_mgr.so` | `aa0a9ee08340310992c77df3fad679c51c81761ba4ccddda3311333f9dfd9094` |
| `/system/bin/fsck.f2fs` (target of `resize.f2fs`) | `fce9b89bc35ee1d8160007406caa402a00bbbae94d662a1ccddd8d129171dc24` |

ARM64 disassembly checks the device path near `0x3d82c` and branches to `0x3dc78` when matching `/dev/block/dm`, bypassing expansion. Resize invocation logs from older HyperOS versions cannot substitute for verifying the current version and device path. The module uses a standalone early maintenance hook and does not modify system libraries, fstab, or encryption configuration to force entry into the stock expansion branch.

See [AOSP MetadataCrypt.cpp](https://android.googlesource.com/platform/system/vold/+/refs/heads/android16-release/MetadataCrypt.cpp) for metadata encryption device setup and mount ordering, and [AOSP f2fs-tools](https://android.googlesource.com/platform/external/f2fs-tools/+/refs/heads/android16-release/fsck/main.c) for offline `resize.f2fs` operations and options.

## SukiSU Early Maintenance

The module's `initrc/storage.rc` imports `/metadata/sunuefi/storage-early.rc`. SukiSU splices module init configs into second-stage init so the imported `late-fs` action runs after stock vendor configs: at that point `vold` has already set up and mounted the decrypted mapping, while `post-fs-data`, APEX data mounts, apps, and KSU's data monitor have not started yet. A regular `post-fs-data.sh` script runs later than KSU's data monitor and cannot replace this hook. Integration uses SukiSU's built-in module interface without modifying BOOT; managers that do not support this interface can still use boot features, but cannot initiate space reclamation.

The static maintenance binary and F2FS tools are stored in `persist/sunuefi/storage/`, avoiding any dependency on the dynamic linker environment that is not ready at this early stage. `/metadata/sunuefi/` stores only the init config; `vold`'s existing keys and other directories remain untouched. Installation atomically updates the binaries and config, and preview, execution, and pre-reboot checks verify the early binary contents and the manager-generated import entries.

After reclamation is confirmed, the program extracts the task ID and target capacity from the existing operation record, verifies the current plaintext F2FS UUID, and writes a root-only one-shot request. On the next boot, init executes it synchronously:

1. In `late-fs`, verify the request, a different boot ID, the original filesystem UUID, a full zero-offset decrypted mapping, and the target capacity. Refuse to run once `post-fs-data` has started.
2. Copy the static F2FS tools from the module into an in-memory directory, verify that the tools can execute, and save `/data`'s mount options.
3. Sync and cleanly unmount `/data`, requiring exclusive open access to the plaintext device. Never use lazy or force unmount, and never stop or unload KSU.
4. Run a read-only check on the filesystem and target layout first, then run `resize.f2fs -k -t <target-sectors>` to expand capacity while reusing the existing metadata area. This preserves the data area start address, node locations, NAT/SIT bitmaps, and checkpoint payload, initializing only newly added free segments without moving existing file blocks. Never use flags that ignore errors or force execution.
5. Run a read-only check on the filesystem again to confirm both superblocks match, the UUID is unchanged, and F2FS covers the target capacity; remount `/data` with its original options and continue Android boot. The module's boot service inspects the result read-only and clears the pending record once confirmed.

The F2FS tools are built from upstream 1.17.0; see the [series manifest](../../../patches/f2fs-tools/1.17.0/series.json) for sources and patches. The SIT and SSA regions retained from online shrinking must still cover the reclaimed capacity; this is checked both before writing the GPT and during early maintenance. Reusing these regions prevents layout recalculation from shifting `main_blkaddr` forward, and avoiding block relocation preserves Android's per-block handling for both metadata encryption and file-based encryption.

Partition targets and filesystem targets are handled separately. Stock F2FS can be smaller than the `userdata` partition, and existing metadata cannot be forced to cover the entire partition. The program calculates the coverable range from SIT, SSA, and section size in both superblocks, then takes the smaller of that range and the partition target; GPT still restores the full partition extent. The operation record stores both `target_userdata_bytes` and `target_f2fs_bytes`. The early tool expands only to the latter, and post-reboot verification checks the physical partition, decrypted mapping, and filesystem separately. When an older record lacks a filesystem target, it is re-derived from current metadata without requiring the user to delete partitions again.

If the post-shrink SIT journal contains old trailing free segments, only out-of-range entries whose valid block count and bitmap are both zero are discarded, and subsequent valid entries continue to be processed. If an out-of-range entry still contains data, an error is returned rather than truncating the entire journal.

Before execution, the request file is renamed to an attempted record. Failures never automatically retry writes or cause reboot loops; if clean unmount fails, Android keeps `/data` mounted and continues booting. If the resize tool or read-back check fails, the device reboots once so the stock boot path can re-check the filesystem. If the reboot call itself fails, init halts to avoid creating directories on an unmounted `/data`; in that case, hold the power button to reboot. Key output is saved to `persist/sunuefi/storage/grow.log`.

## Order of Deletion and Space Reclamation

Expanding `userdata` backward while keeping its start address unchanged is possible only when the dedicated storage area sits immediately after the end of `userdata` and no other partition lies within the reclaimed range. Partition numbers do not determine physical order; GPT start and end addresses must be read.

Deletion first requires early maintenance to be installed, the boot route to be set to Android, and the dedicated partitions to be unmounted, before removing the ESP/root entries and extending the end of `userdata`. Both preview and execution verify expansion preconditions; if unmet, partitions are not deleted. Stock system partitions, encryption configuration, the `userdata` start address, and existing GUIDs all remain unchanged.

The state after writing the partition table is "waiting for reboot to complete Android expansion." The module never runs `resize.f2fs` on a mounted `/data`, nor does it report space as already reclaimed at this stage. After a normal reboot, the physical partition, device-mapper device, and both F2FS superblocks are read, and the operation completes only after each meets the recorded targets; trailing partition slack that existing metadata cannot cover may remain after the end of the filesystem.

## Post-Reboot Read-Only Verification

Source code: [`tools/android/piano_resize_f2fs.c`](../../../tools/android/piano_resize_f2fs.c).

```sh
piano-resize-f2fs status --expect-bytes <new-userdata-bytes> --expect-f2fs-bytes <target-f2fs-bytes>
```

Output fields:

- `userdata_bytes`: Physical partition capacity currently recognized by the kernel.
- `mapped_bytes`: Capacity of the Android decrypted block device.
- `f2fs_bytes`: Capacity declared in the F2FS superblock.
- `mapping_requires_reboot`: The physical partition has grown, but Android is still using the old-capacity mapping.
- `target_f2fs_bytes`: Filesystem target coverable by existing metadata, not exceeding partition capacity.
- `grow_pending`: The filesystem has not yet reached the coverable target capacity, allowing margin aligned to F2FS sections.
- `expansion_verified`: The target capacity, physical partition, mapping, and both superblocks all meet post-expansion requirements.

When target capacities are specified, full verification returns `0`; if targets are not yet met, it returns `3` while still printing the complete status output. This command is read-only and does not modify the filesystem, partition table, or encryption configuration.

On-device early expansion grew F2FS from `219871686656` bytes to `495286484992` bytes in about 10.8 seconds. The physical partition, decrypted mapping, and F2FS capacity matched, both superblocks were consistent, the UUID was preserved, Android finished booting after restoring the original mount, and the BOOT hash remained unchanged. After reboot, user-encrypted files still require unlocking before they can be read; readability of the device-encrypted area cannot substitute for verifying user file contents.
