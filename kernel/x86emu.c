/*
 * x86 Emulator/Interpreter for KikiOS
 * 
 * Real-time translation of x86 (i386) instructions to ARM64
 * 
 * This is a simple interpreter that executes one instruction at a time.
 * It supports a subset of x86 instructions sufficient for simple programs.
 */

#include "x86emu.h"
#include "printf.h"
#include "memory.h"
#include <stddef.h>

// External declarations
extern void *memset(void *s, int c, size_t n);
extern void uart_puts(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);

// Parity lookup table
static const uint8_t parity_table[256] = {
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
    0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1
};

// Helper macros
#define SET_FLAG(f)    (emu->regs.eflags |= (f))
#define CLEAR_FLAG(f)  (emu->regs.eflags &= ~(f))
#define GET_FLAG(f)    ((emu->regs.eflags & (f)) != 0)

// Update flags for arithmetic operations
static void update_flags_add32(x86emu_state_t *emu, uint32_t a, uint32_t b, uint32_t result) {
    // Zero flag
    if (result == 0) SET_FLAG(X86_ZF); else CLEAR_FLAG(X86_ZF);
    
    // Sign flag
    if (result & 0x80000000) SET_FLAG(X86_SF); else CLEAR_FLAG(X86_SF);
    
    // Carry flag
    if (result < a) SET_FLAG(X86_CF); else CLEAR_FLAG(X86_CF);
    
    // Overflow flag
    uint32_t sign_a = a & 0x80000000;
    uint32_t sign_b = b & 0x80000000;
    uint32_t sign_r = result & 0x80000000;
    if ((sign_a == sign_b) && (sign_a != sign_r)) SET_FLAG(X86_OF);
    else CLEAR_FLAG(X86_OF);
    
    // Parity flag
    if (parity_table[result & 0xFF]) SET_FLAG(X86_PF); else CLEAR_FLAG(X86_PF);
}

static void update_flags_sub32(x86emu_state_t *emu, uint32_t a, uint32_t b, uint32_t result) {
    // Zero flag
    if (result == 0) SET_FLAG(X86_ZF); else CLEAR_FLAG(X86_ZF);
    
    // Sign flag
    if (result & 0x80000000) SET_FLAG(X86_SF); else CLEAR_FLAG(X86_SF);
    
    // Carry flag (borrow)
    if (a < b) SET_FLAG(X86_CF); else CLEAR_FLAG(X86_CF);
    
    // Overflow flag
    uint32_t sign_a = a & 0x80000000;
    uint32_t sign_b = b & 0x80000000;
    uint32_t sign_r = result & 0x80000000;
    if ((sign_a != sign_b) && (sign_b == sign_r)) SET_FLAG(X86_OF);
    else CLEAR_FLAG(X86_OF);
    
    // Parity flag
    if (parity_table[result & 0xFF]) SET_FLAG(X86_PF); else CLEAR_FLAG(X86_PF);
}

static void update_flags_logic32(x86emu_state_t *emu, uint32_t result) {
    CLEAR_FLAG(X86_CF | X86_OF);
    if (result == 0) SET_FLAG(X86_ZF); else CLEAR_FLAG(X86_ZF);
    if (result & 0x80000000) SET_FLAG(X86_SF); else CLEAR_FLAG(X86_SF);
    if (parity_table[result & 0xFF]) SET_FLAG(X86_PF); else CLEAR_FLAG(X86_PF);
}

// Memory access functions
uint8_t x86emu_read8(x86emu_state_t *emu, uint32_t addr) {
    if (addr < (uint32_t)(uintptr_t)emu->mem_base || 
        addr >= (uint32_t)(uintptr_t)emu->mem_base + emu->mem_size) {
        // Check stack
        if (addr >= emu->stack_base - emu->stack_size && addr < emu->stack_base) {
            return emu->stack[addr - (emu->stack_base - emu->stack_size)];
        }
        emu->exception = 1;
        return 0;
    }
    return *(uint8_t *)(uintptr_t)addr;
}

uint16_t x86emu_read16(x86emu_state_t *emu, uint32_t addr) {
    return x86emu_read8(emu, addr) | (x86emu_read8(emu, addr + 1) << 8);
}

