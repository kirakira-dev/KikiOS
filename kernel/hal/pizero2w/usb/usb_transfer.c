/*
 * USB Transfer Functions
 * Control transfers and DMA handling
 */

#include "usb_transfer.h"
#include "dwc2_core.h"
#include "dwc2_regs.h"
#include "../../../printf.h"
#include "../../../string.h"

// ============================================================================
// Split Transaction Support (for FS/LS devices behind HS hubs)
// ============================================================================

// Find device by address
static usb_device_t *usb_find_device(int addr) {
    for (int i = 0; i < usb_state.num_devices; i++) {
        if (usb_state.devices[i].address == addr) {
            return &usb_state.devices[i];
        }
    }
    return NULL;
}

// Configure HCSPLT for split transactions if device is FS/LS behind HS hub
void usb_set_split_if_needed(int ch, int dev_addr) {
    int parent_hub, parent_port, dev_speed;

    if (dev_addr == 0) {
        // Address 0: Use enumeration context for split routing
        // This is critical for enumerating FS/LS devices behind HS hubs
        parent_hub = usb_state.enum_parent_hub;
        parent_port = usb_state.enum_parent_port;
        dev_speed = usb_state.enum_speed;

        if (parent_hub == 0) {
            // Device on root port, no split needed
            HCSPLT(ch) = 0;
            return;
        }
    } else {
        // Look up device in device list
        usb_device_t *dev = usb_find_device(dev_addr);
        if (!dev) {
            HCSPLT(ch) = 0;
            return;
        }

        // If device is directly on root port, no split needed
        if (dev->parent_hub == 0) {
            HCSPLT(ch) = 0;
            return;
        }

        parent_hub = dev->parent_hub;
        parent_port = dev->parent_port;
        dev_speed = dev->speed;
    }

    // Find the parent hub to check its speed
    usb_device_t *hub = usb_find_device(parent_hub);
    if (!hub) {
        HCSPLT(ch) = 0;
        return;
    }

    // Check if device is FS/LS (speed != 0) and hub is HS (speed == 0)
    int dev_is_fsls = (dev_speed != 0);  // 1=FS, 2=LS
    int hub_is_hs = (hub->speed == 0);   // 0=HS

    if (dev_is_fsls && hub_is_hs) {
        // Need split transactions!
        uint32_t hcsplt = HCSPLT_SPLITENA |
                          (parent_port & HCSPLT_PRTADDR_MASK) |
                          ((parent_hub & 0x7F) << HCSPLT_HUBADDR_SHIFT) |
                          (HCSPLT_XACTPOS_ALL << HCSPLT_XACTPOS_SHIFT);
        HCSPLT(ch) = hcsplt;
        usb_info("[USB] Split enabled: ch=%d hub=%d port=%d dev_speed=%d HCSPLT=%08x\n",
                 ch, parent_hub, parent_port, dev_speed, hcsplt);
    } else {
        HCSPLT(ch) = 0;
    }
}

// ============================================================================
// DMA Transfer Functions
// ============================================================================

// Wait for split transaction - need at least 1ms for FS transaction to complete
static inline void usb_wait_split_delay(void) {
    // Use msleep which now uses kernel timer ticks (known to work)
    msleep(2);  // 2ms to be safe (rounds up to 10ms with 100Hz tick)
}

// Helper to re-enable channel with updated ODDFRM (critical for split transactions)
static inline void usb_reenable_channel(int ch) {
    uint32_t hcchar = HCCHAR(ch);
    // Update odd/even frame bit for proper scheduling
    hcchar &= ~HCCHAR_ODDFRM;
    if (HFNUM & 1) hcchar |= HCCHAR_ODDFRM;
    hcchar |= HCCHAR_CHENA;
    hcchar &= ~HCCHAR_CHDIS;
    HCCHAR(ch) = hcchar;
    dsb();
}

