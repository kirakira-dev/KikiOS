/*
 * KikiOS Interrupt Handling - Shared Code
 *
 * Platform-specific drivers are in hal/qemu/irq.c and hal/pizero2w/irq.c.
 * This file contains:
 * - Exception handlers (sync, FIQ, SError) shared by all platforms
 * - Legacy API wrappers for QEMU compatibility
 */

#include "irq.h"
#include "printf.h"
#include "hal/hal.h"
#include "fb.h"
#include "process.h"

// Direct UART output (always works, even if printf goes to screen)
extern void uart_puts(const char *s);
extern void uart_putc(char c);

// Kernel base address from linker script (0x0 on QEMU, 0x80000 on Pi)
extern char _kernel_start[];

static void uart_puthex(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    uart_puts("0x");
    // Find first non-zero nibble
    int start = 15;
    while (start > 0 && ((val >> (start * 4)) & 0xF) == 0) {
        start--;
    }
    for (int i = start; i >= 0; i--) {
        uart_putc(hex[(val >> (i * 4)) & 0xF]);
    }
}

// ============================================================================
// Legacy API Wrappers (for QEMU code compatibility)
// These call through to HAL functions
// ============================================================================

void irq_init(void) {
    hal_irq_init();
}

void irq_enable(void) {
    hal_irq_enable();
}

void irq_disable(void) {
    hal_irq_disable();
}

void irq_enable_irq(uint32_t irq) {
    hal_irq_enable_irq(irq);
}

void irq_disable_irq(uint32_t irq) {
    hal_irq_disable_irq(irq);
}

void irq_register_handler(uint32_t irq, irq_handler_t handler) {
    hal_irq_register_handler(irq, handler);
}

void timer_init(uint32_t interval_ms) {
    hal_timer_init(interval_ms);
}

uint64_t timer_get_ticks(void) {
    return hal_timer_get_ticks();
}

void timer_set_interval(uint32_t interval_ms) {
    hal_timer_set_interval(interval_ms);
}

void wfi(void) {
    asm volatile("wfi");
}

void sleep_ms(uint32_t ms) {
    // Timer runs at 100Hz (10ms per tick)
    uint64_t ticks_to_wait = (ms + 9) / 10;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint64_t target = hal_timer_get_ticks() + ticks_to_wait;
    while (hal_timer_get_ticks() < target) {
        wfi();
    }
}

// ============================================================================
// Shared Exception Handlers (used by all platforms)
// Called from vectors.S
// ============================================================================

// WSOD - White Screen of Death
// The final art piece of KikiOS


static const char *wsod_art[] = {
    "          _______",
    "         /       \\",
    "        /   RIP   \\",
    "       |           |",
    "       |   VIBES   |",
    "       |    NOT    |",
    "       | IMMACULATE|",
    "       |           |",
    "       |  ~~~~~~~  |",
    "       |___________|",
    "      /=============\\",
    NULL
};

static void wsod_draw_text(int x, int y, const char *s) {
    while (*s) {
        fb_draw_char(x, y, *s, COLOR_BLACK, COLOR_WHITE);
        x += 8;
        s++;
    }
}

static void wsod_draw_line(int y) {
    for (uint32_t x = 40; x < fb_width - 40; x++) {
        fb_put_pixel(x, y, COLOR_BLACK);
    }
}