uint32_t x86emu_read32(x86emu_state_t *emu, uint32_t addr) {
    return x86emu_read8(emu, addr) | 
           (x86emu_read8(emu, addr + 1) << 8) |
           (x86emu_read8(emu, addr + 2) << 16) |
           (x86emu_read8(emu, addr + 3) << 24);
}

void x86emu_write8(x86emu_state_t *emu, uint32_t addr, uint8_t val) {
    if (addr < (uint32_t)(uintptr_t)emu->mem_base || 
        addr >= (uint32_t)(uintptr_t)emu->mem_base + emu->mem_size) {
        // Check stack
        if (addr >= emu->stack_base - emu->stack_size && addr < emu->stack_base) {
            emu->stack[addr - (emu->stack_base - emu->stack_size)] = val;
            return;
        }
        emu->exception = 1;
        return;
    }
    *(uint8_t *)(uintptr_t)addr = val;
}

void x86emu_write16(x86emu_state_t *emu, uint32_t addr, uint16_t val) {
    x86emu_write8(emu, addr, val & 0xFF);
    x86emu_write8(emu, addr + 1, (val >> 8) & 0xFF);
}

void x86emu_write32(x86emu_state_t *emu, uint32_t addr, uint32_t val) {
    x86emu_write8(emu, addr, val & 0xFF);
    x86emu_write8(emu, addr + 1, (val >> 8) & 0xFF);
    x86emu_write8(emu, addr + 2, (val >> 16) & 0xFF);
    x86emu_write8(emu, addr + 3, (val >> 24) & 0xFF);
}

// Stack operations
void x86emu_push32(x86emu_state_t *emu, uint32_t val) {
    emu->regs.esp -= 4;
    x86emu_write32(emu, emu->regs.esp, val);
}

uint32_t x86emu_pop32(x86emu_state_t *emu) {
    uint32_t val = x86emu_read32(emu, emu->regs.esp);
    emu->regs.esp += 4;
    return val;
}

// Get register pointer by index
static uint32_t *get_reg32(x86emu_state_t *emu, int reg) {
    switch (reg & 7) {
        case 0: return &emu->regs.eax;
        case 1: return &emu->regs.ecx;
        case 2: return &emu->regs.edx;
        case 3: return &emu->regs.ebx;
        case 4: return &emu->regs.esp;
        case 5: return &emu->regs.ebp;
        case 6: return &emu->regs.esi;
        case 7: return &emu->regs.edi;
    }
    return &emu->regs.eax;
}

// Decode ModR/M byte and get effective address
static uint32_t decode_modrm(x86emu_state_t *emu, uint8_t modrm, int *size) {
    int mod = (modrm >> 6) & 3;
    int rm = modrm & 7;
    *size = 1;
    
    if (mod == 3) {
        // Register operand
        return 0;  // Caller handles register case
    }
    
    uint32_t addr = 0;
    
    if (rm == 4) {
        // SIB byte follows
        uint8_t sib = x86emu_read8(emu, emu->regs.eip + *size);
        (*size)++;
        
        int scale = (sib >> 6) & 3;
        int index = (sib >> 3) & 7;
        int base = sib & 7;
        
        // Base register
        if (base == 5 && mod == 0) {
            // disp32
            addr = x86emu_read32(emu, emu->regs.eip + *size);
            *size += 4;
        } else {
            addr = *get_reg32(emu, base);
        }
        
        // Index register (ESP can't be index)
        if (index != 4) {
            addr += (*get_reg32(emu, index)) << scale;
        }
    } else if (rm == 5 && mod == 0) {
        // disp32
        addr = x86emu_read32(emu, emu->regs.eip + *size);
        *size += 4;
    } else {
        addr = *get_reg32(emu, rm);
    }
    
    // Add displacement
    if (mod == 1) {
        int8_t disp8 = (int8_t)x86emu_read8(emu, emu->regs.eip + *size);
        addr += disp8;
        (*size)++;
    } else if (mod == 2) {
        int32_t disp32 = (int32_t)x86emu_read32(emu, emu->regs.eip + *size);
        addr += disp32;
        *size += 4;
    }
    
    return addr;
}

