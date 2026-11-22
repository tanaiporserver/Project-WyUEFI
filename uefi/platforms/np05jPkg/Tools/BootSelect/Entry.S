/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/** @file
  AArch64 Linux Image entry & stack setup for NP05J pre-UEFI BootSelect.
  Adapted from Project SunUEFI (https://github.com/slwyts/Project-SunUEFI).
**/

  .section .text.head, "ax"
  .global _start
_start:
  /* Linux ARM64 Image header (64 bytes) */
  b     _real_entry           /* 0x00: code0 */
  nop                         /* 0x04: code1 */
  .quad 0x0                   /* 0x08: text_offset */
  .quad __image_end - _start  /* 0x10: image_size */
  .quad 0xA                   /* 0x18: flags (LE, 4K pages, any phys base) */
  .quad 0x0                   /* 0x20: res2 */
  .quad 0x0                   /* 0x28: res3 */
  .quad 0x0                   /* 0x30: res4 */
  .ascii "ARM\x64"            /* 0x38: magic = 0x644d5241 */
  .long 0x0                   /* 0x3C: res5 */

_real_entry:
  /* Preserve bootloader arguments (x0 = DTB, x1..x3) */
  mov   x19, x0
  mov   x20, x1
  mov   x21, x2
  mov   x22, x3

  /* Compute runtime load base and image span */
  adr   x23, _start
  adr   x24, __bss_start
  adr   x25, __bss_end

  /* Zero BSS */
1:
  cmp   x24, x25
  b.hs  2f
  stp   xzr, xzr, [x24], #16
  b     1b

2:
  /* Set 16-byte aligned stack inside BSS stack buffer */
  adr   x26, _bootselect_stack_top
  bic   x26, x26, #0xF
  mov   sp, x26

  /* Call uint64_t BootSelectMain(uint64_t LoadBase, uint64_t ImageEnd, uint64_t Dtb) */
  adr   x1, __image_end
  mov   x0, x23
  mov   x2, x19
  bl    BootSelectMain

  /* x0 now holds target entry address (either UEFI entry or relocated RelocShim) */
  mov   x16, x0
  mov   x0, x19
  mov   x1, x20
  mov   x2, x21
  mov   x3, x22
  br    x16

  .section .bss.stack, "aw", %nobits
  .balign 16
_bootselect_stack:
  .skip 16384
_bootselect_stack_top:
