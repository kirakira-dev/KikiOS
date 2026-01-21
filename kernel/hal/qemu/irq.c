/*
 * QEMU virt machine Interrupt Controller Driver
 *
 * GIC-400 (GICv2) driver for QEMU virt machine.
 * Distributor at 0x08000000, CPU Interface at 0x08010000.
 */

#include "../hal.h"
#include "../../printf.h"
#include "../../irq.h"
#include "../../virtio_sound.h"
#include "../../console.h"
#include "../../process.h"

// QEMU virt machine GIC addresses
#define GICD_BASE   0x08000000UL  // Distributor
#define GICC_BASE   0x08010000UL  // CPU Interface

// GIC Distributor registers
#define GICD_CTLR       (*(volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_TYPER      (*(volatile uint32_t *)(GICD_BASE + 0x004))
#define GICD_IGROUPR(n)   (*(volatile uint32_t *)(GICD_BASE + 0x080 + (n)*4))
#define GICD_ISENABLER(n) (*(volatile uint32_t *)(GICD_BASE + 0x100 + (n)*4))
#define GICD_ICENABLER(n) (*(volatile uint32_t *)(GICD_BASE + 0x180 + (n)*4))
#define GICD_ISPENDR(n)   (*(volatile uint32_t *)(GICD_BASE + 0x200 + (n)*4))
#define GICD_ICPENDR(n)   (*(volatile uint32_t *)(GICD_BASE + 0x280 + (n)*4))
#define GICD_IPRIORITYR(n) (*(volatile uint32_t *)(GICD_BASE + 0x400 + (n)*4))
#define GICD_ITARGETSR(n)  (*(volatile uint32_t *)(GICD_BASE + 0x800 + (n)*4))
#define GICD_ICFGR(n)      (*(volatile uint32_t *)(GICD_BASE + 0xC00 + (n)*4))

// GIC CPU Interface registers
#define GICC_CTLR   (*(volatile uint32_t *)(GICC_BASE + 0x000))
#define GICC_PMR    (*(volatile uint32_t *)(GICC_BASE + 0x004))
#define GICC_IAR    (*(volatile uint32_t *)(GICC_BASE + 0x00C))
#define GICC_EOIR   (*(volatile uint32_t *)(GICC_BASE + 0x010))

// Timer IRQ (EL1 Physical Timer is PPI 30)
#define TIMER_IRQ   30

// Maximum number of IRQs
#define MAX_IRQS    128

// IRQ handlers
static void (*irq_handlers[MAX_IRQS])(void);

// Timer state
static uint64_t timer_ticks = 0;
static uint32_t timer_interval_ticks = 0;
static uint32_t timer_freq = 0;

// Memory barriers
static inline void dsb(void) {
    asm volatile("dsb sy" ::: "memory");
}

static inline void isb(void) {
    asm volatile("isb" ::: "memory");
}

// Timer IRQ handler
static void timer_handler(void) {
    timer_ticks++;

    // Pump audio if playing
    virtio_sound_pump();

    // Preemptive scheduling - switch every 20 ticks (200ms timeslice)
    if ((timer_ticks % 20) == 0) {
        process_schedule_from_irq();
    }

    // Reload timer
    asm volatile("msr cntp_tval_el0, %0" :: "r"(timer_interval_ticks));
    isb();
}

// ============================================================================
// HAL Implementation
// ============================================================================