// Wait for DMA transfer to complete (with split transaction support)
int usb_wait_for_dma_complete(int ch, int max_retries) {
    int split_retries = 0;
    const int max_split_retries = 100;  // NYET can happen many times

    for (int retry = 0; retry < max_retries; retry++) {
        int timeout = 100000;
        while (timeout--) {
            uint32_t hcint = HCINT(ch);
            uint32_t hcsplt = HCSPLT(ch);

            int split_enabled = (hcsplt & HCSPLT_SPLITENA) != 0;
            int in_compsplt = (hcsplt & HCSPLT_COMPSPLT) != 0;

            // Check for transfer complete
            if (hcint & HCINT_XFERCOMPL) {
                HCINT(ch) = 0xFFFFFFFF;
                // Clear COMPSPLT for next transfer
                if (split_enabled) {
                    HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                }
                return 0;  // Success
            }

            if (hcint & HCINT_CHHLTD) {
                // Channel halted - check why

                // For split transactions: handle the 2-phase state machine
                if (split_enabled) {
                    // PHASE 1: Start-split phase (COMPSPLT == 0)
                    // ACK or NYET means we need to transition to complete-split
                    if (!in_compsplt && (hcint & (HCINT_ACK | HCINT_NYET))) {
                        if (split_retries < max_split_retries) {
                            HCINT(ch) = 0xFFFFFFFF;
                            HCSPLT(ch) |= HCSPLT_COMPSPLT;  // Transition to complete-split
                            dsb();
                            // Wait for TT to do the FS/LS transaction
                            usb_wait_split_delay();
                            usb_reenable_channel(ch);
                            split_retries++;
                            if (split_retries == 1) {
                                usb_info("[USB] Start-split -> complete-split (hcint=%08x)\n", hcint);
                            }
                            continue;  // Keep waiting
                        }
                        usb_info("[USB] Split timeout in start-split after %d retries\n", split_retries);
                        break;
                    }

                    // PHASE 2: Complete-split phase (COMPSPLT == 1)
                    // NYET means hub TT not ready yet, retry complete-split
                    if (in_compsplt && (hcint & HCINT_NYET)) {
                        if (split_retries < max_split_retries) {
                            HCINT(ch) = 0xFFFFFFFF;
                            // Wait before retry - TT needs time
                            usb_wait_split_delay();
                            usb_reenable_channel(ch);
                            split_retries++;
                            continue;  // Keep waiting
                        }
                        usb_info("[USB] Split timeout in complete-split after %d retries (hcint=%08x)\n", split_retries, hcint);
                        break;
                    }

                    // Complete-split with ACK means success
                    if (in_compsplt && (hcint & HCINT_ACK)) {
                        HCINT(ch) = 0xFFFFFFFF;
                        HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                        return 0;  // Success
                    }

                    // NAK during split - clear COMPSPLT and retry from start
                    if (hcint & HCINT_NAK) {
                        HCINT(ch) = 0xFFFFFFFF;
                        HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                        break;  // Break to main retry loop
                    }
                } else {
                    // Non-split transaction: ACK with halt means done
                    if (hcint & (HCINT_XFERCOMPL | HCINT_ACK)) {
                        HCINT(ch) = 0xFFFFFFFF;
                        return 0;  // Transfer actually completed
                    }

                    // NAK - retry
                    if (hcint & HCINT_NAK) {
                        HCINT(ch) = 0xFFFFFFFF;
                        break;  // Break to retry loop
                    }
                }

                // Error conditions (both split and non-split)
                if (hcint & (HCINT_STALL | HCINT_XACTERR | HCINT_BBLERR | HCINT_AHBERR)) {
                    usb_info("[USB] Transfer error: hcint=%08x hcsplt=%08x\n", hcint, hcsplt);
                    HCINT(ch) = 0xFFFFFFFF;
                    if (split_enabled) HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                    return -1;
                }

                // Unexpected halt during split - log it
                if (split_enabled && !(hcint & (HCINT_ACK | HCINT_NYET | HCINT_NAK))) {
                    usb_info("[USB] Unexpected split halt: hcint=%08x compsplt=%d\n", hcint, in_compsplt);
                }

                // Other halt reason - assume done
                HCINT(ch) = 0xFFFFFFFF;
                if (split_enabled) HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
                return 0;
            }

            // Error checks outside of CHHLTD
            if (hcint & HCINT_AHBERR) {
                usb_debug("[USB] AHB error (bad DMA address?)\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }
            if (hcint & HCINT_STALL) {
                usb_debug("[USB] STALL\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }
            if (hcint & HCINT_BBLERR) {
                usb_debug("[USB] Babble error\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }
            if (hcint & HCINT_XACTERR) {
                usb_debug("[USB] Transaction error\n");
                HCINT(ch) = 0xFFFFFFFF;
                return -1;
            }

            usleep(50);  // Give hardware time to update status (50us)
        }

        if (retry < max_retries - 1) {
            usb_debug("[USB] Retry %d/%d: hcint=%08x hcchar=%08x hctsiz=%08x\n",
                      retry + 1, max_retries, HCINT(ch), HCCHAR(ch), HCTSIZ(ch));
            // Clear COMPSPLT for fresh start-split
            if (HCSPLT(ch) & HCSPLT_SPLITENA) {
                HCSPLT(ch) &= ~HCSPLT_COMPSPLT;
            }
            split_retries = 0;  // Reset split retry counter
            // Re-enable channel for retry
            usb_reenable_channel(ch);
            usleep(1000);
        }
    }

    usb_debug("[USB] Transfer timeout after %d retries\n", max_retries);
    return -1;
}

// Control transfer using DMA (SETUP + optional DATA + STATUS)
int usb_control_transfer(int device_addr, usb_setup_packet_t *setup,
                         void *data, int data_len, int data_in) {
    int ch = 0;  // Use channel 0 for control

    usb_debug("[USB] Control: addr=%d req=%02x val=%04x len=%d %s\n",
              device_addr, setup->bRequest, setup->wValue, data_len,
              data_in ? "IN" : "OUT");

    // Halt channel if active
    usb_halt_channel(ch);

    // Configure split transactions if device is FS/LS behind HS hub
    usb_set_split_if_needed(ch, device_addr);

    // Configure channel for control endpoint
    // Look up device to get MPS and speed
    uint32_t mps = 64;  // Default for FS/HS
    int dev_speed = usb_state.device_speed;

    if (device_addr == 0) {
        mps = (usb_state.device_speed == 2) ? 8 : 64;  // LS=8, FS/HS=64
    } else {
        // Find device in our list
        for (int i = 0; i < usb_state.num_devices; i++) {
            if (usb_state.devices[i].address == device_addr) {
                mps = usb_state.devices[i].max_packet_size;
                dev_speed = usb_state.devices[i].speed;
                break;
            }
        }
        if (mps == 0) mps = 64;
    }

    uint32_t hcchar_base = (mps & HCCHAR_MPS_MASK) |
                           (0 << HCCHAR_EPNUM_SHIFT) |         // EP0
                           (HCCHAR_EPTYPE_CTRL << HCCHAR_EPTYPE_SHIFT) |
                           (device_addr << HCCHAR_DEVADDR_SHIFT) |
                           (1 << HCCHAR_MC_SHIFT);             // 1 transaction per frame

    if (dev_speed == 2) {  // Low-speed
        hcchar_base |= HCCHAR_LSDEV;
    }

    // ========== SETUP Stage (DMA) ==========
    usb_debug("[USB] SETUP stage (DMA)...\n");

    // Copy SETUP packet to DMA buffer
    memcpy(dma_buffer, setup, 8);
    // CRITICAL: Flush CPU cache so DMA controller sees the data!
    clean_data_cache_range((uintptr_t)dma_buffer, 8);
    dsb();

    // Clear all channel interrupts
    HCINT(ch) = 0xFFFFFFFF;

    // Enable interrupts for this channel
    HCINTMSK(ch) = HCINT_XFERCOMPL | HCINT_CHHLTD | HCINT_STALL |
                   HCINT_NAK | HCINT_ACK | HCINT_XACTERR | HCINT_BBLERR | HCINT_AHBERR;

    // Set DMA address (bus address)
    HCDMA(ch) = arm_to_bus(dma_buffer);
    dsb();

    // Configure channel (OUT direction for SETUP)
    HCCHAR(ch) = hcchar_base;
    dsb();

    // Transfer size: 8 bytes, 1 packet, SETUP PID
    HCTSIZ(ch) = 8 | (1 << HCTSIZ_PKTCNT_SHIFT) | (HCTSIZ_PID_SETUP << HCTSIZ_PID_SHIFT);
    dsb();

    usb_debug("[USB] SETUP: HCDMA=%08x HCCHAR=%08x HCTSIZ=%08x\n",
              HCDMA(ch), HCCHAR(ch), HCTSIZ(ch));

    // Enable channel to start the transfer
    HCCHAR(ch) = hcchar_base | HCCHAR_CHENA;
    dsb();

    // Wait for SETUP completion
    if (usb_wait_for_dma_complete(ch, 5) < 0) {
        usb_debug("[USB] SETUP failed\n");
        return -1;
    }
    usb_debug("[USB] SETUP complete\n");

    // ========== DATA Stage (if any) ==========
    int bytes_transferred = 0;

    if (data_len > 0 && data != NULL) {
        usb_debug("[USB] DATA stage (%d bytes, %s)...\n", data_len, data_in ? "IN" : "OUT");

        if (data_len > 512) {
            usb_debug("[USB] Data too large for DMA buffer\n");
            return -1;
        }

        // Configure for data direction
        uint32_t data_hcchar = hcchar_base;
        if (data_in) {
            data_hcchar |= HCCHAR_EPDIR;  // IN
            // Clear DMA buffer for IN transfer
            memset(dma_buffer, 0, data_len);
            // CLEAN (flush) zeros to RAM so buffer is zeroed before DMA writes
            // We'll invalidate AFTER the transfer to read the DMA data
            clean_data_cache_range((uintptr_t)dma_buffer, data_len);
        } else {
            // Copy data to DMA buffer for OUT transfer
            memcpy(dma_buffer, data, data_len);
            // Flush cache so DMA controller sees the data
            clean_data_cache_range((uintptr_t)dma_buffer, data_len);
        }
        dsb();

        // Calculate packet count
        int pkt_count = (data_len + mps - 1) / mps;
        if (pkt_count == 0) pkt_count = 1;

        // Clear interrupts
        HCINT(ch) = 0xFFFFFFFF;

        // Set DMA address
        HCDMA(ch) = arm_to_bus(dma_buffer);
        dsb();

        // Configure channel
        HCCHAR(ch) = data_hcchar;
        dsb();

        // Transfer size, packet count, DATA1 PID (first data after SETUP is always DATA1)
        HCTSIZ(ch) = data_len | (pkt_count << HCTSIZ_PKTCNT_SHIFT) |
                     (HCTSIZ_PID_DATA1 << HCTSIZ_PID_SHIFT);
        dsb();

        usb_debug("[USB] DATA: HCDMA=%08x HCCHAR=%08x HCTSIZ=%08x\n",
                  HCDMA(ch), HCCHAR(ch), HCTSIZ(ch));

        // Enable channel
        HCCHAR(ch) = data_hcchar | HCCHAR_CHENA;
        dsb();

        // Wait for completion
        if (usb_wait_for_dma_complete(ch, 10) < 0) {
            usb_debug("[USB] DATA stage failed\n");
            return -1;
        }

        if (data_in) {
            // Invalidate cache to ensure CPU reads fresh data from RAM
            invalidate_data_cache_range((uintptr_t)dma_buffer, data_len);

            // Copy received data from DMA buffer
            // Calculate actual bytes received from HCTSIZ
            uint32_t remaining = HCTSIZ(ch) & HCTSIZ_XFERSIZE_MASK;
            bytes_transferred = data_len - remaining;
            if (bytes_transferred > 0) {
                memcpy(data, dma_buffer, bytes_transferred);
            }
            usb_debug("[USB] DATA IN: received %d bytes\n", bytes_transferred);
        } else {
            bytes_transferred = data_len;
            usb_debug("[USB] DATA OUT: sent %d bytes\n", bytes_transferred);
        }
    }

    // ========== STATUS Stage ==========
    usb_debug("[USB] STATUS stage...\n");

    // Status is opposite direction of data (or IN if no data)
    int status_in = (data_len > 0) ? !data_in : 1;

    uint32_t status_hcchar = hcchar_base;
    if (status_in) {
        status_hcchar |= HCCHAR_EPDIR;
    }

    // Clear interrupts
    HCINT(ch) = 0xFFFFFFFF;

    // Set DMA address (zero-length, but still need valid address)
    HCDMA(ch) = arm_to_bus(dma_buffer);
    dsb();

    // Configure channel
    HCCHAR(ch) = status_hcchar;
    dsb();

    // Zero-length packet, DATA1 PID
    HCTSIZ(ch) = 0 | (1 << HCTSIZ_PKTCNT_SHIFT) | (HCTSIZ_PID_DATA1 << HCTSIZ_PID_SHIFT);
    dsb();

    usb_debug("[USB] STATUS: HCDMA=%08x HCCHAR=%08x HCTSIZ=%08x\n",
              HCDMA(ch), HCCHAR(ch), HCTSIZ(ch));

    // Enable channel
    HCCHAR(ch) = status_hcchar | HCCHAR_CHENA;
    dsb();

    // Wait for completion
    if (usb_wait_for_dma_complete(ch, 5) < 0) {
        usb_debug("[USB] STATUS failed\n");
        return -1;
    }

    usb_debug("[USB] Control transfer complete, %d bytes\n", bytes_transferred);
    return bytes_transferred;
}
