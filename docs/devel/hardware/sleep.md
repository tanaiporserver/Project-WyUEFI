# Desktop Power Management and Sleep

Target behavior is managed through standard desktop and logind interfaces: a short press of the power button or a lid close turns off and locks the screen first, and after an idle period the system enters sleep according to GNOME/KDE power settings. No standalone userspace sleep-timer daemon is added.

Hibernate and sleep are handled separately. Hibernate requires persistent disk swap space, a kernel resume entry point, and a firmware resume boot path; the current zram setup does not retain contents across power loss and cannot serve as hibernate storage. Enabling `CONFIG_HIBERNATION=y` in the kernel alone does not mean the system can hibernate.

## Sleep and Wake

The default mode is `mem/deep`, enabling standard suspend while masking hibernate, hybrid-sleep, and suspend-then-hibernate. RTC alarm and power-button wake have been verified on hardware, with display, USB networking, audio, and DSP recovering cleanly; extended suspend duration, standby power draw, and all peripheral combinations still require further testing. For default idle timeouts, screen lock, and button policies, see [Power Button, Lid Close, and Sleep](power-button.md).

The board Device Tree describes the firmware sleep statistics region using the mainline SM8750 `qcom,rpmh-stats` binding. Linux's `qcom_stats` driver exposes entry counts and cumulative residency times for `aosd`, `cxsd`, `ddr`, and individual subsystems under `/sys/kernel/debug/qcom_stats/`. Cold-boot checks on the tablet already return counter readings and DDR frequency residency records; these counters must be compared before and after unplugging USB, as a single reading does not prove deep sleep entry or stock-equivalent standby battery life.

## RTC and Stock Time

The PMK8550 RTC is a free-running seconds counter; Android computes UTC by applying a separate persistent offset. Inspection of the device's stock `time_daemon`, `libtime_genoff.so`, and on-device file read-backs confirms that `persist/time/ats_2` stores an 8-byte little-endian millisecond offset, which divided by 1000 and added to the PMIC seconds counter yields Android's UTC time. Sub-second milliseconds are truncated in the second-resolution RTC interface without modifying the raw counter.

`uefi/components/rtc/` opens this file read-only, validates its exact length and value range, and writes the seconds offset into a dedicated `EfiReservedMemoryType` page. On the in-memory Linux Device Tree copy, it creates an `nvmem-rmem` provider, a `fixed-layout`, and a 4-byte `offset` cell linked to the uniquely matched PMIC RTC via the existing `nvmem-cells`/`nvmem-cell-names` properties without requiring the RTC node to already have a `phandle`. Standard EFI and BOOTv2 share the same function; the input Device Tree and Android files remain untouched.

The page is marked `no-map` and `read-only` and persists after exiting UEFI; kernel memory checks validate it as reserved memory without relaxing checks on DMA or other memory regions. `rtc-pm8xxx` reads the offset via its existing NVMEM backend, so standard `rtc0` provides wall-clock date, relative alarms, and suspend timing without needing a custom time-sync service in rootfs. If the firmware file is missing or invalid, or if page allocation fails, handoff is skipped; when the protected counter lacks a valid offset, HCTOSYS is skipped while keeping relative alarms functional instead of setting the system date to the 1970 epoch.

The `piano` kernel enables `RTC_HCTOSYS` while keeping `RTC_SYSTOHC` disabled; the NVMEM provider has no write-back callback. Linux neither alters the raw PMIC counter nor writes time back to Android's `ats_2`. If Android has not yet saved an offset, network time sync can still set the Linux system clock.

On-device testing with Linux 7.3.3 set the correct date from `rtc0` at boot with `hctosys=1`, keeping RTC wall time within one second of system time. An offset-adjusted `+22` second alarm successfully woke the device from `deep` suspend with the boot ID unchanged, suspend success count incremented by one, zero failures, and both clocks remaining aligned after resume.
