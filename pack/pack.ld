/* An episode pack: plain data linked to run in the bank-switched window at
 * $200000 (see inc/jj1_pack.h).  The header comes first; music goes last so
 * the first 64 KB, which battery RAM overlays while it is enabled, never
 * holds anything the Z80 driver is reading. */
OUTPUT_ARCH(m68k)
SECTIONS
{
  .pack PACK_ORG :
  {
    KEEP(*(.packhdr))
    *(.rodata .rodata.* .data .data.*)
    . = ALIGN(2);
    *(.packbin)
    . = ALIGN(256);
    *(.packmusic)
    . = ALIGN(4);
  }
  /DISCARD/ : { *(.comment) *(.note*) *(.bss*) *(.text*) *(COMMON) }
}
