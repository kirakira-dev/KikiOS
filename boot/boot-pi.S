/*
 * KikiOS - Raspberry Pi Zero 2W Bootloader
 *
 * Entry point for Pi Zero 2W (BCM2710/Cortex-A53).
 * GPU loads kernel8.img to 0x80000 and starts CPU at EL2.
 *
 * Memory layout:
 *   0x00000000 - 0x0007FFFF: Reserved (GPU, bootloader)
 *   0x00080000 - ...       : Kernel code (.text)
 *   Stack at end of 512MB RAM (0x1F000000)
 */

.section ".text.boot"

.global _start

_start:
    // Save DTB pointer (firmware passes it in x0) to callee-saved register
    mov     x19, x0

    // Get the CPU ID - only CPU 0 should initialize
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, primary_cpu

    // Secondary CPUs go to sleep
secondary_cpu:
    wfe
    b       secondary_cpu

primary_cpu:
    // Check current exception level
    mrs     x0, CurrentEL
    lsr     x0, x0, #2          // CurrentEL is in bits [3:2]

    cmp     x0, #3
    b.eq    drop_from_el3
    cmp     x0, #2
    b.eq    drop_from_el2
    cmp     x0, #1
    b.eq    at_el1
    // Unknown EL, hang
    b       secondary_cpu

drop_from_el3:
    // We're at EL3 - drop to EL1 (skip EL2)

    // Configure SCR_EL3 (Secure Configuration Register)
    mov     x0, #0x430          // RW=1, NS=0 (secure), no HCE
    msr     scr_el3, x0
    isb

    // Invalidate I-cache before enabling (contains stale GPU bootloader code)
    ic      iallu
    dsb     ish
    isb

    // Initialize SCTLR_EL1 - enable I-cache for faster code execution
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)   // Clear M bit (MMU off)
    bic     x0, x0, #(1 << 2)   // Clear C bit (data cache off - needs MMU for safety)
    orr     x0, x0, #(1 << 12)  // Set I bit (instruction cache ON)
    msr     sctlr_el1, x0
    isb

    // Set up SPSR for EL1h
    mov     x0, #0x3c5          // DAIF masked, EL1h
    msr     spsr_el3, x0

    // Set return address to at_el1
    adr     x0, at_el1
    msr     elr_el3, x0

    eret

drop_from_el2:
    // We're at EL2, need to drop to EL1
    // Pi typically starts at EL2

    // Enable AArch64 for EL1
    mov     x0, #(1 << 31)      // RW=1: EL1 is AArch64
    msr     hcr_el2, x0

    // Invalidate I-cache before enabling (contains stale GPU bootloader code)
    ic      iallu
    dsb     ish
    isb

    // Initialize SCTLR_EL1 - enable I-cache for faster code execution
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)   // Clear M bit (MMU off)
    bic     x0, x0, #(1 << 2)   // Clear C bit (data cache off - needs MMU for safety)
    orr     x0, x0, #(1 << 12)  // Set I bit (instruction cache ON)
    msr     sctlr_el1, x0
    isb

    // Set up SPSR for EL1h
    mov     x0, #0x3c5          // DAIF masked, EL1h
    msr     spsr_el2, x0

    // Set return address to at_el1
    adr     x0, at_el1
    msr     elr_el2, x0

    eret

at_el1:
    // Enable FPU/SIMD (set CPACR_EL1.FPEN = 0b11)
    mov     x0, #(3 << 20)        // FPEN bits [21:20] = 0b11
    msr     cpacr_el1, x0
    isb

    // Set up the stack pointer
    // Pi Zero 2W: cacheable RAM is 0x00000000 - 0x1BFFFFFF (448MB)
    // GPU memory at 0x1C000000+ is non-cacheable, so stack must be below that
    // Stack at 0x1B000000 (432MB) - in cacheable region
    ldr     x0, =0x1B000000
    mov     sp, x0

    // Set up MMU (D-cache will be enabled later from C code)
    bl      setup_mmu

    // D-cache NOT enabled here - will be enabled from kernel after init

    // Clear BSS section
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
clear_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       clear_bss
bss_done:

    // Install exception vector table
    ldr     x0, =exception_vectors
    msr     vbar_el1, x0
    isb

    // Initialize HAL serial (for any early debug via GPIO if connected)
    bl      hal_serial_init

    // Jump to kernel main with DTB pointer
    mov     x0, x19
    bl      kernel_main

    // If kernel_main returns, halt
