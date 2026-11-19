# Standalone UEFI Requests and Stock Reboot Behavior

When no other boot route is saved, a normal boot enters stock Android; after a route such as Linux is saved, normal boots continue using that selection. `reboot recovery` preserves the stock Mi Recovery semantics, while UEFI uses a standalone request.

## Current Status

On 2026-10-08, the `build/` image containing the Recovery-first selector was deployed and stock Android booted to completion. After saving Linux `target2/sequence1` natively from Android and performing a normal reboot, SSH confirmed g086 Linux, `/dev/sda36` mounted as an ext4 rw root, and GDM/ADSP/sensors active. Returning to Android from the Linux CLI also succeeded previously; stock `fastboot continue` remains available as a debug entry point.

Normal reboots into a persistent Linux boot route are now recorded on hardware, so ABL `fastboot continue` is no longer a required entry point (see the internal log directory). Bringing up USB networking does not prove that the UEFI USB core or full-lifecycle fastboot is working; the current core only allows continuing to ESP boot when USB startup fails and rollback has been verified safe, so full USB initialization still requires separate verification.

Earlier Mi Recovery checks used target0 (no Linux preference), so the combination of a persistent NEXT2 route and `reboot recovery` was not covered by those historical logs. The current selector handles stock Recovery before reading the NEXT request pages: by default it hands off to stock GKI while preserving the ABL-provided DTB/initrd and the normal-boot preference; only an active key selection during the early boot window overrides this to enter UEFI.

Identification is based on the internal log directory Bootmode/MNTParam records; `mtdoops.boot_mode=1` is kept only as a cross-reference and is not added as a speculative detection alias. In the same log, ABL loaded both the `boot` and `recovery` partitions (`Loaded Partition boot/recovery`), so we cannot assume Recovery always bypasses the BOOT wrapper. Required host checks using the full captured FDT and this Recovery command line show that the normal route returns CRC target2 and the Recovery route returns stock entry 0; even with both NEXT pages marked unreadable, it still returns cleanly with their contents unchanged. This is a C branch check, not a full Recovery FDT capture or on-device reboot; results are stored in the internal log directory.

`piano-boot-request` provides `status`, `set`, and `consume`, reading and writing only the two request pages owned by the Combined BOOT image without repacking the stock kernel. On Linux it is installed at `/usr/local/sbin/piano-boot-request`. Manually consuming the Linux request and performing a normal reboot back to Android has been verified on hardware; an explicitly saved boot route currently persists and is reused on normal boots. Automatic recording of the selected OS, one-shot consumption, and full integration with the Android module buttons are not yet complete.

The entry point for general Linux users is [`piano-next-boot`](linux-next-boot.md): it parses the Qualcomm GPT A/B attributes read-only to determine the active BOOT slot, then reads that partition's APP generation. By default it only saves the boot route and requires an explicit `--reboot` to trigger a normal reboot; it does not rely on an ESP config file and does not pre-fill `boot_a`.

The low-level developer command remains `piano-boot-request status|set|consume --device <partition>`; writes accept `--expect-generation HEX32`, which the production helper always enforces. `status` now outputs `app_generation` without modifying the request pages.

Valid targets are `android`, `menu`, `linux`, and `setup`. Always specify the Combined BOOT partition explicitly; the command will not guess or write to a stock BOOT image or other formats.

The native C implementation lives in `android/native/piano-boot-request.c` and shares the same CRC implementation as the pre-boot selector. Build a static AArch64 ELF with `./build.sh boot-request --sysroot <arm64-sysroot-dir>`; both Android and Linux use the same `status / set / consume` arguments. It restores the block device's original read-only state, updates only its owned request pages, does not repack BOOT, and does not trigger a reboot.

The native tool has passed host interoperability checks against the Combined BOOT image, the Python tool, and the firmware reader function, and normal reboots for both Linux→Android and Android→on-disk Linux have been verified. It is a standalone request command—not the full OTA repack/restore tool required by the module—and does not automatically clear one-shot requests or complete the module WebUI.

