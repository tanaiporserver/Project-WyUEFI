/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Pre-UEFI Synaptics S3930TB0A0 (TCM v2) touchscreen API for BootSelect.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

#ifndef _NP05J_EARLY_TOUCH_H_
#define _NP05J_EARLY_TOUCH_H_

#include <stdint.h>

typedef struct {
  uint8_t  Initialized;
  uint8_t  TouchDown;
  uint16_t MaxX;
  uint16_t MaxY;
  uint32_t DispX;
  uint32_t DispY;
} NP05J_EARLY_TOUCH_EVENT;

int EarlyTouchInit(void);
int EarlyTouchPoll(NP05J_EARLY_TOUCH_EVENT *Out);
void EarlyTouchShutdown(void);

#endif /* _NP05J_EARLY_TOUCH_H_ */