halt:
    wfe
    b       halt

/*
 * setup_mmu - Configure identity-mapped MMU for D-cache
 *
 * Page table structure (T0SZ=25, 39-bit VA, 4KB granule):
 *   L1 (root): Each entry covers 1GB, points to L2 tables
 *   L2: Each entry is a 2MB block descriptor
 *
 * Memory map:
 *   0x00000000 - 0x3DFFFFFF: Normal Cacheable (RAM) - in L2_0
 *   0x3E000000 - 0x3FFFFFFF: Device (BCM peripherals) - in L2_0
 *   0x40000000 - 0x40FFFFFF: Device (ARM Local) - in L2_1
 *
 * This enables D-cache for ~100x faster memory access.
 */
setup_mmu:
    // NOTE: No stack usage here! This is a leaf function and we must avoid
    // memory accesses across the D-cache enable boundary to prevent coherency
    // hazards. LR (x30) stays in register throughout.

    // --- Step 1: Clear all 3 page tables (L1 + two L2s) ---
    ldr     x0, =page_table_l1
    mov     x1, #(512 * 3)          // 3 tables × 512 entries
1:  str     xzr, [x0], #8
    subs    x1, x1, #1
    b.ne    1b

    // --- Step 2: Set up L1 (root) entries pointing to L2 tables ---
    ldr     x0, =page_table_l1

    // L1[0] → L2_0 (covers 0x00000000 - 0x3FFFFFFF, first 1GB)
    ldr     x1, =page_table_l2_0
    orr     x1, x1, #0x3            // Table descriptor: valid=1, type=1
    str     x1, [x0]

    // L1[1] → L2_1 (covers 0x40000000 - 0x7FFFFFFF, second 1GB)
    ldr     x1, =page_table_l2_1
    orr     x1, x1, #0x3
    str     x1, [x0, #8]

    // --- Step 3: Fill L2_0 (first 1GB: RAM + BCM peripherals) ---
    ldr     x0, =page_table_l2_0

    // RAM blocks: entries 0-223 (0x00000000 - 0x1BFFFFFF) = 448MB
    // This is ARM-accessible RAM with gpu_mem=64
    mov     x1, #0                  // Physical address starts at 0
    ldr     x2, =0x705              // Normal Cacheable attributes (AttrIndx=1)
    mov     x3, #224                // 224 entries × 2MB = 448MB
2:  orr     x4, x1, x2
    str     x4, [x0], #8
    add     x1, x1, #0x200000       // Next 2MB block
    subs    x3, x3, #1
    b.ne    2b

    // GPU memory: entries 224-255 (0x1C000000 - 0x1FFFFFFF) = 64MB
    // Framebuffer lives here - must be Non-Cacheable for GPU coherency!
    // Use Device-nGnRnE (AttrIndx=0) so GPU sees our writes immediately
    ldr     x1, =0x1C000000         // GPU memory starts at 448MB
    ldr     x2, =0x401              // Device attributes (AttrIndx=0, non-cacheable)
    mov     x3, #32                 // 32 entries × 2MB = 64MB
5:  orr     x4, x1, x2
    str     x4, [x0], #8
    add     x1, x1, #0x200000
    subs    x3, x3, #1
    b.ne    5b

    // GAP: entries 256-503 left invalid (0x20000000 - 0x3EFFFFFF)
    // Skip to entry 504: (504-256) × 8 = 1984 bytes
    add     x0, x0, #1984

    // BCM peripheral blocks: entries 504-511 (0x3F000000 - 0x3FFFFFFF)
    ldr     x1, =0x3F000000         // Peripheral base (same on Pi 3B and Zero 2W)
    ldr     x2, =0x401              // Device attributes (AttrIndx=0)
    mov     x3, #8                  // 8 entries = 16MB
3:  orr     x4, x1, x2
    str     x4, [x0], #8
    add     x1, x1, #0x200000
    subs    x3, x3, #1
    b.ne    3b

    // --- Step 4: Fill L2_1 (second 1GB: ARM Local controller) ---
    ldr     x0, =page_table_l2_1

    // ARM Local blocks: entries 0-7 (0x40000000 - 0x40FFFFFF)
    // Physical address 0x40000000 maps to L2_1[0]
    ldr     x1, =0x40000000         // Start at 1GB mark
    ldr     x2, =0x401              // Device attributes
    mov     x3, #8                  // 8 entries = 16MB
4:  orr     x4, x1, x2
    str     x4, [x0], #8
    add     x1, x1, #0x200000
    subs    x3, x3, #1
    b.ne    4b

    // --- Step 5: Set up MAIR_EL1 ---
    // Attr0 (index 0): Device-nGnRnE = 0x00
    // Attr1 (index 1): Normal, Write-Back Cacheable = 0xFF
    ldr     x0, =0x000000000000FF00
    msr     mair_el1, x0
    isb

    // --- Step 6: Set up TCR_EL1 ---
    // T0SZ = 25 (39-bit VA space, 512GB)
    // IRGN0 = 0b01 (Inner Write-Back)
    // ORGN0 = 0b01 (Outer Write-Back)
    // SH0 = 0b11 (Inner Shareable)
    // TG0 = 0b00 (4KB granule)
    // IPS = 0b010 (40-bit PA, 1TB)
    ldr     x0, =0x0000000280803519
    msr     tcr_el1, x0
    isb

    // --- Step 7: Set up TTBR0_EL1 ---
    ldr     x0, =page_table_l1
    msr     ttbr0_el1, x0
    isb

    // --- Step 8: Invalidate TLBs ---
    tlbi    vmalle1
    dsb     sy
    isb

    // --- Step 9: Clean and Invalidate D-cache before enabling ---
    // Clean first (write back dirty data from GPU bootloader), then invalidate.
    // This iterates through all cache sets/ways (L1 and L2)
    mrs     x0, clidr_el1           // Cache Level ID Register
    and     w3, w0, #0x07000000     // Extract LoC (Level of Coherence)
    lsr     w3, w3, #23             // Right-align LoC
    cbz     w3, dcache_done         // No cache levels? Skip

    mov     w10, #0                 // Start at cache level 0
dcache_level:
    add     w2, w10, w10, lsr #1    // w2 = level * 1.5 (for CLIDR indexing)
    lsr     w1, w0, w2              // Extract cache type
    and     w1, w1, #7
    cmp     w1, #2                  // Type 2 or 3 means D-cache
    b.lt    dcache_next_level

    msr     csselr_el1, x10         // Select this cache level
    isb
    mrs     x1, ccsidr_el1          // Read cache geometry
    and     w2, w1, #7              // Line size field
    add     w2, w2, #4              // Add 4 to get log2(line size)
    ubfx    w4, w1, #3, #10         // Number of ways - 1
    clz     w5, w4                  // Bit position for way
    ubfx    w7, w1, #13, #15        // Number of sets - 1

dcache_set:
    mov     w6, w4                  // Reset way counter
dcache_way:
    lsl     w11, w6, w5
    lsl     w12, w7, w2
    orr     w11, w11, w10
    orr     w11, w11, w12
    dc      cisw, x11               // Clean AND Invalidate by set/way
    subs    w6, w6, #1
    b.ge    dcache_way
    subs    w7, w7, #1
    b.ge    dcache_set

dcache_next_level:
    add     w10, w10, #2            // Next level
    cmp     w3, w10
    b.gt    dcache_level

dcache_done:
    dsb     sy
    isb

    // --- Step 10: Enable MMU only (D-cache enabled separately after return) ---
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 0)       // M = 1 (MMU enable)
    // D-cache will be enabled after function returns
    orr     x0, x0, #(1 << 12)      // I = 1 (I-cache enable)
    msr     sctlr_el1, x0
    isb

    // Return directly - x30 (LR) was never modified, no stack restore needed
    ret

/*
 * enable_dcache - Enable D-cache after MMU is already running
 * Call this AFTER setup_mmu returns and we're running with MMU
 */
.globl enable_dcache
enable_dcache:
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 2)       // C = 1 (D-cache enable)
    msr     sctlr_el1, x0
    isb
    ret

/*
 * Page tables - in .data section to avoid BSS clearing
 *
 * CRITICAL: These MUST NOT be in .bss!
 * Boot sequence: setup_mmu() fills tables → enable MMU → clear_bss()
 * If tables are in .bss, clear_bss() zeros the active page tables = crash
 *
 * Putting them in .data adds 12KB to binary but ensures they survive BSS clear.
 */
.section ".data"
.align 12                       // 4KB alignment (2^12 = 4096)

.global page_table_l1
page_table_l1:
    .space 4096

.global page_table_l2_0
page_table_l2_0:
    .space 4096

.global page_table_l2_1
page_table_l2_1:
    .space 4096
