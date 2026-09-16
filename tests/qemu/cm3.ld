/* lm3s6965evb: 256K flash at 0, 64K SRAM at 0x20000000. */
MEMORY {
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
  SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
ENTRY(reset_handler)
SECTIONS {
  .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > FLASH
  _data_load = LOADADDR(.data);
  .data : { _data_start = .; *(.data*) . = ALIGN(4); _data_end = .; } > SRAM AT > FLASH
  .bss : { _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > SRAM
  _stack_top = ORIGIN(SRAM) + LENGTH(SRAM);
}
