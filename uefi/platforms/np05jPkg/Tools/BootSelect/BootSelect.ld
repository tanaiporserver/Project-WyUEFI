/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Linker script for freestanding position-independent NP05J BootSelect */

OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
  . = 0x0;

  .text : ALIGN(8) {
    KEEP(*(.text.head))
    *(.text .text.*)
  }

  .rodata : ALIGN(8) {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(8) {
    *(.data .data.*)
  }

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }

  . = ALIGN(4096);
  __image_end = .;

  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
    *(.ARM.*)
  }
}
