/*
 * KikiOS Kernel Log Implementation
 *
 * Simple circular ring buffer that stores kernel messages.
 * Uses static allocation so it works before memory_init().
 */

#include "klog.h"
#include "string.h"

// Ring buffer (static - no malloc needed)
static char klog_buffer[KLOG_BUFFER_SIZE];
static size_t klog_head = 0;      // Write position
static size_t klog_total = 0;     // Total bytes ever written
static int klog_initialized = 0;

void klog_init(void) {
    memset(klog_buffer, 0, sizeof(klog_buffer));
    klog_head = 0;
    klog_total = 0;
    klog_initialized = 1;
}

void klog_putc(char c) {
    if (!klog_initialized) return;

    klog_buffer[klog_head] = c;
    klog_head = (klog_head + 1) % KLOG_BUFFER_SIZE;
    klog_total++;
}

size_t klog_size(void) {
    if (klog_total > KLOG_BUFFER_SIZE) {
        return KLOG_BUFFER_SIZE;
    }
    return klog_total;
}

size_t klog_read(char *buf, size_t offset, size_t size) {
    if (!klog_initialized || !buf || size == 0) return 0;

    size_t log_size = klog_size();
    if (offset >= log_size) return 0;

    // Calculate available bytes from offset
    size_t available = log_size - offset;
    if (size > available) size = available;

    // Calculate actual buffer position
    // If buffer wrapped, oldest data starts at klog_head
    size_t start;
    if (klog_total > KLOG_BUFFER_SIZE) {
        // Wrapped: oldest is at klog_head, add offset
        start = (klog_head + offset) % KLOG_BUFFER_SIZE;
    } else {
        // Not wrapped: data starts at 0
        start = offset;
    }

    // Copy data (may need to wrap around)
    for (size_t i = 0; i < size; i++) {
        buf[i] = klog_buffer[(start + i) % KLOG_BUFFER_SIZE];
    }

    return size;
}
