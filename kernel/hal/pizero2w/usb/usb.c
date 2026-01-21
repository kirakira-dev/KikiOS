/*
 * USB Driver Main Entry Point
 * Raspberry Pi Zero 2W DWC2 USB Host Controller
 *
 * This file ties together:
 * - dwc2_core: Low-level PHY, reset, cache, mailbox
 * - usb_transfer: Control transfers and DMA
 * - usb_enum: Device enumeration and hub support
 * - usb_hid: Keyboard interrupt handling
 */

#include "dwc2_core.h"
#include "dwc2_regs.h"
#include "usb_transfer.h"
#include "usb_enum.h"
#include "usb_hid.h"
#include "../../hal.h"
#include "../../../printf.h"
#include "../../../string.h"

// IRQ number for USB on Pi
#define IRQ_VC_USB  17  // VideoCore bank1 IRQ 9

// External IRQ registration functions
extern void hal_irq_register_handler(uint32_t irq, void (*handler)(void));
extern void hal_irq_enable_irq(uint32_t irq);

// ============================================================================
// Public API
// ============================================================================

int hal_usb_init(void) {
    printf("[USB] Initializing DWC2 USB controller...\n");

    // Power on USB
    if (usb_set_power(1) < 0) {
        return -1;
    }

    // Give power time to stabilize
    printf("[USB] Waiting 100ms for power stabilize...\n");
    msleep(100);
    printf("[USB] Power stabilized, starting core reset...\n");

    // Core reset
    if (usb_core_reset() < 0) {
        return -1;
    }

    // Initialize host mode
    if (usb_init_host() < 0) {
        return -1;
    }

    // Power on port
    if (usb_port_power_on() < 0) {
        return -1;
    }

    // Wait for device connection
    if (usb_wait_for_device() < 0) {
        printf("[USB] No USB device found - continuing without USB\n");
        return 0;  // Not fatal
    }

    // Reset port (and get device speed)
    if (usb_port_reset() < 0) {
        return -1;
    }

    // Enumerate device (handles hubs recursively)
    if (usb_enumerate_device() < 0) {
        printf("[USB] Device enumeration failed\n");
        return -1;
    }

    usb_state.initialized = 1;
    printf("[USB] USB initialization complete!\n");

    // Warn if no keyboard detected (debug mode hangs in usb_keyboard_debug_loop)
#ifndef PI_DEBUG_MODE
    if (usb_state.keyboard_addr == 0) {
        printf("[USB] Warning: No keyboard detected, continuing without keyboard\n");
    }
#endif

    // Setup USB interrupts if we have keyboard or mouse
    if (usb_state.keyboard_addr || usb_state.mouse_addr) {
        if (usb_state.keyboard_addr) {
            printf("[USB] Keyboard at address %d, endpoint %d\n",
                   usb_state.keyboard_addr, usb_state.keyboard_ep);
        }
        if (usb_state.mouse_addr) {
            printf("[USB] Mouse at address %d, endpoint %d\n",
                   usb_state.mouse_addr, usb_state.mouse_ep);
        }

        // Clear any pending interrupts first
        GINTSTS = 0xFFFFFFFF;
        dsb();

        // Enable host channel interrupts:
        // Channel 1 = keyboard, Channel 2 = mouse
        uint32_t haint_mask = 0;
        if (usb_state.keyboard_addr) haint_mask |= (1 << 1);
        if (usb_state.mouse_addr) haint_mask |= (1 << 2);
        HAINTMSK = haint_mask;
        dsb();

        // Register handler with Pi interrupt controller
        hal_irq_register_handler(IRQ_VC_USB, usb_irq_handler);
        hal_irq_enable_irq(IRQ_VC_USB);

        // NOW enable global interrupts in DWC2 (handler is ready)
        GAHBCFG = GAHBCFG_DMA_EN | GAHBCFG_GLBL_INTR_EN;
        dsb();

        printf("[USB] IRQ setup: IRQ=%d GAHBCFG=%08x GINTMSK=%08x HAINTMSK=%08x\n",
               IRQ_VC_USB, GAHBCFG, GINTMSK, HAINTMSK);

        // Start keyboard transfers
        if (usb_state.keyboard_addr) {
            usb_start_keyboard_transfer();
        }

        // Start mouse transfers
        if (usb_state.mouse_addr) {
            usb_start_mouse_transfer();
        }

        // Debug: check state after starting transfers
        printf("[USB] After start: GINTSTS=%08x HAINT=%08x\n", GINTSTS, HAINT);
    }

    return 0;
}

#ifdef PI_DEBUG_MODE
// USB Keyboard Debug Loop - for debugging USB keyboard on real hardware
void usb_keyboard_debug_loop(void) {
    printf("[DEBUG] USB Keyboard Debug Loop\n");
    printf("[DEBUG] Keyboard: addr=%d EP=%d MPS=%d\n",
           usb_state.keyboard_addr, usb_state.keyboard_ep,
           usb_state.keyboard_mps);

    if (usb_state.keyboard_addr == 0) {
        printf("[DEBUG] ERROR: No keyboard detected!\n");
        printf("[DEBUG] Hanging...\n");
        while (1) {
            asm volatile("wfi");
        }
    }

    printf("[DEBUG] Press keys - watching for HID reports...\n");
    printf("[DEBUG] Legend: . = poll, [HID] = data received\n\n");

    uint8_t report[8];
    uint8_t prev[8] = {0};
    int loop = 0;

    while (1) {
        int ret = hal_usb_keyboard_poll(report, 8);
        loop++;

        // Print status every 1000 polls
        if (loop % 1000 == 0) {
            printf(".");  // Show we're alive
        }

        // Print detailed stats every 10000 polls
        if (loop % 10000 == 0) {
            printf("\n");
            usb_hid_print_stats();
        }

        if (ret > 0) {
            // Got data! Print hex dump
            printf("\n[HID] Got %d bytes: ", ret);
            for (int i = 0; i < 8; i++) {
                printf("%02x ", report[i]);
            }
            printf("\n");

            // Decode modifiers
            if (report[0]) {
                printf("  Mods: ");
                if (report[0] & 0x22) printf("SHIFT ");
                if (report[0] & 0x11) printf("CTRL ");
                if (report[0] & 0x44) printf("ALT ");
                if (report[0] & 0x88) printf("GUI ");
                printf("\n");
            }

            // Show keycodes
            for (int i = 2; i < 8; i++) {
                if (report[i]) {
                    printf("  Key[%d]: 0x%02x", i-2, report[i]);
                    // Common HID codes
                    if (report[i] >= 0x04 && report[i] <= 0x1D) {
                        printf(" (%c)", 'a' + report[i] - 0x04);
                    } else if (report[i] == 0x28) {
                        printf(" (Enter)");
                    } else if (report[i] == 0x2C) {
                        printf(" (Space)");
                    } else if (report[i] == 0x2A) {
                        printf(" (Backspace)");
                    }
                    printf("\n");
                }
            }
            memcpy(prev, report, 8);
        } else if (ret < 0) {
            printf("\n[USB] Transfer error: ret=%d\n", ret);
        }
        // ret == 0 means no data - normal, don't print

        // Small delay between polls (roughly 10ms)
        for (volatile int i = 0; i < 100000; i++);
    }
}
#endif