// Sad Mac icon - 32x32 pixel art
// Classic Macintosh crash icon with X eyes and frown
static const uint32_t sad_mac_bitmap[32] = {
    0x00000000,  //
    0x3FFFFFFC,  // ..############################..
    0x40000002,  // .#............................#.
    0x40000002,  // .#............................#.
    0x4FFFFFF2,  // .#..########################..#.
    0x48000012,  // .#..#......................#..#.
    0x48000012,  // .#..#......................#..#.
    0x48A00512,  // .#..#..#.#........#.#......#..#.  X eyes
    0x48400212,  // .#..#...#..........#.......#..#.
    0x48A00512,  // .#..#..#.#........#.#......#..#.  X eyes
    0x48000012,  // .#..#......................#..#.
    0x48000012,  // .#..#......................#..#.
    0x4807E012,  // .#..#.....######...........#..#.  frown
    0x48180012,  // .#..#....##....##..........#..#.
    0x48600012,  // .#..#...##......##.........#..#.
    0x48000012,  // .#..#......................#..#.
    0x4FFFFFF2,  // .#..########################..#.
    0x40000002,  // .#............................#.
    0x40000002,  // .#............................#.
    0x4FC3FC02,  // .#..######....######..........#.
    0x40000002,  // .#............................#.
    0x4AAAAAA2,  // .#..#.#.#.#.#.#.#.#.#.#.#.....#.
    0x40000002,  // .#............................#.
    0x4FC3FC02,  // .#..######....######..........#.
    0x40000002,  // .#............................#.
    0x40000002,  // .#............................#.
    0x3FFFFFFC,  // ..############################..
    0x10000004,  // ...#........................#...
    0x10000004,  // ...#........................#...
    0x1FFFFFFC,  // ...#######################.....
    0x00000000,  //
    0x00000000,  //
};

static void wsod_draw_sad_mac(int x, int y) {
    for (int row = 0; row < 32; row++) {
        uint32_t bits = sad_mac_bitmap[row];
        for (int col = 0; col < 32; col++) {
            if (bits & (1 << (31 - col))) {
                // Draw 2x2 pixels for better visibility
                fb_put_pixel(x + col*2, y + row*2, COLOR_BLACK);
                fb_put_pixel(x + col*2 + 1, y + row*2, COLOR_BLACK);
                fb_put_pixel(x + col*2, y + row*2 + 1, COLOR_BLACK);
                fb_put_pixel(x + col*2 + 1, y + row*2 + 1, COLOR_BLACK);
            }
        }
    }
}

// EKG heartbeat pattern - relative Y offsets from baseline
// Pattern: flat, small bump, big spike up, big spike down, small bump, flat
static const int ekg_pattern[] = {
    0, 0, 0, 0, 0,           // flat
    -2, -3, -2,               // P wave (small bump)
    0, 0,                     // flat
    -4, -8, 10, -12, 6, -2,   // QRS complex (big spike)
    0, 0, 0,                  // flat
    -3, -5, -5, -3,           // T wave (rounded bump)
    0, 0, 0, 0, 0, 0, 0, 0    // flat trailing
};
#define EKG_PATTERN_LEN (sizeof(ekg_pattern) / sizeof(ekg_pattern[0]))

// Simple delay using ARM system counter
static void wsod_delay(uint32_t ms) {
    uint64_t freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    uint64_t start;
    asm volatile("mrs %0, cntpct_el0" : "=r"(start));
    uint64_t target = start + (freq * ms / 1000);
    uint64_t now;
    do {
        asm volatile("mrs %0, cntpct_el0" : "=r"(now));
    } while (now < target);
}

// Draw thick line (3 pixels tall for visibility)
static void wsod_draw_ekg_point(int x, int y) {
    fb_put_pixel(x, y-1, COLOR_BLACK);
    fb_put_pixel(x, y, COLOR_BLACK);
    fb_put_pixel(x, y+1, COLOR_BLACK);
}

// Animate EKG: 2 heartbeats then flatline
static void wsod_animate_ekg(int base_x, int base_y, int width) {
    int x = base_x;
    int end_x = base_x + width;

    // Draw 2 heartbeats
    for (int beat = 0; beat < 2 && x < end_x; beat++) {
        for (uint32_t i = 0; i < EKG_PATTERN_LEN && x < end_x; i++) {
            int y = base_y + ekg_pattern[i];
            wsod_draw_ekg_point(x, y);
            x += 2;
            wsod_delay(15);  // 15ms per point for animation
        }
    }

    // Flatline the rest with a dramatic pause
    wsod_delay(200);
    while (x < end_x) {
        wsod_draw_ekg_point(x, base_y);
        x += 2;
        wsod_delay(8);  // Faster flatline
    }
}

