/*
 * KikiOS Virtio Block Driver
 *
 * Implements virtio-blk for block device access on QEMU virt machine.
 * Based on virtio 1.0 spec (modern mode).
 */

#include "virtio_blk.h"
#include "printf.h"
#include "string.h"

// Virtio MMIO registers
#define VIRTIO_MMIO_BASE        0x0a000000
#define VIRTIO_MMIO_STRIDE      0x200

// Virtio MMIO register offsets
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4
#define VIRTIO_MMIO_CONFIG          0x100

// Virtio status bits
#define VIRTIO_STATUS_ACK         1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8

// Virtio device types
#define VIRTIO_DEV_BLK  2

// Virtio block request types
#define VIRTIO_BLK_T_IN   0  // Read
#define VIRTIO_BLK_T_OUT  1  // Write

// Virtio block status codes
#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

// Virtqueue structures
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

// Virtio block request header
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} virtio_blk_req_t;

// Virtio block config (from device)
typedef struct __attribute__((packed)) {
    uint64_t capacity;  // Number of 512-byte sectors
    uint32_t size_max;
    uint32_t seg_max;
    // ... more fields we don't need
} virtio_blk_config_t;

// Driver state
static volatile uint32_t *blk_base = NULL;
static virtq_desc_t *desc = NULL;
static virtq_avail_t *avail = NULL;
static virtq_used_t *used = NULL;
static uint64_t device_capacity = 0;

#define QUEUE_SIZE 16
#define DESC_F_NEXT  1
#define DESC_F_WRITE 2

// Statically allocated memory for virtqueue (4KB aligned)
static uint8_t queue_mem[4096] __attribute__((aligned(4096)));

// Request header and status buffers
static virtio_blk_req_t req_header __attribute__((aligned(16)));
static uint8_t req_status __attribute__((aligned(16)));

// Memory barriers for device communication
static inline void mb(void) {
    asm volatile("dsb sy" ::: "memory");
}

static inline uint32_t read32(volatile uint32_t *addr) {
    uint32_t val = *addr;
    mb();
    return val;
}

static inline void write32(volatile uint32_t *addr, uint32_t val) {
    mb();
    *addr = val;
    mb();
}

static volatile uint32_t *find_virtio_blk(void) {
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);

        if (magic == 0x74726976 && device_id == VIRTIO_DEV_BLK) {
            return base;
        }
    }

    return NULL;
}