The full release root builder compiles the static native command from these sources, installs it to `/usr/local/sbin/piano-boot-request`, and records the compiler, source, and binary identities; the Python version is kept as a host reference tool. Both operate on the same request format.

## Current Selector

A stock Recovery request ignores any saved UEFI/Linux/setup boot route and defaults to jumping back to stock GKI, preserving the Recovery DTB and initrd provided by ABL. The early selection window is still shown and overrides this default only when the user actively selects UEFI via key input; it does not clear the previously saved normal-boot route.

The pre-boot selector supports request pages in the Combined BOOT image's owned region. Two 4 KiB pages sit after the stock GKI workspace and before the selector; each contains a 64-byte record with magic, version, sequence number, target, CRC32, current APP identifier, and reserved bytes. The highest valid sequence number determines the target; on a same-sequence conflict, stale payload identifier, or corruption of both pages, the stock path is preserved. By default both pages hold "no request".

Host/QEMU diagnostics still accept the exact token `sunuefi.boot=uefi` in `/chosen/bootargs`; stock Recovery detection takes precedence over both this parameter and saved boot routes. Native request writes and the on-disk Linux boot route have been recorded on hardware. The packer's `--uefi-request` option writes a persistent diagnostic request, not a one-shot request.

The Combined BOOT layout keeps stock GKI at the entry offset and places the request pages, selector, private stack, BootShim, the same production FD, and the digest-verified APP after its `image_size` workspace. The NORMAL path jumps directly to `primary_entry` referenced by stock `code1` without modifying code pages or drawing early graphics, preserving DTB, initrd, `x0–x3`, `SP`, and `DAIF`. A UEFI request enters BootShim and supplies the APP through a fixed boot handoff record, without relying on the stock `init_boot`/`recovery` ramdisk containing the APP.

## Historical Debugging Log (2026-10-07)

The following records early failures and fixes and does not reflect the current state. Although host/QEMU checks had already verified registers, entry points, address ranges, and the boot handoff record on both branches, the first normal BOOT draft still hung at the Xiaomi logo and stock BOOT was restored. Android bypass had not yet been verified at this stage; it succeeded only after the alignment fix described below.

Static ABL inspection confirmed that the raw ARM64 path copies the full payload according to the Android header's `kernel_size` rather than truncating to the old PE `SizeOfImage`, ruling out "only the stock kernel was copied" as a confirmed cause. A simplified direct-entry approach passed QEMU first and was then tested on hardware as described below. Without a valid execution trace from this test, framebuffer, cache, 64 MiB limits, or code-page permissions could not be treated as confirmed root causes.

Early-stage markers were then added to the direct-entry draft and tested on another normal BOOT run, which still hung at the Xiaomi logo. Stock BOOT was restored and Android boot completion was confirmed. Draft marker `29d2e9b3db220f74` did not appear in pstore after recovery, and blackbox showed no corresponding new ABL error; the missing marker could not prove that the selector never executed.

Early markers only appended to an already-matching first 2 MiB `DBG_C` ring at `0xA3500000` without initializing or clearing it. EL1 calls began only after setting up the private stack; NS access permissions and retention across forced recovery remained unproven. Seven diagnostic boundary checks and seven selector checks (including QEMU) passed, but could not substitute for on-device execution logs.

QEMU used the generic `virt` ARM platform with an entry stub checking registers, stack, and boot handoff records, running only this project's selector—stock XBL, ABL, GKI, and HyperOS did not run in emulation. Investigation next used an isolated diagnostic that detoured through a single pure-assembly branch before jumping to the original entry, eliminating interference from C code, the private stack, logging, and extra payloads; this is not a production profile.

The private isolation image (stored in the internal log directory) added a single 4-byte direct branch after the `image_size` workspace before jumping to the original `primary_entry`. Both branch targets, exact stock kernel byte reconstruction, and AVB metadata/payload hashes all verified cleanly; the full install container was 96 MiB. On hardware it booted normally into Android, with boot completion, active slot A, and full partition readback all matching, and the user confirmed normal startup. This proved that basic repacking and two-branch bridging work on this ROM; it is not released as a separate production firmware.

