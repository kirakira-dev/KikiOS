/*
 * KikiOS Interrupt Handling
 */

#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

// Initialize GIC and enable interrupts
void irq_init(void);

// Enable/disable all interrupts (modifies DAIF)
void irq_enable(void);
void irq_disable(void);

// Enable/disable specific IRQ in GIC
void irq_enable_irq(uint32_t irq);
void irq_disable_irq(uint32_t irq);

// Register an IRQ handler
typedef void (*irq_handler_t)(void);
void irq_register_handler(uint32_t irq, irq_handler_t handler);

// Called from vectors.S
void handle_irq(void);
void handle_sync_exception(uint64_t esr, uint64_t elr, uint64_t far, uint64_t *regs);
void handle_fiq(void);
void handle_serror(uint64_t esr);

// Timer functions
void timer_init(uint32_t interval_ms);
void timer_set_interval(uint32_t interval_ms);

// Get timer interrupt count (for debugging)
uint64_t timer_get_ticks(void);

// Wait for interrupt (low power sleep until next interrupt)
void wfi(void);

// Sleep for at least the specified number of milliseconds
// Uses timer ticks (10ms resolution with 100Hz timer)
void sleep_ms(uint32_t ms);

#endif // IRQ_H
