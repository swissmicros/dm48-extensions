/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 SwissMicros GmbH */
/*
 * ext.ld - links a DM48 native extension as one image from address 0: code and read-only data (the code part),
 * then, from a 32-byte boundary, data (the data part), then .bss (NOLOAD, zeroed by the loader right after the image).
 * The calculator's memory protection works in 32-byte lines: the code part is read-only and executable to the
 * extension, the data part and .bss read-write and never executable (format 2, ../README.md). Link with -Wl,-q so
 * the relocations stay in the ELF for tools/extpack.py, and with -Wl,--unresolved-symbols=ignore-all: the dx_*
 * imports are resolved by the loader on the calculator, and extpack.py rejects any other undefined symbol by name.
 */
ENTRY(dx_init)

SECTIONS
{
  . = 0;
  .text : ALIGN(4)
  {
    KEEP(*(.text.dx_init))
    *(.text .text.*)
    *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
  }
  .rodata : ALIGN(8)
  {
    *(.rodata .rodata.*)
  }
  . = ALIGN(32);                /* the data part's start, also when there is no .data (.bss then starts here) */
  .data : ALIGN(32)
  {
    *(.data .data.*)
  }
  .bss (NOLOAD) : ALIGN(8)
  {
    *(.bss .bss.*)
    *(COMMON)
  }
  /DISCARD/ :
  {
    *(.ARM.exidx*) *(.ARM.extab*) *(.init_array*) *(.fini_array*) *(.preinit_array*) *(.ctors*) *(.dtors*)
    *(.comment) *(.note*)
  }
}
