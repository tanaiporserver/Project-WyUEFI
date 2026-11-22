#!/system/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Unified helper CLI invoked by KernelSU/SukiSU WebUI and service/action hooks
# Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI)

set -u

MODDIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
REPACK="$MODDIR/bin/np05j-boot-repack"
PAYLOAD_DIR="$MODDIR/payloads"
DATA_DIR="/data/adb/np05j-sunuefi"
BACKUP_DIR="$DATA_DIR/backups"

mkdir -p "$BACKUP_DIR"

get_slot() {
  local s
  s="$(getprop ro.boot.slot_suffix 2>/dev/null || true)"
  case "$s" in
    _a|_b) echo "$s"; return 0 ;;
  esac
  for cand in /proc/cmdline; do
    if [ -r "$cand" ]; then
      for tok in $(cat "$cand"); do
        case "$tok" in
          androidboot.slot_suffix=_a) echo "_a"; return 0 ;;
          androidboot.slot_suffix=_b) echo "_b"; return 0 ;;
        esac
      done
    fi
  done
  echo "_a"
}

find_boot_dev() {
  local slot="$1"
  local dev="/dev/block/by-name/boot${slot}"
  if [ -e "$dev" ]; then
    echo "$dev"
    return 0
  fi
  dev="/dev/block/bootdevice/by-name/boot${slot}"
  echo "$dev"
}

cmd_status() {
  local slot dev
  slot="$(get_slot)"
  dev="$(find_boot_dev "$slot")"
  echo "device=$(getprop ro.product.model 2>/dev/null || echo nubia_np05j)"
  echo "slot=${slot}"
  echo "boot_dev=${dev}"
  if [ -r "$dev" ]; then
    local tmp="$DATA_DIR/.inspect.img"
    dd if="$dev" of="$tmp" bs=4096 count=130 2>/dev/null || true
    "$REPACK" inspect "$tmp" || true
    rm -f "$tmp"
  else
    echo "sunuefi_combined=0 (device node unavailable in non-root shell)"
  fi
  echo "backups=$(ls -1 "$BACKUP_DIR"/*.img 2>/dev/null | wc -l)"
}

cmd_install() {
  local timeout="${1:-3}"
  local slot dev stock out
  slot="$(get_slot)"
  dev="$(find_boot_dev "$slot")"
  [ -w "$dev" ] || { echo "Error: cannot write $dev"; return 1; }

  stock="$BACKUP_DIR/boot${slot}-stock-$(date +%Y%m%d-%H%M%S).img"
  out="$DATA_DIR/.combined${slot}.img"

  dd if="$dev" of="$stock" bs=1M || return 1
  ln -sf "$stock" "$BACKUP_DIR/latest-stock${slot}.img"

  "$REPACK" repack \
    --stock "$stock" \
    --selector "$PAYLOAD_DIR/np05j-bootselect.bin" \
    --shim "$PAYLOAD_DIR/np05j-reloc-shim.bin" \
    --uefi "$PAYLOAD_DIR/nubia_np05j-kernel" \
    --out "$out" \
    --timeout "$timeout" || return 1

  dd if="$out" of="$dev" bs=1M conv=fsync || return 1
  sync
  rm -f "$out"
  echo "Installed Combined SunUEFI Boot on slot $slot (stock backup: $stock)"
}

cmd_restore() {
  local slot dev tmp out
  slot="$(get_slot)"
  dev="$(find_boot_dev "$slot")"
  [ -w "$dev" ] || { echo "Error: cannot write $dev"; return 1; }

  tmp="$DATA_DIR/.cur${slot}.img"
  out="$DATA_DIR/.restored${slot}.img"
  dd if="$dev" of="$tmp" bs=1M || return 1

  if "$REPACK" restore "$tmp" "$out"; then
    dd if="$out" of="$dev" bs=1M conv=fsync || { rm -f "$tmp" "$out"; return 1; }
    sync
    rm -f "$tmp" "$out"
    echo "Restored verified stock boot image onto slot $slot"
    return 0
  fi
  rm -f "$tmp" "$out"

  if [ -f "$BACKUP_DIR/latest-stock${slot}.img" ]; then
    dd if="$BACKUP_DIR/latest-stock${slot}.img" of="$dev" bs=1M conv=fsync || return 1
    sync
    echo "Restored stock boot from latest backup onto slot $slot"
    return 0
  fi
  echo "Error: Neither SPLITv1 container nor backup found for slot $slot"
  return 1
}

cmd_request() {
  local target="${1:-uefi}"
  local timeout="${2:-0}"
  local slot dev
  slot="$(get_slot)"
  dev="$(find_boot_dev "$slot")"
  "$REPACK" request "$dev" "$target" "$timeout"
  sync
}

cmd_clear_oneshot() {
  local slot dev
  slot="$(get_slot)"
  dev="$(find_boot_dev "$slot")"
  [ -w "$dev" ] && "$REPACK" clear-request "$dev" >/dev/null 2>&1 || true
}

cmd_reboot() {
  local mode="${1:-normal}"
  case "$mode" in
    android)     svc power reboot || reboot ;;
    uefi|bios|windows|linux)
      cmd_request "$mode" 0 || return 1
      svc power reboot || reboot
      ;;
    recovery)    reboot recovery ;;
    bootloader)  reboot bootloader ;;
    edl)         reboot edl ;;
    *)           reboot ;;
  esac
}

case "${1:-status}" in
  status)        cmd_status ;;
  install)       cmd_install "${2:-3}" ;;
  restore)       cmd_restore ;;
  request)       cmd_request "${2:-uefi}" "${3:-0}" ;;
  clear-oneshot) cmd_clear_oneshot ;;
  reboot)        cmd_reboot "${2:-normal}" ;;
  *)
    echo "Usage: $0 {status|install [timeout]|restore|request <target> [timeout]|clear-oneshot|reboot <mode>}"
    exit 1
    ;;
esac
