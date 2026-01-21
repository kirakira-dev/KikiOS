/*
 * USB HID Support
 * Keyboard interrupt handling, ISR, polling
 *
 * Features:
 * - Ring buffer for keyboard reports (no dropped keys)
 * - Debug counters instead of printf in ISR
 * - Watchdog for stuck transfers
 */

#include "usb_hid.h"
#include "usb_transfer.h"
#include "dwc2_core.h"
#include "dwc2_regs.h"
#include "../../../printf.h"
#include "../../../string.h"

// ============================================================================
// Debug Statistics (safe counters, no printf in ISR)
// ============================================================================

static usb_debug_stats_t debug_stats = {0};

const usb_debug_stats_t* usb_hid_get_stats(void) {
    return &debug_stats;
}

void usb_hid_print_stats(void) {
    printf("[USB-STATS] IRQ=%u KBD=%u data=%u NAK=%u NYET=%u err=%u restart=%u port=%u wd=%u\n",
           debug_stats.irq_count,
           debug_stats.kbd_irq_count,
           debug_stats.kbd_data_count,
           debug_stats.kbd_nak_count,
           debug_stats.kbd_nyet_count,
           debug_stats.kbd_error_count,
           debug_stats.kbd_restart_count,
           debug_stats.port_irq_count,
           debug_stats.watchdog_kicks);
    printf("[USB-STATS] MOUSE: IRQ=%u data=%u NAK=%u err=%u\n",
           debug_stats.mouse_irq_count,
           debug_stats.mouse_data_count,
           debug_stats.mouse_nak_count,
           debug_stats.mouse_error_count);
}

// ============================================================================
// Keyboard Ring Buffer (ISR writes, main loop reads)
// ============================================================================

typedef struct {
    uint8_t reports[KBD_RING_SIZE][8];
    volatile int head;  // ISR writes here (producer)
    volatile int tail;  // Consumer reads here
} kbd_ring_t;

static kbd_ring_t kbd_ring = {0};

// ============================================================================
// Mouse Ring Buffer (ISR writes, main loop reads)
// ============================================================================

#define MOUSE_RING_SIZE 32
#define MOUSE_REPORT_SIZE 8  // Boot protocol mouse: 3 bytes min, but some send more

typedef struct {
    uint8_t reports[MOUSE_RING_SIZE][MOUSE_REPORT_SIZE];
    volatile int head;  // ISR writes here (producer)
    volatile int tail;  // Consumer reads here
} mouse_ring_t;

static mouse_ring_t mouse_ring = {0};

// Push a report to the mouse ring buffer (called from ISR)
static inline void mouse_ring_push(const uint8_t *report) {
    int next = (mouse_ring.head + 1) % MOUSE_RING_SIZE;
    if (next != mouse_ring.tail) {  // Not full
        memcpy(mouse_ring.reports[mouse_ring.head], report, MOUSE_REPORT_SIZE);
        mouse_ring.head = next;
    }
}

// Pop a report from the mouse ring buffer (called from main loop)
static inline int mouse_ring_pop(uint8_t *report) {
    if (mouse_ring.head == mouse_ring.tail) {
        return 0;  // Empty
    }
    memcpy(report, mouse_ring.reports[mouse_ring.tail], MOUSE_REPORT_SIZE);
    mouse_ring.tail = (mouse_ring.tail + 1) % MOUSE_RING_SIZE;
    return 1;
}

// Push a report to the ring buffer (called from ISR)
static inline void kbd_ring_push(const uint8_t *report) {
    int next = (kbd_ring.head + 1) % KBD_RING_SIZE;
    if (next != kbd_ring.tail) {  // Not full
        memcpy(kbd_ring.reports[kbd_ring.head], report, 8);
        kbd_ring.head = next;
    }
    // If full, drop the oldest (don't overwrite)
}

// Pop a report from the ring buffer (called from main loop)
static inline int kbd_ring_pop(uint8_t *report) {
    if (kbd_ring.head == kbd_ring.tail) {
        return 0;  // Empty
    }
    memcpy(report, kbd_ring.reports[kbd_ring.tail], 8);
    kbd_ring.tail = (kbd_ring.tail + 1) % KBD_RING_SIZE;
    return 1;
}

