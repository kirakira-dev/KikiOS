/*
 * QEMU virt Block Device HAL
 *
 * Wraps the existing virtio-blk driver to provide the HAL interface.
 */

#include "../hal.h"
#include "../../virtio_blk.h"

int hal_blk_init(void) {
    // virtio_blk is initialized in kernel_main, so nothing to do here
    // The HAL init is called after virtio is already set up
    return 0;
}

int hal_blk_read(uint32_t sector, void *buf, uint32_t count) {
    return virtio_blk_read(sector, count, buf);
}

int hal_blk_write(uint32_t sector, const void *buf, uint32_t count) {
    return virtio_blk_write(sector, count, buf);
}
