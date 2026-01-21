/*
 * DWC2 Core Functions
 * Low-level initialization, cache ops, mailbox, port control
 */

#ifndef DWC2_CORE_H
#define DWC2_CORE_H

#include <stdint.h>
#include <stddef.h>
#include "usb_types.h"

// Debug output control
// 0 = errors only, 1 = key events, 2 = verbose
#define USB_DEBUG_LEVEL 1

#if USB_DEBUG_LEVEL >= 2
#define usb_debug(...) printf(__VA_ARGS__)
#else
#define usb_debug(...) ((void)0)
#endif

#if USB_DEBUG_LEVEL >= 1
#define usb_info(...) printf(__VA_ARGS__)
#else
#define usb_info(...) ((void)0)
#endif

// Memory barriers
void dmb(void);
void dsb(void);

// Timing
void usleep(uint32_t us);
void msleep(uint32_t ms);

// Cache operations (required for DMA on real hardware)
void clean_data_cache_range(uintptr_t start, size_t length);
void invalidate_data_cache_range(uintptr_t start, size_t length);

// Address conversion for DMA
uint32_t arm_to_bus(void *ptr);

// Power control via mailbox
int usb_set_power(int on);

// Core initialization
int usb_core_reset(void);
int usb_flush_fifos(void);
int usb_init_host(void);

// Port control
int usb_port_power_on(void);
int usb_port_reset(void);
int usb_wait_for_device(void);

// Channel control
void usb_halt_channel(int ch);

// DMA buffer (shared, 64-byte aligned for cache)
extern uint8_t dma_buffer[512] __attribute__((aligned(64)));

// Mailbox buffer (16-byte aligned)
extern volatile uint32_t mbox_buf[36] __attribute__((aligned(16)));

#endif // DWC2_CORE_H
