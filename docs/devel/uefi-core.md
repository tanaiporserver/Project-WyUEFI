# UEFI Product Core Implementation

This project maintains a single UEFI product core, compiled to produce `artifacts/product/PianoUEFI-product.img`. Combined BOOT, temporary `fastboot boot`, and the OS installation tools all share this same core; multiple test firmware variants are no longer maintained.

## Firmware Build and Assembly

The firmware is built via `./build.sh uefi`. The build runs in an isolated workspace under `build/firmware-workspace/` without modifying the upstream submodules in `upstream/`:

1. `tools/apply_firmware_patches.py` applies the patches listed in `patches/firmware/series.json`;
2. `tools/prepare_product_handoff.py`, `prepare_product_pump.py`, `prepare_product_ui.py`, and `prepare_nv_runtime_guard.py` inject late identity verification, the event pump, the menu UI, and runtime variable protection;
3. `tools/build_simpleinit.sh --product-gui-pump` compiles the SimpleInit graphical menu;
4. `tools/prepare_product.py` uses `uefi/platforms/pianoProbePkg` as the baseline and assembles the source files from `uefi/core/` and `uefi/components/` to generate `pianoProductPkg`;
5. `tools/build_stage0.sh product` compiles the firmware, `tools/build_integrity.py` verifies input and output integrity, and `tools/package_product.py` packages the final product image.

## Firmware Boot Flow

The resident product core validates the stock-supplied Device Tree, takes over the display, starts the background USB service, exposes UFS block devices, and restores saved settings. It then proceeds to the firmware boot order, one-time boot selection, or firmware setup. Both OS booting and full firmware reboot return control to the resident core, which handles the corresponding resource teardown.

The core obtains both the Device Tree and the boot target from the same boot payload check. Temporary boot searches for SimpleInit only within the initrd range declared in the Device Tree and completely mapped in memory; Combined BOOT uses the payload range passed by the selector. Both paths verify the firmware-embedded application digest and the ARM64 EFI format. Before actually loading SimpleInit, the core still independently re-verifies the handoff record and the application rather than reusing validation results from initialization.

## UFS Writes

