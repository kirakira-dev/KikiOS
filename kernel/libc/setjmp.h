// Minimal setjmp for aarch64
#ifndef _SETJMP_H
#define _SETJMP_H

// jmp_buf needs to hold:
// x19-x28 (10 callee-saved regs)
// x29 (frame pointer)
// x30 (link register)
// sp (stack pointer)
// Total: 13 * 8 = 104 bytes, round up to 128 for alignment
typedef unsigned long jmp_buf[16];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
