/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  Position-independent Android kernel relocator shim for NP05J BootSelect.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).

  Calling convention:
    x0 = DTB / FDT address to forward in x0
    x1 = DstBase (final load address where Android kernel expects to start)
    x2 = SrcBase (packed stock kernel inside SPLITv1 container)
    x3 = SizeBytes
**/

  .section .text.reloc, "ax"
  .global _reloc_start
_reloc_start:
  mov   x9, x0    /* Save DTB */
  mov   x10, x1   /* Save DstBase (also final entry point) */
  mov   x11, x2   /* Src */
  mov   x12, x3   /* Size */

  cbz   x12, .Ldone
  cmp   x10, x11
  b.eq  .Lflush
  b.lo  .Lfwd

  /* Dst > Src: check if overlapping, copy backward to be safe */
  add   x13, x11, x12
  cmp   x10, x13
  b.hs  .Lfwd

  add   x14, x10, x12
.Lrev:
  ldrb  w15, [x13, #-1]!
  strb  w15, [x14, #-1]!
  subs  x12, x12, #1
  b.ne  .Lrev
  b     .Lflush

.Lfwd:
  cmp   x12, #8
  b.lo  .Lfwd_byte
  ldr   x15, [x11], #8
  str   x15, [x10], #8
  sub   x12, x12, #8
  b     .Lfwd

.Lfwd_byte:
  cbz   x12, .Lflush
  ldrb  w15, [x11], #1
  strb  w15, [x10], #1
  sub   x12, x12, #1
  b     .Lfwd_byte

.Lflush:
  /* Clean D-cache to PoU across [DstBase .. DstBase + Size) and flush I-cache */
  mov   x10, x1
  add   x13, x1, x3
  bic   x10, x10, #63
.Ldc_loop:
  cmp   x10, x13
  b.hs  .Lic_sync
  dc    cvau, x10
  add   x10, x10, #64
  b     .Ldc_loop

.Lic_sync:
  dsb   ish
  ic    iallu
  dsb   ish
  isb

.Ldone:
  mov   x0, x9
  mov   x1, #0
  mov   x2, #0
  mov   x3, #0
  br    x10
