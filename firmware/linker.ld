/* GD32F303CB - app linked to run from the bootloader's app slot at 0x08005000.
 * The stock bootloader erases 0xd000 (52K) at 0x08005000 and flashes our image
 * there; it never touches 0x08000000-0x08004000, so it's always our un-brick path. */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08005000, LENGTH = 52K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 48K
}

/* Stack at MID-RAM (0x20008000), NOT the top of RAM. The stock bootloader corrupts the top of
 * RAM while flashing/launching an app image larger than ~900 bytes; a stack at 0x2000C000 then
 * takes an imprecise bus fault on its first push (see docs/bring-up-log.md, "The size-threshold
 * fault"). 0x20008000 leaves 16 KB of headroom below the corrupted region - ample for this app. */
_estack = ORIGIN(RAM) + 0x8000;

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

  .text : {
    *(.text*)
    *(.rodata*)
  } > FLASH

  _sidata = LOADADDR(.data);
  .data : {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM AT> FLASH

  .bss : {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM
}
