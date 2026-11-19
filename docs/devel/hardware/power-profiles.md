# Power Profiles (Power Saver / Balanced / Performance)

Debian GNOME, Debian KDE, Nyarch, and SteamOS share `piano-power-profiles`. GNOME and KDE select profiles via the standard PowerProfiles D-Bus interface; SteamOS Game Mode connects to the same interface through Manager. `tuned-ppd` maps profiles to TuneD configurations, and TuneD's `sysfs` plugin sets the CPU and GPU frequency caps.

## Why Not power-profiles-daemon

Upstream `power-profiles-daemon` finds no ACPI platform profile interface on this device and falls back to the `placeholder` backend, registering only `balanced` and `power-saver`. Neither profile actually caps the CPU or GPU, and KDE on SteamOS does not expose them as selectable profiles:

```console
$ busctl get-property org.freedesktop.UPower.PowerProfiles \
    /org/freedesktop/UPower/PowerProfiles \
    org.freedesktop.UPower.PowerProfiles Profiles
aa{sv} 2 ... "Profile" s "power-saver" ... "Profile" s "balanced"
```

`tuned-ppd` provides the same D-Bus interface (`org.freedesktop.UPower.PowerProfiles`, also aliased as `net.hadess.PowerProfiles`), with the driver name `tuned` and all three profiles available:

```console
$ busctl get-property ... Profiles
aa{sv} 3 "power-saver" "balanced" "performance"
```

The two are mutually exclusive; images install only `tuned-ppd` (which depends on `tuned`).

## Profiles and Caps

| Power Profile | TuneD Profile | policy0 Cap | policy6 Cap | CPU Boost | GPU Cap |
| --- | --- | ---: | ---: | ---: | ---: |
| Power Saver | `piano-power-saver` | 1785600 kHz | 1958400 kHz | 0 | 342 MHz |
| Balanced (default) | `piano-balanced` | 3532800 kHz | 4089600 kHz | 0 | 1100 MHz |
| Performance | `piano-performance` | 3532800 kHz | 4320000 kHz | 1 | 1100 MHz |

The TuneD profiles fix minimum frequencies at 384000 / 1017600 kHz and use the `schedutil` governor across all profiles. These values come from `device/power/tuned/profiles` (MIT) in the upstream piano integration. The 4320000 kHz cap on `policy6` does not appear in `scaling_available_frequencies` (which ends at 4089600); it is an upper limit accepted by the driver and does not mean the CPU always runs at that frequency. The kernel may select lower frequencies based on temperature or load. CPU thermal throttling uses on-chip thermal sensors and cpufreq cooling devices, while the GPU uses devfreq cooling devices; power profiles do not alter kernel thermal thresholds.

## Two Important Constraints

**Write only the GPU max frequency, never the min frequency.** devfreq's `min_freq` must not be written before `max_freq`: writing 160 MHz to `min_freq` and then lowering `max_freq` to 342 MHz causes the driver to raise `min_freq` to match `max_freq`, pinning the GPU at 342 MHz. Writing only the max frequency preserves the default 160 MHz floor. Furthermore, the driver modifies `min_freq` on its own so read-back values are unreliable (writing 160000000 and reading back 320000000 is normal), which is why the profiles omit this sysfs path altogether.

**Do not overwrite the conffile with the mapping file.** `tuned-ppd` ships `/etc/tuned/ppd.conf` as a dpkg conffile; replacing it directly triggers a keep-or-replace prompt on every package upgrade, and choosing to keep the package default drops our mapping. Instead, place the mapping at `/etc/tuned/piano-ppd.conf` and bind-mount it into the service via a drop-in:

```ini
# /usr/lib/systemd/system/tuned-ppd.service.d/50-piano-ppd.conf
[Service]
BindReadOnlyPaths=/etc/tuned/piano-ppd.conf:/etc/tuned/ppd.conf
```

`systemctl show tuned-ppd -p BindReadOnlyPaths` should output `/etc/tuned/piano-ppd.conf:/etc/tuned/ppd.conf:rbind`, while `/etc/tuned/ppd.conf` itself remains managed by the distro package. Uninstalling `piano-power-profiles` and restarting the service restores the distro's default TuneD mapping.

## Installation and Packaging

Images pre-install the profile package and enable `tuned-ppd.service`. The package is published in SunUEFI's APT repository and the `sunuefi` pacman repository; Debian sid and Arch Linux ARM provide TuneD. Valve's ARM64 repository does not include TuneD, so SteamOS uses packages from `sunuefi-steamos` built against Valve's dependency versions.

The profile sources live in [`linux/bsp/power-profiles`](../../../linux/bsp/power-profiles). Build the Debian package with `./build.sh bsp package --power-profiles --target debian --format deb --output DIRECTORY`; for Arch, change the target and format to `arch` and `pacman`.

When installing the package on an older image, check any manually placed TuneD files first. APT can take over default files whose contents match; if pacman reports a file conflict, back up and move the conflicting files before installing. After installation, place custom configurations under TuneD's `/etc/tuned/profiles` or your own systemd override files rather than overwriting packaged files under `/usr/lib`.

## Kernel Scheduler Interface

The shared kernel across all distros enables per-task and cgroup `uclamp`, allowing applications to submit performance hints via `sched_setattr()` and systemd via `CPUUtilizationMin=`/`CPUUtilizationMax=`. Normal tasks default to no minimum utilization requirement and no maximum cap; real-time tasks retain the kernel's default performance guarantees. The project adds no global utilization floor, CPU pinning policy, or tuning daemon, and leaves the existing `schedutil` frequency scaling and BORE parameters unchanged. Enabling this interface does not impose any power-saving policy on its own.

The stock final Device Tree provides `capacity-dmips-mhz` values of 1792 for CPU0–5 and 1894 for CPU6–7, which the kernel combines with each cluster's maximum frequency to compute relative compute capacity for task placement. The release board Device Tree already includes these values, and the base SM8750 DTS retains the same definitions; this source alignment does not alter the shipped boot Device Tree. Provenance is recorded in the [kernel patch series](../../../patches/linux/7.3/series.json).

## On-Device Profile Switching

After installing via pacman on a real SteamOS device, KDE PowerDevil detects all three profiles. Switching through each profile via KDE's interface yields the same frequency caps measured via PowerProfiles on Debian GNOME:

```console
power-saver -> tuned=piano-power-saver p0=1785600 p6=1958400 boost=0 gpu=342000000
performance -> tuned=piano-performance p0=3532800 p6=4320000 boost=1 gpu=1100000000
balanced    -> tuned=piano-balanced    p0=3532800 p6=4089600 boost=0 gpu=1100000000
```

The GPU minimum frequency stays at 160000000 throughout. The boot default is set by `default=balanced` in `piano-ppd.conf`.

After installing the updated Manager in a Gamescope session on the same tablet, `steamosctl` reads all three profiles; switching to Power Saver and Performance from the game UI, and switching back to Balanced via the PowerProfiles interface, produces the same read-back caps shown above. After restarting `tuned-ppd`, the existing game session can continue switching profiles without restarting.

Profile checks only verify that CPU/GPU frequency caps take effect and cannot be used to estimate standby battery life. Power draw while screen-off active and during full-system suspend must be measured separately; see [Desktop Power Management and Sleep](sleep.md).