// ============================================================================
// DMA Buffers and State
// ============================================================================

// DMA buffer for keyboard interrupt transfers (64-byte aligned for cache)
static uint8_t __attribute__((aligned(64))) intr_dma_buffer[64];

// DMA buffer for mouse interrupt transfers (64-byte aligned for cache)
static uint8_t __attribute__((aligned(64))) mouse_dma_buffer[64];

// Data toggle for keyboard interrupt endpoint
static int keyboard_data_toggle = 0;

// Data toggle for mouse interrupt endpoint
static int mouse_data_toggle = 0;

// Helper to re-enable channel with updated ODDFRM (critical for split transactions)
static inline void kbd_reenable_channel(int ch) {
    uint32_t hcchar = HCCHAR(ch);
    // Update odd/even frame bit for proper scheduling
    hcchar &= ~HCCHAR_ODDFRM;
    if (HFNUM & 1) hcchar |= HCCHAR_ODDFRM;
    hcchar |= HCCHAR_CHENA;
    hcchar &= ~HCCHAR_CHDIS;
    HCCHAR(ch) = hcchar;
    dsb();
}

// Transfer state - Keyboard (channel 1)
static volatile int kbd_transfer_pending = 0;
static volatile uint32_t kbd_last_transfer_tick = 0;  // For watchdog

// Transfer state - Mouse (channel 2)
static volatile int mouse_transfer_pending = 0;
static volatile uint32_t mouse_last_transfer_tick = 0;  // For watchdog

// Split transaction state for keyboard ISR
static volatile uint16_t split_start_frame = 0;   // Frame when start-split completed
static volatile int split_nyet_count = 0;         // NYET retries in complete-split
#define MAX_SPLIT_NYET_RETRIES 50                 // Max NYETs before restart
#define SPLIT_FRAME_WAIT 8                        // Wait ~1ms (8 microframes) for FS transaction

// Split transaction state for mouse ISR
static volatile uint16_t mouse_split_start_frame = 0;
static volatile int mouse_split_nyet_count = 0;

// Port recovery state (set by IRQ, handled by timer)
static volatile int port_reset_pending = 0;
static volatile uint32_t port_reset_start_tick = 0;

// Tick counter
static uint32_t tick_counter = 0;

// ============================================================================
// Internal Transfer Functions
// ============================================================================

// Internal: configure and start a keyboard transfer on channel 1
static void usb_do_keyboard_transfer(void) {
    int ch = 1;
    int ep = usb_state.keyboard_ep;
    int addr = usb_state.keyboard_addr;

    debug_stats.kbd_restart_count++;

    // Check if channel is still enabled (shouldn't be!)
    uint32_t old_hcchar = HCCHAR(ch);
    if (old_hcchar & HCCHAR_CHENA) {
        // Channel still active - don't start another transfer
        return;
    }

    kbd_transfer_pending = 1;
    kbd_last_transfer_tick = tick_counter;

    // Reset split state for fresh transfer
    split_nyet_count = 0;
    split_start_frame = 0;

    // Configure split transactions if keyboard is FS/LS behind HS hub
    usb_set_split_if_needed(ch, addr);

    // Configure channel for interrupt IN endpoint
    uint32_t mps = 64;  // Full speed max
    uint32_t hcchar = (mps & HCCHAR_MPS_MASK) |
                      (ep << HCCHAR_EPNUM_SHIFT) |
                      HCCHAR_EPDIR |                              // IN direction
                      (HCCHAR_EPTYPE_INTR << HCCHAR_EPTYPE_SHIFT) |
                      (addr << HCCHAR_DEVADDR_SHIFT) |
                      (1 << HCCHAR_MC_SHIFT);

    // Odd/even frame scheduling
    uint32_t fnum = HFNUM & 0xFFFF;
    if (fnum & 1) {
        hcchar |= HCCHAR_ODDFRM;
    }

    // Clear DMA buffer and flush zeros to RAM for receive
    memset(intr_dma_buffer, 0, 8);
    // CLEAN (flush) zeros to RAM - we'll invalidate AFTER DMA completes
    clean_data_cache_range((uintptr_t)intr_dma_buffer, 8);
    dsb();

    // Configure channel interrupts
    // Enable CHHLTD (channel halted), NYET (for split transactions), and errors
    HCINT(ch) = 0xFFFFFFFF;
    HCINTMSK(ch) = HCINT_CHHLTD | HCINT_NYET | HCINT_XACTERR | HCINT_BBLERR;
    HCDMA(ch) = arm_to_bus(intr_dma_buffer);
    HCCHAR(ch) = hcchar;

    // Transfer size: 8 bytes, 1 packet, DATA0/DATA1 toggle
    uint32_t pid = keyboard_data_toggle ? HCTSIZ_PID_DATA1 : HCTSIZ_PID_DATA0;
    HCTSIZ(ch) = 8 | (1 << HCTSIZ_PKTCNT_SHIFT) | (pid << HCTSIZ_PID_SHIFT);
    dsb();

    // Enable channel - transfer starts, interrupt fires on completion
    HCCHAR(ch) = hcchar | HCCHAR_CHENA;
    dsb();
}

