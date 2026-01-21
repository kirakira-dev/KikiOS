/*
 * KikiOS Virtio Network Driver
 *
 * Implements virtio-net for network access on QEMU virt machine.
 * Based on virtio 1.0 spec (modern mode).
 */

#include "virtio_net.h"
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
#define VIRTIO_DEV_NET  1

// Virtio net feature bits
#define VIRTIO_NET_F_MAC        (1 << 5)   // Device has given MAC address

// Virtio net header (prepended to every packet)
typedef struct __attribute__((packed)) {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    // uint16_t num_buffers;  // Only present if VIRTIO_NET_F_MRG_RXBUF
} virtio_net_hdr_t;

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

// Driver state
static volatile uint32_t *net_base = NULL;
static int net_device_index = -1;

// MAC address
static uint8_t mac_addr[6];

// Receive queue (queue 0)
static virtq_desc_t *rx_desc = NULL;
static virtq_avail_t *rx_avail = NULL;
static virtq_used_t *rx_used = NULL;
static uint16_t rx_last_used_idx = 0;

// Transmit queue (queue 1)
static virtq_desc_t *tx_desc = NULL;
static virtq_avail_t *tx_avail = NULL;
static virtq_used_t *tx_used = NULL;

#define QUEUE_SIZE 16
#define DESC_F_NEXT  1
#define DESC_F_WRITE 2

// Virtio IRQ base (same as other virtio devices)
#define VIRTIO_IRQ_BASE 48

// Queue memory (4KB aligned)
static uint8_t rx_queue_mem[4096] __attribute__((aligned(4096)));
static uint8_t tx_queue_mem[4096] __attribute__((aligned(4096)));

// Receive buffers: virtio header + ethernet frame
typedef struct __attribute__((aligned(16))) {
    virtio_net_hdr_t hdr;
    uint8_t data[NET_MTU];
} rx_buffer_t;

static rx_buffer_t rx_buffers[QUEUE_SIZE] __attribute__((aligned(16)));

// Transmit buffer (single, reused)
typedef struct __attribute__((aligned(16))) {
    virtio_net_hdr_t hdr;
    uint8_t data[NET_MTU];
} tx_buffer_t;

static tx_buffer_t tx_buffer __attribute__((aligned(16)));

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

// Find virtio-net device
static volatile uint32_t *find_virtio_net(void) {
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);

        if (magic == 0x74726976 && device_id == VIRTIO_DEV_NET) {
            net_device_index = i;
            return base;
        }
    }
    return NULL;
}

