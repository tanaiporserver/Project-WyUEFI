/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  SPMI v7 key polling implementation for NP05J pre-UEFI BootSelect.
**/

#include "EarlyKeys.h"

extern uint32_t EarlyMmioRead32(uint64_t Addr);
extern void     EarlyMmioWrite32(uint64_t Addr, uint32_t Val);
extern void     EarlyDelayUs(uint64_t Us);

#define NP05J_SPMI_CORE 0x0C400000ULL
#define NP05J_SPMI_OBS  0x0C440000ULL

static int SpmiReadByte(uint16_t Ppid, uint8_t Reg, uint8_t *Out) {
  uint32_t Version = EarlyMmioRead32(NP05J_SPMI_CORE);
  uint32_t Count   = EarlyMmioRead32(NP05J_SPMI_CORE + 4U) & 0x7FFU;
  uint32_t Apid    = 0xFFFFFFFFU;
  uint64_t Chan;

  if ((Version < 0x70000000U) || (Version >= 0x80000000U) || (Count == 0U) || (Count > 1024U)) {
    return 0;
  }

  for (uint32_t I = 0; I < Count; I++) {
    uint32_t Map = EarlyMmioRead32(NP05J_SPMI_CORE + 0x2000ULL + 4ULL * I);
    if ((Map != 0U) && (((Map >> 8) & 0xFFFU) == (uint32_t)Ppid)) {
      Apid = I;
      break;
    }
  }

  if (Apid == 0xFFFFFFFFU) {
    return 0;
  }

  Chan = NP05J_SPMI_OBS + 0x20ULL * Apid;
  EarlyMmioWrite32(Chan, (1U << 27) | ((uint32_t)Reg << 4));
  for (uint32_t Spin = 0; Spin < 500U; Spin++) {
    uint32_t St = EarlyMmioRead32(Chan + 8ULL);
    if ((St & 1U) != 0U) {
      if ((St & 0xEU) != 0U) {
        return 0;
      }
      *Out = (uint8_t)EarlyMmioRead32(Chan + 0x18ULL);
      return 1;
    }
    EarlyDelayUs(1U);
  }
  return 0;
}

uint32_t EarlyKeysPoll(void) {
  uint32_t Mask = 0U;
  uint8_t  Val  = 0U;

  if (SpmiReadByte(0x013U, 0x10U, &Val)) {
    if ((Val & (1U << 7)) != 0U) {
      Mask |= NP05J_KEY_POWER;
    }
    if ((Val & (1U << 6)) != 0U) {
      Mask |= NP05J_KEY_VOL_DOWN;
    }
  }

  if (SpmiReadByte(0x18DU, 0x10U, &Val)) {
    if ((Val & 1U) == 0U) {
      Mask |= NP05J_KEY_VOL_UP;
    }
  }

  return Mask;
}
