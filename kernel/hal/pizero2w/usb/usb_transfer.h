/*
 * USB Transfer Functions
 * Control transfers and DMA handling
 */

#ifndef USB_TRANSFER_H
#define USB_TRANSFER_H

#include <stdint.h>
#include "usb_types.h"

// Wait for DMA transfer to complete
int usb_wait_for_dma_complete(int ch, int max_retries);

// Configure split transactions for FS/LS devices behind HS hubs
void usb_set_split_if_needed(int ch, int dev_addr);

// Control transfer (SETUP + optional DATA + STATUS)
int usb_control_transfer(int device_addr, usb_setup_packet_t *setup,
                         void *data, int data_len, int data_in);

#endif // USB_TRANSFER_H