// Check if address is an import
static int check_import(x86emu_state_t *emu, uint32_t addr) {
    for (int i = 0; i < emu->num_imports; i++) {
        if (emu->imports[i].addr == addr) {
            if (emu->winapi_call) {
                return emu->winapi_call(emu, emu->imports[i].dll, emu->imports[i].func);
            }
            return -1;
        }
    }
    return 0;
}

// Add import entry
void x86emu_add_import(x86emu_state_t *emu, uint32_t addr, const char *dll, const char *func) {
    if (emu->num_imports < 256) {
        emu->imports[emu->num_imports].addr = addr;
        emu->imports[emu->num_imports].dll = dll;
        emu->imports[emu->num_imports].func = func;
        emu->num_imports++;
    }
}

// Execute one instruction
int x86emu_step(x86emu_state_t *emu) {
    if (!emu->running || emu->halted || emu->exception) {
        return -1;
    }
    
    if (emu->max_insns > 0 && emu->insn_count >= emu->max_insns) {
        uart_puts("[x86] Instruction limit reached\r\n");
        emu->halted = 1;
        return -1;
    }
    
    uint32_t eip = emu->regs.eip;
    uint8_t opcode = x86emu_read8(emu, eip);
    int insn_len = 1;
    
    emu->insn_count++;
    
    // Handle prefixes
    int prefix_66 = 0;  // Operand size override
    int prefix_67 = 0;  // Address size override
    int prefix_f2 = 0;  // REPNE
    int prefix_f3 = 0;  // REP
    
    while (opcode == 0x66 || opcode == 0x67 || opcode == 0xF2 || opcode == 0xF3 ||
           opcode == 0x26 || opcode == 0x2E || opcode == 0x36 || opcode == 0x3E ||
           opcode == 0x64 || opcode == 0x65) {
        if (opcode == 0x66) prefix_66 = 1;
        if (opcode == 0x67) prefix_67 = 1;
        if (opcode == 0xF2) prefix_f2 = 1;
        if (opcode == 0xF3) prefix_f3 = 1;
        eip++;
        insn_len++;
        opcode = x86emu_read8(emu, eip);
    }
    
    // Decode and execute
    switch (opcode) {
        // NOP
        case 0x90:
            break;
            
        // PUSH reg32
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            x86emu_push32(emu, *get_reg32(emu, opcode - 0x50));
            break;
            
        // POP reg32
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            *get_reg32(emu, opcode - 0x58) = x86emu_pop32(emu);
            break;
            
        // PUSH imm32
        case 0x68: {
            uint32_t imm = x86emu_read32(emu, eip + 1);
            x86emu_push32(emu, imm);
            insn_len += 4;
            break;
        }
        
        // PUSH imm8 (sign extended)
        case 0x6A: {
            int8_t imm = (int8_t)x86emu_read8(emu, eip + 1);
            x86emu_push32(emu, (int32_t)imm);
            insn_len += 1;
            break;
        }
        
        // MOV r32, imm32
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
            uint32_t imm = x86emu_read32(emu, eip + 1);
            *get_reg32(emu, opcode - 0xB8) = imm;
            insn_len += 4;
            break;
        }
        
        // MOV r/m32, r32
        case 0x89: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            if (mod == 3) {
                *get_reg32(emu, rm) = *get_reg32(emu, reg);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                x86emu_write32(emu, addr, *get_reg32(emu, reg));
            }
            break;
        }
        
        // MOV r32, r/m32
        case 0x8B: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            if (mod == 3) {
                *get_reg32(emu, reg) = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                *get_reg32(emu, reg) = x86emu_read32(emu, addr);
            }
            break;
        }
        
        // MOV r/m32, imm32 (0xC7 /0)
        case 0xC7: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            if (mod == 3) {
                uint32_t imm = x86emu_read32(emu, eip + 2);
                *get_reg32(emu, rm) = imm;
                insn_len += 4;
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                uint32_t imm = x86emu_read32(emu, eip + insn_len);
                x86emu_write32(emu, addr, imm);
                insn_len += 4;
            }
            break;
        }
        
        // LEA r32, m
        case 0x8D: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int reg = (modrm >> 3) & 7;
            int modrm_size;
            
            insn_len++;
            uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
            insn_len += modrm_size - 1;
            *get_reg32(emu, reg) = addr;
            break;
        }
        
        // ADD r/m32, r32
        case 0x01: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t src = *get_reg32(emu, reg);
            uint32_t dst, result;
            
            if (mod == 3) {
                dst = *get_reg32(emu, rm);
                result = dst + src;
                *get_reg32(emu, rm) = result;
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                dst = x86emu_read32(emu, addr);
                result = dst + src;
                x86emu_write32(emu, addr, result);
            }
            update_flags_add32(emu, dst, src, result);
            break;
        }
        
        // ADD r32, r/m32
        case 0x03: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t dst = *get_reg32(emu, reg);
            uint32_t src;
            
            if (mod == 3) {
                src = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                src = x86emu_read32(emu, addr);
            }
            
            uint32_t result = dst + src;
            *get_reg32(emu, reg) = result;
            update_flags_add32(emu, dst, src, result);
            break;
        }
        
        // ADD EAX, imm32
        case 0x05: {
            uint32_t imm = x86emu_read32(emu, eip + 1);
            uint32_t dst = emu->regs.eax;
            uint32_t result = dst + imm;
            emu->regs.eax = result;
            update_flags_add32(emu, dst, imm, result);
            insn_len += 4;
            break;
        }
        
        // SUB r/m32, r32
        case 0x29: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t src = *get_reg32(emu, reg);
            uint32_t dst, result;
            
            if (mod == 3) {
                dst = *get_reg32(emu, rm);
                result = dst - src;
                *get_reg32(emu, rm) = result;
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                dst = x86emu_read32(emu, addr);
                result = dst - src;
                x86emu_write32(emu, addr, result);
            }
            update_flags_sub32(emu, dst, src, result);
            break;
        }
        
        // SUB r32, r/m32
        case 0x2B: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t dst = *get_reg32(emu, reg);
            uint32_t src;
            
            if (mod == 3) {
                src = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                src = x86emu_read32(emu, addr);
            }
            
            uint32_t result = dst - src;
            *get_reg32(emu, reg) = result;
            update_flags_sub32(emu, dst, src, result);
            break;
        }
        
        // SUB EAX, imm32
        case 0x2D: {
            uint32_t imm = x86emu_read32(emu, eip + 1);
            uint32_t dst = emu->regs.eax;
            uint32_t result = dst - imm;
            emu->regs.eax = result;
            update_flags_sub32(emu, dst, imm, result);
            insn_len += 4;
            break;
        }
        
        // XOR r/m32, r32
        case 0x31: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t src = *get_reg32(emu, reg);
            uint32_t result;
            
            if (mod == 3) {
                result = *get_reg32(emu, rm) ^ src;
                *get_reg32(emu, rm) = result;
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                result = x86emu_read32(emu, addr) ^ src;
                x86emu_write32(emu, addr, result);
            }
            update_flags_logic32(emu, result);
            break;
        }
        
        // XOR r32, r/m32
        case 0x33: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t src;
            if (mod == 3) {
                src = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                src = x86emu_read32(emu, addr);
            }
            
            uint32_t result = *get_reg32(emu, reg) ^ src;
            *get_reg32(emu, reg) = result;
            update_flags_logic32(emu, result);
            break;
        }
        
        // CMP r/m32, r32
        case 0x39: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t src = *get_reg32(emu, reg);
            uint32_t dst;
            
            if (mod == 3) {
                dst = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                dst = x86emu_read32(emu, addr);
            }
            
            uint32_t result = dst - src;
            update_flags_sub32(emu, dst, src, result);
            break;
        }
        
        // CMP r32, r/m32
        case 0x3B: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t dst = *get_reg32(emu, reg);
            uint32_t src;
            
            if (mod == 3) {
                src = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                src = x86emu_read32(emu, addr);
            }
            
            uint32_t result = dst - src;
            update_flags_sub32(emu, dst, src, result);
            break;
        }
        
        // CMP EAX, imm32
        case 0x3D: {
            uint32_t imm = x86emu_read32(emu, eip + 1);
            uint32_t dst = emu->regs.eax;
            uint32_t result = dst - imm;
            update_flags_sub32(emu, dst, imm, result);
            insn_len += 4;
            break;
        }
        
        // TEST r/m32, r32
        case 0x85: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t src = *get_reg32(emu, reg);
            uint32_t dst;
            
            if (mod == 3) {
                dst = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                dst = x86emu_read32(emu, addr);
            }
            
            uint32_t result = dst & src;
            update_flags_logic32(emu, result);
            break;
        }
        
        // INC reg32
        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47: {
            uint32_t *reg = get_reg32(emu, opcode - 0x40);
            uint32_t dst = *reg;
            uint32_t result = dst + 1;
            *reg = result;
            // INC doesn't affect CF
            int cf = GET_FLAG(X86_CF);
            update_flags_add32(emu, dst, 1, result);
            if (cf) SET_FLAG(X86_CF); else CLEAR_FLAG(X86_CF);
            break;
        }
        
        // DEC reg32
        case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            uint32_t *reg = get_reg32(emu, opcode - 0x48);
            uint32_t dst = *reg;
            uint32_t result = dst - 1;
            *reg = result;
            // DEC doesn't affect CF
            int cf = GET_FLAG(X86_CF);
            update_flags_sub32(emu, dst, 1, result);
            if (cf) SET_FLAG(X86_CF); else CLEAR_FLAG(X86_CF);
            break;
        }
        
        // Group 1 (ADD/OR/ADC/SBB/AND/SUB/XOR/CMP) r/m32, imm32
        case 0x81: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int op = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t dst;
            uint32_t addr = 0;
            
            if (mod == 3) {
                dst = *get_reg32(emu, rm);
            } else {
                addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                dst = x86emu_read32(emu, addr);
            }
            
            uint32_t imm = x86emu_read32(emu, eip + insn_len);
            insn_len += 4;
            
            uint32_t result;
            switch (op) {
                case 0: result = dst + imm; update_flags_add32(emu, dst, imm, result); break;  // ADD
                case 1: result = dst | imm; update_flags_logic32(emu, result); break;         // OR
                case 4: result = dst & imm; update_flags_logic32(emu, result); break;         // AND
                case 5: result = dst - imm; update_flags_sub32(emu, dst, imm, result); break; // SUB
                case 6: result = dst ^ imm; update_flags_logic32(emu, result); break;         // XOR
                case 7: result = dst - imm; update_flags_sub32(emu, dst, imm, result);        // CMP
                        goto skip_store;
                default:
                    uart_puts("[x86] Unhandled group 1 opcode\r\n");
                    emu->exception = 1;
                    return -1;
            }
            
            if (mod == 3) {
                *get_reg32(emu, rm) = result;
            } else {
                x86emu_write32(emu, addr, result);
            }
            skip_store:
            break;
        }
        
        // Group 1 (ADD/OR/ADC/SBB/AND/SUB/XOR/CMP) r/m32, imm8 (sign extended)
        case 0x83: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int op = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len++;
            
            uint32_t dst;
            uint32_t addr = 0;
            
            if (mod == 3) {
                dst = *get_reg32(emu, rm);
            } else {
                addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                dst = x86emu_read32(emu, addr);
            }
            
            int32_t imm = (int8_t)x86emu_read8(emu, eip + insn_len);
            insn_len++;
            
            uint32_t result;
            switch (op) {
                case 0: result = dst + imm; update_flags_add32(emu, dst, imm, result); break;  // ADD
                case 1: result = dst | imm; update_flags_logic32(emu, result); break;         // OR
                case 4: result = dst & imm; update_flags_logic32(emu, result); break;         // AND
                case 5: result = dst - imm; update_flags_sub32(emu, dst, imm, result); break; // SUB
                case 6: result = dst ^ imm; update_flags_logic32(emu, result); break;         // XOR
                case 7: result = dst - imm; update_flags_sub32(emu, dst, imm, result);        // CMP
                        goto skip_store2;
                default:
                    uart_puts("[x86] Unhandled group 1 opcode\r\n");
                    emu->exception = 1;
                    return -1;
            }
            
            if (mod == 3) {
                *get_reg32(emu, rm) = result;
            } else {
                x86emu_write32(emu, addr, result);
            }
            skip_store2:
            break;
        }
        
        // JMP rel8
        case 0xEB: {
            int8_t rel = (int8_t)x86emu_read8(emu, eip + 1);
            insn_len = 2;
            emu->regs.eip = eip + insn_len + rel;
            return 0;
        }
        
        // JMP rel32
        case 0xE9: {
            int32_t rel = (int32_t)x86emu_read32(emu, eip + 1);
            insn_len = 5;
            emu->regs.eip = eip + insn_len + rel;
            return 0;
        }
        
        // Jcc rel8
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            int8_t rel = (int8_t)x86emu_read8(emu, eip + 1);
            insn_len = 2;
            
            int cc = opcode & 0xF;
            int take = 0;
            
            switch (cc) {
                case 0x0: take = GET_FLAG(X86_OF); break;                    // JO
                case 0x1: take = !GET_FLAG(X86_OF); break;                   // JNO
                case 0x2: take = GET_FLAG(X86_CF); break;                    // JB/JC
                case 0x3: take = !GET_FLAG(X86_CF); break;                   // JAE/JNC
                case 0x4: take = GET_FLAG(X86_ZF); break;                    // JE/JZ
                case 0x5: take = !GET_FLAG(X86_ZF); break;                   // JNE/JNZ
                case 0x6: take = GET_FLAG(X86_CF) || GET_FLAG(X86_ZF); break;// JBE
                case 0x7: take = !GET_FLAG(X86_CF) && !GET_FLAG(X86_ZF); break;// JA
                case 0x8: take = GET_FLAG(X86_SF); break;                    // JS
                case 0x9: take = !GET_FLAG(X86_SF); break;                   // JNS
                case 0xA: take = GET_FLAG(X86_PF); break;                    // JP
                case 0xB: take = !GET_FLAG(X86_PF); break;                   // JNP
                case 0xC: take = GET_FLAG(X86_SF) != GET_FLAG(X86_OF); break;// JL
                case 0xD: take = GET_FLAG(X86_SF) == GET_FLAG(X86_OF); break;// JGE
                case 0xE: take = GET_FLAG(X86_ZF) || (GET_FLAG(X86_SF) != GET_FLAG(X86_OF)); break;// JLE
                case 0xF: take = !GET_FLAG(X86_ZF) && (GET_FLAG(X86_SF) == GET_FLAG(X86_OF)); break;// JG
            }
            
            if (take) {
                emu->regs.eip = eip + insn_len + rel;
                return 0;
            }
            break;
        }
        
        // Two-byte opcodes (0F xx)
        case 0x0F: {
            uint8_t op2 = x86emu_read8(emu, eip + 1);
            insn_len = 2;
            
            // Jcc rel32
            if (op2 >= 0x80 && op2 <= 0x8F) {
                int32_t rel = (int32_t)x86emu_read32(emu, eip + 2);
                insn_len = 6;
                
                int cc = op2 & 0xF;
                int take = 0;
                
                switch (cc) {
                    case 0x0: take = GET_FLAG(X86_OF); break;
                    case 0x1: take = !GET_FLAG(X86_OF); break;
                    case 0x2: take = GET_FLAG(X86_CF); break;
                    case 0x3: take = !GET_FLAG(X86_CF); break;
                    case 0x4: take = GET_FLAG(X86_ZF); break;
                    case 0x5: take = !GET_FLAG(X86_ZF); break;
                    case 0x6: take = GET_FLAG(X86_CF) || GET_FLAG(X86_ZF); break;
                    case 0x7: take = !GET_FLAG(X86_CF) && !GET_FLAG(X86_ZF); break;
                    case 0x8: take = GET_FLAG(X86_SF); break;
                    case 0x9: take = !GET_FLAG(X86_SF); break;
                    case 0xA: take = GET_FLAG(X86_PF); break;
                    case 0xB: take = !GET_FLAG(X86_PF); break;
                    case 0xC: take = GET_FLAG(X86_SF) != GET_FLAG(X86_OF); break;
                    case 0xD: take = GET_FLAG(X86_SF) == GET_FLAG(X86_OF); break;
                    case 0xE: take = GET_FLAG(X86_ZF) || (GET_FLAG(X86_SF) != GET_FLAG(X86_OF)); break;
                    case 0xF: take = !GET_FLAG(X86_ZF) && (GET_FLAG(X86_SF) == GET_FLAG(X86_OF)); break;
                }
                
                if (take) {
                    emu->regs.eip = eip + insn_len + rel;
                    return 0;
                }
            }
            // MOVZX r32, r/m8
            else if (op2 == 0xB6) {
                uint8_t modrm = x86emu_read8(emu, eip + 2);
                int mod = (modrm >> 6) & 3;
                int reg = (modrm >> 3) & 7;
                int rm = modrm & 7;
                int modrm_size;
                
                insn_len = 3;
                
                uint8_t src;
                if (mod == 3) {
                    src = (uint8_t)(*get_reg32(emu, rm) & 0xFF);
                } else {
                    uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                    insn_len += modrm_size - 1;
                    src = x86emu_read8(emu, addr);
                }
                
                *get_reg32(emu, reg) = (uint32_t)src;
            }
            // MOVZX r32, r/m16
            else if (op2 == 0xB7) {
                uint8_t modrm = x86emu_read8(emu, eip + 2);
                int mod = (modrm >> 6) & 3;
                int reg = (modrm >> 3) & 7;
                int rm = modrm & 7;
                int modrm_size;
                
                insn_len = 3;
                
                uint16_t src;
                if (mod == 3) {
                    src = (uint16_t)(*get_reg32(emu, rm) & 0xFFFF);
                } else {
                    uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                    insn_len += modrm_size - 1;
                    src = x86emu_read16(emu, addr);
                }
                
                *get_reg32(emu, reg) = (uint32_t)src;
            }
            else {
                printf("[x86] Unhandled 0F opcode: 0x%02X\r\n", op2);
                emu->exception = 1;
                return -1;
            }
            break;
        }
        
        // CALL rel32
        case 0xE8: {
            int32_t rel = (int32_t)x86emu_read32(emu, eip + 1);
            insn_len = 5;
            uint32_t ret_addr = eip + insn_len;
            x86emu_push32(emu, ret_addr);
            emu->regs.eip = eip + insn_len + rel;
            return 0;
        }
        
        // CALL r/m32 (FF /2)
        case 0xFF: {
            uint8_t modrm = x86emu_read8(emu, eip + 1);
            int mod = (modrm >> 6) & 3;
            int op = (modrm >> 3) & 7;
            int rm = modrm & 7;
            int modrm_size;
            
            insn_len = 2;
            
            uint32_t target;
            
            if (mod == 3) {
                target = *get_reg32(emu, rm);
            } else {
                uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                insn_len += modrm_size - 1;
                target = x86emu_read32(emu, addr);
            }
            
            if (op == 2) {
                // CALL r/m32
                // Check if it's an import call
                int import_result = check_import(emu, target);
                if (import_result != 0) {
                    // Import was handled
                    if (import_result < 0) {
                        emu->halted = 1;
                    }
                    emu->regs.eip += insn_len;
                    return 0;
                }
                
                x86emu_push32(emu, eip + insn_len);
                emu->regs.eip = target;
                return 0;
            } else if (op == 4) {
                // JMP r/m32
                emu->regs.eip = target;
                return 0;
            } else if (op == 6) {
                // PUSH r/m32
                x86emu_push32(emu, target);
            } else if (op == 0) {
                // INC r/m32
                uint32_t result = target + 1;
                if (mod == 3) {
                    *get_reg32(emu, rm) = result;
                } else {
                    uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                    x86emu_write32(emu, addr, result);
                }
            } else if (op == 1) {
                // DEC r/m32
                uint32_t result = target - 1;
                if (mod == 3) {
                    *get_reg32(emu, rm) = result;
                } else {
                    uint32_t addr = decode_modrm(emu, modrm, &modrm_size);
                    x86emu_write32(emu, addr, result);
                }
            } else {
                printf("[x86] Unhandled FF opcode: 0x%02X\r\n", op);
                emu->exception = 1;
                return -1;
            }
            break;
        }
        
        // RET
        case 0xC3: {
            emu->regs.eip = x86emu_pop32(emu);
            return 0;
        }
        
        // RET imm16
        case 0xC2: {
            uint16_t imm = x86emu_read16(emu, eip + 1);
            emu->regs.eip = x86emu_pop32(emu);
            emu->regs.esp += imm;
            return 0;
        }
        
        // LEAVE
        case 0xC9: {
            emu->regs.esp = emu->regs.ebp;
            emu->regs.ebp = x86emu_pop32(emu);
            break;
        }
        
        // INT 3 (breakpoint)
        case 0xCC: {
            uart_puts("[x86] Breakpoint (INT 3)\r\n");
            x86emu_dump_regs(emu);
            emu->halted = 1;
            break;
        }
        
        // HLT
        case 0xF4: {
            uart_puts("[x86] HLT instruction\r\n");
            emu->halted = 1;
            break;
        }
        
        default:
            printf("[x86] Unhandled opcode: 0x%02X at 0x%08X\r\n", opcode, eip);
            x86emu_dump_regs(emu);
            emu->exception = 1;
            return -1;
    }
    
    emu->regs.eip += insn_len;
    return 0;
}