int virtio_blk_init(void) {
    blk_base = find_virtio_blk();
    if (!blk_base) {
        printf("[BLK] No device found\n");
        return -1;
    }

    // Reset device (with timeout to prevent hang)
    write32(blk_base + VIRTIO_MMIO_STATUS/4, 0);
    int timeout = 100000;
    while (read32(blk_base + VIRTIO_MMIO_STATUS/4) != 0 && --timeout > 0) {
        asm volatile("nop");
    }
    if (timeout == 0) {
        printf("[BLK] Device reset timeout\n");
        return -1;
    }

    // Acknowledge and set driver
    write32(blk_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK);
    write32(blk_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    // Accept no special features
    write32(blk_base + VIRTIO_MMIO_DEVICE_FEATURES_SEL/4, 0);
    write32(blk_base + VIRTIO_MMIO_DRIVER_FEATURES_SEL/4, 0);
    write32(blk_base + VIRTIO_MMIO_DRIVER_FEATURES/4, 0);
    write32(blk_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    uint32_t status = read32(blk_base + VIRTIO_MMIO_STATUS/4);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        printf("[BLK] Feature negotiation failed\n");
        return -1;
    }

    // Read device capacity
    volatile uint8_t *config = (volatile uint8_t *)blk_base + VIRTIO_MMIO_CONFIG;
    device_capacity = *(volatile uint64_t *)config;

    // Setup virtqueue 0
    write32(blk_base + VIRTIO_MMIO_QUEUE_SEL/4, 0);
    uint32_t max_queue = read32(blk_base + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    if (max_queue < QUEUE_SIZE) {
        printf("[BLK] Queue too small\n");
        return -1;
    }

    write32(blk_base + VIRTIO_MMIO_QUEUE_NUM/4, QUEUE_SIZE);

    // Setup queue memory
    desc = (virtq_desc_t *)queue_mem;
    avail = (virtq_avail_t *)(queue_mem + QUEUE_SIZE * sizeof(virtq_desc_t));
    used = (virtq_used_t *)(queue_mem + 2048);

    uint64_t desc_addr = (uint64_t)desc;
    uint64_t avail_addr = (uint64_t)avail;
    uint64_t used_addr = (uint64_t)used;

    write32(blk_base + VIRTIO_MMIO_QUEUE_DESC_LOW/4, (uint32_t)desc_addr);
    write32(blk_base + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, (uint32_t)(desc_addr >> 32));
    write32(blk_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, (uint32_t)avail_addr);
    write32(blk_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, (uint32_t)(avail_addr >> 32));
    write32(blk_base + VIRTIO_MMIO_QUEUE_USED_LOW/4, (uint32_t)used_addr);
    write32(blk_base + VIRTIO_MMIO_QUEUE_USED_HIGH/4, (uint32_t)(used_addr >> 32));

    avail->flags = 0;
    avail->idx = 0;

    write32(blk_base + VIRTIO_MMIO_QUEUE_READY/4, 1);
    write32(blk_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    status = read32(blk_base + VIRTIO_MMIO_STATUS/4);
    if (status & 0x40) {
        printf("[BLK] Device failure\n");
        return -1;
    }

    printf("[BLK] Ready (%d MB)\n", (uint32_t)(device_capacity / 2048));
    return 0;
}

// Perform a block request (read or write)
static int do_request(uint32_t type, uint64_t sector, uint32_t count, void *buf) {
    if (!blk_base) return -1;
    if (sector + count > device_capacity) return -1;

    // Setup request header
    req_header.type = type;
    req_header.reserved = 0;
    req_header.sector = sector;

    // Clear status
    req_status = 0xff;

    // Setup descriptor chain:
    // desc[0]: request header (device reads)
    // desc[1]: data buffer (device reads or writes depending on request type)
    // desc[2]: status byte (device writes)

    desc[0].addr = (uint64_t)&req_header;
    desc[0].len = sizeof(virtio_blk_req_t);
    desc[0].flags = DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (uint64_t)buf;
    desc[1].len = count * 512;
    desc[1].flags = DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? DESC_F_WRITE : 0);
    desc[1].next = 2;

    desc[2].addr = (uint64_t)&req_status;
    desc[2].len = 1;
    desc[2].flags = DESC_F_WRITE;
    desc[2].next = 0;

    // Add to available ring
    mb();
    uint16_t old_used_idx = used->idx;
    uint16_t avail_slot = avail->idx % QUEUE_SIZE;
    avail->ring[avail_slot] = 0;  // First descriptor in chain
    mb();
    avail->idx++;
    mb();

    // Notify device
    write32(blk_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    // Poll for completion - wait for used->idx to change
    int timeout = 10000000;
    while (used->idx == old_used_idx && timeout > 0) {
        mb();
        timeout--;
    }

    if (timeout == 0) {
        printf("[BLK] Request timed out!\n");
        return -1;
    }

    // Ack interrupt
    write32(blk_base + VIRTIO_MMIO_INTERRUPT_ACK/4, read32(blk_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));

    // Check status
    if (req_status != VIRTIO_BLK_S_OK) {
        printf("[BLK] Request failed with status %d\n", req_status);
        return -1;
    }

    return 0;
}

int virtio_blk_read(uint64_t sector, uint32_t count, void *buf) {
    return do_request(VIRTIO_BLK_T_IN, sector, count, buf);
}

int virtio_blk_write(uint64_t sector, uint32_t count, const void *buf) {
    return do_request(VIRTIO_BLK_T_OUT, sector, count, (void *)buf);
}

uint64_t virtio_blk_get_capacity(void) {
    return device_capacity;
}
