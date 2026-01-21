/*
 * KikiOS Virtio Block Driver
 *
 * Implements virtio-blk for block device access on QEMU virt machine.
 */

#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include <stdint.h>
#include <stddef.h>

// Initialize the virtio-blk device
int virtio_blk_init(void);

// Read sectors from the block device
// sector: starting sector number (512 bytes each)
// count: number of sectors to read
// buf: buffer to read into (must be at least count * 512 bytes)
// Returns 0 on success, -1 on error
int virtio_blk_read(uint64_t sector, uint32_t count, void *buf);

// Write sectors to the block device
// sector: starting sector number (512 bytes each)
// count: number of sectors to write
// buf: buffer to write from (must be at least count * 512 bytes)
// Returns 0 on success, -1 on error
int virtio_blk_write(uint64_t sector, uint32_t count, const void *buf);

// Get the total number of sectors on the device
uint64_t virtio_blk_get_capacity(void);

#endif
