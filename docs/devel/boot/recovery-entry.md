# Loading Issues with Persistent Recovery Boot

Stock Recovery is currently restored and Android boots normally. Standalone UEFI Recovery has not succeeded yet; do not treat Fastboot's `Writing ... OKAY` as a successful boot.

By reading the ABL logs from stock `blackbox` in read-only mode, we distinguished three failure stages:

| Image | Error | Stage |
| --- | --- | --- |
| Early image without a valid AVB footer | `ERROR_INVALID_METADATA` | AVB metadata parsing failed |
| v3/v4 image with a valid AVB footer | `Err: line:1752 LoadImageAndAuthVB2() status: Not Found` | Failed to find loaded `vendor_boot` |
| v2 image with bundled DTB and a valid AVB footer | `ERROR: BootLinux: Get pvmfw Image failed!` | Missing loaded `pvmfw` after passing image authentication and loading |

The v2 log also recorded `Authenticate complete! boot state is: orange` and `Loaded Partition: recovery`. It therefore resolved the previous loading error, but did not complete boot. None of the three versions proved entry into the UEFI build under test.

## Binary Analysis

The SHA256 of the ABL PE tested here is `3c40148ca732e962c1f2593619aeaf83b56850e21871c76f8859fba702aef9cd`. The addresses below are RVAs in that PE and apply only to this firmware build:

* `0x3b530`: Skips the `vendor_boot` lookup when the primary Recovery image's header version is less than 3.
* `0x3b538–0x3b54c`: Calls `GetLoadedImageData` with the string `vendor_boot` (`0x99731`).
* `0x3b590`: Logs source line number 1752 for the failure above.
* `0x78c4–0x78e4`: Looks up `pvmfw` if the boot info flags indicate pVM firmware is enabled.
* `0x794c–0x7970`: Logs `Get pvmfw Image failed!` and aborts boot when the lookup fails.

The stock Recovery v4 header has a kernel size of 0 and borrows the kernel from the `boot` partition. The experimental image contains its own UEFI kernel and takes a different loading path. Fixing this requires having that path properly request and load the required stock companion images; changing the header version alone is not enough.

Standalone Recovery request construction is located at `0x3ae54–0x3ae9c`, which only adds `recovery`. The normal boot path adds `vendor_boot` at `0x3ab44` and `pvmfw` at `0x3abfc`. In addition, the raw bytes `a80000b5` at `0x3b414` in the Recovery header supplemental scan decode to `CBNZ x8`: here `x8` is the partition name pointer, so any non-null valid name is skipped. Without a preloaded primary image, this scan cannot be relied upon to automatically complete the load requests.

`OK_NOT_SIGNED` did not immediately abort loading on this unlocked test device. Current logs do not support the assumption that "simply disabling AVB will fix it", nor should ABL, XBL, or `pvmfw` be modified or broken for this. Companion images existing on disk and appearing in ABL's loaded-image list are two separate things.

The normal `boot` entry also cannot be used immediately as a dual-boot installation method: modifying `boot` alters the default power-on path. Currently, the execution backend in `PianoRawLinuxBoot.c` accepts a v2 Linux bundle with an explicit DTB, and does not directly accept the stock v4 `boot`/`vendor_boot`/`init_boot` combination. Passing the application payload along this path and verifying return from UEFI to stock Android must be solved before considering deployment. None of `boot`, `vbmeta`, ABL, XBL, or `pvmfw` have been modified.

The normal `boot` request list is constructed before the AVB call: `0x3aae4` adds `boot`, `0x3ab38–0x3ab50` adds `vendor_boot` when header ≥3, and `0x3abcc–0x3abec` adds `init_boot` when header ≥4 and `init_boot` is present; `0x3abf0–0x3ac04` adds `pvmfw` when present, with no header version threshold. The normal v2/v3 `boot` path is therefore worth further evaluation, though flashing that entry was not tested here.

v4 has another clear difference: at `0x756c–0x7588`, ABL checks the `init_boot`-present flag and the `boot` header version, then queries `init_boot` at `0x75a0` and replaces the `boot` image's ramdisk with `init_boot`'s header/ramdisk. Writing an `APPv1` payload into a v4 `boot` image itself does not clear this flag. v3 does not enter this replacement branch; choosing a header version must be based on the entry point's full flow rather than assuming "newer is always more compatible".

## Reading Failure Logs

After returning the device to rooted Android, run:

```sh
python3 tools/collect_piano_bootfail.py \
  --serial <ADB_SERIAL> \
  --output <internal-log-dir>
```

The tool validates the blackbox header, log ring, and offset bounds, reading only ABL failure log entries without writing to the device or collecting Android minidumps. The output includes raw logs, text, and the `boot index` for each entry; use `boot index` to distinguish new failures from historical records.

The tool has already read back records from this device's 15-slot log ring. Full private records are stored in `<internal-log-dir>`.

For the purpose of stock `pvmfw` and ABL loading conventions, see [AOSP Protected Virtual Machine Firmware](https://android.googlesource.com/platform/packages/modules/Virtualization/+/refs/heads/android16-release/guest/pvmfw/README.md). This project has not yet verified using this virtualization capability after booting Linux from UEFI.
