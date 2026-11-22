#!/usr/bin/env bash
# Phase-1 Hybrid builder for nubia NP05J inside Project-WyUEFI.
# Uses a disposable workspace under build/np05j-workspace/ so upstream/ stays clean.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_BUILD="${1:-DEBUG}"   # DEBUG, RELEASE, or ALL
WS="$ROOT/build/np05j-workspace/Mu-Silicium"
OUT="$ROOT/artifacts/np05j"
TOOLS="${SUNUEFI_TOOLCHAIN_ROOT:-$ROOT/build/host-tools/usr}"

if [[ -d "$TOOLS/bin" ]]; then
  export PATH="$TOOLS/bin:$PATH"
  export LD_LIBRARY_PATH="$TOOLS/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
if [[ -d "$ROOT/.venv/bin" ]]; then
  export PATH="$ROOT/.venv/bin:$PATH"
fi
PYTHON_COMMAND="${PYTHON_COMMAND:-$(command -v python3)}"

UPSTREAM_MU="$ROOT/upstream/Mu-Silicium"
if [[ ! -e "$UPSTREAM_MU/.git" ]]; then
  echo "Missing upstream/Mu-Silicium; run './build.sh sources --group firmware' first." >&2
  exit 1
fi

echo "[1/7] Preparing disposable workspace at $WS..."
rm -rf "$ROOT/build/np05j-workspace"
mkdir -p "$ROOT/build/np05j-workspace" "$OUT"
git clone --local "$UPSTREAM_MU" "$WS"

for sub in \
  Mu_Basecore \
  Common/Mu \
  Common/Mu_OEM_Sample \
  Binaries \
  Silicium-ACPI \
  Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl \
  Mu_Basecore/BaseTools/Source/C/BrotliCompress/brotli \
  Mu_Basecore/MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
  Mu_Basecore/MdeModulePkg/Universal/RegularExpressionDxe/oniguruma \
  Mu_Basecore/MdePkg/Library/BaseFdtLib/libfdt \
  Mu_Basecore/MdePkg/Library/MipiSysTLib/mipisyst \
  Mu_Basecore/SecurityPkg/DeviceSecurity/SpdmLib/libspdm; do
  if [[ -e "$UPSTREAM_MU/$sub/.git" ]]; then
    mkdir -p "$WS/$(dirname "$sub")"
    rm -rf "$WS/$sub"
    git clone --local "$UPSTREAM_MU/$sub" "$WS/$sub"
  fi
done

echo "[2/7] Applying NP05J firmware patches (64-bit ClockDxe, InPlace BootShim, Silicium-ACPI)..."
git -C "$WS" apply "$ROOT/patches/firmware/np05j/0001-mu-silicium-64bit-clockdxe-smbios-bootoptions.patch"
git -C "$WS" apply "$ROOT/patches/firmware/np05j/0002-mu-silicium-bootshim-inplace-and-ramdisk.patch"
git -C "$WS/Silicium-ACPI" apply "$ROOT/patches/firmware/np05j/0003-silicium-acpi-pakala-and-np05j.patch"

for mupatch in Auth-Service.patch Boot-Manager.patch Timer.patch Usb-Bus.patch; do
  if git -C "$WS/Mu_Basecore" apply --check "$WS/Resources/MuPatches/$mupatch" 2>/dev/null; then
    git -C "$WS/Mu_Basecore" apply "$WS/Resources/MuPatches/$mupatch"
  elif ! git -C "$WS/Mu_Basecore" apply --reverse --check "$WS/Resources/MuPatches/$mupatch" 2>/dev/null; then
    echo "Mu_Basecore patch failed: $mupatch" >&2
    exit 1
  fi
done

echo "[3/7] Staging NP05J platform, Pakala DXEs, DTB, and config..."
rm -rf "$WS/Platforms/ZTE/nubia_np05jPkg" "$WS/Binaries/nubia_np05j"
mkdir -p "$WS/Platforms/ZTE/nubia_np05jPkg" "$WS/Binaries/nubia_np05j" "$WS/Resources/DTBs" "$WS/Resources/Configs"
cp -a "$ROOT/uefi/platforms/np05jPkg/." "$WS/Platforms/ZTE/nubia_np05jPkg/"
cp -a "$ROOT/vendor/np05j/pakala-dxe/." "$WS/Binaries/nubia_np05j/"
cp -a "$ROOT/vendor/np05j/dtb/np05j.dtb" "$WS/Resources/DTBs/nubia_np05j.dtb"
cp -a "$ROOT/uefi/platforms/np05jPkg/np05j.toml" "$WS/Resources/Configs/nubia_np05j.toml"
python3 "$WS/Binaries/nubia_np05j/build_pakala_patches.py"

echo "[4/7] Building BaseTools..."
make -C "$WS/Mu_Basecore/BaseTools/Source/C" -j"$(nproc)"
mkdir -p "$WS/Build" "$WS/Conf"
export WORKSPACE="$WS"
export EDK_TOOLS_PATH="$WS/Mu_Basecore/BaseTools"
export PACKAGES_PATH="$WS/Platforms/ZTE:$WS/Common/Mu:$WS/Common/Mu_OEM_Sample:$WS/Mu_Basecore:$WS/Silicon/Qualcomm:$WS/Silicon/Silicium:$WS/Silicium-ACPI:$WS/Silicium-ACPI/Platforms/ZTE:$WS/Silicium-ACPI/Silicon/Qualcomm"
for cfg in target tools_def build_rule; do
  cp "$EDK_TOOLS_PATH/Conf/${cfg}.template" "$WS/Conf/${cfg}.txt"