// Called from ISR to restart transfer (channel already halted)
static void usb_restart_keyboard_transfer(void) {
    usb_do_keyboard_transfer();
}

// Internal: configure and start a mouse transfer on channel 2
static void usb_do_mouse_transfer(void) {
    int ch = 2;
    int ep = usb_state.mouse_ep;
    int addr = usb_state.mouse_addr;

    // Check if channel is still enabled (shouldn't be!)
    uint32_t old_hcchar = HCCHAR(ch);
    if (old_hcchar & HCCHAR_CHENA) {
        // Channel still active - don't start another transfer
        return;
    }

    mouse_transfer_pending = 1;
    mouse_last_transfer_tick = tick_counter;

    // Reset split state for fresh transfer
    mouse_split_nyet_count = 0;
    mouse_split_start_frame = 0;

    // Configure split transactions if mouse is FS/LS behind HS hub
    usb_set_split_if_needed(ch, addr);

    // Configure channel for interrupt IN endpoint
    uint32_t mps = 64;  // Full speed max
    uint32_t hcchar = (mps & HCCHAR_MPS_MASK) |
                      (ep << HCCHAR_EPNUM_SHIFT) |
                      HCCHAR_EPDIR |                              // IN direction
                      (HCCHAR_EPTYPE_INTR << HCCHAR_EPTYPE_SHIFT) |
                      (addr << HCCHAR_DEVADDR_SHIFT) |
                      (1 << HCCHAR_MC_SHIFT);

    // Odd/even frame scheduling
    uint32_t fnum = HFNUM & 0xFFFF;
    if (fnum & 1) {
        hcchar |= HCCHAR_ODDFRM;
    }

    // Clear DMA buffer and flush zeros to RAM for receive
    memset(mouse_dma_buffer, 0, MOUSE_REPORT_SIZE);
    // CLEAN (flush) zeros to RAM - we'll invalidate AFTER DMA completes
    clean_data_cache_range((uintptr_t)mouse_dma_buffer, MOUSE_REPORT_SIZE);
    dsb();

    // Configure channel interrupts
    HCINT(ch) = 0xFFFFFFFF;
    HCINTMSK(ch) = HCINT_CHHLTD | HCINT_NYET | HCINT_XACTERR | HCINT_BBLERR;
    HCDMA(ch) = arm_to_bus(mouse_dma_buffer);
    HCCHAR(ch) = hcchar;

    // Transfer size: 8 bytes (mouse boot protocol), 1 packet, DATA0/DATA1 toggle
    uint32_t pid = mouse_data_toggle ? HCTSIZ_PID_DATA1 : HCTSIZ_PID_DATA0;
    HCTSIZ(ch) = MOUSE_REPORT_SIZE | (1 << HCTSIZ_PKTCNT_SHIFT) | (pid << HCTSIZ_PID_SHIFT);
    dsb();

    // Enable channel - transfer starts, interrupt fires on completion
    HCCHAR(ch) = hcchar | HCCHAR_CHENA;
    dsb();
}

// Called from ISR to restart mouse transfer (channel already halted)
static void usb_restart_mouse_transfer(void) {
    usb_do_mouse_transfer();
}