Standard Block I/O exposes the stock UFS read-only. For the write scope of the firmware variable file, see [Variable Persistence](boot/uefi-console.md#uefi-variables-in-persist).

Fastboot enables writes only for flashing `sunuefi_esp` and `sunuefi_root`. Before writing, it locates the partition by GPT name, re-verifies the media identifier, GUID, and range, and unmounts the target filesystem. Each written chunk is read back and compared, followed by a device cache sync. Android sparse images are structurally validated in full beforehand, and holes preserve existing data.

GPT validation checks structure, CRC, partition overlap, and logical boundaries. Queries, scans, and boot probing only read from disk.

## Safe ExitBootServices

Before booting Linux, performing a software reboot, or shutting down, the resident core shuts down hardware in the same fixed order before exiting Boot Services:

1. Close the touchscreen GPI channels and event rings, confirm the serial engine has stopped, release DMA buffers and the SMMU domain, and restore registers, pins, and clocks. The invalid SMMU routing that existed prior to touchscreen acquisition must also be restored so it does not alter USB/UFS exit checks.
2. Stop DWC3 endpoint transfers, drain the event ring, and release USB DMA buffers; if the USB port is in host mode, disconnect USB device drivers first, then stop and reset xHCI.
3. Complete queued UFS transfers, synchronize caches, and safely disconnect the storage channel. USB and UFS each unmap address translations and free page tables after stopping DMA.
4. Close the physical button and official magnetic keyboard input protocols, place the keyboard MCU back into reset, restore RPMh votes for L11B/L14B, pins, and SE6 registers, release QUP1 clock references, and finally release display clocks.

If any step cannot confirm that a peripheral has stopped, the firmware retains memory that may still be in use by DMA and refuses to proceed. Full system reboot and shutdown invoke `ResetSystem` only after `ExitBootServices` succeeds. For implementation details, see [Touchscreen Implementation](boot/uefi-console.md#touchscreen) and [Power Actions](boot/uefi-console.md#power-actions-and-exit).

During DWC3 shutdown, active endpoint transfers are ended first before clearing `RUN_STOP`. While waiting for `DEVCTRLHLT`, the firmware continues acknowledging events in the event buffer without processing new requests or submitting TRBs; the USB2 PHY stays awake throughout shutdown. Only after receiving controller halt confirmation does it return DMA buffers and release the SMMU and clocks.

## Resident USB Fastboot Service

While the firmware is running, a USB Fastboot service stays resident in the background (with fixed serial number `SunUEFI-piano`) and remains online across the firmware menu, setup, and Shell interfaces. It supports the following debugging operations:

* `oem help`: list commands available in the current mode;
* `getvar product` / `version` / `storage-policy`: query firmware state;
* `oem status` / `oem ramlog`: export in-memory diagnostic logs (up to 256 KiB);
* `oem screenshot`: read a framebuffer screenshot (24-bit BMP);
* `fetch`: read partition contents read-only up to the backend limit (up to 256 KiB per request on product builds);
* `flash` / `erase`: write or zero a specified partition by exact GPT name;
* `oem boot-stable`: attempt to boot an installed OS following the firmware boot order;
* `reboot` / `continue`: save settings, sync storage, complete safe exit, and reboot.

Before flashing, only the target filesystem is unmounted. Each subsequent write chunk re-verifies that the target remains unmounted and that the media and partition have not changed; after writing, data is read back for verification, and success is reported only after syncing. Each product UFS read/write command transfers up to 1 MiB, split into at most four 256 KiB DMA segments; a single Fastboot `fetch` still returns at most 256 KiB.

Product USB bulk transfers use a 64 KiB DMA buffer, while commands still occupy a single USB packet. USB events are processed with priority, and keyboard and touchscreen inputs continue to be polled between batches; SimpleInit's auto-boot countdown pauses during download, flashing, or erase. If the host aborts a read, the firmware waits for the previous reply to finish and confirms the endpoint has stopped before accepting new commands. For command usage, see [UEFI Fastboot](../user/fastboot.md).

After a Type-C replug, the firmware confirms the new orientation, waits for the current command to finish and DMA on the old connection to come to a clean stop, and then switches the USB3 lanes. If high-speed lane initialization fails, it attempts to restore the previous configuration and keep the USB2 connection active.

The error screen runs its diagnostic loop only while BootServices, the USB device, and its SMMU resources remain valid. The loop bypasses boot policy, input handling, and USB role switching, servicing only the existing USB device; a failure during input teardown can still preserve a healthy USB session. Fastboot provides status, logs, screenshots, and snapshot uploads. The firmware accepts a reboot only when it confirms resources can still be safely exited. For detailed conditions on reboot, teardown failures, and connection preservation, see [Power Actions and Exit](boot/uefi-console.md#power-actions-and-exit). Status queries output only the saved image, Device Tree, memory, and exit check results without re-probing failed hardware.

## SimpleInit Graphical Menu and Boot Splash

The firmware integrates a customized SimpleInit graphical menu with 4x font scaling for the 3200×2136 tablet display:

* **Controls**: Volume +/- moves focus, Power confirms, and pressing both Volume buttons simultaneously goes back. For limitations of other input methods, see [Known Issues](../user/known-issues.md#uefi-menu-and-usb); for background initialization and standard protocols, see [UEFI Setup Page](boot/uefi-console.md#official-magnetic-keyboard-and-touchpad);
* **Menu Options**: "Boot Linux" is selected by default; "Boot from File" lets you choose an EFI application, alongside firmware setup, UEFI Shell, file manager, tools, UEFI boot entries, and power actions;
* **Progress Display**: The boot splash shows real-time staged progress of low-level hardware initialization to avoid prolonged black screens.

## ESP Boot Files and OS Handoff

After console initialization, the resident core starts first to complete device initialization, variable restoration, and safe-exit protocol installation before selecting an OS. The firmware reads standard `Boot####` variables: it consumes one-time `BootNext` first, then tries enabled entries in `BootOrder`. SimpleInit is also a regular boot entry located via a fixed firmware volume GUID; it ranks first when no user ordering exists, and an existing order is never rearranged by firmware boot. Pressing Esc at startup opens the one-time boot menu without modifying `BootOrder` whether an entry is selected or canceled; pressing F12 enters firmware setup. Device paths are expanded by the upstream UEFI BootManager library, supporting GPT short-form paths, file paths, and the standard `EFI/BOOT/BOOTAA64.EFI` fallback path. When no matching filesystem exists, the standard `EFI_LOAD_FILE_PROTOCOL` on the same path is invoked. The firmware does not restrict partition labels, UFS LUNs, or systemd-boot filenames, and does not automatically synthesize an internal firmware fallback entry named Linux.

The product ESP installs systemd-boot by default. When no regular boot entry is registered, the standard fallback file is used. systemd-boot uses separate kernel, initramfs, and Device Tree files by default, and can also discover versioned UKIs. SimpleInit selects "Boot Linux" by default and, after five seconds of inactivity, searches for an OS following the boot order while skipping SimpleInit itself; the systemd-boot menu then stays open for three seconds to allow choosing an older kernel. If auto-boot finds no usable entry, it returns to the menu. After an OS bootloader returns an error, the firmware continues to the next entry in the order only if the application has been unloaded, firmware services remain available, and no new exit request is pending. If a manually selected boot entry fails, or if all attempts fail to boot, an error is displayed while keeping recovery access available. A five-minute UEFI watchdog is started before launching an OS bootloader and is disabled once the application exits or the firmware completes OS handoff checks. If the bootloader times out, the firmware retains resources still in use and prompts you to hold the power button to reboot, then press Volume Up to enter Android. For the complete file layout, see [Boot Architecture](architecture.md#storage-layout).

The firmware applies per-boot memory, panel, and Gunyah fixups to the loaded Device Tree via `EFI_DT_FIXUP_PROTOCOL` and validates reserved regions. The Linux EFI stub populates the EFI memory map and initrd details. SEC reads the full RAM layout from the final Device Tree passed in by ABL for the current boot and builds the memory table together with SMEM, native firmware regions, and cold-boot objects; newly added conventional RAM remains `EfiLoaderData` and does not enter the firmware allocation pool. Before exit, the core re-verifies the actual EFI/GCD descriptors as well as identity mappings and cache attributes in the page tables before handing them over for Linux to reclaim.

`PianoProductMemoryApplyDtb()` uses the early-collected result—protected by a CRC and per-boot identifier—to replace `/memory/reg`, DDR size/vendor/type, and `/ddr-regions` in the Linux DTB, removing extraneous memory nodes. The DTB is fixed up in memory via the standard protocol rather than building per-RAM-size images or rewriting the ESP during installation. Linux hardware nodes, CMA requests, and reserved regions are left intact; pre-handoff checks reject incompatible reserved regions instead of mistaking free EFI pages for total physical RAM.

The boot image is produced by the builder, written to FAT, and read back for verification. When UKI mode is selected, `ukify` packages the image and verifies the `.linux`, `.initrd`, `.dtb`, and `.cmdline` sections. The kernel has no built-in command line or fallback values. Post-install kernel updates are maintained by distribution packages and do not require reflashing the entire ESP.

`ExitBootServices` uses the handle of the currently running innermost boot application and verifies its parent chain up to the selected EFI application, the loaded identity of the outer application, and its source device. The parent chain is capped at 32 levels; child loaders may open other volumes, but cannot borrow an image identity from a different boot chain. When systemd-stub invokes the kernel directly, it reuses the UKI handle while the outer systemd-boot identity remains unchanged. The outer application's parent handle and source device remain invariant throughout the boot process. Rejections before hardware teardown begins can return normally; once teardown has started, any error other than a stale memory map halts execution. On the first call, once identity and memory checks pass, the firmware immediately tears down resources—even if the supplied MapKey is already stale—rather than deferring teardown to a subsequent call. Linux refreshes the memory map and retries without performing allocations, file accesses, or duplicate hardware shutdowns.

`PianoRawLinuxBoot.c` retains only diagnostic booting from a verified USB download; it does not read the ESP and does not serve as a fallback when UKI boot fails. It shares the resource exit checks with product UKI booting; the single-download limit for UEFI Fastboot remains 64 MiB.

All four EFI time services return `EFI_UNSUPPORTED` and are advertised as such via `EFI_RT_PROPERTIES_TABLE`; no placeholder EFI RTC is exposed. Linux uses the PMIC RTC, while UEFI reads only the stock `persist/time/ats_2` file and passes the calendar offset via a standard NVMEM cell in reserved memory; see [RTC and Stock Time](hardware/sleep.md#rtc-and-stock-time).

## UEFI Core Code Structure

| Directory | Responsibility |
| --- | --- |
| `uefi/core/` | Product core logic: entry control, hardware state observation, read-only block devices, USB service, button handling, and boot policy |
| `uefi/components/os-boot/` | OS boot file parsing, memory leasing, and boot session control |
| `uefi/components/linux-loader/` | Linux kernel and Device Tree loader |
| `uefi/components/product-handoff/` | Firmware handoff, late memory validation, and exit hooks |
| `uefi/components/pogo-product/` | Official magnetic keyboard and touchpad: QUP1 SE6 I²C, MCU firmware download, standard input protocols, and pre-exit release |
| `uefi/components/touch-product/` | Touchscreen: QUP1 SE2 SPI, touch firmware download, raw matrix to touch contacts, `AbsolutePointer`, and pre-exit release |
| `uefi/components/product-support/` | GOP display adaptation, in-memory log printing, and BDS support library |
| `uefi/components/usb-host/` | USB host mode: Type-C state reading, xHCI, and DMA |
| `uefi/components/simple-init/` | SimpleInit menu and graphical rendering components |
| `uefi/handoff/bootselect/` | Combined BOOT pre-boot selector and key capture |
| `uefi/handoff/bootshim/` | Boot shim and early memory environment setup |
| `uefi/handoff/early-memory/` | Cold-boot object scanning and early memory descriptor parsing |
| `uefi/platforms/pianoPkg/` | Base platform description and baseline component references |
| `uefi/platforms/pianoProbePkg/` | Diagnostic platform and product firmware assembly template |