static const char *get_exception_name(uint32_t ec) {
    switch (ec) {
        case 0x00: return "Unknown";
        case 0x01: return "Trapped WFI/WFE";
        case 0x0E: return "Illegal State";
        case 0x15: return "SVC (Syscall)";
        case 0x20: return "Instruction Abort (Lower EL)";
        case 0x21: return "Instruction Abort";
        case 0x22: return "PC Alignment Fault";
        case 0x24: return "Data Abort (Lower EL)";
        case 0x25: return "Data Abort";
        case 0x26: return "SP Alignment Fault";
        case 0x2C: return "FP Exception";
        default:   return "Exception";
    }
}

// Decode DFSC/IFSC (Data/Instruction Fault Status Code)
static const char *get_fault_status(uint32_t fsc) {
    switch (fsc & 0x3F) {
        case 0x00: return "Address Size Fault L0";
        case 0x01: return "Address Size Fault L1";
        case 0x02: return "Address Size Fault L2";
        case 0x03: return "Address Size Fault L3";
        case 0x04: return "Translation Fault L0";
        case 0x05: return "Translation Fault L1";
        case 0x06: return "Translation Fault L2";
        case 0x07: return "Translation Fault L3";
        case 0x09: return "Access Flag Fault L1";
        case 0x0A: return "Access Flag Fault L2";
        case 0x0B: return "Access Flag Fault L3";
        case 0x0D: return "Permission Fault L1";
        case 0x0E: return "Permission Fault L2";
        case 0x0F: return "Permission Fault L3";
        case 0x10: return "Synchronous External Abort";
        case 0x11: return "Synchronous Tag Check Fault";
        case 0x14: return "Sync External Abort L0";
        case 0x15: return "Sync External Abort L1";
        case 0x16: return "Sync External Abort L2";
        case 0x17: return "Sync External Abort L3";
        case 0x18: return "Sync Parity Error";
        case 0x21: return "Alignment Fault";
        case 0x30: return "TLB Conflict Abort";
        case 0x31: return "Unsupported Atomic";
        default:   return "Unknown Fault";
    }
}

static void wsod_int(char *buf, int val) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int idx = 0;
    char tmp[16];
    int ti = 0;
    if (val < 0) {
        buf[idx++] = '-';
        val = -val;
    }
    while (val > 0) {
        tmp[ti++] = '0' + (val % 10);
        val /= 10;
    }
    while (ti > 0) buf[idx++] = tmp[--ti];
    buf[idx] = '\0';
}

