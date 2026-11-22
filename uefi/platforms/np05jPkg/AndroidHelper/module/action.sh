#!/system/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
# One-tap module Action button: prints active boot slot & SunUEFI status

MODDIR="${0%/*}"
sh "$MODDIR/common/np05j-helper.sh" status
