# Kernel Source and Configuration

Debian GNOME, Debian KDE, Nyarch GNOME, and SteamOS share the same device kernel.
The default build uses stable Linux 7.3.3, with the version string `7.3.3-piano-sunuefi-cachy-optimize`.
The desktop name belongs to the root image; the kernel version string does not include `gnome` or the build directory's Git suffix.

## Source Code and Hardware Adaptation

The Linux baseline is upstream `v7.3`, commit
`c03c1a14507dfaf8b856c2608f9ac216021fc222`. Public hardware adaptation for Piano comes from
`blu-sharky/linux-piano`, while retaining this project's display, audio, input,
charging, sleep, and codec fixes on the new baseline.

Patches are grouped by platform, display, input and wireless, media, audio, Gunyah, and BORE,
with subsequent interface fixes appended separately. Original provenance, patch summaries, application order, and the source tree for each step
are recorded in [`series.json`](../../../patches/linux/7.3/series.json);
older kernel patches are kept for provenance reference and are no longer layered onto the new version.

`./build.sh linux` prepares the source from the pinned upstream commit, applies patches in `build/kernel-worktrees/`,
and produces `artifacts/kernels/release/` from an isolated build copy.
Worktrees under `upstream/` are never modified. Local builds and CI share the same entry point and configuration.

Linux 7.3 changed internal interfaces for DRM atomic commits, video memory allocation, reverse memory mapping, and other subsystems, so the upgrade
includes driver ports. Preserving legacy feature code does not guarantee identical runtime behavior; after upgrading,
on-device display, input, wireless, audio, camera, and power must still be verified. When updating, the new kernel's
modules, `v4l2loopback` camera module, and initramfs must all be installed together—do not replace only `Image`.

Boot arguments come solely from the boot image (ESP) and are not compiled into the kernel. `kernel.command_line` in `config/release.json`
is the source of initial release parameters, written by default to BLS boot entries,
or to the `.cmdline` section when UKI is selected.
The kernel uses `CONFIG_CMDLINE=""` with `CONFIG_CMDLINE_FORCE` disabled and keeps no built-in fallback.
Packaging verifies the root partition, `rdinit=/pianoinit`, and command-line length, then reads back the boot files for validation;
native kernel packages installed later use the distribution's initramfs and standard `root=` parameters.

The current DTB includes an enabled `simple-framebuffer`, and the kernel boots via the standard Linux EFI stub.
`console=tty0` produces output only after the framebuffer console registers;
`console=ttyGS0` also becomes available only after the USB gadget is brought up, so it cannot serve as an earliest-stage serial console.
The screen remaining on the firmware handoff page does not mean the kernel is not running; diagnosis must combine pstore, initramfs
logs, USB state, and subsequent display takeover rather than attributing it directly to a firmware hang.

## Internal Display Low Resolution and Hardware Scaling

