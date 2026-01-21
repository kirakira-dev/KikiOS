/*
 * KikiOS Exception Vector Table
 *
 * AArch64 exception vectors must be 2KB aligned, with each entry 128 bytes.
 * There are 16 entries total (4 exception types x 4 exception sources).
 *
 * Supports preemptive multitasking - IRQ handler saves/restores full context.
 */

// Offset of cpu_context_t within process_t (calculated from struct layout)
// Must match the actual offset in process.h!
#define CONTEXT_OFFSET 0x50

.section .text

// Each vector entry is 128 bytes (32 instructions max)
.macro VECTOR_ENTRY label
.align 7
\label:
.endm

// Save all general-purpose registers to stack (for kernel/sync handlers)
.macro SAVE_REGS
    sub     sp, sp, #272         // 256 for GPRs + 16 for elr/spsr
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #256]
.endm

// Restore all general-purpose registers from stack
.macro RESTORE_REGS
    ldp     x0, x1, [sp, #256]
    msr     elr_el1, x0
    msr     spsr_el1, x1
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #272
.endm

/*
 * Exception Vector Table
 * Must be 2KB (0x800) aligned
 */

.align 11   // 2KB aligned
.global exception_vectors
exception_vectors:

// Current EL with SP0 (not used - we use SPx)
VECTOR_ENTRY vec_sync_sp0
    b       sync_handler

VECTOR_ENTRY vec_irq_sp0
    b       irq_handler_entry

VECTOR_ENTRY vec_fiq_sp0
    b       fiq_handler

VECTOR_ENTRY vec_serror_sp0
    b       serror_handler

// Current EL with SPx (this is what we use)
VECTOR_ENTRY vec_sync_spx
    b       sync_handler

VECTOR_ENTRY vec_irq_spx
    b       irq_handler_entry

VECTOR_ENTRY vec_fiq_spx
    b       fiq_handler

VECTOR_ENTRY vec_serror_spx
    b       serror_handler

// Lower EL using AArch64 (userspace)
VECTOR_ENTRY vec_sync_a64
    b       sync_handler

VECTOR_ENTRY vec_irq_a64
    b       irq_handler_entry

VECTOR_ENTRY vec_fiq_a64
    b       fiq_handler

VECTOR_ENTRY vec_serror_a64
    b       serror_handler

// Lower EL using AArch32 (not supported)
VECTOR_ENTRY vec_sync_a32
    b       sync_handler

VECTOR_ENTRY vec_irq_a32
    b       irq_handler_entry

VECTOR_ENTRY vec_fiq_a32
    b       fiq_handler

VECTOR_ENTRY vec_serror_a32
    b       serror_handler

/*
 * Exception Handlers
 */

// Synchronous exception (data abort, instruction abort, SVC, etc.)
sync_handler:
    SAVE_REGS

    // Get exception info
    mrs     x0, esr_el1     // Exception Syndrome Register
    mrs     x1, elr_el1     // Exception Link Register (return address)
    mrs     x2, far_el1     // Fault Address Register
    mov     x3, sp          // Pointer to saved registers on stack

    // Call C handler
    bl      handle_sync_exception

    RESTORE_REGS
    eret

/*
 * IRQ Handler with Preemptive Multitasking Support
 *
 * If a process is running (current_process != NULL):
 *   - Save full context to current_process->context
 *   - Call handle_irq (which may change current_process via scheduler)
 *   - Restore from (possibly different) current_process->context
 *
 * If kernel is running (current_process == NULL):
 *   - Use simple stack-based save/restore
 */
irq_handler_entry:
    // Save x0, x1 temporarily to stack
    stp     x0, x1, [sp, #-16]!

    // Check if a process is running
    adrp    x0, current_process
    ldr     x0, [x0, :lo12:current_process]
    cbnz    x0, .Lprocess_irq

    // ========== KERNEL PATH ==========
    // Restore x0, x1 and use simple stack save
    ldp     x0, x1, [sp], #16
    SAVE_REGS
    bl      handle_irq

    // Check if a process should now run (process_schedule_from_irq may have set current_process)
    dsb     sy
    adrp    x0, current_process
    ldr     x0, [x0, :lo12:current_process]
    cbz     x0, .Lkernel_return

    // A process should run! Save kernel context and switch to it
    // First, we need to save current kernel state to kernel_context
    adrp    x1, kernel_context
    add     x1, x1, :lo12:kernel_context

    // Copy saved regs from stack to kernel_context
    // Stack layout from SAVE_REGS: 272 bytes at sp
    // We need to copy to kernel_context so process_exit can return here
    mov     x2, sp
    ldp     x3, x4, [x2, #0]      // x0, x1
    stp     x3, x4, [x1, #0x00]
    ldp     x3, x4, [x2, #16]     // x2, x3
    stp     x3, x4, [x1, #0x10]
    ldp     x3, x4, [x2, #32]     // x4, x5
    stp     x3, x4, [x1, #0x20]
    ldp     x3, x4, [x2, #48]     // x6, x7
    stp     x3, x4, [x1, #0x30]
    ldp     x3, x4, [x2, #64]     // x8, x9
    stp     x3, x4, [x1, #0x40]
    ldp     x3, x4, [x2, #80]     // x10, x11
    stp     x3, x4, [x1, #0x50]
    ldp     x3, x4, [x2, #96]     // x12, x13
    stp     x3, x4, [x1, #0x60]
    ldp     x3, x4, [x2, #112]    // x14, x15
    stp     x3, x4, [x1, #0x70]
    ldp     x3, x4, [x2, #128]    // x16, x17
    stp     x3, x4, [x1, #0x80]
    ldp     x3, x4, [x2, #144]    // x18, x19
    stp     x3, x4, [x1, #0x90]
    ldp     x3, x4, [x2, #160]    // x20, x21
    stp     x3, x4, [x1, #0xa0]
    ldp     x3, x4, [x2, #176]    // x22, x23
    stp     x3, x4, [x1, #0xb0]
    ldp     x3, x4, [x2, #192]    // x24, x25
    stp     x3, x4, [x1, #0xc0]
    ldp     x3, x4, [x2, #208]    // x26, x27
    stp     x3, x4, [x1, #0xd0]
    ldp     x3, x4, [x2, #224]    // x28, x29
    stp     x3, x4, [x1, #0xe0]
    ldr     x3, [x2, #240]        // x30
    str     x3, [x1, #0xf0]
    ldp     x3, x4, [x2, #256]    // elr, spsr
    str     x3, [x1, #0x100]      // pc
    str     x4, [x1, #0x108]      // pstate

    // Save sp (after removing SAVE_REGS frame)
    add     x3, sp, #272
    str     x3, [x1, #0xf8]

    // Save FPU state to kernel_context (x1 still points to kernel_context)
    mrs     x3, fpcr
    str     x3, [x1, #0x110]
    mrs     x3, fpsr
    str     x3, [x1, #0x118]
    add     x3, x1, #0x120
    stp     q0,  q1,  [x3, #0x00]
    stp     q2,  q3,  [x3, #0x20]
    stp     q4,  q5,  [x3, #0x40]
    stp     q6,  q7,  [x3, #0x60]
    stp     q8,  q9,  [x3, #0x80]
    stp     q10, q11, [x3, #0xa0]
    stp     q12, q13, [x3, #0xc0]
    stp     q14, q15, [x3, #0xe0]
    stp     q16, q17, [x3, #0x100]
    stp     q18, q19, [x3, #0x120]
    stp     q20, q21, [x3, #0x140]
    stp     q22, q23, [x3, #0x160]
    stp     q24, q25, [x3, #0x180]
    stp     q26, q27, [x3, #0x1a0]
    stp     q28, q29, [x3, #0x1c0]
    stp     q30, q31, [x3, #0x1e0]

    // Now switch to the process - restore from current_process (x0)
    // x0 still contains current_process from after handle_irq
    // Add context offset to get cpu_context_t pointer
    add     x0, x0, #CONTEXT_OFFSET

    // Clean up kernel stack first
    add     sp, sp, #272

    // Jump to process restore path
    b       .Lrestore_process

.Lkernel_return:
    RESTORE_REGS
    eret

.Lprocess_irq:
    // ========== PROCESS PATH ==========
    // x0 = current_process (process_t*), original x0/x1 on stack
    // Need to add CONTEXT_OFFSET to get to cpu_context_t

    // Get context pointer: x0 = &current_process->context
    add     x0, x0, #CONTEXT_OFFSET

    // First save x2-x30 (they're still intact!)
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

    // Now pop original x0, x1 from stack and save to context
    ldp     x2, x3, [sp], #16
    stp     x2, x3, [x0, #0x00]     // context.x[0], x[1]

    // Save sp (process stack pointer)
    mov     x1, sp
    str     x1, [x0, #0xf8]

    // Save elr_el1 (pc) and spsr_el1 (pstate)
    mrs     x1, elr_el1
    str     x1, [x0, #0x100]
    mrs     x1, spsr_el1
    str     x1, [x0, #0x108]

    // Save FPU state
    mrs     x1, fpcr
    str     x1, [x0, #0x110]
    mrs     x1, fpsr
    str     x1, [x0, #0x118]
    add     x1, x0, #0x120
    stp     q0,  q1,  [x1, #0x00]
    stp     q2,  q3,  [x1, #0x20]
    stp     q4,  q5,  [x1, #0x40]
    stp     q6,  q7,  [x1, #0x60]
    stp     q8,  q9,  [x1, #0x80]
    stp     q10, q11, [x1, #0xa0]
    stp     q12, q13, [x1, #0xc0]
    stp     q14, q15, [x1, #0xe0]
    stp     q16, q17, [x1, #0x100]
    stp     q18, q19, [x1, #0x120]
    stp     q20, q21, [x1, #0x140]
    stp     q22, q23, [x1, #0x160]
    stp     q24, q25, [x1, #0x180]
    stp     q26, q27, [x1, #0x1a0]
    stp     q28, q29, [x1, #0x1c0]
    stp     q30, q31, [x1, #0x1e0]

    // Call C handler (may change current_process via scheduler)
    bl      handle_irq

    // Memory barrier to ensure we see updated current_process
    dsb     sy
    isb

    // Load (possibly new) current_process
    adrp    x0, current_process
    ldr     x0, [x0, :lo12:current_process]

    // If NULL, something went wrong - shouldn't happen during process IRQ
    cbz     x0, .Lprocess_irq_error

    // Add context offset to get cpu_context_t pointer
    add     x0, x0, #CONTEXT_OFFSET

    // ========== RESTORE FROM PROCESS CONTEXT ==========
.Lrestore_process:
    // x0 = cpu_context_t pointer (current_process->context)

    // Restore FPU state first
    add     x1, x0, #0x120
    ldp     q0,  q1,  [x1, #0x00]
    ldp     q2,  q3,  [x1, #0x20]
    ldp     q4,  q5,  [x1, #0x40]
    ldp     q6,  q7,  [x1, #0x60]
    ldp     q8,  q9,  [x1, #0x80]
    ldp     q10, q11, [x1, #0xa0]
    ldp     q12, q13, [x1, #0xc0]
    ldp     q14, q15, [x1, #0xe0]
    ldp     q16, q17, [x1, #0x100]
    ldp     q18, q19, [x1, #0x120]
    ldp     q20, q21, [x1, #0x140]
    ldp     q22, q23, [x1, #0x160]
    ldp     q24, q25, [x1, #0x180]
    ldp     q26, q27, [x1, #0x1a0]
    ldp     q28, q29, [x1, #0x1c0]
    ldp     q30, q31, [x1, #0x1e0]

    ldr     x1, [x0, #0x110]
    msr     fpcr, x1
    ldr     x1, [x0, #0x118]
    msr     fpsr, x1

    // Restore elr_el1 and spsr_el1
    ldr     x1, [x0, #0x100]
    msr     elr_el1, x1
    ldr     x1, [x0, #0x108]
    msr     spsr_el1, x1

    // Restore sp
    ldr     x1, [x0, #0xf8]
    mov     sp, x1

    // Restore x2-x30
    ldp     x2,  x3,  [x0, #0x10]
    ldp     x4,  x5,  [x0, #0x20]
    ldp     x6,  x7,  [x0, #0x30]
    ldp     x8,  x9,  [x0, #0x40]
    ldp     x10, x11, [x0, #0x50]
    ldp     x12, x13, [x0, #0x60]
    ldp     x14, x15, [x0, #0x70]
    ldp     x16, x17, [x0, #0x80]
    ldp     x18, x19, [x0, #0x90]
    ldp     x20, x21, [x0, #0xa0]
    ldp     x22, x23, [x0, #0xb0]
    ldp     x24, x25, [x0, #0xc0]
    ldp     x26, x27, [x0, #0xd0]
    ldp     x28, x29, [x0, #0xe0]
    ldr     x30, [x0, #0xf0]

    // Restore x0, x1 last (x0 still points to context)
    ldp     x0, x1, [x0, #0x00]

    eret

.Lprocess_irq_error:
    // current_process became NULL during process IRQ - shouldn't happen
    // Just hang for debugging
    b       .Lprocess_irq_error

// FIQ handler (not used)
fiq_handler:
    SAVE_REGS
    bl      handle_fiq
    RESTORE_REGS
    eret

// SError handler (asynchronous abort)
serror_handler:
    SAVE_REGS
    mrs     x0, esr_el1
    bl      handle_serror
    RESTORE_REGS
    eret
