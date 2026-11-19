# Linux Next-Boot Entry

`piano-next-boot` uses the existing Combined BOOT request pages to select Android, the boot menu, Linux, or UEFI Setup. By default, it only saves the next boot route; `--reboot` is required to request a normal reboot via systemd. This tool does not repack the kernel, switch slots, write to `misc`/PMIC, or add background services.

The current selection persists across subsequent normal boots until another route is chosen. Automatic consumption of one-shot requests is not yet implemented. The newly deployed selector gives priority to stock Recovery; combined testing of a persistent Linux preference with Recovery is still pending.

Android and Linux can switch back and forth through the same Combined BOOT. The native tool validates the current boot record, syncs and reads back after writing, and then performs a normal reboot.

## Usage

```sh
piano-next-boot status
piano-next-boot android          # Save stock Android as the next boot target
piano-next-boot menu             # Save the boot selection menu as the next boot target
piano-next-boot android --reboot # Reboot normally after saving succeeds
```

Regular users invoke it via `pkexec`. Administrators in the `sudo`/`wheel` group within a local active session can use this polkit authorization; other users require administrator authentication. See the project homepage for the default account and password. `root` can invoke the same CLI directly.

The two `.desktop` entries are "Return to Android" and "SunUEFI Boot Selection Menu". Clicking either first displays a GTK confirmation dialog reminding you to save your current work before invoking the same CLI/helper. Canceling does not save the request; failures display an error. The desktop environment requires a standard polkit authentication agent and Python GI/GTK3, while the CLI only requires the Python standard library, `pkexec`, and the native tool.

## Direct Active Slot Detection

No installation configuration is required, and no copy of the BOOT slot is stored in ESP or root. The tool locates the disk containing `boot_a` and `boot_b` via standard partition names, performs read-only verification of the primary and backup GPTs, selects the currently bootable slot based on Qualcomm's active, priority, successful, and remaining-tries flags, and verifies the partition extents reported by the kernel.

Qualcomm GPT attributes use bits 48–49 for priority, bit 50 for the active slot, bits 51–53 for remaining tries, bit 54 for boot successful, and bit 55 for unbootable. Field definitions come from Qualcomm's public [partition_parser.h](https://github.com/msm8916-mainline/lk2nd/blob/master/platform/msm_shared/include/partition_parser.h), and the selection logic follows [ab_partition_parser.c](https://github.com/msm8916-mainline/lk2nd/blob/master/platform/msm_shared/ab_partition_parser.c). The device's current GPT matches the active B slot reported by Android.

GPT only determines whether stock firmware loads the A or B BOOT slot; the Android/UEFI/Linux selection is still written to SunUEFI's own dual request pages inside that BOOT partition. The tool does not modify GPT, slot priority, or the stock kernel. If the primary and backup GPTs do not match, the active slot cannot be determined, or the current BOOT does not contain SunUEFI, the tool fails explicitly and does not fall back to an older slot.

Every operation reads the current APP generation from BOOT and re-verifies the active slot before and after writing; the native write tool further checks the generation, request page contents, sync, and readback. An unmounted ESP does not affect OS switching.

The APP generation is a 16-byte value at offset `+96` in the `SPLITv1` metadata, and is also the bound value at offset `+40` in `NEXTv1`. The native `piano-boot-request status --device ...` command adds the `app_generation` JSON field for readback verification during installation. `set`/`consume --expect-generation HEX32` verifies this value twice: after initial discovery, and again when opening a writable FD and re-locating the metadata; if it does not match, the request page is not written. Legacy arguments remain compatible, but the production helper always passes the pin and does not reimplement the SPLIT parser or CRC.

Target IDs are Android=0, menu=1, Linux=2, and Setup=3; selecting Android writes target 0 with a higher sequence number, superseding any previous Linux request. Shared CRC, dual request pages, `fsync`/readback, and restoring the block device's original read-only state remain handled by the native tool.

## Packaging Locations

| Repository Source | Guest Install Path |
| --- | --- |
| `linux/userspace/piano-next-boot` | `/usr/bin/piano-next-boot`, `0755 root` |
| `linux/userspace/piano-next-boot-helper` | `/usr/libexec/piano-next-boot-helper`, `0755 root` |
| Native `piano-boot-request` ELF | `/usr/local/sbin/piano-boot-request`, `0755 root` |
| `org.sunuefi.boot-request.policy` | `/usr/share/polkit-1/actions/org.sunuefi.boot-request.policy`, `0644 root` |
| `49-piano-boot-request.rules` | `/usr/share/polkit-1/rules.d/49-piano-boot-request.rules`, `0644 root` |
| Both `org.sunuefi.*.desktop` files | `/usr/share/applications/`, `0644 root` |

The polkit action is bound strictly to the fixed helper executable. The scoped rule only allows local, active `sudo`/`wheel` administrators; other active users still require `auth_admin`, and inactive/`any` sessions are denied. No persistent authorization is retained. The helper only accepts the four fixed targets or `status` plus the optional `--reboot` flag; it does not accept external devices, configs, binaries, or arbitrary commands. The native tool validates the SunUEFI layout, request CRC, and generation inside the selected partition. The helper is not setuid and requires no new daemon. Removing the rules file reverts all active users to `auth_admin` without altering `sudoers` or account passwords.

The release root builder installs the same toolset, static native binary, and Python GI/GTK3 and `pkexec` dependencies for Debian, Nyarch, and SteamOS, ready to use without extra setup commands.

Required source-level checks cover rejecting writes on generation mismatch, shared CRC, request page bounds, and invalid arguments; Python, `.desktop`, and polkit format checks pass. See the current status above for deployment, reboot, and UI verification coverage.
