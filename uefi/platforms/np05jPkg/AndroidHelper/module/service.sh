#!/system/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# Late-start service: clears any one-shot NEXTv1 request once Android booted cleanly

MODDIR="${0%/*}"
HELPER="$MODDIR/common/np05j-helper.sh"
LOGDIR="/data/adb/np05j-sunuefi"
mkdir -p "$LOGDIR"

{
  echo "[service] $(date) NP05J SunUEFI helper starting"
  sh "$HELPER" status
  sh "$HELPER" clear-oneshot
} >"$LOGDIR/last-boot.log" 2>&1
