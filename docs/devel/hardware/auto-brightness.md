# Auto-Brightness and Stock Policy

GNOME Power Manager adjusts screen brightness using Piano's ambient light thresholds and relative brightness curve, preserving the user's manually chosen brightness without altering SensorProxy's illuminance readings. Dimming stops when the display turns off; on wake, the saved brightness is restored before processing new ambient light readings.

## Stock Configuration and Sources

The primary ambient light sensor is a Sensortek STK3BCX, sampled at 250 ms in stock firmware. HyperOS's display service also uses 1000/3000 ms fast/slow ambient windows, a secondary rear sensor, a proximity sensor, and scene-based policies; the GNOME patch uses only the primary sensor and does not port the full scene-detection stack.

The three illuminance threshold curves in `linux/desktops/gnome/ambient-policy/piano.ini` come from the runtime configuration of the stock HyperOS `OS3.0.309.0.WPYCNXM` display service on the same device: 15 points for large brightening, 19 points for darkening, and 15 points for small brightening, requiring continuous threshold crossing for 1000, 1000, and 5000 ms respectively. These are absolute lux curves rather than a generic percentage hysteresis table. For example, at an accepted illuminance of 136.01251 lux, the three curves yield 314.8489, 27.4224, and 243.3144 lux, matching the thresholds held by the stock display service.

The 108-point target luminance curve comes from `/overlay/AospFrameworkResOverlay.apk` inside `product.img` of HyperOS 4.0.6 on the same device: 0 lux is prepended to `config_autoBrightnessLevels` and paired point by point with `config_autoBrightnessDisplayValuesNits`. Linux linearly interpolates between these points instead of treating Android's backlight array directly as Linux percentages.

| Ambient Light | Stock Target Luminance |
| --- | --- |
| 0 lux | 3.5 nit |
| 7 lux | 24 nit |
| 30 lux | 97 nit |
| 100 lux | 103 nit |
| 197 lux | 113.7 nit |
| 300 lux | 123 nit |
| 500 lux | 144 nit |

The backlight uses the KTZ8866 exponential current mapping. The chip's [04b datasheet](https://kinet-ic.com/uploads/web/KTZ8866/KTZ8866-04b.pdf) specifies `I ∝ 1.003040572^code`; both chips on the device have `BL_CFG1=0x53`, where `BLED_MAP=0`, matching this mode. For stock's normal/HBM brightness ranges and `mi_bl_level_remap` conversion, see [HDR Brightness and Backlight](display-depth.md#hdr-brightness-and-backlight).

## Relative Brightness Mapping

[Upstream GSD 48.1](https://github.com/GNOME/gnome-settings-daemon/blob/48.1/plugins/power/gsd-power-manager.c#L2833) scales brightness percentage linearly with lux. When illuminance drops from 197 to 7 lux, 60% drops to around 2%; yet the stock target luminance only decreases from 113.7 to 24 nit, so direct lux-ratio scaling over-dims the display.

The Piano policy first uses the three threshold curves to determine the accepted illuminance, then looks up the target luminance `N(lux)`. Once the user selects percentage `P0` at `L0` lux, subsequent targets are computed as:

```text
P(lux) = clamp(P0 + K × ln(N(lux) / N(L0)), 0, 100)
K = 100 / ((2047 - 20) × ln(1.003040572))
  = 16.249888161019676
```

`20..2047` is the code range GSD uses on this backlight interface. This formula computes relative current change and does not claim that Linux output is calibrated to absolute nits; content-adaptive backlight control, panel transmittance, and stock HDR compensation are also excluded. If the meaning of kernel backlight code values changes in the future, this coefficient must be updated accordingly.

For example, setting brightness manually to 60% at 197 lux yields a target of about 35% when dropping to 7 lux, and about 61% when rising to 300 lux. Setting it manually to 61% at 7 lux yields about 30% at 0 lux, instead of collapsing straight to the minimum level when illuminance reaches zero. Users can still manually select minimum brightness; both 0% and 0 lux serve as finite baselines, allowing brightness to recover when ambient light increases again.

After the debounce threshold is met, GNOME's 0.1 Hz exponential smoothing (time constant ~1.59 seconds) is retained. A timer completes transitions when no further sensor notifications arrive; identical integer targets are not resubmitted. Manual brightness changes immediately rebuild the baseline, while releasing or losing the sensor cancels the timer and resets threshold timing. While the display is off, new dimming targets are ignored, preserving both the brightness baseline and the user-requested value.

## Desktop and Build Integration

The Piano curve is selected only when the Device Tree contains `xiaomi,piano` and SensorProxy reports units in `lux`. Other devices, other units, or missing configuration fall back to the upstream mapping. Both the configuration and binary are installed inside the standard GSD package, without adding an extra dimming daemon.

Debian sid and Nyarch share GSD 51's standard Shell brightness API, preserving the Shell's user brightness offset while GSD outputs the relative auto-brightness target; this also includes keyboard backlight hotplug refresh and [lock-screen state synchronization at startup](power-button.md#default-settings). The shared patch series covers 51.0 and 51.1. The Debian package appends `+sunuefi1` to the source version selected from signed APT; Nyarch keeps the upstream version from the target pacman repository and appends `.1` to `pkgrel`.

`./build.sh gsd` selects and verifies the source from the target sid signed APT index and builds it in an isolated ARM64 builder following the original `debian/rules`. `build/gsd/runtime` provides the daemon and common packages, and `SOURCE.json` records the source, target ABI, patches, and package digests. See the [policy packaging guide](../../../linux/desktops/gnome/ambient-policy/README.md) for build commands.

## Standard Settings UI

The Power page in GNOME Settings uses the native automatic brightness toggle. It requires SensorProxy to report `HasAmbientLight=true` and GSD to expose controllable screen brightness; `ambient-enabled` controls whether it is enabled. Hardware capabilities are not force-advertised, and no second settings entry is added.
