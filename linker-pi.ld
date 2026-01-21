/*
 * KikiOS Linker Script - Raspberry Pi Zero 2W
 *
 * Memory layout for Pi Zero 2W (BCM2710)
 * GPU loads kernel8.img to 0x80000
 *
 * Pi Zero 2W has 512MB RAM:
 *   0x00000000 - 0x0007FFFF: GPU/bootloader reserved
 *   0x00080000 - ...       : Our kernel
 *
 * Unlike QEMU, there's no separate flash region.
 * Everything runs from RAM.
 */

ENTRY(_start)

MEMORY
{
    /* Pi loads kernel at 0x80000 */
    RAM (rwx) : ORIGIN = 0x00080000, LENGTH = 480M
}

SECTIONS
{
    /* Kernel base address for debug info */
    _kernel_start = 0x00080000;

    /* Boot code first */
    .text.boot : {
        *(.text.boot)
    } > RAM

    /* Rest of code */
    .text : {
        *(.text)
        *(.text.*)
    } > RAM

    /* Read-only data */
    .rodata : ALIGN(8) {
        *(.rodata)
        *(.rodata.*)
    } > RAM

    /* Initialized data */
    .data : ALIGN(8) {
        _data_start = .;
        *(.data)
        *(.data.*)
        /* GOT sections must be included (contain runtime addresses) */
        *(.got)
        *(.got.plt)
        _data_end = .;
    } > RAM

    /* For compatibility with QEMU linker script */
    _data_load = _data_start;

    /* BSS section (uninitialized data) */
    .bss : ALIGN(8) {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    /* DMA buffers - placed in uncached GPU memory region (0x1C000000+) */
    /* This region is mapped as Device memory, so no cache maintenance needed */
    .dma_buffers 0x1C100000 (NOLOAD) : {
        _dma_start = .;
        *(.dma_buffers)
        *(.dma_buffers.*)
        . = ALIGN(64);
        _dma_end = .;
    }

    /* Page tables are now defined in boot-pi.S with explicit .align 12 */

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.stack)
    }
}