// Helper to re-enable mouse channel with updated ODDFRM
static inline void mouse_reenable_channel(int ch) {
    uint32_t hcchar = HCCHAR(ch);
    hcchar &= ~HCCHAR_ODDFRM;
    if (HFNUM & 1) hcchar |= HCCHAR_ODDFRM;
    hcchar |= HCCHAR_CHENA;
    hcchar &= ~HCCHAR_CHDIS;
    HCCHAR(ch) = hcchar;
    dsb();
}

// ============================================================================
// USB IRQ Handler (NO PRINTF ALLOWED!)
// ============================================================================

void usb_irq_handler(void) {
    uint32_t gintsts = GINTSTS;
    debug_stats.irq_count++;

    // Port interrupt - check what changed and react accordingly
    // WARNING: PRTENA is W1C - writing 1 DISABLES the port!
    if (gintsts & GINTSTS_PRTINT) {
        uint32_t hprt = HPRT0;
        debug_stats.port_irq_count++;

        // Check what happened
        int port_enabled = (hprt & HPRT0_PRTENA) ? 1 : 0;
        int port_connected = (hprt & HPRT0_PRTCONNSTS) ? 1 : 0;
        int enable_changed = (hprt & HPRT0_PRTENCHNG) ? 1 : 0;
        int connect_changed = (hprt & HPRT0_PRTCONNDET) ? 1 : 0;

        // Clear W1C status bits (but NOT PRTENA!)
        uint32_t hprt_write = hprt & ~HPRT0_PRTENA;
        HPRT0 = hprt_write;
        dsb();

        // React to port changes
        if (enable_changed && !port_enabled && port_connected) {
            // Port got disabled but device still connected - need to re-reset!
            // Assert reset
            hprt = HPRT0;
            hprt &= ~(HPRT0_PRTENA | HPRT0_PRTCONNDET | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG);
            hprt |= HPRT0_PRTRST;
            HPRT0 = hprt;
            dsb();

            // Set flag for timer to complete the reset (can't block 50ms in IRQ)
            port_reset_pending = 1;
            port_reset_start_tick = 0;  // Will be set by timer
            kbd_transfer_pending = 0;   // Stop keyboard polling during reset
        }

        if (connect_changed && !port_connected) {
            // Device disconnected
            usb_state.device_connected = 0;
            usb_state.keyboard_addr = 0;
            usb_state.mouse_addr = 0;
            kbd_transfer_pending = 0;
            mouse_transfer_pending = 0;
        }
    }

    // Host channel interrupt
    if (gintsts & GINTSTS_HCHINT) {
        uint32_t haint = HAINT;

        for (int ch = 0; ch < 16; ch++) {
            if (haint & (1 << ch)) {
                uint32_t hcint = HCINT(ch);

                // Channel 1 = keyboard interrupt transfers
                if (ch == 1 && usb_state.keyboard_addr != 0) {
                    debug_stats.kbd_irq_count++;

                    // Check split transaction state
                    uint32_t hcsplt = HCSPLT(ch);
                    int split_enabled = (hcsplt & HCSPLT_SPLITENA) != 0;
                    int in_compsplt = (hcsplt & HCSPLT_COMPSPLT) != 0;

                    if (hcint & HCINT_XFERCOMPL) {
                        // Transfer complete with data
                        keyboard_data_toggle = !keyboard_data_toggle;

                        // CRITICAL: Invalidate cache to read fresh DMA data
                        invalidate_data_cache_range((uintptr_t)intr_dma_buffer, 8);

                        uint32_t remaining = HCTSIZ(1) & HCTSIZ_XFERSIZE_MASK;
                        int received = 8 - remaining;
                        if (received > 0) {
                            kbd_ring_push(intr_dma_buffer);
                            debug_stats.kbd_data_count++;
                        }
                        // Clear COMPSPLT for next transfer
                        if (split_enabled) {
                            HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                        }
                    }
                    else if (hcint & HCINT_CHHLTD) {
                        // Channel halted - handle split transaction 2-phase state machine

                        if (split_enabled) {
                            // PHASE 1: Start-split phase (COMPSPLT == 0)
                            // ACK or NYET means transition to complete-split
                            if (!in_compsplt && (hcint & (HCINT_ACK | HCINT_NYET))) {
                                HCINT(ch) = 0xFFFFFFFF;
                                HCSPLT(ch) |= HCSPLT_COMPSPLT;  // Transition to complete-split
                                split_start_frame = HFNUM & 0xFFFF;  // Record frame for timing
                                split_nyet_count = 0;  // Reset NYET counter
                                dsb();
                                // Don't re-enable immediately - let timer tick handle it
                                // This gives TT time to do the FS transaction (~1ms)
                                continue;  // Stay in split flow, don't restart transfer
                            }

                            // PHASE 2: Complete-split phase (COMPSPLT == 1)
                            // NYET means hub TT not ready yet, retry complete-split
                            if (in_compsplt && (hcint & HCINT_NYET)) {
                                HCINT(ch) = 0xFFFFFFFF;
                                split_nyet_count++;
                                debug_stats.kbd_nyet_count++;

                                // Check if too many NYETs - abort and restart fresh
                                if (split_nyet_count >= MAX_SPLIT_NYET_RETRIES) {
                                    debug_stats.kbd_error_count++;
                                    HCSPLT(ch) &= ~HCSPLT_COMPSPLT;  // Clear for fresh start
                                    split_nyet_count = 0;
                                    // Fall through to restart transfer
                                } else {
                                    // Check if enough frames have passed since start-split
                                    uint16_t current_frame = HFNUM & 0xFFFF;
                                    uint16_t frames_elapsed = (current_frame - split_start_frame) & 0x3FFF;

                                    if (frames_elapsed >= SPLIT_FRAME_WAIT) {
                                        // Enough time passed, retry complete-split
                                        kbd_reenable_channel(ch);
                                    }
                                    // Otherwise don't re-enable - timer tick will handle it
                                    continue;  // Stay in split flow
                                }
                            }

                            // Complete-split with ACK means data received (success)
                            if (in_compsplt && (hcint & HCINT_ACK)) {
                                keyboard_data_toggle = !keyboard_data_toggle;

                                // CRITICAL: Invalidate cache to read fresh DMA data
                                invalidate_data_cache_range((uintptr_t)intr_dma_buffer, 8);

                                uint32_t remaining = HCTSIZ(1) & HCTSIZ_XFERSIZE_MASK;
                                int received = 8 - remaining;
                                if (received > 0) {
                                    kbd_ring_push(intr_dma_buffer);
                                    debug_stats.kbd_data_count++;
                                }
                                HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                                // Fall through to restart transfer
                            }
                            // NAK during split - clear and restart
                            else if (hcint & HCINT_NAK) {
                                debug_stats.kbd_nak_count++;
                                HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                            }
                            // Error during split
                            else if (hcint & (HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR)) {
                                debug_stats.kbd_error_count++;
                                HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                            }
                        } else {
                            // Non-split transaction
                            if (hcint & HCINT_ACK) {
                                // Got ACK with halt - data received
                                keyboard_data_toggle = !keyboard_data_toggle;

                                // CRITICAL: Invalidate cache to read fresh DMA data
                                invalidate_data_cache_range((uintptr_t)intr_dma_buffer, 8);

                                uint32_t remaining = HCTSIZ(1) & HCTSIZ_XFERSIZE_MASK;
                                int received = 8 - remaining;
                                if (received > 0) {
                                    kbd_ring_push(intr_dma_buffer);
                                    debug_stats.kbd_data_count++;
                                }
                            }
                            else if (hcint & HCINT_NAK) {
                                // NAK = no data available (normal for HID when no key pressed)
                                debug_stats.kbd_nak_count++;
                            }
                            else if (hcint & (HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR)) {
                                // Error - increment counter (no printf!)
                                debug_stats.kbd_error_count++;
                            }
                        }
                    }
                    // Note: Other cases (CHHLTD without specific bits) just fall through

                    // Clear channel interrupt first
                    HCINT(ch) = 0xFFFFFFFF;

                    // Immediately restart transfer for faster polling (~1ms vs 10ms)
                    // This ensures we catch quick keypresses and releases
                    kbd_transfer_pending = 0;
                    usb_restart_keyboard_transfer();

                    continue;  // Skip the HCINT clear below
                }

                // Channel 2 = mouse interrupt transfers
                if (ch == 2 && usb_state.mouse_addr != 0) {
                    debug_stats.mouse_irq_count++;

                    // Check split transaction state
                    uint32_t hcsplt = HCSPLT(ch);
                    int split_enabled = (hcsplt & HCSPLT_SPLITENA) != 0;
                    int in_compsplt = (hcsplt & HCSPLT_COMPSPLT) != 0;

                    if (hcint & HCINT_XFERCOMPL) {
                        // Transfer complete with data
                        mouse_data_toggle = !mouse_data_toggle;

                        // Invalidate cache to read fresh DMA data
                        invalidate_data_cache_range((uintptr_t)mouse_dma_buffer, MOUSE_REPORT_SIZE);

                        uint32_t remaining = HCTSIZ(2) & HCTSIZ_XFERSIZE_MASK;
                        int received = MOUSE_REPORT_SIZE - remaining;
                        if (received > 0) {
                            mouse_ring_push(mouse_dma_buffer);
                            debug_stats.mouse_data_count++;
                        }
                        if (split_enabled) {
                            HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                        }
                    }
                    else if (hcint & HCINT_CHHLTD) {
                        if (split_enabled) {
                            // Start-split phase
                            if (!in_compsplt && (hcint & (HCINT_ACK | HCINT_NYET))) {
                                HCINT(ch) = 0xFFFFFFFF;
                                HCSPLT(ch) |= HCSPLT_COMPSPLT;
                                mouse_split_start_frame = HFNUM & 0xFFFF;
                                mouse_split_nyet_count = 0;
                                dsb();
                                continue;
                            }

                            // Complete-split phase - NYET
                            if (in_compsplt && (hcint & HCINT_NYET)) {
                                HCINT(ch) = 0xFFFFFFFF;
                                mouse_split_nyet_count++;

                                if (mouse_split_nyet_count >= MAX_SPLIT_NYET_RETRIES) {
                                    debug_stats.mouse_error_count++;
                                    HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                                    mouse_split_nyet_count = 0;
                                } else {
                                    uint16_t current_frame = HFNUM & 0xFFFF;
                                    uint16_t frames_elapsed = (current_frame - mouse_split_start_frame) & 0x3FFF;
                                    if (frames_elapsed >= SPLIT_FRAME_WAIT) {
                                        mouse_reenable_channel(ch);
                                    }
                                    continue;
                                }
                            }

                            // Complete-split with ACK - data received
                            if (in_compsplt && (hcint & HCINT_ACK)) {
                                mouse_data_toggle = !mouse_data_toggle;
                                invalidate_data_cache_range((uintptr_t)mouse_dma_buffer, MOUSE_REPORT_SIZE);

                                uint32_t remaining = HCTSIZ(2) & HCTSIZ_XFERSIZE_MASK;
                                int received = MOUSE_REPORT_SIZE - remaining;
                                if (received > 0) {
                                    mouse_ring_push(mouse_dma_buffer);
                                    debug_stats.mouse_data_count++;
                                }
                                HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                            }
                            else if (hcint & HCINT_NAK) {
                                debug_stats.mouse_nak_count++;
                                HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                            }
                            else if (hcint & (HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR)) {
                                debug_stats.mouse_error_count++;
                                HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                            }
                        } else {
                            // Non-split transaction
                            if (hcint & HCINT_ACK) {
                                mouse_data_toggle = !mouse_data_toggle;
                                invalidate_data_cache_range((uintptr_t)mouse_dma_buffer, MOUSE_REPORT_SIZE);

                                uint32_t remaining = HCTSIZ(2) & HCTSIZ_XFERSIZE_MASK;
                                int received = MOUSE_REPORT_SIZE - remaining;
                                if (received > 0) {
                                    mouse_ring_push(mouse_dma_buffer);
                                    debug_stats.mouse_data_count++;
                                }
                            }
                            else if (hcint & HCINT_NAK) {
                                debug_stats.mouse_nak_count++;
                            }
                            else if (hcint & (HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR)) {
                                debug_stats.mouse_error_count++;
                            }
                        }
                    }

                    HCINT(ch) = 0xFFFFFFFF;
                    mouse_transfer_pending = 0;
                    usb_restart_mouse_transfer();
                    continue;
                }

                // Clear this channel's interrupts (for non-keyboard/mouse channels)
                HCINT(ch) = 0xFFFFFFFF;
            }
        }
    }

    // Clear global interrupt status
    GINTSTS = gintsts;
}

