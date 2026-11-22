/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Lightweight early RAM ring-buffer trace logger for NP05J BootSelect.
**/

#ifndef _NP05J_EARLY_TRACE_H_
#define _NP05J_EARLY_TRACE_H_

#include <stdint.h>

#define NP05J_EARLY_TRACE_ADDR  0xA7FFF200ULL
#define NP05J_EARLY_TRACE_MAGIC 0x54524331U /* 'TRC1' */

typedef struct {
  uint32_t Magic;
  uint32_t WriteIndex;
  char     Ring[1528];
} NP05J_EARLY_TRACE_BUF;

void EarlyTraceInit(void);
void EarlyTraceLog(const char *Msg);

#endif /* _NP05J_EARLY_TRACE_H_ */
