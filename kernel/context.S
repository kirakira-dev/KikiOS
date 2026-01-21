/*
 * KikiOS Context Switch
 *
 * Saves ALL registers for preemptive multitasking.
 * Used for voluntary context switches (process_schedule).
 * The IRQ handler in vectors.S handles preemptive switches.
 *
 * AArch64 cpu_context_t layout:
 *   0x000 - 0x0F0: x[0-30] (31 registers, 248 bytes)
 *   0x0F8: sp
 *   0x100: pc (return address)
 *   0x108: pstate
 *   0x110: fpcr
 *   0x118: fpsr
 *   0x120: fp_regs[0-63] (q0-q31, 512 bytes)
 */

.global context_switch

/*
 * void context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx)
 *
 * old_ctx: x0 - where to save current context (can be NULL to skip save)
 * new_ctx: x1 - context to restore
 *
 * Saves all registers to old_ctx, restores from new_ctx, returns to new process.
 */

context_switch:
    // Save current context to old_ctx (x0)
    // If old_ctx is NULL, skip saving
    cbz     x0, .Lrestore

    // Save x2-x30 first (we need x2 as scratch)
    stp     x2,  x3,  [x0, #0x10]
    stp     x4,  x5,  [x0, #0x20]
    stp     x6,  x7,  [x0, #0x30]
    stp     x8,  x9,  [x0, #0x40]
    stp     x10, x11, [x0, #0x50]
    stp     x12, x13, [x0, #0x60]
    stp     x14, x15, [x0, #0x70]
    stp     x16, x17, [x0, #0x80]
    stp     x18, x19, [x0, #0x90]
    stp     x20, x21, [x0, #0xa0]
    stp     x22, x23, [x0, #0xb0]
    stp     x24, x25, [x0, #0xc0]
    stp     x26, x27, [x0, #0xd0]
    stp     x28, x29, [x0, #0xe0]
    str     x30, [x0, #0xf0]

    // Now save x0, x1 (our parameters - need to save their values from caller)
    // x0 was old_ctx, x1 was new_ctx - these are the values at entry
    // Actually we need the CALLER's x0, x1 which are lost. For voluntary switch,
    // the caller doesn't care about x0, x1 (they're caller-saved).
    // Just save current values which are fine for voluntary switch.
    mov     x2, x0          // Save old_ctx pointer
    mov     x3, x1          // Save new_ctx pointer
    stp     x2, x3, [x0, #0x00]  // Save x0, x1 (just use parameter values)

    // Save sp
    mov     x4, sp
    str     x4, [x2, #0xf8]

    // Save pc (return address) - x30 contains where to return
    str     x30, [x2, #0x100]

    // Save pstate - need full SPSR-compatible value for eret compatibility
    // CurrentEL gives us current exception level, DAIF gives interrupt masks
    // For EL1h (EL1 using SP_EL1), mode bits should be 0b00101 = 5
    // Build: DAIF[9:6] | mode[4:0]
    mrs     x4, daif            // Get DAIF flags (bits 9:6)
    mov     x5, #5              // EL1h mode
    orr     x4, x4, x5          // Combine DAIF with mode
    str     x4, [x2, #0x108]

    // Save FPU control/status registers
    mrs     x4, fpcr
    str     x4, [x2, #0x110]
    mrs     x4, fpsr
    str     x4, [x2, #0x118]

    // Save FP registers q0-q31 (each is 128-bit = 16 bytes)
    add     x4, x2, #0x120
    stp     q0,  q1,  [x4, #0x00]
    stp     q2,  q3,  [x4, #0x20]
    stp     q4,  q5,  [x4, #0x40]
    stp     q6,  q7,  [x4, #0x60]
    stp     q8,  q9,  [x4, #0x80]
    stp     q10, q11, [x4, #0xa0]
    stp     q12, q13, [x4, #0xc0]
    stp     q14, q15, [x4, #0xe0]
    stp     q16, q17, [x4, #0x100]
    stp     q18, q19, [x4, #0x120]
    stp     q20, q21, [x4, #0x140]
    stp     q22, q23, [x4, #0x160]
    stp     q24, q25, [x4, #0x180]
    stp     q26, q27, [x4, #0x1a0]
    stp     q28, q29, [x4, #0x1c0]
    stp     q30, q31, [x4, #0x1e0]

    // Restore new_ctx pointer (saved in x3)
    mov     x1, x3

.Lrestore:
    // Restore context from new_ctx (x1)

    // Restore FP registers first (before we clobber x1)
    add     x2, x1, #0x120
    ldp     q0,  q1,  [x2, #0x00]
    ldp     q2,  q3,  [x2, #0x20]
    ldp     q4,  q5,  [x2, #0x40]
    ldp     q6,  q7,  [x2, #0x60]
    ldp     q8,  q9,  [x2, #0x80]
    ldp     q10, q11, [x2, #0xa0]
    ldp     q12, q13, [x2, #0xc0]
    ldp     q14, q15, [x2, #0xe0]
    ldp     q16, q17, [x2, #0x100]
    ldp     q18, q19, [x2, #0x120]
    ldp     q20, q21, [x2, #0x140]
    ldp     q22, q23, [x2, #0x160]
    ldp     q24, q25, [x2, #0x180]
    ldp     q26, q27, [x2, #0x1a0]
    ldp     q28, q29, [x2, #0x1c0]
    ldp     q30, q31, [x2, #0x1e0]

    // Restore FPU control/status registers
    ldr     x2, [x1, #0x110]
    msr     fpcr, x2
    ldr     x2, [x1, #0x118]
    msr     fpsr, x2

    // Restore sp
    ldr     x2, [x1, #0xf8]
    mov     sp, x2

    // Set up elr_el1 and spsr_el1 for eret (to properly restore PSTATE/IRQ state)
    ldr     x2, [x1, #0x100]    // pc -> elr_el1
    msr     elr_el1, x2
    ldr     x2, [x1, #0x108]    // pstate -> spsr_el1
    msr     spsr_el1, x2

    // Restore general purpose registers x2-x30
    ldp     x2,  x3,  [x1, #0x10]
    ldp     x4,  x5,  [x1, #0x20]
    ldp     x6,  x7,  [x1, #0x30]
    ldp     x8,  x9,  [x1, #0x40]
    ldp     x10, x11, [x1, #0x50]
    ldp     x12, x13, [x1, #0x60]
    ldp     x14, x15, [x1, #0x70]
    ldp     x16, x17, [x1, #0x80]
    ldp     x18, x19, [x1, #0x90]
    ldp     x20, x21, [x1, #0xa0]
    ldp     x22, x23, [x1, #0xb0]
    ldp     x24, x25, [x1, #0xc0]
    ldp     x26, x27, [x1, #0xd0]
    ldp     x28, x29, [x1, #0xe0]
    ldr     x30, [x1, #0xf0]

    // Restore x0, x1 last
    ldp     x0, x1, [x1, #0x00]

    // Use eret to properly restore PSTATE (including IRQ enable state)
    eret