// ============================================================================
// Public API
// ============================================================================

void usb_start_keyboard_transfer(void) {
    if (kbd_transfer_pending) {
        return;
    }
    if (usb_state.keyboard_addr == 0) {
        return;
    }

    // If channel is still active, request disable (shouldn't happen normally)
    if (HCCHAR(1) & HCCHAR_CHENA) {
        HCCHAR(1) |= HCCHAR_CHDIS;
        dsb();
        return;  // Will be restarted by ISR when halt completes
    }

    printf("[USB] Starting keyboard transfers (addr=%d ep=%d)\n",
           usb_state.keyboard_addr, usb_state.keyboard_ep);
    usb_do_keyboard_transfer();
}

// Called from timer tick (every 10ms)
// Handles: port reset recovery, watchdog for stuck transfers
void hal_usb_keyboard_tick(void) {
    tick_counter++;

    // Handle port reset recovery (set by port IRQ)
    if (port_reset_pending == 1) {
        if (port_reset_start_tick == 0) {
            // First tick after reset asserted - record start time
            port_reset_start_tick = tick_counter;
            return;
        }

        // Wait 5 ticks (50ms) then de-assert reset
        if (tick_counter - port_reset_start_tick >= 5) {
            uint32_t hprt = HPRT0;
            hprt &= ~(HPRT0_PRTENA | HPRT0_PRTCONNDET | HPRT0_PRTENCHNG | HPRT0_PRTOVRCURRCHNG);
            hprt &= ~HPRT0_PRTRST;  // De-assert reset
            HPRT0 = hprt;
            dsb();

            // Wait for port to become enabled (check in future ticks)
            port_reset_pending = 2;  // Phase 2: waiting for enable
            port_reset_start_tick = tick_counter;
        }
        return;
    }

    // Phase 2: Wait for port to enable after reset
    if (port_reset_pending == 2) {
        uint32_t hprt = HPRT0;
        if (hprt & HPRT0_PRTENA) {
            printf("[USB] Port re-enabled after reset\n");
            port_reset_pending = 0;
            // Resume keyboard and mouse polling
            if (usb_state.keyboard_addr != 0) {
                usb_do_keyboard_transfer();
            }
            if (usb_state.mouse_addr != 0) {
                usb_do_mouse_transfer();
            }
        } else if (tick_counter - port_reset_start_tick >= 10) {
            // Timeout - port didn't enable
            printf("[USB] Port enable timeout after reset\n");
            port_reset_pending = 0;
        }
        return;
    }

    // Normal keyboard polling and watchdog
    if (!usb_state.initialized || !usb_state.device_connected) {
        return;
    }
    if (usb_state.keyboard_addr == 0) {
        return;
    }

    // Handle split transaction waiting (ISR deferred complete-split)
    // Check if channel 1 is in complete-split state but not enabled
    if ((HCSPLT(1) & HCSPLT_COMPSPLT) && !(HCCHAR(1) & HCCHAR_CHENA)) {
        uint16_t current_frame = HFNUM & 0xFFFF;
        uint16_t frames_elapsed = (current_frame - split_start_frame) & 0x3FFF;

        // At 10ms tick rate, we're definitely past the ~1ms FS transaction time
        // Re-enable channel to issue complete-split
        if (frames_elapsed >= SPLIT_FRAME_WAIT) {
            kbd_reenable_channel(1);
        }
        return;  // Don't do watchdog while in split flow
    }

    // WATCHDOG: If no successful transfer in 50ms (5 ticks), force restart
    if (kbd_transfer_pending &&
        (tick_counter - kbd_last_transfer_tick) >= 5) {

        debug_stats.watchdog_kicks++;

        // Force halt channel if still active
        if (HCCHAR(1) & HCCHAR_CHENA) {
            HCCHAR(1) |= HCCHAR_CHDIS;
            dsb();
            // Wait for halt (with longer timeout to prevent hardware race)
            for (int i = 0; i < 100000; i++) {
                dsb();
                if (HCINT(1) & HCINT_CHHLTD) break;
            }
            HCINT(1) = 0xFFFFFFFF;
        }

        // Clear any split transaction state
        HCSPLT(1) &= ~HCSPLT_COMPSPLT;
        split_nyet_count = 0;
        split_start_frame = 0;

        kbd_transfer_pending = 0;
        usb_do_keyboard_transfer();
        return;
    }

    // If no transfer pending and channel idle, start one (fallback)
    if (!kbd_transfer_pending) {
        if (!(HCCHAR(1) & HCCHAR_CHENA)) {
            usb_do_keyboard_transfer();
        }
    }

    // --- Mouse handling ---
    if (usb_state.mouse_addr == 0) {
        return;
    }

    // Handle mouse split transaction waiting
    if ((HCSPLT(2) & HCSPLT_COMPSPLT) && !(HCCHAR(2) & HCCHAR_CHENA)) {
        uint16_t current_frame = HFNUM & 0xFFFF;
        uint16_t frames_elapsed = (current_frame - mouse_split_start_frame) & 0x3FFF;
        if (frames_elapsed >= SPLIT_FRAME_WAIT) {
            mouse_reenable_channel(2);
        }
        return;
    }

    // Mouse watchdog
    if (mouse_transfer_pending &&
        (tick_counter - mouse_last_transfer_tick) >= 5) {

        if (HCCHAR(2) & HCCHAR_CHENA) {
            HCCHAR(2) |= HCCHAR_CHDIS;
            dsb();
            // Wait for halt (with longer timeout to prevent hardware race)
            for (int i = 0; i < 100000; i++) {
                dsb();
                if (HCINT(2) & HCINT_CHHLTD) break;
            }
            HCINT(2) = 0xFFFFFFFF;
        }

        HCSPLT(2) &= ~HCSPLT_COMPSPLT;
        mouse_split_nyet_count = 0;
        mouse_split_start_frame = 0;

        mouse_transfer_pending = 0;
        usb_do_mouse_transfer();
        return;
    }

    // If no mouse transfer pending, start one
    if (!mouse_transfer_pending) {
        if (!(HCCHAR(2) & HCCHAR_CHENA)) {
            usb_do_mouse_transfer();
        }
    }
}