// Initialize emulator
int x86emu_init(x86emu_state_t *emu, uint8_t *mem, uint32_t mem_size) {
    memset(emu, 0, sizeof(x86emu_state_t));
    
    emu->mem_base = mem;
    emu->mem_size = mem_size;
    
    // Initialize registers
    emu->regs.eflags = 0x202;  // IF set
    
    // Allocate stack (1MB)
    emu->stack_size = 1024 * 1024;
    emu->stack = malloc(emu->stack_size);
    if (!emu->stack) {
        return -1;
    }
    
    // Stack grows down from high address
    emu->stack_base = 0x7FFF0000;
    emu->regs.esp = emu->stack_base - 4;
    
    emu->max_insns = 100000000;  // 100M instructions max
    
    return 0;
}

// Reset emulator state
void x86emu_reset(x86emu_state_t *emu) {
    emu->regs.eax = emu->regs.ebx = emu->regs.ecx = emu->regs.edx = 0;
    emu->regs.esi = emu->regs.edi = emu->regs.ebp = 0;
    emu->regs.esp = emu->stack_base - 4;
    emu->regs.eflags = 0x202;
    emu->running = 0;
    emu->halted = 0;
    emu->exception = 0;
    emu->insn_count = 0;
}

// Set entry point
int x86emu_set_entry(x86emu_state_t *emu, uint32_t entry_point) {
    emu->regs.eip = entry_point;
    emu->running = 1;
    emu->halted = 0;
    emu->exception = 0;
    return 0;
}

// Run until halt or exception
int x86emu_run(x86emu_state_t *emu) {
    while (emu->running && !emu->halted && !emu->exception) {
        if (x86emu_step(emu) < 0) {
            break;
        }
    }
    
    return emu->exception ? -1 : 0;
}

// Dump registers
void x86emu_dump_regs(x86emu_state_t *emu) {
    printf("[x86] Registers:\r\n");
    printf("  EAX=0x%08X EBX=0x%08X ECX=0x%08X EDX=0x%08X\r\n",
           emu->regs.eax, emu->regs.ebx, emu->regs.ecx, emu->regs.edx);
    printf("  ESI=0x%08X EDI=0x%08X EBP=0x%08X ESP=0x%08X\r\n",
           emu->regs.esi, emu->regs.edi, emu->regs.ebp, emu->regs.esp);
    printf("  EIP=0x%08X EFLAGS=0x%08X\r\n",
           emu->regs.eip, emu->regs.eflags);
}
