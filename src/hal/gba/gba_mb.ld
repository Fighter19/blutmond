/* Multiboot image: everything is loaded into EWRAM by the BIOS (at most 256 KB). */
/* SPDX-License-Identifier: BSD-2-Clause OR MIT */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
  rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
  ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
  iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}

/* Stacks at the top of IWRAM; the BIOS uses 0x03007FE0..0x03007FFF. */
__sp_irq = 0x03007FA0;
__sp_usr = 0x03007F00;
__sp_usr_size = 0x1000;

SECTIONS
{
  .crt0 : { KEEP(*(.crt0)) } > ewram

  .text :
  {
    *(.text .text.* .gnu.linkonce.t.*)
    *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
    . = ALIGN(4);
  } > ewram

  .rodata :
  {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    . = ALIGN(4);
  } > ewram

  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > ewram
  .init_array : { KEEP(*(.init_array*)) } > ewram
  .fini_array : { KEEP(*(.fini_array*)) } > ewram

  /* Code and data that run from IWRAM, copied there by crt0. */
  .iwram : ALIGN(4)
  {
    __iwram_start = .;
    *(.iwram .iwram.*)
    *(.data .data.* .gnu.linkonce.d.*)
    . = ALIGN(4);
    __iwram_end = .;
  } > iwram AT> ewram
  __iwram_lma = LOADADDR(.iwram);

  .bss (NOLOAD) : ALIGN(4)
  {
    __bss_start = .;
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > iwram

  /* Initialised data placed in EWRAM, copied there by crt0 (no copy for multiboot). */
  .ewram : ALIGN(4)
  {
    __ewram_start = .;
    *(.ewram .ewram.*)
    . = ALIGN(4);
    __ewram_end = .;
  } > ewram AT> ewram
  __ewram_lma = LOADADDR(.ewram);

  .sbss (NOLOAD) : ALIGN(4)
  {
    __sbss_start = .;
    *(.sbss .sbss.*)
    . = ALIGN(4);
    __sbss_end = .;
  } > ewram

  /* What is left of EWRAM and IWRAM belongs to the engine's heaps. */
  __ewram_heap_start = ALIGN(__sbss_end, 4);
  __ewram_heap_end = ORIGIN(ewram) + LENGTH(ewram);
  __iwram_heap_start = ALIGN(__bss_end, 4);
  __iwram_heap_end = __sp_usr - __sp_usr_size;

  ASSERT(__iwram_heap_start <= __iwram_heap_end, "IWRAM overflows into the stack")

  /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