Low resolutions on the internal display are scaled up to `3200×2136` by the DPU's SSPP, while DSI, DSC, and the panel
continue using native timings at the same refresh rate. Supported sizes, refresh rates, and standard properties are listed in
[Resolutions and Refresh Rates](../hardware/display-depth.md#resolutions-and-refresh-rates).

After selecting a low resolution in KDE, set display scaling to 100%. The desktop's logical dimensions match
200% scaling at native resolution, while applications and the compositor can render lower-resolution buffers.
Setting the native mode to 200% scaling alone does not activate this hardware scaling path.

The DSI bridge selects the native `adjusted_mode` at the same refresh rate from the connector mode list; after desktop-coordinate
clipping, the DPU plane transforms only each hardware pipe's destination rectangle without modifying the user-submitted position or size.
Scaler allocation and dual-DSI splitting use the transformed hardware dimensions. When inspecting this path, verify the
framebuffer size, plane source and destination rectangles, and the CRTC's actual output timings together.

Piano's QSEED 3.4 uses a horizontal preload of 7 on the linear RGB, unrotated 2× upscale path
so that each pipe starts from the corresponding source pixel. At the internal split boundary, one extra subsequent source pixel is fetched
for bilinear interpolation of the final half-step; outer screen edges do not sample past the layer's source region.
This prevents the default preload from repeating pixels at the start of each pipe and creating a blurry vertical seam down the center.
Vertical pixel extension for the alpha channel likewise uses the corresponding vertical register configuration.

## CachyOS and General Configuration

* Incorporates **BORE 6.8.0** prepared by CachyOS for Linux 7.3, keeping its default parameters.
  Built on the fair scheduler, it adjusts scheduling based on task runtime characteristics to improve interactive
  responsiveness under load; actual frame rate, power consumption, and thermal gains must be measured rather than inferred from enabling the option.
* Enables **MGLRU**, allowing the kernel to reclaim memory based on multi-generational page access tracking. Existing Zstd ZRAM
  is retained; both features are part of mainline Linux.
* Uses **300 Hz** timer frequency, dynamic preemption defaulting to full preemption, and retains `NO_HZ_IDLE` (stopping periodic ticks
  while idle) and `schedutil` dynamic frequency scaling.
* Does not impose extra CPU frequency caps, and does not introduce x86-specific optimizations or sysctl parameters
  that no longer apply to current BORE. Note that 300 Hz is neither the display refresh rate nor the input polling rate.

Specific options live in [`piano-platform.config`](../../../linux/configs/piano-platform.config),
with provenance and licenses documented in [THIRD_PARTY.md](../../../THIRD_PARTY.md).

Both the kernel and the out-of-tree camera module are built with a pinned LLVM 23.1.5; download URLs and checksums are in
[`kernel-llvm.json`](../../../config/kernel-llvm.json). Compilation enables `-O3`,
ThinLTO, `-march=armv8.7-a`, and `-mtune=oryon-1`. The latter is the Oryon scheduling model provided by LLVM
and does not imply a complete description of SM8750's second-generation cores. General kernel code still uses
`-mgeneral-regs-only`, retaining pointer authentication and standard runtime instruction selection; SVE/SME (not present in hardware)
are not enabled, nor are compiler flags used to alter frequencies or thermal control.

## Filesystems for Removable Storage

The stock `piano_defconfig` enables only `VFAT_FS`. FAT32 media can therefore be mounted directly,
whereas exFAT and NTFS—far more common on consumer USB flash drives—are rejected at mount time with
`Filesystem type exfat not configured in kernel`. Partition enumeration itself works and
`lsblk` shows the device; only mounting fails.

[`piano-filesystem.config`](../../../linux/configs/piano-filesystem.config)
adds `EXFAT_FS`, `NTFS3_FS`, and `NLS_UTF8`, which both filesystems require at mount time. These are built into
the kernel rather than compiled as modules for the same reason as `CONFIG_USB_HID`: removable media should work immediately upon insertion
without depending on manual module loading later. `NLS_UTF8` is the easiest item to overlook—without it, filesystem
probing succeeds and mounting fails with `Unable to load IO charset utf8`, which looks like an unrelated error.

Adding these options only proves the kernel contains the drivers; actual read/write behavior still requires testing with physical media.

## Gunyah

The stock boot chain already has Gunyah occupying EL2. Integrating its resource manager and VM interfaces here
does not replace the stock Hypervisor, nor does it directly provide KVM.
The drivers are built into the kernel to preserve unmapping from the kernel direct map when lending memory.

Resource handles in the stock Device Tree are valid only for test boots. UEFI preserves these objects early in boot
and updates the target Device Tree along both the raw and EFI Linux handoff paths. If valid data is not obtained,
only the Gunyah resource manager is disabled and Linux still boots normally.
Source files are located at `uefi/handoff/early-memory/PianoColdBootObjects.c` and
`uefi/core/PianoProductBootObjects.c`.

GNOME's "About" page omits the generic `vm-other` line on Qualcomm's primary system domain,
while keeping underlying systemd detection and Gunyah interfaces intact. Desktop patches and packaging status are in
[System Information Adaptation](../../../linux/desktops/gnome/patches/control-center/README.md).
Gunyah VM execution still requires on-device verification; the presence of device nodes alone is not sufficient proof.

## Piano Boot Configuration

During packaging, `tools/package_release.py` converts the flattened stock DT into the final Device Tree used by mainline.
Legacy SDE/CPAS/VIDC/SPF nodes are replaced by mainline display, CAMSS, Iris, and audio nodes; legacy ICC
arguments and vendor flags misidentified as GPIOs are no longer passed to the kernel as standard bindings. ADSP firmware,
memory, and power resources are preserved, removing only legacy ICC properties unused by the mainline PAS driver.

Legacy GPIO regulators, CCI, and debug UARTs for display and camera, as well as unconsumed legacy touch power rails
and empty USB PHY nodes, are also excluded from probing. Display, camera, and Bluetooth use their respective mainline nodes
and no longer wait on Android's `pinctrl@f000000`. Legacy display/camera nodes are disabled only when their mainline replacements
exist and are enabled. Zero-address, zero-length Demura placeholders are disabled while valid memory
reservations remain intact.

In the flattened tree, `bias-disable = <0>` is converted into a standard boolean flag, preserving zero-argument pinconf semantics.
The legacy `cnss_pci0` placeholder node without a `compatible` string has its empty `reg` removed while preserving memory and IOMMU references;
valid PCI addresses remain unchanged. Both formatting errors block schema decoding, so validator exit codes alone cannot be trusted
to indicate a clean pass—binding errors in diagnostic output must also be inspected.

Stock `sdam@7000/sm1510_present@5d` declares an invalid bit range of offset 5 and length 5 within a single byte.
Mainline has no consumer for this vendor flag, so the full raw byte is retained and the invalid bit slice is dropped
without guessing its intended width. This allows the SDAM provider and OCP records in the same region to register normally.

Four optional PMICs that likewise fail to probe on this board under stock Android, along with two hardware-voted RSC nodes
not belonging to the AP, are disabled in the final tree; the AP's `rsc@16500000` and peripheral supplies remain enabled.
`piano-platform.config` excludes NXP SCMI extensions while keeping the standard protocols used by Qualcomm.
The USB PHY connects to standard orientation and mode-switching interfaces via the USB-C connector topology.

This configuration has been verified against clean-boot kernel logs: the aforementioned reference, SPMI, SCMI, DRM bridge, and RSC
errors disappear, while ADSP, audio, keyboard, Bluetooth, and USB debug networking continue working.

SoC thermal zones use the complete `thermal-zones` definitions from the pinned kernel's `sm8750.dtsi`. During packaging,
they are extracted as an include and merged into the root node by `piano-tsens.dtso`, preserving all 47 sensor mappings,
temperature thresholds, and eight GPU cooling maps; GPU references point to the board mainline node `gpu_ml` with `#cooling-cells`
added. Android's vendor `/soc/thermal-zones` policy is not used as mainline thermal zones.
CPU throttling adds cooling maps from the nine stock zones to `cpu0`/`cpu6`:
verified from stock Android sysfs on the same device as 95°C passive with 8°C hysteresis, corresponding to
`policy0` (CPUs 0–5) and `policy6` (CPUs 6–7). The other seven per-core zones use high-temperature core-offlining protection
in stock firmware and are not part of the 95°C throttling policy; the four `cpuss` zones have no cooling maps in stock.
Linux preserves the SoC's native hot/critical thresholds without replicating vendor core-offlining policies.
The existing `piano-skin` charging thermal zone is retained. Kernel thermal and hwmon
interfaces expose readings directly with no userspace polling daemon required; SoC temperatures and battery JEITA temperatures are handled separately.
After on-device boot and resume from deep suspend, all 47 SoC thermal readings are accessible, and all eight GPU thermal zones bind to the
`devfreq-3d00000.gpu` cooling device.

### Gamepads

[`piano-gamepad.config`](../../../linux/configs/piano-gamepad.config) enables mainline `xpad`, `hid-microsoft`, `hid-nintendo`, `hid-playstation`,
and `hid-sony` for all four systems, along with joystick device interfaces, force feedback, and driver-supported LEDs.
USB Xbox/XInput controllers use `xpad`; Bluetooth HID devices reach evdev through BlueZ, UHID/HIDP, and the
corresponding HID driver rather than USB `xpad`. When a controller connects successfully but produces no input in Steam,
inspect HID binding, input events, and Steam permissions separately—do not assume Bluetooth pairing means the driver is working.
These options do not include out-of-tree `xpadneo`, nor do they guarantee that all third-party compatibility modes use the same protocol.

Disk boot uniformly uses `linux/userspace/disk-bootstrap`, supporting `LABEL` for release images and
`PARTUUID` for development images. ext4 replays its journal normally before switching to read-write; the rescue environment includes
the GENI I²C, GPI, and Nanosic drivers along with MCU RAM firmware required by the official magnetic keyboard.

Camera and video initialization scripts reuse existing IOMMU mappings when the domain type is already `DMA` or `DMA-FQ`,
allowing services to be restarted cleanly. Rewriting an IOMMU group's type after drivers are bound is rejected by the kernel;
do not misdiagnose such service restart failures as camera hardware faults, and never unbind active mappings.

## USB Type-C

[`piano-usb-typec.dtso`](../../../linux/dts/piano-usb-typec.dtso) wires the standard endpoints of PMIC GLINK,
QMP PHY, and DWC3 following Qualcomm's upstream
[`sm8750-qrd.dts`](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sm8750-qrd.dts).
Configuration and pin states from HyperOS 4 on the same device confirm that GPIO61 is the plug-orientation input;
UCSI reads this signal and controls QMP through the Type-C switch rather than hardcoding orientation in the Device Tree.
The stock DWC3 speed limit is SuperSpeed (5 Gbit/s).

DWC3 runs in dual-role mode and registers a USB role switch, while still defaulting to peripheral mode
for existing USB debug networking. When fixed to `peripheral`, the driver does not create a role-switch provider even if the Device Tree declares
`usb-role-switch`, leaving UCSI unable to acquire that interface.

Both the USB connector and the stylus regulator sit under PMIC GLINK. Kernel patches 0019 and 0020
make the GLINK altmode and UCSI drivers enumerate connectors only; 0021 makes the UCSI core match explicit
`reg` port numbers first before falling back to legacy firmware ordering. Otherwise, the earlier regulator node is mistaken for a
port—the interface appears to report orientation, yet never acquires the PHY orientation switch.
0022 adds the initial USB mode configuration required for USB partners already attached before boot,
aligning it with the hotplug path; mode setup runs before orientation and role notifications.

GLINK reuses the UCSI instance after an ADSP restart. 0025 clears the pointer after unregistering ports and freeing the connector array;
otherwise, if the next initialization fails early, a second unregister accesses freed memory and
causes a kernel error during USB-C recovery.

Charging firmware may still report the supply type as `Unknown` after reconnection, causing MCA to cap input current
at 500 mA. 0026 cross-checks the PD sink state reported by the UCSI power supply under the same PMIC GLINK,
the charging path, the stock `SNK_Ready` state, and a valid fixed 5 V PDO before restoring normal PD classification.
Voltage, current, and temperature limits remain governed by existing charging policies; read failures skip that policy cycle.
High-voltage retries after type recovery keep the per-connection retry limit, and UCSI property reads synchronize with supply unregistration via
the power supply subsystem's existing semaphore to prevent accessing freed connectors during ADSP restarts.
Normal PD retains the stock buck policy's `in_pd = 1600 mA`, further bounded by the supply's PDO current limit.
This is a limit of the normal PD path; actual input cannot be judged from the charger's rated wattage.
0027 performs the Xiaomi MiPPS challenge-response inside the existing charging workqueue and reports the result via the standard power-supply
`authentic` property without requiring an authentication daemon. When firmware omits or retains a stale charger ID, the driver searches for and verifies
the ID against the real response to a random challenge; recovery search runs at most 65,536 iterations, while a valid
32-bit ID read directly is verified immediately. If no match is found, charging continues with ordinary PD/PPS without relaxing current limits based on nameplate ratings or cached identity.

Piano's stock authentication sequence is command 7 followed by command 6, omitting reverse-authentication command 8 which causes subsequent
firmware PD requests to fail. Authentication does not switch USB data roles; it invalidates on ADSP restart and is attempted only once
per connection. When UCSI processes a connection change, it notifies MCA synchronously and tracks unplug/replug cycles with a connection generation counter;
even if reconnected before the next poll, stale authentication and charging policies are cleared. Ordinary PD voltage resets do not emit this connection event,
so the brief post-authentication reset flow for the same supply is preserved without reusing the 10-second backoff interval meant for direct-charging current rollback.
Both authentication and normal buck charging bind to the ADSP and connection generation sampled for that cycle, re-checking supply capability reads,
PMIC limits, and 5/9 V requests after acquiring the transport lock; after unplug/replug or ADSP restart,
requests generated from stale samples are discarded rather than re-fetching the generation and continuing. Temperature read failures skip that policy cycle.
Firmware types follow the 4.0.6 enumeration: PPS is 11, and authenticated PD is 12. Direct charging stores the minimum and maximum voltage
of the selected APDO and keeps all requests strictly within that window rather than checking only the global ceiling.
After successful authentication, the unlocked PPS profiles are re-read,
and the existing charging policy still selects between buck and SC8541 direct charging while preserving battery voltage, temperature, and
current limits.

Authentication requests are bound to GLINK's service generation and verified under the service-state lock at transmission time so commands from old sessions
are never sent to a restarted ADSP. `VERIFY_PROCESS` registers a cleanup obligation before being asserted; if a request or cleanup
response is lost, subsequent polls retry it. Failed authentication also clears `PD_VERIFIED`, and neither a success result is exposed
nor high-voltage/direct-charging policies proceed until cleanup finishes. On unbind, polling is disabled and drained first to prevent still-registered
GLINK callbacks from requeuing work.

0028 mirrors stock `qcom_subpmic_sync_cfg_work()` by syncing OTG configuration from the existing `mca,qcom_subpmic` Device Tree node
to ADSP via `0x201a`. The four little-endian 32-bit fields are
`otg_boost_src`, `gpio_chip_type`, `vdd_boost_en`, and `otg_ovp_en`;
on Piano the first two are 0 and 1, while the latter two GPIOs are unconfigured and keep stock `-1`.
The driver accepts only this wiring, re-syncs after every ADSP boot, and keeps ordinary charging while retrying on failure.
When this initialization is omitted, firmware's `mca_misc_enable_usb_otg()` reports `gpio not init`,
times out with `FFFF`, and crashes `TCPC_0` when unplugging a dock acting as power source.

ADSP crash recovery also synchronously unregisters the ASoC sound card; if an audio process holds an open handle to the removed card,
`snd_card_disconnect_sync()` waits for that handle to close, blocking PDR and ADSP restart.
During this state, charging properties return `-ENODEV`, which must be distinguished from normal power or thermal throttling. 0028 addresses
the OTG initialization trigger above and does not alter the wait conditions for sound-card unregistration or DMA resource release.

When direct charging is not active, Linux steps battery charging current down by 300 mA per 1°C above 38°C, dropping ordinary PD to
500 mA from 47°C upward; as temperature falls, it recovers at most one step every 30 seconds with 1°C hysteresis.
This adjustment does not lower the USB input current limit. Detecting disconnects requires checking input voltage, current, and
UCSI connection state rather than relying solely on the battery's `Charging` / `Discharging` status. User-visible symptoms and
workarounds are documented in [Power and Sleep](../../user/known-issues.md#power-and-sleep).

ADSP initialization loads standard `pmic_glink_altmode` and `ucsi_glink`; MCA handles only
Xiaomi's charging protocol with `pan_ack` disabled so the two drivers do not double-acknowledge the same notifications.
The existing USB networking service is started via standard udev readiness on the Type-C port: started when orientation is valid and data role
is device mode, and stopped on disconnect or host-role switch. The debug gadget connects only after orientation has been applied to the PHY,
and booting unplugged does not wait for USB. The service name remains `piano-usb.service` with
no additional resident daemon introduced.

### Normal Charging Table

`cp_charge=1` uses the normal charging table for Piano's CN variant `lwn` battery, with currents stated as the sum across both cells.
Normal mode does not exceed 8 A, and `cp_ibat_max`, skin temperature, and user limits can tighten it further.
Temperature zones, voltage stages, and PMIC float voltages are handled separately; the PMIC handles final CV taper, termination, and recharge.
When the conditions in the next section are met, the FFC table is used instead; both modes share protection and recovery paths.

| Temperature | Charge-Pump Battery Current Cap | Normal Voltage Stages | PMIC Float Voltage |
| --- | --- | --- | --- |
| 8–<13°C | 4.32 A | 4.493 V | 4.500 V |
| 13–<18°C | 7.10 A | 4.493 V | 4.500 V |
| 18–<48°C | 8 A, 5.94 A after stage transition | 4.150 V → 4.473 V | 4.480 V |
| 48–<55°C | 4.32 A | 4.053 V | 4.060 V |

Constant voltage is held at 4.150 V until current drops to 7.23 A before advancing to the next stage; within the same temperature-zone combination,
stages only advance and are not reset by suspend. The final stage's charge-pump voltage target sits 9 mV below the table value,
which is 4.464 V at room temperature; once voltage is at least 4.460 V and net charging current remains below 3 A for 3.5 seconds,
the charge pump stops and hands off to the PMIC for the 4.480 V taper phase. Per the stock program on this device, PMIC termination current
is set to 472 mA (the normal table's 452 mA plus 20 mA compensation); this is handled separately from the 3 A charge-pump
handoff threshold. No termination-enable switch absent from the stock proxy is introduced.

Normal full-pack completion, charge stop, and recharge are managed by the kernel. Full charge is declared only when the current normal FV exceeds 4.400 V,
the charge pump is stopped, and input is online: either PMIC `charge_status=5`, or the highest cell voltage
is at least FV−8 mV and combined net charging current is below 259 mA for five valid samples spaced at least
one second apart. 259 mA is 115% of the stock FG termination current of 226 mA, not the
PMIC's 472 mA. Temporary termination in a low-FV thermal zone is never reported as normal full charge.

After full charge, battery charging is disabled while maintaining FV and system input. Raw SOC at 0.01% resolution is reconstructed using
the stock per-cell FCC percentage weights and integer arithmetic order; the full-charge stop vote is cleared only when raw SOC drops to 97.00% or below,
without using the integer percentage shown on the desktop. A real connection change also clears this vote, whereas ADSP
restarts, temperature-zone transitions, raising current limits, or leaving manual charge inhibition do not clear it automatically. Stock per-cell CO
isolation is not ported; charging stops for the whole pack without writing to battery data flash.

Temperature readings from both cells and the firmware battery pack must all be valid, using the minimum and maximum temperatures to pick the stricter
current and voltage limits. The PMIC separately handles slow charging below 8°C per the normal JEITA table while capping its own current at
3 A; charging stops below -9°C or at/above 55°C. Tighter limits take effect immediately,
while restoring higher voltages keeps thermal hysteresis: after entering the low-voltage zone at 48°C, recovery requires dropping below 46°C;
after high-temperature stop, PMIC recovery requires dropping below 53°C; after low-temperature stop, recovery requires rising to -8°C.
After a low-temperature charge-pump exit, retries require temperature above 8°C. Replugging the cable neither clears these thermal recovery conditions nor alters sensor readings.

The on-device-verified 2 A PMIC current sharing is retained to share heat with the charge pump, with both paths sharing the normal table's
total current budget. This is a project thermal-sharing policy. Verified stock single-pump logic in non-FFC mode
requires a final battery current target of at least 9 A and adapter type PPS (11) to allow parallel operation; otherwise it
suspends buck via an input current-limit vote. Because the normal 8 A policy never reaches that threshold, this project's
current sharing must not be called stock normal-table behavior, nor is stock buck input suspension copied verbatim.

The stock `single_cp_pmic` 4650 mV buck FV override applies only when the fuel-gauge fast-charge flag is non-zero,
the single-pump path is active, and the temperature zone still permits fast charging. Non-FFC paths drop this override; on stop, the
charge pump is shut down first before revoking the override and re-running the current FV election. This override is used solely for the low-voltage
sharing phase of FFC, never for normal mode or high-temperature charge stop.

Normal mode uses 4.060, 4.480, and 4.500 V, derated by the authenticated battery's aging state;
termination current is the stock normal value of 472 mA. FFC's compensated voltages and termination currents are checked separately, and every write
requires firmware acknowledgment bound to the current ADSP and connection generation.
Firmware FCC and ITERM reads are no-ops that return success without populating data, so a zero return cannot confirm a setting;
model `0x56`'s FV getter also uses twice the setter's step size, so its raw return value must not be treated
as the actual cell float voltage. PMIC enable/disable goes through the stock `0x2003` single-byte boolean interface,
whose getter actually reads the charge-enable bit. On read failures, confirm charge-pump stop first and then disable PMIC
charging—never substitute 500 mA for stopping charging. When entering a warm zone with the battery already above the
new float voltage, shut down the charge pump first as well. SC8541 uses this device's stock 4.800 V battery
overvoltage and 4.600 V alarm thresholds, retaining write-readback, hardware watchdog, and software timeout; alarms only
trigger interrupts and cannot replace software CV regulation. The 2:1 bus ceiling is restored to the stock `div_max_curr`
of 6 A (12 A battery-side), with bus OCP at stock 7 A. Third-party PPS follows the CN variant's
`third_pps_*` to cap adapter requests at 11 V and 4.1 A; this is the total adapter budget, from which the
charge pump also subtracts the PMIC input share and a 200 mA margin. The battery target subtracts an additional 200 mA from existing thermal-zone and
cycle limits, capped at 8 A. If stock adapter authentication is lost, stop the pump first and rebuild under
third-party limits rather than carrying over previous high-current requests. SC8541 bus overvoltage is set to stock
13 V, the alarm threshold stays at 10.5 V, and the software voltage window continues bounding actual requests.

Stock `support_mode=3` permits both 1:1 and 2:1. Initial capability selection compares
`min(div_max_curr, adapter_current × ratio)` across APDOs and chooses the mode with the highest battery-side capability, keeping 1:1 on a tie.
4.0.6 exposes 1:1 only to authenticated PD (type 12), requiring an APDO minimum voltage
at most 3.6 V and maximum at least 4.5 V; requests are additionally capped at 5.5 V.
Mode comparison caps 1:1 at 5 A, the single charge-pump path uses a 4.5 A limit, and the start threshold is
800 mA. Bus current conversion, PMIC share, pump-enable voltage, software voltage window, and CV regulation
all calculate using the active ratio while preserving dual-cell, source, temperature-zone, and user budgets. The five-field 1:1
step table uses 40 mV when the current delta reaches 2 A, and 20 mV otherwise.

SC8541 selects 2:1/1:1 via standard `charge_type` values `Standard`/`Bypass`,
allowing writes only after the pump is stopped and the delayed stop check completes, followed by reading back the mode bit. MCA requests adapter voltage only after
configuring mode and overcurrent protection successfully; if any step fails, the pump remains stopped. Piano does not enable
`support-cp-mode-switch`, so the ratio never switches dynamically mid-charge based on current. When entering 1:1 from 9 V
buck, wait for input voltage to drop into the target window before enabling the charge pump.

Normal screen-off continues charging. Before entering actual system suspend or shutting down the driver, confirm charge-pump stop first,
then confirm PMIC charge enable is off and issue FCC=0; if any step fails, reject suspend.
This is because software JEITA stops running while the host is suspended, and the full normal table cannot yet be delegated autonomously to firmware.
Stopping charging does not overwrite FV or ITERM; on resume, battery state is re-read and limits are configured for the current temperature zone
before re-enabling charging.

Parameters come from `batt_para_lwn`, `normal_volt_para*`, and normal `jeita_para` in this device's final Device Tree;
field parsing and CC/CV ordering reference public MCA implementations of the same architecture, with versions and
provenance recorded in the patch series manifest. Temperature hysteresis, taper-charge criteria, and firmware read/write interfaces were also cross-checked by disassembling
this device's stock charging proxy, JEITA, buck, and quickchg modules; topology enums and firmware property IDs differ from reference implementations,
so this device keeps Piano's dual fuel gauges and verified mappings.

### Quick Charge Input and Direct Charging

QC2 and standard QC3 use the buck path to request 9 V; QC3-B and QC3+ (firmware type
`HVDCP3P5`) can also enter SC8541 2:1 direct charging. Existing `hv_charge` and
`cp_charge` settings are reused without adding protocol-specific switches. Buck input ceilings are 1.2 A for QC2 and
1.6 A for QC3/QC3+; these are PMIC-path input limits, not charge-pump bus capabilities.

| Direct-Charging Type | Bus Voltage Cap | Bus Current Cap | Combined Two-Cell Current Cap | Voltage Step |
| --- | --- | --- | --- | --- |
| QC3-B | 9.5 V | 2.5 A | 5 A | 200 mV |
| QC3+ | 10 V | 2.1 A | 4.2 A | 20 mV |

Startup first confirms the pump is stopped, configures overcurrent protection, disables PMIC battery charging, and requests an adapter voltage
equal to twice cell voltage plus 300 mV. The stock 3-second ramp wait is retained, and the charge pump is enabled only after input enters the target window;
if the adapter does not respond, at most 40 checks are performed before stopping the pump and falling back rather than ramping voltage indefinitely.
At runtime, regulation uses charge-pump bus voltage, bus current, and the combined current of both fuel gauges, issuing at most one pulse every 700 ms.
QC3-B and QC3+ preserve their respective current, voltage-margin, and step-down count criteria
while honoring thermal-zone, cycle-count, user, and thermal limits. Pulses never exceed the voltage ceilings in the table.

Absolute voltage requests use 4.0.6's `0x2011` and pulses use `0x201d`, with step-up and step-down commands
`0x20` and `0x10` respectively—not the older public-source enum values, and `0x2018` is never written.
QC taper hands off to the PMIC once voltage is within 29 mV (QC3-B) or 24 mV (QC3+) below FV and current stays below 2.4 A
for 3.5 seconds. Exit confirms pump stop first and then requests 9 V for buck; if confirmation fails,
it stays stopped and retries without issuing PD/PPS requests. Suspend, unbind, and connection-generation checks
are shared with the existing direct-charging path. Source-to-stock module mappings are listed under `quick_charge` in the patch manifest.

### FFC Fast Charging

FFC requires an original adapter that passes MiPPS authentication, both fuel gauges passing battery authentication, both cell temperatures within
18–<48°C, and all six IIO temperature channels valid. FFC mode entry requires raw SOC below 95%;
charge-pump startup is additionally bounded by stock `enable_cp_para` SOC and final-stage voltage headroom limits.
Manual charge inhibition, full-charge stop, and thermal throttling still apply independently; FFC does not bypass thermal limits to raise ceilings.

| Stage Voltage | Combined Two-Cell Current Cap | Stage-Transition Current |
| --- | --- | --- |
| 4.144 V | 12.40 A | 10.65 A |
| 4.300 V | 10.65 A | 9.04 A |
| 4.475 V | 9.04 A | 7.23 A |
| 4.540 V | 7.23 A | 2.350 A for 18–<35°C, 3.164 A for 35–<48°C |

The first stage skips CV per the stock stage mask. Cell FV is 4.550 V, derated by cycle count by
0, 20, 40, or 50 mV (for ≤100, ≤300, <1000, and ≥1000 cycles); between 200 and 800 cycles with
capacity health below 85%, an extra 40 mV is subtracted. Above 1000 cycles, per-stage currents are also reduced. Stock
`drop_fcc_para` subtracts 0, 500, and 1000 mA for <300, 300–799, and ≥800 cycles respectively,
acting alongside thermal and user budgets.

Charge-pump startup is further tightened per stock `enable_cp_para`: for <300, 300–799, 800–1199, and
≥1200 cycles, SOC ceilings are 90/85/80/70% and final-stage voltage headroom floors are
70/90/200/250 mV; at 300 cycles and above, an additional 20 mV of headroom is added. The 95% raw SOC
FFC mode entry criterion remains intact and is evaluated independently.

Below 4.480 V, stock single-pump PMIC current sharing may be used with a base FV override of 4.650 V plus
40/45 mV buck temperature compensation; PMIC sharing stops upon reaching 4.480 V. Taper charging uses cell FV
plus the same temperature compensation, with software CV stepping down 5 mV at a time up to 25 mV. On temperature out-of-range, authentication loss,
sensor errors, or session changes, confirm both charge-pump stop and PMIC charge stop before switching modes on the two fuel gauges.
If either gauge mode write fails, the driver attempts to restore both to normal mode and does not raise charging voltage until confirmed.

Once FFC taper finishes during a connection, FFC is not re-entered during that same connection; normal full-charge and 97% raw SOC recharge keep their existing
logic. Implementation and parameters come from this device's final Device Tree, stock charging modules, and active BAA configuration, summarized under
`authenticated_ffc` in the patch manifest. These are charging-policy parameters; builds or transaction regression tests cannot
substitute for measuring adapter input power and real charging curves.

Although 4.0.6 parses and stores `non_mi_pps_max_curr` into working-mode parameters, the active limiting
branch requires `support_pps_100w` to be enabled. On Piano this value is 0, so the CN variant's
`third_pps_*` remains in effect; the 100 W policy is not enabled, nor are the Global variant's 10 V / 5 A parameters applied.

PPS voltage regulation uses the stock five-field `vstep_para` (conversion ratio, cycle-count ceiling,
battery current delta, cell voltage headroom, and 20 mV step multiplier). For 2:1, current deltas reaching
600/1000/2000/3000/4000 mA use steps of 40/80/140/160/200 mV respectively,
and 20 mV for smaller deltas. Step-ups during CV, at ≥800 cycles, or within 100 mV of the final-stage target
all use 20 mV, and large steps in the 8–<13°C zone are capped at 80 mV. Both bus and battery current
must still satisfy step-up conditions; CV overshoot backs off based on cell voltage error by at most 100 mV per step
without bypassing source, current, or temperature limits.

Stock DTPT battery alerts are conveyed through the two fuel gauges' standard `HEALTH` property. Board-level
`ti,health-alerts` enables the stock extended read-only byte registers SOA `0x70` and ISC `0x72`;
when SOA is non-zero or ISC is 3, `Unspecified failure` is reported and MCA evaluates both cells.
The direct-charging battery target drops to 80% of the current table value minus 200 mA, still subject to cycle, thermal,
user, and source caps. Alert read failures pause charging rather than treating the failure as a cleared alert;
fuel-gauge data flash is never written, and no private charging properties are added.

### Standard Charge Current Limiting

MCA's `charge_control_limit` sets the combined charging cap across both cells in microamperes, ranging from
`100000` to `12400000` in steps of `1000`, where `0` stops battery charging without disabling external power input.
`charge_control_limit_max` exposes the upper bound. Normal PMIC and charge pump share a single budget
rather than each taking a separate allowance; supply capability, JEITA, and hardware protections can still tighten it further.

The board Device Tree provides descending limit steps controlled by the standard `piano-charger` thermal cooling device.
When stepping down, the driver confirms charge-pump stop first, tightens PMIC current next, and then decides whether to restart the charge pump
under the new combined budget. Communication failures retain the stricter requirement and return an error for the existing
charging work item to retry; suspend, shutdown, and limit transitions are serialized with no extra userspace daemon.
Restoring limits reconfigures charge-pump protections and lifts only this single restriction—it does not clear existing hardware faults or
taper-charge stops, bypass thermal protections, or alter normal-table voltages.

External cooling steps come from this device's `wired_thermal` direct-charging column, adding a `0` stop state and a
12.4 A unthrottled state. The normal table still never exceeds 8 A. The kernel simultaneously runs the Global stock high-load charging policy from the same device
using its screen-on tiers, without depending on Android's app-scene or screen-state selector and without merging the normal
scene curve. The effective limit is the minimum of both constraints and any manual limit.

| Model Trip-Up Threshold | Cooldown Clear Threshold | Stock Tier | Pack Ceiling | 5 V Buck FCC | 9 V Buck FCC |
| --- | --- | --- | --- | --- | --- |
| 35°C | 32°C | 1 | 10.5 A | 1.8 A | 3.5 A |
| 37°C | 35°C | 2 | 9 A | 1.8 A | 3.5 A |
| 39°C | 37°C | 8 | 4.4 A | 1.8 A | 3.5 A |
| 40°C | 39°C | 9 | 3.6 A | 1.7 A | 3.5 A |
| 44°C | 42°C | 12 | 2 A | 1.2 A | 2 A |
| 45°C | 44°C | 15 | 0.7 A | 1 A | 0.7 A |

The pack ceiling remains bounded by the active normal or FFC table, and the PMIC is constrained by sharing, thermal zone, and pack budget,
so even at 5 V the highest tier never exceeds 0.7 A. Buck chooses its FCC column from measured input voltage,
and charge-pump sharing uses the same limit; this skin-temperature policy does not separately reduce system input ICL.
JEITA, cell voltage, and hardware protections continue operating independently.

During normal charging, when the highest measured cell temperature is 48–54°C, PMIC float voltage adds 30 mV per stock calibration,
writing 4090 mV in the warm zone. JEITA charge-stop and recovery thresholds still use the uncompensated
4060 mV target rather than rising with hardware compensation; FFC uses separate compensation rules.

The skin-temperature model reads raw temperature register `0x1e` from both BQ27Z561 gauges together with four board thermistors: quiet,
charger1, Wi-Fi, and CPU. Existing use of gauge `TEMP` register `0x06` is unchanged.
Stock cold-zone logic uses the lower of the two raw cell temperatures and warm-zone logic uses the higher; the model keeps weights
`500, 131, -9, 89, 232`, denominator `1000`, and fixed offset `1600`.
Standard IIO exposes millidegrees Celsius, so the model internally converts the four board temperatures back to the Q10 units used by the original calibration.
Board-level offset keeps the public stock kernel default of `0` without introducing Android userspace
runtime overrides; uncollected device-specific overrides are not claimed to be zero.

`piano-skin` is this model's virtual thermal zone, not an added physical surface thermistor. If sensors are missing,
fail to read, or raw temperatures are out of range, battery charging stops while preserving input, FV, and tier hysteresis; charging resumes conditionally
once valid readings return. The thermal zone refuses to report samples older than four seconds. Board ADC keeps existing thermistor
input states, and PMIH010x GPIOs match this device's stock standard 18-pin layout without altering charging
mode registers. The runtime charging state machine stays in the kernel; power-off charging continues using the stock boot chain.

### USB-C Port Overheat Protection

MCA reads the two stock port thermistors via IIO and uses the hotter channel. Protection triggers when the port reaches 65°C,
reaches 60°C while board temperature is at most 50°C, or reaches 35°C with a temperature rise rate of at least 4°C/s.
Board temperature reuses the five-channel model from stock `VIRTUAL-SENSOR` on the same device; if board temperature cannot be read,
the 60°C threshold is used. If either port thermistor cannot be read, isolation is triggered as well rather than continuing to charge on stale readings.

When a protection threshold is reached or port temperature is unreadable, the driver isolates the port via PM8550 GPIO9 first, then
stops the charge pump, PMIC charging, and USB power delivery. ADSP communication timeouts never delay an already-triggered isolation.
After isolation, tablet-side VBUS readings verify the result. The MCA USB supply reports overheating via standard `health`.
Clearing isolation requires UCSI to confirm cable removal. ADSP CID, OTG, or VBUS still above the isolation threshold blocks recovery; low bus voltage serves only to cross-check UCSI's unplug result.
Overheat protection clears only after unplugging, exiting OTG, port temperature dropping below 55°C, and rise rate falling below 4°C/s;
any cabling or GPIO write failure during recovery keeps charging stopped.

While plugged in or unable to confirm unplugging, the screen may turn off, but system suspend is blocked so temperature monitoring continues.
Once unplugging is confirmed, port power is isolated before suspend, and temperature is re-measured on resume before deciding whether to restore power.

### Input Recovery and Charging Stop

Every charge-pump startup recalculates targets from the current APDO, normal temperature zone, and combined charging budget,
configures the corresponding bus overcurrent protection after confirming the pump is off, and only then enables the pump. A single input loss
does not prove the adapter is weaker than its APDO, so pre-drop current minus a fixed margin is no longer latched permanently as an
input cap. When actual VBUS offline is observed, normal policy can be rebuilt within the same connection; after 11 cumulative non-authenticated
dropouts, charging falls back to buck-only until a real connection change clears the counter. Stock uses the same
threshold across broader charging errors; here, other hardware faults latch stopped directly and never auto-restart.

Temperature zones, user charge inhibition, and current limits are recoverable policy pauses; hardware faults and taper completion stay
stopped. If stop confirmation fails, the original recovery ownership is preserved, and switching back to `auto` before confirmation completes
does not lose the user's inhibition ownership. Ordinary VBUS fluctuations, ADSP restarts, suspend, or raising current
limits never clear an existing fault. A new startup attempt does not inherit fault-recovery authority from a previous pause.

Both a zero charge limit and user charge inhibition continue maintaining normal JEITA FV, valid source negotiation, and input current limits.
After stopping the charge pump and confirming shutdown, buck takeover finishes within the same poll cycle; before switching input voltage, the driver ensures the
input current cap satisfies both pre-switch and post-switch supply capabilities, skipping the voltage switch if tightening ICL fails;
when stepping back down to 5 V, current is never raised before confirmation, and failures retry only the step-down.
PPS regulation still sends unchanged request values, matching stock HLOS request semantics; firmware itself also implements a
PPS timer/ping, so natural renegotiation cannot be attributed solely to missing host keepalives.

When bus voltage, bus current, input current limit, or source type reads fail, both charge pump and PMIC charging stop first
without interpreting failed reads as zero, a real disconnect, or a new authentication session. No new voltage boost or direct charging is allowed while authentication state is unknown;
if an ADSP session change occurs during active charging, confirm pump stop first and rebuild configuration under the new session
without reusing old APDOs. Work items discard cached host current and voltage values across ADSP generations and connection changes
so late stale results never mask firmware default limits; user requests, faults, and dropout counts each keep their own ownership.
Cell temperatures, voltage hysteresis, and hardware protections continue operating independently.

### High-Temperature Charge Stop and System Power

On stock Android at room temperature with FV at 4480 mV, charging remained stopped for roughly 178 seconds with both fuel gauges
staying at 0–6 mA throughout; during the first and last 12-second load intervals, USB input was roughly 2.88 W and 3.02 W respectively.
Both cells' full-charge flags remained 0 across the entire capture, and charging current resumed both before the stop and after lifting the restriction,
so this result is not limited to full-charge termination. The capture lacks exact SOC, so battery percentage cannot be backfilled from it.
This supports preserving external system power at normal FV, though there is no evidence the battery is physically disconnected. Power delivery at low FV
remains unstable, and real high-temperature on-device conditions have not yet been verified.

Piano's stock proxy exposes charge enable `0x2003`, battery charge current `0x2005`,
input current limit `0x2008`, and input suspend `0x2019`; enable and suspend take single-byte booleans,
while currents take little-endian 32-bit mA values. Stock JEITA's `jeita-hot` clears the charging vote without suspending input.
Provenance follows `normal_charging` in the patch series manifest; property IDs from reference devices cannot be substituted.

In 0079, `piano_mca_normal_buck()` still returned `-ERANGE` after successfully disabling charging when a temperature zone required charge stop,
causing `piano_mca_apply_policy()` to skip 9 V negotiation and input current-limit
recovery, potentially leaving behind a 500 mA direct-charging cap or 1500 mA parallel-phase input cap. Successful charge stop
should continue input policy, while communication and stop failures still return errors; however, this omission alone does not explain input dropping to zero.

Stock normal JEITA has an additional charge-stop vote for zones whose table FV is below 4201 mV: charging stops when the higher
of the two cell voltages reaches `FV - 5 mV`, resumes only below `FV - 150 mV`, and holds the existing vote in between;
the restriction clears when table FV returns to 4201 mV or above. For the 4060 mV zone, this means stopping at 4055 mV,
resuming below 3910 mV, and holding at exactly 3910 mV. Stock stops charging first before lowering FV;
evaluation uses the table FV, without `smart_fv` reductions participating in the zone check.

0079 lacked this voltage stop and recovery vote. When a battery around 4.39 V entered the 48–<55°C zone,
the PMIC would still drop to 4060 mV first, then set current and enable charging; charge-pump exit and restart did not share
this state either, potentially restarting prematurely in the 4000 mV hold band. Adding these conditions fixes normal
charging-table execution, but does not guarantee continuous external power to the system at low FV.

When temperature jumped directly from room temperature into the ≥55°C stop zone, the normal buck path also returned early before updating FV,
potentially retaining the previous 4480 mV. Even though an earlier research prototype added voltage-stop hysteresis,
that early return still skipped state updates: dropping from 56°C to 52°C with cell voltage at 4000 mV
could resume charging before voltage fell below 3910 mV. Thermal-zone stop and FV updates must be handled separately:
after confirming stop, continue applying 4060 mV and updating voltage hysteresis; if an FV write fails or its ACK is lost,
keep charging stopped and do not raise input or enable charging. An earlier test mistakenly accepted retaining
4480 mV at high temperature and has been corrected against the stock table. These fixes enter the formal patch series in 0093; real-driver
UML regressions cover thermal zones, voltage hysteresis, communication failures, and user inhibition, but cannot simulate PMIC electrical power-path behavior.

Negative-temperature conversion requires aligning the entire input chain separately: for the same input of -90 or -99 (in 0.1°C units),
stock JEITA produces -10°C whereas Linux `/ 10` produces -9°C; however, stock quickchg also truncates
toward zero, so JEITA's negative-value handling cannot be applied globally. Stock fuel-gauge raw temperature words subtract 2730 while Linux
subtracts 2731, and stock additionally aggregates and compensates temperatures. Ignoring compensation, the same raw word 2641 yields -9°C through both existing
paths, so changing Linux's rounding in isolation would actually diverge. Local conversion differences therefore do not yet
prove an on-device threshold error, and the existing implementation is kept for this cycle. Thermal zones use fuel-gauge and firmware battery-pack readings, not SoC temperatures.

`0x2003` is a composite operation whose getter reads only CHGR `0x2646 bit0`; disabling also sets
bit1 and clears bit0 of `0x2c42` and writes 4 to `0x2641[2:0]`, while the enable branch writes 5.
Targeted readbacks confirmed that both normal FV 4480 mV and low FV 4060 mV keep the charge bit at 0,
the low three mode bits at 4, and `0x2c42=0x52`; of the four checked items, only the FV encoding changed (`0xb0` → `0x5c`),
remaining identical across low-FV idle, load, and unload. Linux now sends the full boolean set operation every time
instead of skipping DCDC configuration when the single enable bit already matches; even a successful stock vote write does not guarantee a low-level ACK.
The getter still cannot verify all mode fields. Nevertheless, because both enable and mode bits were already in place during the low-FV counterexamples,
the failure cannot be blamed on unexecuted charge stop. Neither an ACK nor a single bit confirms
external system power, and undocumented modes are not labeled as bypass or battery isolation.

At room temperature, setting the FV register to 4060 mV—or to 4090 mV after partial stock warm-zone compensation—both
reproduced USB input dropping to zero under sustained load, with
each cell discharging at roughly 0.32–0.35 A; requesting charge stop beforehand produced the same counterexample. Suspending and resuming
input current limit, setting FCC code 0, and re-running the charge-stop callback all failed to restore external power under that load.
Removing the load while keeping low FV recovered in some trials, though not always within a few seconds. Consequently,
pre-stopping charging, resending commands, or raising ICL cannot be treated as confirmed recovery methods.

Switching to roughly 8.9 V actual input still reproduced zero input at 4090 mV FV; hardware ICL
read back as 1600 mA and input suspend was not asserted. The fault is therefore neither limited to 5 V supplies
nor explainable solely by cached input limits.

Another trial that requested both charge stop and FCC code 0 before lowering FV maintained external power across an approximately 12-second load
sampling window. Because state and timing differed from earlier failures, causality was not established;
a subsequent research module attempted to write `SYS_MIN` over physical SPMI and triggered a kernel crash, halting power-path experiments.
The previous-boot log preserved in the stock diagnostic bundle explicitly recorded `run_pulse` entering `pmic_arb_write_cmd` via
`regmap_update_bits` and taking a synchronous external abort at the data-register write; the module Build ID matched the research binary, and the subsequent command commit never executed.
Availability of a read-only channel does not prove a physical write window is accessible; exact execution-domain or security permissions still need verification.
An earlier inference that no write was attempted based on missing success output was wrong: in shell scripts, `set -e` does not exit
when the left-hand side of `&&` fails. Direct physical-write prototypes and scripts are now disabled.
After reboot, FV reduction was 0, stock full-charge policy operated normally, and the research module was unloaded; stock diagnostic bundles
preserve actual crash evidence, whereas stale UEFI content in current pstore cannot substitute for the previous-boot log.

0093 also exposes `auto` / `inhibit-charge` via standard `charge_behaviour`.
User mode switches are serialized with suspend and driver removal; inhibition requests persist across communication failures while continuing to maintain thermal-zone
FV. Restoring `auto` re-evaluates normal policy without forcibly enabling charging; only a charge pump stopped and confirmed
stopped by this request can re-enter policy through it, while existing fault latches remain active.
Readback reflects the requested mode—it does not promise physical battery disconnection or recovery from abnormal input loss, nor does it hold a higher stale FV to keep external power alive.

The formal test entry point is selectable only under UML, running real-driver regressions against mock transports and power supplies
without touching tablet hardware; ARM64 production configs exclude it. No project-specific switches or
rootfs daemons are added; on-device verification still needs to cover physical power paths, the full temperature chain, and composite enable readbacks.

Piano uses the PMIH010x/PM8750BH family and cannot directly reuse PM8550B suspend bits;
verified MCA properties have no usable standalone system-power-floor write entry point, and writes to `0x200c` have no effect.
Stock PmicDxe resource construction, architecture dispatch, and get/set paths instead point to CHGR
`0x2640[3:0]`; on-device resource subtype `0xba`, CHGR subtype `0xbe`, and
architecture byte 0 match this path. The 16-step `SYS_MIN` table stops incrementing by 100 mV at the top end,
with its final four steps at 4000, 4200, 4400, and 4600 mV; current code 4 corresponds to 3000 mV.
No register write succeeded over the physical channel above. The firmware proxy described below completed both a same-value writeback and a
brief boost trial; the boosted value read back successfully, yet failed to restore external power at low FV, so bypass is not achieved.

Stock provides another firmware-forwarded register proxy using GLINK owner `32783` with read and write opcodes
`55` and `56` respectively; the existing proxy already returns the PMIC type byte read-only. ADSP configuration data
in `adsp_dtb.b01` originally sets `gen-test-interface = [02]` in platform config, and overlay configs
do not override it; matching firmware write handling permits value 2, routing access through firmware rather than HLOS
writing the physical window directly. Configuration was left untouched. The stock proxy logs non-zero write responses and waits until timeout
without transaction IDs, so errors, late replies, and recovery ordering must be handled explicitly; successful reads and static
permissions never substitute for live testing. With user approval, a temporary module ran read-only prechecks and a single 3000 mV
same-value writeback; firmware returned success, independent readback stayed at code 4, boot ID did not change, temporary modules and
files were removed, and stock full-charge policy remained normal. This confirmed the firmware proxy accepts the write request,
without proving the electrical effect of raising the system floor.

With separate explicit approval, under roughly 4.95 V actual input, charge stop, FCC code 0, and FV
4090 mV, load-induced zero input was first confirmed, after which the firmware proxy changed `SYS_MIN`
from code 4 to 15 and restored it to 4 after roughly 8.06 seconds. All 10 samples during the window read back 15,
USB current stayed at 0 throughout, the two cells discharged at averages of roughly 373 and 381 mA, and VPH averaged
roughly 4.385 V without rising to 4.6 V. After restoring `SYS_MIN`, input remained absent at low FV
and reappeared only after restoring FV to 4480 mV. One VPH ADC sample during recovery
read 3.938 V with adjacent samples around 4.418 V; a single sample cannot distinguish a real transient from a
sampling glitch, and its duration is unconfirmed. In 120 follow-up control samples taken under untouched stock settings,
two VPH readings around 4.04 V also appeared while simultaneous VBAT ADC and both
fuel-gauge voltages stayed near 4.42 V. VPH uses PMK8550's ADC5 Gen3 rather than GLINK
ADC; these dips are not unique to the experiment and their cause remains unproven. The device did not reboot and thermal protections were untouched.

This result rules out raising `SYS_MIN` alone as a recovery method under these conditions; it does not cover
9 V input, pre-setting `SYS_MIN` beforehand, or real high temperature, and cannot be generalized to mean the register does nothing.
The trial's recovery branch retried only the original value without re-boosting; on error or proxy loss, charge stop remained enforced,
though recovery within 8 seconds cannot be guaranteed under timeouts. This debug proxy is not a verified production interface.

Afterward, independent checks confirmed `SYS_MIN` at 3000 mV, FV at 4480 mV, and FCC at stock
3000 mA; the trial's charge-stop vote returned to neutral 1, the temporary FCC vote was disabled, and all forced
votes were 0. Stock's full-charge vote remained 0, so charging staying off matched stock policy. The voter
summary's effective client name may still display the trial client name and should not be mistaken for an unreleased constraint—check
each individual client's value instead. Temporary modules and uploaded files were removed, and debugfs was restored.

Suspending and resuming input is not equivalent to an AICL restart. Matching firmware property `0x2014` issues a single command to
`0x2c50 bit1` via the DCDC callback; that callback does not touch `SYS_MIN`, FV, charge
enable, or thermal protections. `0x2013`'s rerun involves additional state and control steps, so the two must not be conflated.
The stock interface discards restart callback errors, so an ACK alone does not prove completion. With separate user
approval, under roughly 4.95 V actual input, FV 4090 mV, FCC code 0, charging disabled, and input already
lost, one such command was issued via the firmware proxy; firmware returned success and the command byte subsequently
read back 0. Across 10 samples over the next 13.18 seconds, USB current remained 0, the two cells
discharged at averages of roughly 375 and 381 mA, VPH averaged roughly 4.383 V, and `SYS_MIN` stayed at 3000 mV.
Input returned only after restoring FV to 4480 mV. This command is likewise insufficient to restore external power under these conditions;
completion of all AICL stages was not proven, and pre-set ordering, 9 V, and real high temperature remain untested.

Hardware ICL feedback changed from code `0x0c` before the command to `0x0a` afterward, and settled at
`0x0b` after restoring stock FV; the stock software ICL vote stayed at 1600 mA. Because feedback is an automatic runtime result,
one cannot claim every hardware status register returned to its exact pre-trial value. FV, FCC, and trial votes were
independently verified restored, the command byte read 0, both fuel-gauge currents read 0, the device did not reboot,
temporary modules and uploaded files were removed, debugfs was restored, and no production interface or driver patch was added.

Keeping the charge bit off, FCC code 0, FV 4090 mV, and `SYS_MIN` 3000 mV,
another approved trial changed only `0x2641[2:0]` from 4 to 5 and `0x2c42[1:0]` from 2
to 1, then restored both. These are the field values used by the matching firmware's normal-enable branch, not a known
bypass enum. Start and restore logs were separated by roughly 8.10 seconds; all 9 samples in between read back the test values,
USB current stayed 0 throughout, the two cells discharged at averages of roughly 371 and 353 mA, and VPH averaged roughly 4.383 V.
Restoring the fields still yielded no input, which returned only after restoring FV to 4480 mV. This combination is also insufficient
to restore external power under the tested 5 V room-temperature conditions. The device did not reboot, mode fields, FV, FCC, and votes were
independently verified restored, and temporary modules and files were removed; initialization order, 9 V, and high temperature remain untested.

Stock `smart_bypass` is a policy that selects FCC based on SOC, board temperature, and similar inputs: its worker votes on
`smartchg_fcc_voter`, whose `set_fcc` callback feeds into the normal charge-current voter, and passing 0
revokes that limit vote. Nothing in the verified call chain independently switches system power or isolates the battery.
`smart_bypass_support = <0>` in the Device Tree means this policy is disabled; setting it to 1 will not grant
the desired bypass capability. Security Center in installed `OS3.0.304.0.WPYMIXM` contains SideRoad and
Bypass Charging Plus code, but `persist.vendor.smart.bypass.plus` is empty,
so the battery-protection entry is hidden and the service does not initialize the SideRoad manager. That manager sends a
bit 10 request to `smart_chg`, which ends up in the FCC policy above; merely exposing the UI entry neither
proves the underlying capability works nor serves as evidence from current ROM defaults that hardware lacks support.

`/sys/devices/platform/soc/soc:smart_charge/smart_night` and
`/sys/class/xm_power/charger/smart_charge/smart_night` are the same node, currently 0.
Stock handling of any non-zero request fixes the target at 80%; the argument itself is not a battery percentage.
When not overridden by a higher-priority policy, charging pauses at 80% SOC, clears the pause at 75% SOC,
and holds state in between. Writing 0 removes only this policy without overriding other stop conditions.
[HyperSmartCharge](https://github.com/buffcow/HyperSmartCharge) sets custom SOC thresholds via
`IMiCharge.setMiChargePath("smart_chg", …)` using bit 4; together with `smart_night`, it goes through the same SOC worker and
`set_soc_limit_sts` callback into the `soc_limit` charge-enable vote. The normal wired path also
votes 0 on `input_voltage`, targeting 5000 mV input; this voter sits at struct offset
`0xe8`, confirmed by both matching stock BTF and probe callback bindings, and is not a charge-pump enable voter.
This does not represent a separate PMIC bypass mode, nor does a software target alone reveal actual adapter voltage.
Both paths exist in this device's stock kernel; this cycle inspected only code and sysfs nodes without issuing control writes or loading modules.
They provide stock charge-stop hooks, but do not yet prove external input continues powering the system at low FV and high temperature.

On the same platform, OnePlus's
[PLC implementation](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8750/blob/1328964c65419be8d970f05cd61f15de5cc81cd8/vendor/oplus/kernel/charger/v2/oplus_chg_plc.c)
stops charging on the buck path while keeping input unsuspended and also sends separate OEM firmware state via HAL;
any correspondence between those commands and Piano's MCA is unconfirmed, so property IDs cannot be ported.
SC8541's `bypass` mode is 1:1 direct charging from adapter to battery—see the
[vendor EVM user guide](https://www.southchip.com/rest/assets/2226fc0b-727f-4cc2-a865-63ed16233dc1/SC8541_EVM_User%2520guide_20241217.pdf)—which is different from powering the system while battery charging is stopped.

Under real high temperature, battery thermal-zone stop, USB port overheat, and power-device protections must still be distinguished;
maintaining input after hardware safety limits trip cannot be required. Verification must simultaneously observe input power tracking load changes, individual currents of both cells,
and system voltage, plus cooldown, replug, and suspend/resume; current findings do not indicate that high-temperature external power
is working on Linux.

### Power-Off Charging

Piano's stock bootloader requires the little-endian 32-bit `power_off_mode` field at offset `0x1000` in the `charger` partition
to equal 1 in order to enter power-off charging after a USB-power wakeup. On stock Android,
`MiuiBatteryServiceImpl` sets this field via the charging service during user shutdown broadcasts;
`svc power shutdown` does not pass a user-initiated shutdown reason and cannot substitute for testing via the power menu.

MCA handles `SYS_POWER_OFF` on `xiaomi,piano` before device shutdown. Both when starting the transaction and immediately before writing,
it re-reads VBUS and OTG within the same ADSP and USB connection cycle and requires UCSI to report
the tablet as connected in the sink role. The power-off charging flag is updated only when all of these conditions hold simultaneously.
This is an explicit exception to the read-only rule for stock partitions: only those four bytes may be set to 1, no arbitrary write
interface is exposed, no disk write occurs during normal charging, reboot, suspend, or unbind, and zero-speed, protection policies, and partition headers are never modified.

The driver selects the target by the unique GPT label `charger` and valid partition UUID under the internal SM8750 UFS controller,
without relying on `/dev/sdX`, partition numbers, or a device-specific fixed UUID. After opening exclusively,
it performs a read-only check of the stock magic, version 2, initialized state 0/1, and available state, then read-modify-writes the single
logical block containing the flag while preserving every other byte. Before writing, it re-verifies the partition header, original block, and power state,
then flushes to disk and reads back the entire block for comparison. Unsupported formats or a busy partition skip writing; I/O errors halt further operations and log the failure.
Remaining connected to USB power may boot Android, while unplugging stays powered off. The stock bootloader consumes the flag and resets it to 2.

Stock module, system service, and format evidence is recorded under the `poweroff_charging` entry in the patch series.
Kernel KUnit tests call the driver's read-modify-write transaction directly, covering block boundaries, neighboring fields, repeated calls, unplugging,
and read, write, flush, and readback failures; these tests do not replace on-device power-menu and USB-powered shutdown checks.

### Battery Pack and Status Notifications

Each of the two parallel cell banks has its own BQ27Z561 fuel gauge. `piano-battery.dtso` marks them as
`ti,cell-monitor`, retaining raw current, voltage, and temperature for charging policies while
`piano_battery` exposes the single system battery. Pack current and charge are summed, voltage is averaged,
and SOC is weighted by each bank's full-charge capacity; charging/discharging status is determined by combined net current and full-charge flags.
Internal gauges no longer register separately as system batteries, so UPower and GNOME display a single
unified battery pack with no desktop patches or filter rules needed.

For both gauges, `current_now` reads measured current from `0x0c` and `current_avg` reads
filtered current from `0x14`, both reported in signed microamperes. The system battery aggregates each pair of readings separately;
charging/discharging status and MCA policies continue using filtered current, leaving stop, recharge, and fast-charge thresholds unchanged.

Section 12.1.15 of the [TI Technical Reference Manual](https://www.ti.com/lit/ug/sluuc54c/sluuc54c.pdf#page=80)
specifies that standard BQ27Z561 `AveragePower()` reads `0x22/0x23` and returns signed milliwatts
(positive for charging, negative for discharging). Piano's HyperOS 4 gauge driver reads two different registers instead,
reporting `0x7b` as `power_deviation` and `0x89` as `action_power`.
Deviation is not average power, and neither the unit nor averaging window of `action_power` has been verified,
so standard command scaling cannot be assumed.
On the tablet, `power_avg` returns `ENODATA` on both gauges while voltage and current
remain readable; charging regulation does not depend on this power field.

Before enabling FFC, both fuel gauges must also pass battery authentication—charger MiPPS authentication must not be confused with
battery authentication. The BQ27Z561 driver uses the stock 32-byte random challenge and HMAC-SHA256 mailbox,
exposing results through the standard read-only `authentic` property; the battery pack reports success only when both banks pass.
Vendor public verification parameters come from the board Device Tree; other BQ27xxx
devices without that property keep their original measurement behavior. The stock `batterysecret` algorithm has been verified with fresh challenges on both `lwn`
cell banks of this device, and the driver rejects userspace attempts to write authentication success.

Gauge FFC mode uses stock `0x003e`/`0x003f` commands with checksums, controlled via standard
`charge_type`; enabling requires successful authentication, communication errors and resume clear cached results,
and suspend or driver removal restores normal mode. This property records the last successfully sent mode command and, as in stock,
is not a hardware-bit readback. The charging policy switches between normal and FFC modes through this standard property, under the entry
conditions and voltage/current bounds described above; provenance and on-device binary hashes are in the patch series manifest.

If the MAC write to disable fast-charge mode fails before suspend, the driver returns the original error and immediately rearms gauge
polling. Failed devices do not receive a resume callback during system suspend rollback, so recovery cannot depend on the next read
or plug notification. UML regressions directly invoke the real PM callbacks and MAC setter,
covering command-frame failure, checksum-frame failure, clean disable, and devices without authentication parameters.

On shutdown and reboot, the BQ27xxx driver waits for any in-flight reads or writes to finish before stopping polling.
Already-queued power-supply notifications may still run, but will not access the shut-down I²C bus through the gauge.
This path alters neither power-off charging configuration nor runtime current readings.

Being plugged in only indicates external power is online and does not guarantee input is sufficient to power the system. Whenever measured current direction
changes, MCA notifies the gauges through existing power-supply links, and the gauges notify the battery pack; starting to charge after authentication or voltage adjustment
does not wait for slow background gauge polling, nor is actual negative current displayed as charging.

### MiPPS Authentication and Connection Recovery Verification

Parameter comparison tests change only one parameter per boot and keep boot files, modules, and root configurations synchronized.
Products share the initial boot image (ESP), where SimpleInit boots automatically after 5 seconds of inactivity; press a key or touch the screen
to stay in the menu. If a boot fails, enter Android from the preceding Android/UEFI selector
and update the boot image (ESP) without modifying stock system partitions.

Piano has only one USB-C port. When testing direct connections to an original charger and cable, ensure Wi-Fi SSH and
persistent journald are working first and collect data over Wi-Fi; losing USB serial and USB networking upon unplugging from a computer is
expected. A PD dock cannot substitute for direct charger authentication.

On every boot, record the source revision, `uname -r`, `/proc/sys/kernel/random/boot_id`,
`/proc/cmdline`, and the live values under `/sys/module/piano_mca/parameters/`. Keep the charging policy parameters
`charge_policy=1 hv_charge=1 charge_current=1 pan_ack=0` unchanged.
`mipps_auth` and `cp_charge` have `0444` permissions; set them in the distribution's module parameter configuration,
update boot entries, and reboot for changes to take effect—do not write sysfs or unbind MCA to toggle them live.

| Stage | Procedure | Pass Criteria |
| --- | --- | --- |
| Baseline regression | `mipps_auth=0 cp_charge=0`; verify desktop, touch, Wi-Fi, USB, suspend/resume, and normal PD charging for 15 minutes | No unexpected reboots, ADSP faults, or USB role failures; `authentic=0`; normal charging works |
| Authentication | Next boot, change only `mipps_auth=1`; connect directly to a known MiPPS-capable charger and cable across 3 independent attachments | Each attachment ends at `authentic=1`, `xiaomi/pd_verifed=1`, `xiaomi/verify_process=0`; no duplicate authentication within the same attachment |
| Normal PD negative test | Switch to a PD/PPS supply known not to support MiPPS | `authentic=0`, `verify_process=0` after the attempt finishes, normal PD/PPS still charges |
| Rapid replug | Unplug/replug with <3 s intervals for 10 rounds; cover replug after success, unplug mid-authentication, and immediate switch to MiPPS after normal PD | UCSI logs connect changes; every new attachment re-authenticates without reusing stale results, attempt flags, or old CP/HV policies |
| Supply self-reset | Observe natural behavior with a MiPPS supply that self-resets after authentication | Proof is retained on VBUS/PD-only resets without a connect change; if CC physically disconnects, re-authentication runs |
| Direct charging | After all prior stages pass, change only `cp_charge=1` on a new boot with default current caps | SC8541 enters and exits cleanly, stops on unplug, and never carries a prior supply's direct-charging state to a new attachment |

Direct-charging startup requires battery voltage at least `3.5 V` and below the active normal-table CV target, with all temperature readings
inside their windows and satisfying recovery hysteresis; direct charging is not required to engage when these conditions are not met. Low-temperature or fault-reproduction
tests may explicitly set `cp_ibus_max=4000` and `cp_ibat_max=8000` instead of release defaults;
actual input remains bounded by PDO, battery state, and temperature, so results cannot be judged by charger nameplate wattage or desktop
`Charging` labels.

Before enabling SC8541, the driver writes and reads back protection thresholds and a 5-second hardware watchdog while keeping a 3-second software communication-loss
stop. Per Section 9.2.6 of the [Southchip SC8541 datasheet](https://www.southchip.com/rest/assets/8fa17c3a-a4a3-4044-a7dc-3fd3c2f56767),
any register read or write resets the hardware watchdog, so continuous ADC polling cannot
prove hardware timeout behavior when the control thread stalls. After stopping the charge pump, the driver waits 500 ms, stops again, and reads back
`CHG_EN`; if confirmation fails, it keeps retrying stop, stops requesting high voltage, and fails suspend.
Voltage regulation binds to the ADSP and connection generation captured at direct-charging startup, and the minimum and maximum of the two cell temperatures feed
low-temperature and high-temperature checks respectively. MCA's freezable workqueue and device dependencies ensure charging policy stops before suspend
suspends the charge pump and I²C controller.

Continuously capture `journalctl -kf -o short-monotonic` and cross-reference connection changes with the existing
`ucsi:ucsi_connector_change` tracepoint. Bit `0x4000` in `change` indicates Connect Change; not every UCSI notification is an unplug.
Sample `authentic`, `online`, `voltage_now`, `current_now`, and `input_current_limit` under
`/sys/class/power_supply/piano-mca-usb/` roughly once per second; once authentication is enabled, also sample
`verify_process`, `pd_verifed`, and `adapter_id` under `xiaomi/` (the attribute is indeed spelled `pd_verifed`).
Simultaneously record temperatures, voltages, and currents from both `bq27z561` gauges. Avoid blanket dynamic debug or high-frequency sysfs
reads that could disturb authentication timing. Observe each attachment for 30 seconds first and archive the full log segment if any anomaly occurs.

Lost ACKs, cleanup retries, PDR generation races, and post-unbind callbacks are verified with fault injection in an isolated kernel
rather than forcibly crashing ADSP, dropping messages, or unbinding MCA on real hardware. If a kernel error, repeated ADSP restarts,
false authentication retention on a normal PD supply, or an unclearable authentication flag after communication recovery occurs, stop the run, save
logs, and revert to the previous boot entry.

## Wi-Fi MAC Address and Resume

ath12k prefers a valid MAC address provided by firmware, falling back next to a standard firmware-node address; if both are missing,
it generates a locally administered address for the current device instance. When firmware restart or resume from sleep returns an all-zero address again,
the driver retains the already-selected valid address instead of overwriting it with a newly randomized one. NetworkManager's
scan randomization still operates per distribution policy, separate from the interface's permanent MAC address.

On real hardware, the same permanent MAC address is preserved across normal boot and `deep` resume without logging another
random-address generation on resume. This fallback persists only for the current device instance and does not constitute handoff of the stock Wi-Fi MAC address.

## UFS Multi-Circular Queue (MCQ)

[`piano-ufs-mcq.dtso`](../../../linux/dts/piano-ufs-mcq.dtso) adds the MCQ register window and ITS interrupt wiring used by stock on this device.
The standard controller sits at `0x1d84000`, while
MCQ configuration, vendor status, and queue doorbells start at `0x1da0000`, `0x1da4000`, and
`0x1da5000` respectively; the mainline driver accesses them through a single 28 KiB `mcq` window.
The ITS UFS device ID keeps stock `0x60`, and DMA continues using existing SMMU routing.

After a clean reboot on 2026-10-22, the driver enabled 8 I/O queues and 1 poll queue
with a queue depth of 64; all 8 ESI interrupts recorded completions, and SteamOS booted from internal storage.
This resolves the earlier fallback to legacy single-queue mode caused by missing MCQ resources. The boot-time write to
`wExceptionEventControl` is still rejected by the device and remains a separate attribute-compatibility issue.

## CDSP

[`piano-dsp-resources.dtso`](../../../linux/dts/piano-dsp-resources.dtso) uses mainline
NSP NoC bindings, two-cell interconnect arguments, and the AP BCM voter to hand the CDSP-to-memory
bandwidth path over to `qcom_q6v5`. The driver requests bandwidth before starting signed firmware and releases it once boot handoff completes,
eliminating reliance on bus state left behind by Android. The packager preserves this converted path.

[`piano-cdsp-fastrpc.dtso`](../../../linux/dts/piano-cdsp-fastrpc.dtso) connects the 11 stock
non-secure compute sessions to the Linux-managed apps SMMU, preserving each session's
stream ID and mask. Stock secure session 9 is not exposed as a normal Linux compute session.
Normal DMA mapping is performed by the kernel without writing SMMU registers from userspace.

After a clean reboot on 2026-10-22, CDSP received its ready/handover interrupts and entered the running state,
with all 11 compute sessions using DMA domains. `cdsprpcd` opened the DSP-side
`adsp_default_listener` without needing a service restart, and the previous continuous capability-query errors disappeared.
This proves firmware boot and basic RPC transport work; specific neural-network, vision, and compute libraries still require
corresponding userspace and DSP binary support, so service startup alone cannot be claimed as full application readiness.

In a subsequent `deep` suspend woken by RTC after roughly 22 seconds, both ADSP and CDSP remained running,
`cdsprpcd`'s restart count stayed at zero, and Wi-Fi, Bluetooth, and USB reconnected.
GPIO interrupt-affinity warnings still appear during CPU offlining and have neither been suppressed nor marked as fixed.
