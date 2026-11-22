/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Early MMIO, cache maintenance, and ARM generic timer helpers for BootSelect.
**/

  .section .text
  .global EarlyMmioRead32
  .global EarlyMmioWrite32
  .global EarlyDelayUs
  .global EarlyFlushRange

EarlyMmioRead32:
  ldr   w0, [x0]
  dmb   sy
  ret

EarlyMmioWrite32:
  dmb   sy
  str   w1, [x0]
  dmb   sy
  ret

EarlyDelayUs:
  mrs   x1, cntfrq_el0
  cbz   x1, .Lfallback_delay
  mrs   x2, cntpct_el0
  movz  x3, #0x4240
  movk  x3, #0x000F, lsl #16   /* 1000000 */
  mul   x0, x0, x1
  udiv  x0, x0, x3
  add   x2, x2, x0
.Lwait_cnt:
  mrs   x4, cntpct_el0
  cmp   x4, x2
  b.lo  .Lwait_cnt
  ret

.Lfallback_delay:
  lsl   x0, x0, #6
1:
  subs  x0, x0, #1
  b.ne  1b
  ret

EarlyFlushRange:
  add   x1, x0, x1
  bic   x0, x0, #63
1:
  cmp   x0, x1
  b.hs  2f
  dc    civac, x0
  add   x0, x0, #64
  b     1b
2:
  dsb   ish
  ic    iallu
  dsb   ish
  isb
  ret
