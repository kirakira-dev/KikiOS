/*
 * Raspberry Pi Serial Driver
 *
 * PL011 UART at 0x3F201000
 *
 * Works on both real Pi (with serial cable) and QEMU raspi3b.
 * On real Pi, requires disable_bt in config.txt to free PL011 from Bluetooth.
 */

#include "../hal.h"

// Peripheral base for Pi Zero 2W (BCM2710)
#define PERI_BASE       0x3F000000

// PL011 UART base
#define UART0_BASE      (PERI_BASE + 0x201000)

// PL011 registers
#define UART_DR         (*(volatile uint32_t *)(UART0_BASE + 0x00))  // Data Register
#define UART_FR         (*(volatile uint32_t *)(UART0_BASE + 0x18))  // Flag Register
#define UART_IBRD       (*(volatile uint32_t *)(UART0_BASE + 0x24))  // Integer Baud Rate
#define UART_FBRD       (*(volatile uint32_t *)(UART0_BASE + 0x28))  // Fractional Baud Rate
#define UART_LCRH       (*(volatile uint32_t *)(UART0_BASE + 0x2C))  // Line Control
#define UART_CR         (*(volatile uint32_t *)(UART0_BASE + 0x30))  // Control Register
#define UART_ICR        (*(volatile uint32_t *)(UART0_BASE + 0x44))  // Interrupt Clear

// Flag register bits
#define UART_FR_TXFF    (1 << 5)    // Transmit FIFO Full
#define UART_FR_RXFE    (1 << 4)    // Receive FIFO Empty

// Line control bits
#define UART_LCRH_WLEN_8BIT (3 << 5)  // 8-bit word length
#define UART_LCRH_FEN       (1 << 4)  // Enable FIFOs

// Control register bits
#define UART_CR_UARTEN  (1 << 0)    // UART enable
#define UART_CR_TXE     (1 << 8)    // Transmit enable
#define UART_CR_RXE     (1 << 9)    // Receive enable

void hal_serial_init(void) {
    // Disable UART while configuring
    UART_CR = 0;

    // Clear pending interrupts
    UART_ICR = 0x7FF;

    // Set baud rate to 115200 (assuming 48MHz UART clock)
    // Divider = 48000000 / (16 * 115200) = 26.042
    // Integer part = 26, Fractional part = 0.042 * 64 = 2.688 ~ 3
    UART_IBRD = 26;
    UART_FBRD = 3;

    // 8 bits, no parity, 1 stop bit, enable FIFOs
    UART_LCRH = UART_LCRH_WLEN_8BIT | UART_LCRH_FEN;

    // Enable UART, TX and RX
    UART_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
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
