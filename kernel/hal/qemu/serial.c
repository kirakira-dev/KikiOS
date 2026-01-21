/*
 * QEMU virt machine Serial Driver
 *
 * PL011 UART at 0x09000000
 */

#include "../hal.h"

// PL011 UART base address (QEMU virt machine)
#define UART0_BASE  0x09000000

// PL011 registers
#define UART_DR     (*(volatile uint32_t *)(UART0_BASE + 0x00))  // Data Register
#define UART_FR     (*(volatile uint32_t *)(UART0_BASE + 0x18))  // Flag Register

// Flag register bits
#define UART_FR_TXFF    (1 << 5)    // Transmit FIFO Full
#define UART_FR_RXFE    (1 << 4)    // Receive FIFO Empty

void hal_serial_init(void) {
    // PL011 is already initialized by QEMU
    // Nothing to do
}

void hal_serial_putc(char c) {
    // Wait until transmit FIFO is not full
    while (UART_FR & UART_FR_TXFF) {
        asm volatile("nop");
    }
    UART_DR = c;
}

int hal_serial_getc(void) {
    // Return -1 if no data available
    if (UART_FR & UART_FR_RXFE) {
        return -1;
    }
    return UART_DR & 0xFF;
}
