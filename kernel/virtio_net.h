/*
 * KikiOS Virtio Network Driver
 *
 * Implements virtio-net for network access on QEMU virt machine.
 */

#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include <stdint.h>
#include <stddef.h>

// Maximum ethernet frame size (without virtio header)
#define NET_MTU 1514

// Initialize the virtio-net device
// Returns 0 on success, -1 on error
int virtio_net_init(void);

// Get MAC address (6 bytes)
void virtio_net_get_mac(uint8_t *mac);

// Send a raw ethernet frame
// data: pointer to ethernet frame (dst mac, src mac, ethertype, payload)
// len: length of frame in bytes
// Returns 0 on success, -1 on error
int virtio_net_send(const void *data, uint32_t len);

// Receive a raw ethernet frame (polling)
// buf: buffer to receive into
// maxlen: maximum bytes to receive
// Returns number of bytes received, 0 if no packet, -1 on error
int virtio_net_recv(void *buf, uint32_t maxlen);

// Check if a packet is available
int virtio_net_has_packet(void);

// IRQ handler (called from irq.c)
void virtio_net_irq_handler(void);

// Get the network device's IRQ number
uint32_t virtio_net_get_irq(void);

#endif
