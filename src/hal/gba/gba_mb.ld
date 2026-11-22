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
  /* The pack slot of an exported game (src/pack/rom_slot.h), right after the header. */
  .bm_rom_slot : { KEEP(*(.bm_rom_slot)) } > ewram

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

  /* The embedded pack last of what is loaded, and the content pool right after it: once
     another pack arrives over the link, the room of the embedded one is used again. */
  .bm_pack : ALIGN(4)
  {
    __bm_pool_start = .;
    KEEP(*(.bm_pack))
    . = ALIGN(4);
  } > ewram

  /* Zeroed EWRAM data at the top of EWRAM, so that the pool and the heap are one piece below
     it (and a multiboot image does not carry it). */
  .sbss ((ORIGIN(ewram) + LENGTH(ewram) - SIZEOF(.sbss)) & ~15) (NOLOAD) :
  {
    __sbss_start = .;
    *(.sbss .sbss.*)
    . = ALIGN(4);
    __sbss_end = .;
  } > ewram

  /* Between: the engine's EWRAM heap (__bm_ewram_heap_size, 4 KB unless the image says
     otherwise), and below it the content pool for packs (src/link/live.h). IWRAM's rest is the
     IWRAM heap. */
  PROVIDE(__bm_ewram_heap_size = 4K);
  __ewram_heap_end = ADDR(.sbss);
  __ewram_heap_start = __ewram_heap_end - __bm_ewram_heap_size;
  __bm_pool_end = __ewram_heap_start;
  __iwram_heap_start = ALIGN(__bss_end, 4);
  __iwram_heap_end = __sp_usr - __sp_usr_size;

  ASSERT(__bm_pool_start <= __bm_pool_end, "EWRAM overflows: no room left for the content pool and the heap")
  ASSERT(__iwram_heap_start <= __iwram_heap_end, "IWRAM overflows into the stack")

  /DISCARD/ : { *(.ARM.attributes) *(.comment) }
}