void hal_irq_init(void) {
    printf("[IRQ] Initializing GIC...\n");

    // Clear handler table
    for (int i = 0; i < MAX_IRQS; i++) {
        irq_handlers[i] = 0;
    }

    // Disable distributor while configuring
    dsb();
    GICD_CTLR = 0;
    dsb();

    // Get number of IRQ lines
    uint32_t typer = GICD_TYPER;
    uint32_t num_irqs = ((typer & 0x1F) + 1) * 32;
    printf("[IRQ] GIC supports %d IRQs\n", num_irqs);

    // Disable all IRQs
    for (uint32_t i = 0; i < num_irqs / 32; i++) {
        GICD_ICENABLER(i) = 0xFFFFFFFF;
    }
    dsb();

    // Clear all pending IRQs
    for (uint32_t i = 0; i < num_irqs / 32; i++) {
        GICD_ICPENDR(i) = 0xFFFFFFFF;
    }
    dsb();

    // Set all SPIs to Group 0 (Secure)
    for (uint32_t i = 0; i < num_irqs / 32; i++) {
        GICD_IGROUPR(i) = 0x00000000;
    }
    dsb();
    printf("[IRQ] All interrupts set to Group 0 (Secure)\n");

    // Set all IRQs to mid priority
    for (uint32_t i = 0; i < num_irqs / 4; i++) {
        GICD_IPRIORITYR(i) = 0xA0A0A0A0;
    }
    dsb();

    // Route all SPIs to CPU 0
    for (uint32_t i = 8; i < num_irqs / 4; i++) {
        GICD_ITARGETSR(i) = 0x01010101;
    }
    dsb();
    printf("[IRQ] All SPIs targeted to CPU 0\n");

    // Configure all SPIs as level-sensitive
    for (uint32_t i = 2; i < num_irqs / 16; i++) {
        GICD_ICFGR(i) = 0x00000000;
    }
    dsb();

    // Enable distributor
    GICD_CTLR = 0x1;
    dsb();

    // Configure CPU interface
    GICC_PMR = 0xFF;
    dsb();
    GICC_CTLR = 0x1;
    dsb();

    printf("[IRQ] GIC initialized (Secure, Group 0)\n");
}

void hal_irq_enable(void) {
    asm volatile("msr daifclr, #2" ::: "memory");
}

void hal_irq_disable(void) {
    asm volatile("msr daifset, #2" ::: "memory");
}

void hal_irq_enable_irq(uint32_t irq) {
    if (irq >= MAX_IRQS) return;
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    dsb();
    GICD_ISENABLER(reg) = (1 << bit);
    dsb();
}

void hal_irq_disable_irq(uint32_t irq) {
    if (irq >= MAX_IRQS) return;
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    dsb();
    GICD_ICENABLER(reg) = (1 << bit);
    dsb();
}

void hal_irq_register_handler(uint32_t irq, void (*handler)(void)) {
    if (irq < MAX_IRQS) {
        irq_handlers[irq] = handler;
    }
}

// ============================================================================
// Timer HAL
// ============================================================================

void hal_timer_init(uint32_t interval_ms) {
    // Get timer frequency
    asm volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));
    printf("[TIMER] Frequency: %u Hz\n", timer_freq);

    // Calculate ticks per interval
    timer_interval_ticks = (timer_freq / 1000) * interval_ms;
    printf("[TIMER] Interval: %u ms (%u ticks)\n", interval_ms, timer_interval_ticks);

    // Set timer value
    asm volatile("msr cntp_tval_el0, %0" :: "r"(timer_interval_ticks));
    isb();

    // Enable timer
    asm volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1));
    isb();

    // Enable timer IRQ in GIC
    hal_irq_enable_irq(TIMER_IRQ);

    printf("[TIMER] Timer initialized\n");
}

uint64_t hal_timer_get_ticks(void) {
    return timer_ticks;
}

void hal_timer_set_interval(uint32_t interval_ms) {
    timer_interval_ticks = (timer_freq / 1000) * interval_ms;
}

// ============================================================================
// Main IRQ Handler (called from vectors.S)
// ============================================================================

void handle_irq(void) {
    dsb();

    // Read interrupt ID
    uint32_t iar = GICC_IAR;
    uint32_t irq = iar & 0x3FF;

    // Check for spurious interrupt
    if (irq == 1023) {
        return;
    }

    // Handle the interrupt
    if (irq == TIMER_IRQ) {
        timer_handler();
    } else if (irq_handlers[irq]) {
        irq_handlers[irq]();
    } else {
        printf("[IRQ] Unhandled IRQ %d\n", irq);
    }

    // Signal end of interrupt
    dsb();
    GICC_EOIR = iar;
    dsb();
}