done
export PATH="$EDK_TOOLS_PATH/Source/C/bin:$EDK_TOOLS_PATH/BinWrappers/PosixLike:$PATH"
export PYTHONPATH="$EDK_TOOLS_PATH/Source/Python${PYTHONPATH:+:$PYTHONPATH}"
export CLANG_BIN="${CLANG_BIN:-$(dirname "$(command -v clang)")/}"
export IASL_PREFIX="${IASL_PREFIX:-$(dirname "$(command -v iasl)")/}"
export NASM_PREFIX="${NASM_PREFIX:-$(dirname "$(command -v nasm)")/}"

build_profile() {
  local mode="$1"
  local fd_path="$WS/Build/nubia_np05jPkg/${mode}_CLANGPDB/FV/SILICIUM_UEFI.fd"
  local kernel_path="$WS/Build/nubia_np05jPkg/nubia_np05j-kernel"
  echo "[5/7] Building NP05J UEFI ($mode)..."
  rm -f "$WS/BootShim/BootShim.elf" "$WS/BootShim/BootShim.bin"
  make -C "$WS/BootShim" REQUIRES_KERNEL_HEADER=1 IN_PLACE=1 FD_BASE=0xA8010000 FD_SIZE=0x200000
  (
    cd "$WS"
    "$PYTHON_COMMAND" "$EDK_TOOLS_PATH/Source/Python/build/build.py" \
      -a AARCH64 -b "$mode" -t CLANGPDB -p nubia_np05jPkg/nubia_np05j.dsc -n "$(nproc)" \
      -D ENABLE_SECUREBOOT=0 \
      -D FD_BASE=0xA8010000 -D FD_SIZE=0x200000 -D FD_BLOCKS=0x200 \
      -D DEVICE_MODEL=0 -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE \
      -D BUILDID_STRING="SunUEFI-np05j-hybrid" \
      -D NP05J_SPMI_DEBUGGER="${NP05J_SPMI_DEBUGGER:-0}" \
      -D NP05J_BUTTONS_ENTER="${NP05J_BUTTONS_ENTER:-0}" \
      -D NP05J_NX729J_DXE="${NP05J_NX729J_DXE:-0}"
  )
  cat "$WS/BootShim/BootShim.bin" "$fd_path" "$WS/Resources/DTBs/nubia_np05j.dtb" > "$kernel_path"
  "$PYTHON_COMMAND" "$WS/Resources/Scripts/mkbootimg.py" \
    --header_version 4 \
    --kernel "$kernel_path" \
    -o "$OUT/Mu-nubia_np05j-${mode}.img"
  cp "$fd_path" "$OUT/SILICIUM_UEFI-${mode}.fd"
}

case "$TARGET_BUILD" in
  DEBUG|RELEASE) build_profile "$TARGET_BUILD" ;;
  ALL) build_profile DEBUG; build_profile RELEASE ;;
  *) echo "Unknown build mode: $TARGET_BUILD (use DEBUG, RELEASE, or ALL)" >&2; exit 2 ;;
esac

echo "[6/7] Building Pre-UEFI BootSelect & RelocShim..."
make -C "$WS/Platforms/ZTE/nubia_np05jPkg/Tools/BootSelect" clean all
cp -a "$WS/Build/nubia_np05jPkg/BootSelect" "$OUT/"

echo "[7/7] Building Android Boot Repacker & KSU/SukiSU Helper ZIP..."
mkdir -p "$WS/Platforms/ZTE/nubia_np05jPkg/AndroidHelper"
if [[ -d "$ROOT/android-np05j" ]]; then
  cp -a "$ROOT/android-np05j/." "$WS/Platforms/ZTE/nubia_np05jPkg/AndroidHelper/"
fi
make -C "$WS/Platforms/ZTE/nubia_np05jPkg/AndroidHelper/native" clean all
if [[ -f "$OUT/Mu-nubia_np05j-RELEASE.img" ]]; then
  python3 -c '
import pathlib, struct
img = pathlib.Path("'"$OUT/Mu-nubia_np05j-RELEASE.img"'").read_bytes()
ks = struct.unpack_from("<I", img, 8)[0]
pathlib.Path("'"$WS/Build/nubia_np05jPkg/nubia_np05j-kernel"'").write_bytes(img[4096:4096+ks])
'
elif [[ -f "$OUT/Mu-nubia_np05j-DEBUG.img" ]]; then
  python3 -c '
import pathlib, struct
img = pathlib.Path("'"$OUT/Mu-nubia_np05j-DEBUG.img"'").read_bytes()
ks = struct.unpack_from("<I", img, 8)[0]
pathlib.Path("'"$WS/Build/nubia_np05jPkg/nubia_np05j-kernel"'").write_bytes(img[4096:4096+ks])
'
fi
python3 "$WS/Platforms/ZTE/nubia_np05jPkg/AndroidHelper/build_helper_module.py"
cp "$WS/Build/nubia_np05jPkg/NP05J-SunUEFI-Helper.zip" "$OUT/"

echo "Done! Artifacts written to $OUT:"
ls -lh "$OUT"
