/* The stock boot ROM loads the OS file into RAM and jumps to it. Where it
   loads it and where it jumps are not known yet; they come from
   disassembling OS 3.53 (see docs/hardware.md). LOAD_ADDR is passed in by
   the Makefile so it is set in one place. */
ENTRY(_start)

SECTIONS
{
    . = LOAD_ADDR;
    .text : { KEEP(*(.text.start)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
