#!/system/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Installer hook for NP05J SunUEFI Boot Helper (KernelSU / SukiSU / APatch / Magisk)

ui_print "- Installing NP05J SunUEFI Boot Helper"
ui_print "- Adapted from Project SunUEFI (github.com/slwyts/Project-SunUEFI)"

ARCH="$(uname -m)"
case "$ARCH" in
  aarch64|arm64) ;;
  *) abort "! Unsupported architecture: $ARCH (requires aarch64)" ;;
esac

DEVICE_PROP="$(getprop ro.product.device 2>/dev/null)"
MODEL_PROP="$(getprop ro.product.model 2>/dev/null)"
ui_print "- Detected device: ${MODEL_PROP:-unknown} (${DEVICE_PROP:-unknown})"

set_perm "$MODPATH/bin/np05j-boot-repack" 0 0 0755
set_perm "$MODPATH/common/np05j-helper.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755

mkdir -p /data/adb/np05j-sunuefi/backups
ui_print "- Open WebUI or tap Action in KernelSU/SukiSU to inspect or install."