// Poll keyboard for HID report (non-blocking)
// Returns number of bytes if data available, 0 if none, -1 on error
int hal_usb_keyboard_poll(uint8_t *report, int report_len) {
    if (!usb_state.initialized || !usb_state.device_connected) {
        return -1;
    }

    if (usb_state.keyboard_addr == 0) {
        return -1;
    }

    // Pop from ring buffer
    uint8_t ring_report[8];
    if (kbd_ring_pop(ring_report)) {
        int len = (report_len < 8) ? report_len : 8;
        memcpy(report, ring_report, len);
        return len;
    }

    return 0;  // No data available
}

// Start mouse interrupt transfers
void usb_start_mouse_transfer(void) {
    if (mouse_transfer_pending) {
        return;
    }
    if (usb_state.mouse_addr == 0) {
        return;
    }

    // If channel is still active, request disable
    if (HCCHAR(2) & HCCHAR_CHENA) {
        HCCHAR(2) |= HCCHAR_CHDIS;
        dsb();
        return;
    }

    printf("[USB] Starting mouse transfers (addr=%d ep=%d)\n",
           usb_state.mouse_addr, usb_state.mouse_ep);
    usb_do_mouse_transfer();
}

// Poll mouse for HID report (non-blocking)
// Returns number of bytes if data available, 0 if none, -1 on error
int hal_usb_mouse_poll(uint8_t *report, int report_len) {
    if (!usb_state.initialized || !usb_state.device_connected) {
        return -1;
    }

    if (usb_state.mouse_addr == 0) {
        return -1;
    }

    // Pop from ring buffer
    uint8_t ring_report[MOUSE_REPORT_SIZE];
    if (mouse_ring_pop(ring_report)) {
        int len = (report_len < MOUSE_REPORT_SIZE) ? report_len : MOUSE_REPORT_SIZE;
        memcpy(report, ring_report, len);
        return len;
    }

    return 0;  // No data available
}

// Check if mouse is available
int hal_usb_mouse_available(void) {
    return usb_state.initialized && usb_state.device_connected && usb_state.mouse_addr != 0;
}
