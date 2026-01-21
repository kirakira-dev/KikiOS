/*
 * KikiOS Linker Script
 *
 * Memory layout for QEMU virt machine (aarch64)
 * Using -bios mode: flash starts at 0x0, RAM at 0x40000000
 * We boot at EL3 with full hardware access
 *
 * Code runs from flash (0x0), data/BSS in RAM
 *
 * IMPORTANT: QEMU places the DTB at 0x40000000 (start of RAM).
 * We start our RAM usage at 0x40010000 to preserve the DTB.
 * DTB is typically ~8-16KB, so 64KB buffer is plenty.
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 64M
    /* Skip first 2MB of RAM - that's where the DTB lives (can be up to 1MB+) */
    RAM   (rwx) : ORIGIN = 0x40200000, LENGTH = 256M - 2M
}

SECTIONS
{
    /* Kernel base address for debug info */
    _kernel_start = 0x00000000;

    /* Boot code at start of flash */
    .text.boot : {
        *(.text.boot)
    } > FLASH

    /* Rest of code in flash */
    .text : {
        *(.text)
        *(.text.*)
    } > FLASH

    /* Read-only data in flash */
    .rodata : ALIGN(8) {
        *(.rodata)
        *(.rodata.*)
    } > FLASH

    /* End of flash content - save for data copy (must be 8-byte aligned for ldr) */
    . = ALIGN(8);
    _data_load = .;

    /* Initialized data - loaded from flash, runs in RAM */
    .data : AT(_data_load) {
        _data_start = .;
        *(.data)
        *(.data.*)
        /* GOT sections must be in RAM (contain runtime addresses) */
        *(.got)
        *(.got.plt)
        _data_end = .;
    } > RAM

    /* BSS section (uninitialized data) in RAM */
    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.stack)
    }
}