A large-size control image also booted Android normally. It kept the 67,579,904-byte kernel payload, the same two branches, and the same FD/APP, without executing C code, a private stack, tracing, graphics, or EDK2 at entry; boot completion, active slot A, and 96 MiB BOOT readback all matched. This ruled out the large payload size itself as the boot failure cause and shifted investigation to selector execution. This control image is not released as a production build; stock Recovery was left untouched.

The production `selector.bin` (not a rebuilt test stub) also passed both normal boot and explicit UEFI request tests in QEMU. Because the emulated log region was all zeros and had no display reserved region, this could not prove MMU, cache, stack, or log-region access conditions at hardware entry. A proposal to move the stack into the tail of stock GKI was not adopted because that region overlaps the stock kernel's early page tables.

Next, only the first instruction of `PianoBootSelectTrace` was patched to `RET`, leaving the rest of the selector bytes, stack, address layout, and FD/APP unchanged; the patched binary again passed both branches in QEMU. On hardware, normal BOOT still hung at the Xiaomi logo, and stock BOOT was restored after the user entered stock Fastboot. This ruled out early log-region access as the root cause, though it could not prove that stack or other memory accesses were clean. Production metadata passed host validation and layout checks against the previously captured stock runtime DTB addresses, but register state from the failed boot remained unavailable.

After restoration, Android booted to completion and stock BOOT readback matched. At that point the blackbox boot counter was 204 while the latest failure record remained 194, yielding no corresponding new ABL error; pstore showed no kernel panic, though retained logs from the previous boot could not be taken as evidence that the failed draft reached the kernel. Disassembly of the matching production binary found no SIMD/FP instructions, and the stock GKI early entry initializes `x19–x30` and condition flags itself with no observed dependency on incoming values; investigation continued separating appended stack writes, C calls, and FDT reads.

### DTB Aligned Read Fix

In the stock runtime DTB, `/chosen/linux,initrd-end` is an 8-byte value at file offset `0x634`, and `/reserved-memory/splash_region/reg` is at `0x366c`; both are only 4-byte aligned. The old production binary optimized the source byte-assembly helper `Be64` into `LDR X8, [X0]`, risking an alignment fault under early execution conditions after ABL disables the MMU. The FDT specification itself does not guarantee 8-byte alignment for properties, so pointer accesses requiring 64-bit alignment cannot be used.

