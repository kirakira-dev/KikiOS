/*
 * x86 Emulator/Interpreter for KikiOS
 * 
 * Executes x86 (i386) instructions on ARM64
 */

#ifndef _X86EMU_H
#define _X86EMU_H

#include <stdint.h>

// x86 Registers
typedef struct {
    // General purpose registers
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    
    // Instruction pointer
    uint32_t eip;
    
    // Flags register
    uint32_t eflags;
    
    // Segment registers (simplified - we use flat model)
    uint16_t cs, ds, es, fs, gs, ss;
} x86_regs_t;

// EFLAGS bits
#define X86_CF  (1 << 0)   // Carry flag
#define X86_PF  (1 << 2)   // Parity flag
#define X86_AF  (1 << 4)   // Auxiliary carry flag
#define X86_ZF  (1 << 6)   // Zero flag
#define X86_SF  (1 << 7)   // Sign flag
#define X86_TF  (1 << 8)   // Trap flag
#define X86_IF  (1 << 9)   // Interrupt flag
#define X86_DF  (1 << 10)  // Direction flag
#define X86_OF  (1 << 11)  // Overflow flag

// Forward declaration
typedef struct x86emu_state x86emu_state_t;

// Emulator state
struct x86emu_state {
    x86_regs_t regs;
    
    // Memory bounds
    uint8_t *mem_base;
    uint32_t mem_size;
    
    // Stack
    uint8_t *stack;
    uint32_t stack_size;
    uint32_t stack_base;
    
    // Execution state
    int running;
    int halted;
    int exception;
    
    // Windows API callback
    int (*winapi_call)(x86emu_state_t *emu, const char *dll, const char *func);
    
    // Console output callback
    void (*console_write)(const char *str, int len);
    
    // Import address table (for API resolution)
    struct {
        uint32_t addr;
        const char *dll;
        const char *func;
    } imports[256];
    int num_imports;
    
    // Instruction counter (for debugging/limiting)
    uint64_t insn_count;
    uint64_t max_insns;
};

// Emulator functions
int x86emu_init(x86emu_state_t *emu, uint8_t *mem, uint32_t mem_size);
void x86emu_reset(x86emu_state_t *emu);
int x86emu_set_entry(x86emu_state_t *emu, uint32_t entry_point);
int x86emu_run(x86emu_state_t *emu);
int x86emu_step(x86emu_state_t *emu);
void x86emu_dump_regs(x86emu_state_t *emu);

// Memory access
uint8_t  x86emu_read8(x86emu_state_t *emu, uint32_t addr);
uint16_t x86emu_read16(x86emu_state_t *emu, uint32_t addr);
uint32_t x86emu_read32(x86emu_state_t *emu, uint32_t addr);
void x86emu_write8(x86emu_state_t *emu, uint32_t addr, uint8_t val);
void x86emu_write16(x86emu_state_t *emu, uint32_t addr, uint16_t val);
void x86emu_write32(x86emu_state_t *emu, uint32_t addr, uint32_t val);

// Stack operations
void x86emu_push32(x86emu_state_t *emu, uint32_t val);
uint32_t x86emu_pop32(x86emu_state_t *emu);

// Add import entry
void x86emu_add_import(x86emu_state_t *emu, uint32_t addr, const char *dll, const char *func);

#endif // _X86EMU_H
