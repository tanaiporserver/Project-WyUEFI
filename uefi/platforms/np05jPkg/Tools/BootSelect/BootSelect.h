/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Pre-UEFI BootSelect UI layout, hitboxes, and preview API for nubia NP05J.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#ifndef _NP05J_BOOT_SELECT_H_
#define _NP05J_BOOT_SELECT_H_

#include <stdint.h>
#include "BootRequest.h"

#define NP05J_FB_BASE         0xFC800000ULL
#define NP05J_FB_WIDTH        2400U
#define NP05J_FB_HEIGHT       1504U
#define NP05J_FB_STRIDE_PX    2400U

#define NP05J_CARD_COUNT      3U

typedef struct {
  int32_t  X;
  int32_t  Y;
  int32_t  W;
  int32_t  H;
  uint32_t Target;
} NP05J_BOOT_CARD_RECT;

void BootSelectGetCardRect(uint32_t Index, NP05J_BOOT_CARD_RECT *Out);
int32_t BootSelectHitTest(uint32_t X, uint32_t Y);
void BootSelectRenderFrame(
  uint32_t *FbBgra,
  uint32_t  Width,
  uint32_t  Height,
  uint32_t  StridePx,
  uint32_t  SelectedCard,
  uint32_t  ProgressPct,
  uint32_t  RemainingSec,
  int       TouchOnline
  );

#endif /* _NP05J_BOOT_SELECT_H_ */
