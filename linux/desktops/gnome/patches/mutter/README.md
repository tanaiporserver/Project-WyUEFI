# Mutter Adaptation

Debian sid builds using the distribution's original `debian/rules`, `debian/control`, and quilt setup, sharing two fixes with Nyarch: empty Gamma LUT handling and cancelled touch grabs. The shared patch series is defined in `patches/mutter/51.0/series.json`, covering versions 51.0 and 51.1. The source version is selected from the `Source` field of the target ARM64 APT candidate, with `+sunuefi1` appended to the package version instead of pinning the legacy Debian 13 ABI.

`0001-kms-empty-gamma-bypass.patch` represents a zero-entry Gamma LUT using the existing NULL bypass path, avoiding division by zero and empty array accesses during resampling while leaving non-empty curves unchanged. Signed sid sources in `meta-kms-update.c` still pass zero-entry LUTs into `meta_gamma_lut_copy_to_size()`, so this patch must remain in place.

`0003-wayland-touch-ignore-cancelled-grab.patch` rejects interactive move and resize requests after a touch sequence is cancelled and focus has been cleared, preventing a null pointer dereference. In sid sources, `touch_can_grab_surface()` still dereferences `focus_surface->surface` directly. Both are downstream patches with no claim of upstream acceptance; before dropping either one, verify the distribution source code and runtime behavior against the conditions in the shared series.

No local patch is used anymore for EDID-less outputs. Upstream distribution Mutter's `meta-color-store.c` already generates an sRGB fallback keyed by the real device ID, and `meta-color-device.c` preserves the default colord profile; the kernel internal display driver also supplies a panel-matched synthetic EDID. Generic sRGB is not a measured panel calibration and cannot be used to claim HDR support or color accuracy.

```sh
sudo ./build.sh mutter --sysroot build/distros/release/rootfs --jobs 4
```

The input is a completed sid ARM64 sysroot with signed APT binary and source indices intact. The build tool clones it first, then verifies the SHA256 hashes of `InRelease`, `Sources`, `.dsc`, and source archives inside an isolated chroot. The cache is bound to the current source version, target desktop ABI, and patch set, and cannot reuse legacy `libmutter-16` packages.

If an isolated builder already exists, pass `--builder-root <path> --skip-install-dependencies`; `dpkg-checkbuilddeps -Pnocheck` is still executed. Do not pass a live device rootfs or a finalized release rootfs.

`build/mutter/runtime/` provides five standard runtime packages, including `libmutter-51-0` and `gir1.2-mutter-51`. Outputs also preserve `.changes`, `.buildinfo`, `SOURCE.json`, and `SHA256SUMS`. Building with `DEB_BUILD_OPTIONS=nocheck parallel=4` skips the full upstream test suite; on-device verification must still install the packages normally, restart GNOME, and test standard Night Light, empty-LUT round-trips, and original state restoration.
