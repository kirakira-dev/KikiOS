// Minimal setjmp/longjmp for aarch64
// Used by MicroPython GC to scan registers

.global setjmp
.global longjmp

// int setjmp(jmp_buf env)
// x0 = pointer to jmp_buf
setjmp:
    // Save callee-saved registers
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]    // frame pointer, link register
    mov x1, sp
    str x1, [x0, #96]          // stack pointer
    mov x0, #0                 // return 0 on first call
    ret

// void longjmp(jmp_buf env, int val)
// x0 = pointer to jmp_buf
// x1 = return value (or 1 if 0)
longjmp:
    // Restore callee-saved registers
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]
    ldr x2, [x0, #96]
    mov sp, x2
    // Return val, or 1 if val is 0
    cmp x1, #0
    csinc x0, x1, x1, ne       // x0 = (x1 != 0) ? x1 : x1+1
    ret                        // returns to caller of setjmp via x30