Both the Makefile and the production packer now pass `-mstrict-align`, making `Be64` emit byte-by-byte reads in the built binary. QEMU checks enable `SCTLR_EL1.A` and feed 64-bit initrd properties aligned only to 4 bytes; both the Makefile and production packer build paths cover normal boot and explicit UEFI requests. See the [Clang documentation](https://clang.llvm.org/docs/ClangCommandLineReference.html) for the compiler flag and the [Arm alignment documentation](https://support.arm.com/documentation/102376/latest/Alignment-and-endianness/Alignment) for alignment constraints.

Offline capture of the old production binary parsing the stock DTB hit `ESR=0x96000021`, `FAR=0xB5B76634`, `ELR=0xA8001428`, matching the unaligned property and 64-bit load instruction exactly; the aligned build passed the same check. The fixed image (keeping early logging disabled) then booted Android normally on hardware with matching readback. A production image rebuilt from official source with ultra-early logging disabled by default also booted normally; standard `reboot recovery` entered stock Mi Recovery and returned to Android on the same production image with matching readback.

The production draft that passed normal boot at that point was stored in the internal log directory with SHA256 `9ecf61c36b3715d69195b55004bdd2bd6e458de19d5acca0b0ca48534600f916`. Early probes do not touch `0xA3500000` by default, while logging after EDK2 sets up memory mappings remains enabled. This alignment fix resolved the normal-boot hang; subsequent Linux boot route results are in Current Status.

The explicit-request diagnostic modifies only the Combined BOOT image's owned request pages: page 1 holds sequence 1, target UEFI menu, the matching current APP identifier, and CRC; page 2 retains the original "no request" record. Testing the C reader against the stock runtime DTB verified that the decision switched from normal boot to UEFI. Automatic consumption is not yet implemented, so a no-request production image must be restored after diagnostics; do not install this diagnostic image as a daily module.

## On-Device Reboot Reason Channels

The current Android `qcom-reboot-reason.ko` matches the build-id of the inspected driver copy. The DTB points to PMK8550 `sdam@7100/restart@48`, a 7-bit field starting at bit 1 written by the driver via nvmem—not an arbitrary 32-bit MMIO scratchpad.

| String Accepted by Driver | Logical Value |
| --- | --- |
| recovery | 0x01 |
| bootloader | 0x02 |
| rtc | 0x03 |
| dm-verity device corrupted / enforcing | 0x04 / 0x05 |
| keys clear | 0x06 |
| ffus / ffuf | 0x40 / 0x41 |
| panic | 0x21 |
| Unmatched string | 0x20 |

The current table does not include `uefi`. ABL's `ResetReason` path reads the reason and invokes a clear interface, falling back to Normal Boot for unhandled values; there is no record of the raw SDAM value being passed intact to the kernel selector. Adding a string mapping to the kernel alone therefore does not prove `reboot uefi` would work.

The DTB also describes IMEM `restart_reason` (base `0x14680000`, offset `0x65c`, 4 bytes). Its persistence across reboots, firmware usage, clearing behavior, and current EL1 access permissions are all unverified. No magic values have been test-written to this location or other PMIC addresses.

The product goal remains `reboot uefi`. Without a verified native channel, a module can provide a command wrapper that submits a controlled, consumable UEFI request and then performs a supported normal reboot. A command wrapper and a new kernel reboot mode are distinct implementations and must be documented as such; stock semantics for both `system` and `recovery` must be preserved.

KernelSU supports [`webroot` WebUI](https://kernelsu.org/guide/module-webui.html); KernelSU 4.2.0 is confirmed on device. Official Magisk provides [`action.sh`](https://topjohnwu.github.io/Magisk/guides.html), and WebUI can be hosted by apps such as [MMRL](https://mmrl.dev/). The current module UI is a read-only preview and is not yet wired to executable boot requests.

### Historical Explicit Entry and Storage Checks

On hardware, the CRC request produced a white screen after the logo and did not enumerate UEFI Fastboot or the Linux debug interface. After restoring the no-request production image and confirming Android booted normally, the last retained console record was `PIANO_PRODUCT_STORAGE_DISCOVERY status=Security Violation`. This corresponds to the optional FAT/NV backend on LUN4 rather than ESP/root on LUN0; the post-recovery LUN4 snapshot passed the old check, so post-hoc data could not reconstruct the exact mismatch at failure time.

Investigation removed the runtime historical GPT fingerprint, full-table/original-header comparisons, and hardcoded Android boot-attribute rules, and decoupled production vendor inputs from GPT capture. Standard GPT validation and this project's writable-range checks remain; when no writable container is configured, write-capability checks are skipped without isolating the entire storage device, continuing with read-only BlockIO/USB.

A new core with those historical checks removed was compiled and re-tested via BOOT with an explicit request. Retained logs clearly showed `owned-entry-absent` and `STORAGE_DISCOVERY status=Not Found`, progressing into USB initialization; the live `boot_a` attribute was `0x0037000000000000` compared to `0x0077000000000000` in the historical table, a difference exceeding the old priority mask. The next stop point was `PIANO_PRODUCT_USB_START status=Time out`, where USB teardown left no retained context and clock/power release succeeded. No Linux boot request was issued.

After this round of diagnostics, the `product-cleanup-normal` no-request Combined BOOT image was restored, Android booted to completion, and readback matched SHA256 `284008b2af4c043321af6327d309a8922e8f5253589c69c440e915143b27da36`. This round verified only the normal Android path and did not re-test stock Recovery; subsequent deployment and boot route results are in Current Status.
