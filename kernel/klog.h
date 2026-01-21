/*
 * KikiOS Kernel Log
 *
 * Circular ring buffer for kernel messages.
 * All printf output is captured here for later retrieval via dmesg.
 */

#ifndef KLOG_H
#define KLOG_H

#include <stddef.h>

// Ring buffer size (64KB)
#define KLOG_BUFFER_SIZE (64 * 1024)

// Initialize the kernel log
void klog_init(void);

// Write a character to the log (called by printf)
void klog_putc(char c);

// Read from the log buffer
// Returns number of bytes copied to buf
// offset: starting position in the logical log (0 = oldest message)
// size: max bytes to read
size_t klog_read(char *buf, size_t offset, size_t size);

// Get total size of logged data (up to KLOG_BUFFER_SIZE)
size_t klog_size(void);

#endif