static void wsod_hex(char *buf, uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';

    // Find first non-zero nibble (but always show at least one digit)
    int start = 15;
    while (start > 0 && ((val >> (start * 4)) & 0xF) == 0) {
        start--;
    }

    int pos = 2;
    for (int i = start; i >= 0; i--) {
        buf[pos++] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[pos] = '\0';
}

// Saved register layout from SAVE_REGS macro:
// sp+0:   x0, x1    sp+16:  x2, x3    sp+32:  x4, x5    sp+48:  x6, x7
// sp+64:  x8, x9    sp+80:  x10,x11   sp+96:  x12,x13   sp+112: x14,x15
// sp+128: x16,x17   sp+144: x18,x19   sp+160: x20,x21   sp+176: x22,x23
// sp+192: x24,x25   sp+208: x26,x27   sp+224: x28,x29   sp+240: x30
// sp+256: elr_el1, spsr_el1

void handle_sync_exception(uint64_t esr, uint64_t elr, uint64_t far, uint64_t *regs) {
    uint32_t ec = (esr >> 26) & 0x3F;
    uint32_t iss = esr & 0x1FFFFFF;

    // Always print to UART for serial debugging
    uart_puts("\n\n");
    uart_puts("========================================\n");
    uart_puts("  KERNEL PANIC: ");
    uart_puts(get_exception_name(ec));
    uart_puts("\n========================================\n");
    uart_puts("  Fault Address:  "); uart_puthex(far); uart_puts("\n");
    uart_puts("  Return Address: "); uart_puthex(elr); uart_puts("\n");
    uart_puts("  ESR:            "); uart_puthex(esr); uart_puts("\n");
    if (ec == 0x24 || ec == 0x25 || ec == 0x20 || ec == 0x21) {
        uart_puts("  Access Type:    ");
        uart_puts((iss & (1 << 6)) ? "Write" : "Read");
        uart_puts("\n");
    }
    if (current_process) {
        uart_puts("  Process:        ");
        uart_puts(current_process->name);
        uart_puts("\n");
    }
    if (regs) {
        uart_puts("  Registers:\n");
        for (int i = 0; i < 8; i++) {
            uart_puts("    x"); uart_putc('0' + i); uart_puts(": ");
            uart_puthex(regs[i]); uart_puts("\n");
        }
        uint64_t *orig_sp = (uint64_t *)((uint8_t *)regs + 272);
        uart_puts("    SP:  "); uart_puthex((uint64_t)orig_sp); uart_puts("\n");
        uart_puts("    FP:  "); uart_puthex(regs[29]); uart_puts("\n");
        uart_puts("    LR:  "); uart_puthex(regs[30]); uart_puts("\n");

        // Backtrace with offsets
        uart_puts("  Backtrace:\n");
        uint64_t fp = regs[29];
        int depth = 0;
        while (fp != 0 && depth < 10) {
            // Valid RAM: QEMU 0x40000000-0x50000000, Pi 0x00080000-0x20000000
            int valid_qemu = (fp >= 0x40000000 && fp < 0x50000000);
            int valid_pi = (fp >= 0x00080000 && fp < 0x20000000);
            if (!valid_qemu && !valid_pi) break;
            if (fp & 0x7) break;
            uint64_t *frame = (uint64_t *)fp;
            uint64_t ret_addr = frame[1];
            if (ret_addr == 0) break;
            uart_puts("    ["); uart_putc('0' + depth); uart_puts("] ");
            uart_puthex(ret_addr);
            // Show location
            if (current_process && ret_addr >= current_process->load_base &&
                ret_addr < current_process->load_base + current_process->load_size) {
                uart_puts(" ("); uart_puts(current_process->name); uart_puts(" +");
                uart_puthex(ret_addr - current_process->load_base); uart_puts(")");
            } else {
                uart_puts(" (kernel +");
                uart_puthex(ret_addr - (uint64_t)_kernel_start);
                uart_puts(")");
            }
            uart_puts("\n");
            fp = frame[0];
            depth++;
        }
    }
    uart_puts("========================================\n");

    // Draw WSOD if framebuffer is available
    if (fb_base && fb_width > 0 && fb_height > 0) {
        // Reset hardware scroll to top (Pi has hardware scrolling)
        hal_fb_set_scroll_offset(0);

        // Fill screen white
        fb_clear(COLOR_WHITE);

        // Draw Sad Mac icon on the left
        wsod_draw_sad_mac(60, 30);

        // Draw the tombstone ASCII art (right of Sad Mac)
        int art_y = 20;
        int tombstone_x = 160;  // Position tombstone to the right of Sad Mac
        for (int i = 0; wsod_art[i] != NULL; i++) {
            wsod_draw_text(tombstone_x, art_y, wsod_art[i]);
            art_y += 14;  // Tighter line spacing
        }

        // Draw separator line
        int info_y = art_y + 10;
        wsod_draw_line(info_y);
        info_y += 14;

        // Two-column layout: exception info on left, process info on right
        int left_col = 60;
        int right_col = fb_width / 2 + 40;
        char buf[64];

        // Left column - Exception info, Right column - Process/uptime
        wsod_draw_text(left_col, info_y, "Exception:");
        wsod_draw_text(left_col + 96, info_y, get_exception_name(ec));
        if (current_process) {
            wsod_draw_text(right_col, info_y, "Process:");
            wsod_draw_text(right_col + 72, info_y, current_process->name);
        }
        info_y += 16;

        wsod_draw_text(left_col, info_y, "Fault:");
        wsod_hex(buf, far);
        wsod_draw_text(left_col + 96, info_y, buf);
        // Uptime on right
        {
            uint64_t ticks = hal_timer_get_ticks();
            uint32_t secs = ticks / 100;
            uint32_t mins = secs / 60;
            secs %= 60;
            wsod_draw_text(right_col, info_y, "Uptime:");
            char uptime_buf[16];
            int pos = 0;
            wsod_int(buf, mins);
            for (int i = 0; buf[i]; i++) uptime_buf[pos++] = buf[i];
            uptime_buf[pos++] = 'm';
            uptime_buf[pos++] = ' ';
            wsod_int(buf, secs);
            for (int i = 0; buf[i]; i++) uptime_buf[pos++] = buf[i];
            uptime_buf[pos++] = 's';
            uptime_buf[pos] = '\0';
            wsod_draw_text(right_col + 72, info_y, uptime_buf);
        }
        info_y += 16;

        wsod_draw_text(left_col, info_y, "Return:");
        wsod_hex(buf, elr);
        wsod_draw_text(left_col + 96, info_y, buf);
        if (ec == 0x24 || ec == 0x25 || ec == 0x20 || ec == 0x21) {
            wsod_draw_text(right_col, info_y, get_fault_status(iss));
            wsod_draw_text(right_col + 180, info_y, (iss & (1 << 6)) ? "(W)" : "(R)");
        }
        info_y += 16;

        // Registers: x0-x3 on one line, x4-x7 on next
        if (regs) {
            wsod_draw_line(info_y);
            info_y += 10;

            for (int row = 0; row < 2; row++) {
                int x = left_col;
                for (int col = 0; col < 4; col++) {
                    int reg_num = row * 4 + col;
                    char label[4] = {'x', '0' + reg_num, ':', '\0'};
                    wsod_draw_text(x, info_y, label);
                    wsod_hex(buf, regs[reg_num]);
                    wsod_draw_text(x + 24, info_y, buf);
                    x += 160;
                }
                info_y += 14;
            }

            // SP, FP, LR on one line
            uint64_t *orig_sp = (uint64_t *)((uint8_t *)regs + 272);
            wsod_draw_text(left_col, info_y, "SP:");
            wsod_hex(buf, (uint64_t)orig_sp);
            wsod_draw_text(left_col + 24, info_y, buf);
            wsod_draw_text(left_col + 180, info_y, "FP:");
            wsod_hex(buf, regs[29]);
            wsod_draw_text(left_col + 204, info_y, buf);
            wsod_draw_text(left_col + 360, info_y, "LR:");
            wsod_hex(buf, regs[30]);
            wsod_draw_text(left_col + 384, info_y, buf);
            info_y += 14;

            // Backtrace - show with offsets
            wsod_draw_text(left_col, info_y, "Trace:");
            info_y += 14;
            uint64_t fp = regs[29];
            int depth = 0;
            while (fp != 0 && depth < 3) {
                // Valid RAM: QEMU 0x40000000-0x50000000, Pi 0x00080000-0x20000000
                int valid_qemu = (fp >= 0x40000000 && fp < 0x50000000);
                int valid_pi = (fp >= 0x00080000 && fp < 0x20000000);
                if ((!valid_qemu && !valid_pi) || (fp & 0x7)) break;
                uint64_t ret_addr = ((uint64_t *)fp)[1];
                if (ret_addr == 0) break;

                // Format: "  [n] addr (location +offset)"
                char trace_buf[80];
                int pos = 0;
                trace_buf[pos++] = ' '; trace_buf[pos++] = ' ';
                trace_buf[pos++] = '['; trace_buf[pos++] = '0' + depth; trace_buf[pos++] = ']';
                trace_buf[pos++] = ' ';
                wsod_hex(buf, ret_addr);
                for (int i = 0; buf[i]; i++) trace_buf[pos++] = buf[i];
                trace_buf[pos++] = ' '; trace_buf[pos++] = '(';

                // Determine location - check process first, then assume kernel
                if (current_process && ret_addr >= current_process->load_base &&
                    ret_addr < current_process->load_base + current_process->load_size) {
                    // Current process
                    const char *name = current_process->name;
                    // Just show last part of path
                    const char *slash = name;
                    for (const char *p = name; *p; p++) if (*p == '/') slash = p + 1;
                    while (*slash && pos < 60) trace_buf[pos++] = *slash++;
                    trace_buf[pos++] = ' '; trace_buf[pos++] = '+';
                    wsod_hex(buf, ret_addr - current_process->load_base);
                    for (int i = 0; buf[i]; i++) trace_buf[pos++] = buf[i];
                } else {
                    // Kernel - use linker-provided base address
                    trace_buf[pos++] = 'k'; trace_buf[pos++] = 'e'; trace_buf[pos++] = 'r';
                    trace_buf[pos++] = 'n'; trace_buf[pos++] = 'e'; trace_buf[pos++] = 'l';
                    trace_buf[pos++] = ' '; trace_buf[pos++] = '+';
                    wsod_hex(buf, ret_addr - (uint64_t)_kernel_start);
                    for (int i = 0; buf[i]; i++) trace_buf[pos++] = buf[i];
                }
                trace_buf[pos++] = ')';
                trace_buf[pos] = '\0';

                wsod_draw_text(left_col, info_y, trace_buf);
                info_y += 14;
                fp = ((uint64_t *)fp)[0];
                depth++;
            }
        }

        // Bottom section
        info_y = fb_height - 60;
        wsod_draw_line(info_y);
        info_y += 16;

        // System halted message
        const char *msg = "System halted. Please restart your computer.";
        int msg_len = 0;
        while (msg[msg_len]) msg_len++;
        int msg_x = (fb_width - msg_len * 8) / 2;
        wsod_draw_text(msg_x, info_y, msg);

        // Animated EKG flatline at the very bottom
        int ekg_y = fb_height - 20;
        wsod_animate_ekg(40, ekg_y, fb_width - 80);
    }

    hal_irq_disable();
    while (1) {
        asm volatile("wfi");
    }
}

void handle_fiq(void) {
    printf("[IRQ] FIQ received (unexpected)\n");
}

void handle_serror(uint64_t esr) {
    // Always print to UART for serial debugging
    uart_puts("\n\n");
    uart_puts("========================================\n");
    uart_puts("  KERNEL PANIC: SError (Async Abort)\n");
    uart_puts("========================================\n");
    uart_puts("  ESR: "); uart_puthex(esr); uart_puts("\n");
    if (current_process) {
        uart_puts("  Process: ");
        uart_puts(current_process->name);
        uart_puts("\n");
    }
    uart_puts("========================================\n");

    // Draw WSOD
    if (fb_base && fb_width > 0 && fb_height > 0) {
        // Reset hardware scroll to top (Pi has hardware scrolling)
        hal_fb_set_scroll_offset(0);

        fb_clear(COLOR_WHITE);

        int art_y = 30;
        for (int i = 0; wsod_art[i] != NULL; i++) {
            const char *line = wsod_art[i];
            int len = 0;
            while (line[len]) len++;
            int art_x = (fb_width - len * 8) / 2;
            if (art_x < 0) art_x = 8;
            wsod_draw_text(art_x, art_y, line);
            art_y += 16;
        }

        int info_y = art_y + 20;
        wsod_draw_line(info_y);
        info_y += 20;

        int left_col = 60;
        int right_col = fb_width / 2 + 40;
        char buf[64];

        wsod_draw_text(left_col, info_y, "Exception:");
        wsod_draw_text(left_col + 136, info_y, "SError (Async Abort)");

        if (current_process) {
            wsod_draw_text(right_col, info_y, "Process:");
            wsod_draw_text(right_col + 80, info_y, current_process->name);
        }
        info_y += 20;

        wsod_draw_text(left_col, info_y, "ESR:");
        wsod_hex(buf, esr);
        wsod_draw_text(left_col + 136, info_y, buf);
        info_y += 20;

        info_y = fb_height - 60;
        wsod_draw_line(info_y);
        info_y += 16;

        const char *msg = "System halted. Please restart your computer.";
        int msg_len = 0;
        while (msg[msg_len]) msg_len++;
        int msg_x = (fb_width - msg_len * 8) / 2;
        wsod_draw_text(msg_x, info_y, msg);
    }

    hal_irq_disable();
    while (1) {
        asm volatile("wfi");
    }
}