// Setup a virtqueue
static int setup_queue(int queue_idx, uint8_t *queue_mem,
                       virtq_desc_t **desc_out, virtq_avail_t **avail_out, virtq_used_t **used_out) {
    write32(net_base + VIRTIO_MMIO_QUEUE_SEL/4, queue_idx);

    uint32_t max_queue = read32(net_base + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    if (max_queue < QUEUE_SIZE) {
        printf("[NET] Queue %d too small (max=%d)\n", queue_idx, max_queue);
        return -1;
    }

    write32(net_base + VIRTIO_MMIO_QUEUE_NUM/4, QUEUE_SIZE);

    // Setup queue memory layout
    *desc_out = (virtq_desc_t *)queue_mem;
    *avail_out = (virtq_avail_t *)(queue_mem + QUEUE_SIZE * sizeof(virtq_desc_t));
    *used_out = (virtq_used_t *)(queue_mem + 2048);

    uint64_t desc_addr = (uint64_t)*desc_out;
    uint64_t avail_addr = (uint64_t)*avail_out;
    uint64_t used_addr = (uint64_t)*used_out;

    write32(net_base + VIRTIO_MMIO_QUEUE_DESC_LOW/4, (uint32_t)desc_addr);
    write32(net_base + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, (uint32_t)(desc_addr >> 32));
    write32(net_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, (uint32_t)avail_addr);
    write32(net_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, (uint32_t)(avail_addr >> 32));
    write32(net_base + VIRTIO_MMIO_QUEUE_USED_LOW/4, (uint32_t)used_addr);
    write32(net_base + VIRTIO_MMIO_QUEUE_USED_HIGH/4, (uint32_t)(used_addr >> 32));

    (*avail_out)->flags = 0;
    (*avail_out)->idx = 0;

    write32(net_base + VIRTIO_MMIO_QUEUE_READY/4, 1);

    return 0;
}

int virtio_net_init(void) {
    net_base = find_virtio_net();
    if (!net_base) {
        printf("[NET] No virtio-net device found\n");
        return -1;
    }

    printf("[NET] Found virtio-net at device slot %d\n", net_device_index);

    // Reset device (with timeout to prevent hang)
    write32(net_base + VIRTIO_MMIO_STATUS/4, 0);
    int timeout = 100000;
    while (read32(net_base + VIRTIO_MMIO_STATUS/4) != 0 && --timeout > 0) {
        asm volatile("nop");
    }
    if (timeout == 0) {
        printf("[NET] Device reset timeout\n");
        return -1;
    }

    // Acknowledge and set driver
    write32(net_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK);
    write32(net_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    // Read device features
    write32(net_base + VIRTIO_MMIO_DEVICE_FEATURES_SEL/4, 0);
    uint32_t features = read32(net_base + VIRTIO_MMIO_DEVICE_FEATURES/4);
    printf("[NET] Device features: 0x%x\n", features);

    // Accept MAC feature only (don't need checksums, GSO, etc)
    write32(net_base + VIRTIO_MMIO_DRIVER_FEATURES_SEL/4, 0);
    write32(net_base + VIRTIO_MMIO_DRIVER_FEATURES/4, VIRTIO_NET_F_MAC);

    write32(net_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    uint32_t status = read32(net_base + VIRTIO_MMIO_STATUS/4);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        printf("[NET] Feature negotiation failed\n");
        return -1;
    }

    // Read MAC address from config space
    volatile uint8_t *config = (volatile uint8_t *)net_base + VIRTIO_MMIO_CONFIG;
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = config[i];
    }
    printf("[NET] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);

    // Setup receive queue (queue 0)
    if (setup_queue(0, rx_queue_mem, &rx_desc, &rx_avail, &rx_used) < 0) {
        return -1;
    }

    // Setup transmit queue (queue 1)
    if (setup_queue(1, tx_queue_mem, &tx_desc, &tx_avail, &tx_used) < 0) {
        return -1;
    }

    // Pre-populate receive queue with buffers
    for (int i = 0; i < QUEUE_SIZE; i++) {
        rx_desc[i].addr = (uint64_t)&rx_buffers[i];
        rx_desc[i].len = sizeof(rx_buffer_t);
        rx_desc[i].flags = DESC_F_WRITE;  // Device writes to this buffer
        rx_desc[i].next = 0;

        rx_avail->ring[i] = i;
    }
    rx_avail->idx = QUEUE_SIZE;
    mb();

    // Set driver OK
    write32(net_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
            VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    status = read32(net_base + VIRTIO_MMIO_STATUS/4);
    if (status & 0x40) {
        printf("[NET] Device reported failure\n");
        return -1;
    }

    // Notify device that receive buffers are available
    write32(net_base + VIRTIO_MMIO_QUEUE_SEL/4, 0);
    write32(net_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    printf("[NET] Ready\n");
    return 0;
}

void virtio_net_get_mac(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = mac_addr[i];
    }
}

int virtio_net_send(const void *data, uint32_t len) {
    if (!net_base) return -1;
    if (len > NET_MTU) return -1;

    // Prepare virtio header (all zeros - no offloads)
    memset(&tx_buffer.hdr, 0, sizeof(virtio_net_hdr_t));

    // Copy data
    memcpy(tx_buffer.data, data, len);

    // Setup descriptor chain: header + data in one buffer
    tx_desc[0].addr = (uint64_t)&tx_buffer;
    tx_desc[0].len = sizeof(virtio_net_hdr_t) + len;
    tx_desc[0].flags = 0;  // Device reads from this buffer
    tx_desc[0].next = 0;

    // Add to available ring
    mb();
    uint16_t avail_idx = tx_avail->idx % QUEUE_SIZE;
    tx_avail->ring[avail_idx] = 0;
    mb();
    tx_avail->idx++;
    mb();

    // Save used index for polling
    uint16_t old_used = tx_used->idx;

    // Notify device (select queue 1 first)
    write32(net_base + VIRTIO_MMIO_QUEUE_SEL/4, 1);
    write32(net_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 1);

    // Poll for completion
    int timeout = 1000000;
    while (tx_used->idx == old_used && timeout > 0) {
        mb();
        timeout--;
    }

    if (timeout == 0) {
        printf("[NET] TX timeout\n");
        return -1;
    }

    // Ack interrupt
    write32(net_base + VIRTIO_MMIO_INTERRUPT_ACK/4,
            read32(net_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));

    return 0;
}

int virtio_net_has_packet(void) {
    if (!net_base) return 0;
    mb();
    return rx_used->idx != rx_last_used_idx;
}

int virtio_net_recv(void *buf, uint32_t maxlen) {
    if (!net_base) return -1;

    mb();
    if (rx_used->idx == rx_last_used_idx) {
        return 0;  // No packet
    }

    // Get the completed descriptor
    uint16_t used_idx = rx_last_used_idx % QUEUE_SIZE;
    uint32_t desc_idx = rx_used->ring[used_idx].id;
    uint32_t total_len = rx_used->ring[used_idx].len;

    rx_last_used_idx++;

    // Skip virtio header, copy ethernet frame
    rx_buffer_t *rxbuf = &rx_buffers[desc_idx];
    uint32_t frame_len = total_len - sizeof(virtio_net_hdr_t);

    if (frame_len > maxlen) {
        frame_len = maxlen;
    }

    memcpy(buf, rxbuf->data, frame_len);

    // Re-add buffer to available ring
    uint16_t avail_idx = rx_avail->idx % QUEUE_SIZE;
    rx_avail->ring[avail_idx] = desc_idx;
    mb();
    rx_avail->idx++;
    mb();

    // Notify device
    write32(net_base + VIRTIO_MMIO_QUEUE_SEL/4, 0);
    write32(net_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    // Ack interrupt
    write32(net_base + VIRTIO_MMIO_INTERRUPT_ACK/4,
            read32(net_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));

    return frame_len;
}

uint32_t virtio_net_get_irq(void) {
    if (net_device_index < 0) return 0;
    return VIRTIO_IRQ_BASE + net_device_index;
}

void virtio_net_irq_handler(void) {
    if (!net_base) return;

    // Just ack the interrupt - don't consume packets here!
    // Packets will be processed by net_poll() in the network stack.
    write32(net_base + VIRTIO_MMIO_INTERRUPT_ACK/4,
            read32(net_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));
}
