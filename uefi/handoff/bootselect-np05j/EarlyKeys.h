/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Hardware SPMI v7 button reader (Power / Vol+ / Vol-) for pre-UEFI BootSelect.
  Verified against Device_np05j/nubia_np05j-live.dts:
    - PMK8550 PON (PPID 0x013, reg 0x10): bit 7 = Power, bit 6 = Vol Down
    - PM8550B GPIO06 (PPID 0x18D, reg 0x10): active LOW -> !(val & 1) = Vol Up
**/

#ifndef _NP05J_EARLY_KEYS_H_
#define _NP05J_EARLY_KEYS_H_

#include <stdint.h>

#define NP05J_KEY_POWER    (1U << 0)
#define NP05J_KEY_VOL_UP   (1U << 1)
#define NP05J_KEY_VOL_DOWN (1U << 2)

uint32_t EarlyKeysPoll(void);

#endif /* _NP05J_EARLY_KEYS_H_ */
