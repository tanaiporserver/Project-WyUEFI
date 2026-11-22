#!/usr/bin/env bash
# One host-built product integration candidate. No device/flash operation.
set -euo pipefail
piano_product_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sun_device="piano"
while (($#)); do
  case "$1" in
    --device)
      [[ $# -ge 2 ]] || { echo '--device requires a value' >&2; exit 2; }
      sun_device="$2"; shift 2;;
    *) echo "Unknown argument: $1" >&2; exit 2;;
  esac
done
case "$sun_device" in
  piano)
    sun_profile=product
    sun_out_dir=artifacts/product
    sun_dtb_name=piano.dtb
    sun_vendor_mod=piano_vendor_inputs
    ;;
  np05j)
    sun_profile=product-np05j
    sun_out_dir=artifacts/product-np05j
    sun_dtb_name=np05j.dtb
    sun_vendor_mod=np05j_vendor_inputs
    ;;
  *) echo "Unsupported device: $sun_device" >&2; exit 2 ;;
esac
if [[ ! -f "$piano_product_root/.firmware-workspace.json" ]]; then
  exec python3 "$piano_product_root/tools/firmware_workspace.py" build --device "$sun_device"
fi
mkdir -p "$piano_product_root/build/logs" "$piano_product_root/$sun_out_dir"
python3 "$piano_product_root/tools/apply_firmware_patches.py"
# Stage the board DTB from the same fixed bundle used by product preparation.
python3 - "$piano_product_root" "$sun_vendor_mod" "$sun_dtb_name" <<'PY_STAGE'
import importlib, sys
from pathlib import Path
root=Path(sys.argv[1]); mod_name=sys.argv[2]; dtb_name=sys.argv[3]
sys.path.insert(0,str(root/'tools'))
bundle=importlib.import_module(mod_name).load(root)
target=root/'upstream/Mu-Silicium/Resources/DTBs'/dtb_name
target.parent.mkdir(parents=True,exist_ok=True)
target.write_bytes(bundle['board_dtb'])
PY_STAGE
python3 "$piano_product_root/tools/prepare_product_handoff.py" apply >/dev/null
rm -f "$piano_product_root/$sun_out_dir/build-ok.json" "$piano_product_root/$sun_out_dir/manifest.json"
python3 "$piano_product_root/tools/prepare_product_pump.py" apply >/dev/null
python3 "$piano_product_root/tools/prepare_product_ui.py" apply >/dev/null
python3 "$piano_product_root/tools/prepare_nv_runtime_guard.py" apply >/dev/null
if [[ "$sun_device" == np05j ]]; then
  bash "$piano_product_root/tools/build_simpleinit.sh" --product-gui-pump --device np05j
  python3 "$piano_product_root/tools/prepare_product.py" --device np05j
  bash "$piano_product_root/tools/build_stage0.sh" product-np05j
  python3 "$piano_product_root/tools/package_product.py" --device np05j
else
  bash "$piano_product_root/tools/build_simpleinit.sh" --product-gui-pump
  python3 "$piano_product_root/tools/prepare_product.py"
  bash "$piano_product_root/tools/build_stage0.sh" product
  python3 "$piano_product_root/tools/package_product.py"
fi
