/*
 * KikiOS - aarch64 Bootloader
 *
 * Entry point for the kernel. Sets up the stack and jumps to C code.
 * Targets QEMU virt machine.
 */

.section ".text.boot"

.global _start

_start:
    // Get the CPU ID - only CPU 0 should initialize
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, primary_cpu

    // Secondary CPUs go to sleep
secondary_cpu:
    wfe
    b       secondary_cpu

primary_cpu:
    // Debug: print current EL to UART
    mov     x1, #0x09000000
    mov     w2, #'E'
    str     w2, [x1]
    mov     w2, #'L'
    str     w2, [x1]

    // Check current exception level
    mrs     x0, CurrentEL
    lsr     x0, x0, #2          // CurrentEL is in bits [3:2]

    // Print EL number
    add     w2, w0, #'0'
    str     w2, [x1]
    mov     w2, #'\n'
    str     w2, [x1]

    cmp     x0, #3
    b.eq    drop_from_el3
    cmp     x0, #2
    b.eq    drop_from_el2
    cmp     x0, #1
    b.eq    at_el1
    // Unknown EL, hang
    b       secondary_cpu

drop_from_el3:
    // Debug: print '3'
    mov     x1, #0x09000000
    mov     w2, #'3'
    str     w2, [x1]

    // We're at EL3 (Secure) - drop DIRECTLY to EL1 (skip EL2)
    // Stay in Secure world so we have full GIC access

    // Configure SCR_EL3 (Secure Configuration Register)
    // Bit 10 (RW): Lower ELs are AArch64
    // Bit 0 (NS): Non-secure bit (0 = SECURE)
    // Don't set HCE since we're skipping EL2
    mov     x0, #0x430          // RW=1, NS=0 (secure), no HCE
    msr     scr_el3, x0
    isb

    mov     w2, #'a'
    str     w2, [x1]

    // Initialize SCTLR_EL1 to safe defaults (caches off, MMU off)
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)   // Clear M bit (MMU off)
    bic     x0, x0, #(1 << 2)   // Clear C bit (data cache off)
    bic     x0, x0, #(1 << 12)  // Clear I bit (instruction cache off)
    msr     sctlr_el1, x0
    isb

    mov     w2, #'b'
    str     w2, [x1]

    // Set up SPSR for EL1h (EL1 with SP_EL1, all exceptions masked)
    mov     x0, #0x3c5          // DAIF masked, EL1h
    msr     spsr_el3, x0

    mov     w2, #'c'
    str     w2, [x1]

    // Set return address to at_el1
    adr     x0, at_el1
    msr     elr_el3, x0

    mov     w2, #'d'
    str     w2, [x1]

    // Return directly to EL1
    eret

drop_from_el2:
    // Debug: print '2'
    mov     x1, #0x09000000
    mov     w2, #'2'
    str     w2, [x1]

    // We're at EL2, need to drop to EL1

    // Enable AArch64 for EL1
    mov     x0, #(1 << 31)      // RW=1: EL1 is AArch64
    msr     hcr_el2, x0

    // Set up SPSR for EL1h (EL1 with SP_EL1)
    mov     x0, #0x3c5          // DAIF masked, EL1h
    msr     spsr_el2, x0

    // Set return address to at_el1
    adr     x0, at_el1
    msr     elr_el2, x0

    mov     w2, #'\n'
    str     w2, [x1]

    // Return to EL1
    eret

at_el1:
    // x9 = UART base for debug output
    mov     x9, #0x09000000

    // Debug: confirm we're at EL1
    mov     w10, #'!'
    str     w10, [x9]
    mov     w10, #'1'
    str     w10, [x9]
    mov     w10, #'\n'
    str     w10, [x9]

    // Enable FPU/SIMD (set CPACR_EL1.FPEN = 0b11)
    mov     w10, #'F'
    str     w10, [x9]
    mov     x0, #(3 << 20)        // FPEN bits [21:20] = 0b11
    msr     cpacr_el1, x0
    isb
    mov     w10, #'f'
    str     w10, [x9]

    // Set up the stack pointer (stack grows downward)
    // Note: DTB is at 0x40000000, our RAM starts at 0x40010000 (linker.ld)
    // Stack at 0x5F000000 - near end of 512MB RAM (ends at 0x60000000)
    mov     w10, #'S'
    str     w10, [x9]
    ldr     x0, =0x5F000000       // Stack top in RAM (above heap and programs)
    mov     sp, x0
    mov     w10, #'s'
    str     w10, [x9]

    // Copy .data section from flash (LMA) to RAM (VMA)
    mov     w10, #'D'
    str     w10, [x9]
    ldr     x0, =_data_load       // Source (in flash)
    ldr     x1, =_data_start      // Dest (in RAM)
    ldr     x2, =_data_end        // End of dest
copy_data:
    cmp     x1, x2
    b.ge    data_done
    ldr     x3, [x0], #8
    str     x3, [x1], #8
    b       copy_data
data_done:
    mov     w10, #'d'
    str     w10, [x9]

    // Clear BSS section (in RAM)
    mov     w10, #'B'
    str     w10, [x9]
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
clear_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       clear_bss
bss_done:
    mov     w10, #'b'
    str     w10, [x9]

    // Install exception vector table
    mov     w10, #'V'
    str     w10, [x9]
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb
    mov     w10, #'v'
    str     w10, [x9]

    // Jump to kernel main
    mov     w10, #'K'
    str     w10, [x9]
    bl      kernel_main

    // If kernel_main returns, halt
halt:
    wfe
    b       halt

// Stack is in its own section, placed after BSS by linker
.section ".stack", "aw", @nobits
.align 16
_stack_bottom:
    .skip 0x10000  // 64KB stack
_stack_top:
