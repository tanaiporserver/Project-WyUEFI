/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Early RAM trace ring implementation for NP05J BootSelect.
**/

#include "EarlyTrace.h"

void EarlyTraceInit(void) {
  volatile NP05J_EARLY_TRACE_BUF *Buf = (volatile NP05J_EARLY_TRACE_BUF *)(uintptr_t)NP05J_EARLY_TRACE_ADDR;
  Buf->Magic      = NP05J_EARLY_TRACE_MAGIC;
  Buf->WriteIndex = 0U;
}

void EarlyTraceLog(const char *Msg) {
  volatile NP05J_EARLY_TRACE_BUF *Buf = (volatile NP05J_EARLY_TRACE_BUF *)(uintptr_t)NP05J_EARLY_TRACE_ADDR;
  if (!Msg || (Buf->Magic != NP05J_EARLY_TRACE_MAGIC)) {
    return;
  }
  while (*Msg != '\0') {
    uint32_t Idx = Buf->WriteIndex % (uint32_t)sizeof(Buf->Ring);
    Buf->Ring[Idx] = *Msg++;
    Buf->WriteIndex++;
  }
  Buf->Ring[Buf->WriteIndex % (uint32_t)sizeof(Buf->Ring)] = '\n';
  Buf->WriteIndex++;
}
