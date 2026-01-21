/*
 * KikiOS Virtio Mouse/Tablet Driver
 *
 * Uses virtio-input (tablet mode) for absolute positioning.
 * QEMU provides virtio-tablet which sends EV_ABS events.
 * Falls back to HAL mouse driver on Pi (USB HID).
 */

#include "mouse.h"
#include "printf.h"
#include "string.h"
#include "hal/hal.h"

// Virtio MMIO registers (same as keyboard)
#define VIRTIO_MMIO_BASE        0x0a000000
#define VIRTIO_MMIO_STRIDE      0x200

#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
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

#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_DEV_INPUT        18

// Linux input event types
#define EV_SYN      0x00
#define EV_KEY      0x01
#define EV_REL      0x02
#define EV_ABS      0x03

// Absolute axis codes
#define ABS_X       0x00
#define ABS_Y       0x01

// Mouse button codes (BTN_LEFT = 0x110, etc)
#define BTN_LEFT    0x110
#define BTN_RIGHT   0x111
#define BTN_MIDDLE  0x112

// Virtio input config
#define VIRTIO_INPUT_CFG_SELECT  0x100
#define VIRTIO_INPUT_CFG_SUBSEL  0x101
#define VIRTIO_INPUT_CFG_SIZE    0x102
#define VIRTIO_INPUT_CFG_DATA    0x108
#define VIRTIO_INPUT_CFG_ID_NAME 0x01

// Virtio input event structure
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;

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

#define QUEUE_SIZE 16
#define DESC_F_WRITE 2

// Virtio MMIO IRQs start at 48 (SPI 16) on QEMU virt
#define VIRTIO_IRQ_BASE 48

// Mouse state
static volatile uint32_t *mouse_base = NULL;
static int mouse_device_index = -1;  // Which virtio device slot (for IRQ calculation)
static virtq_desc_t *desc = NULL;
static virtq_avail_t *avail = NULL;
static virtq_used_t *used = NULL;
static virtio_input_event_t *events = NULL;
static uint16_t last_used_idx = 0;

// Queue memory
static uint8_t queue_mem[4096] __attribute__((aligned(4096)));
static virtio_input_event_t event_bufs[QUEUE_SIZE] __attribute__((aligned(16)));

// Current mouse state
static int mouse_x = 0;        // Raw value (0-32767)
static int mouse_y = 0;
static int mouse_last_x = 0;   // For delta calculation
static int mouse_last_y = 0;
static int mouse_dx = 0;       // Accumulated delta
static int mouse_dy = 0;
static uint8_t mouse_buttons = 0;
static int mouse_event_pending = 0;

// Memory barriers
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

// Find virtio-tablet device (mouse with absolute positioning)
static volatile uint32_t *find_virtio_tablet(void) {
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);
        volatile uint8_t *base8 = (volatile uint8_t *)(uintptr_t)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);

        if (magic != 0x74726976 || device_id != VIRTIO_DEV_INPUT) {
            continue;
        }

        // Check device name for "Tablet"
        base8[VIRTIO_INPUT_CFG_SELECT] = VIRTIO_INPUT_CFG_ID_NAME;
        base8[VIRTIO_INPUT_CFG_SUBSEL] = 0;
        mb();

        uint8_t size = base8[VIRTIO_INPUT_CFG_SIZE];
        char name[32] = {0};
        for (int j = 0; j < 31 && j < size; j++) {
            name[j] = base8[VIRTIO_INPUT_CFG_DATA + j];
        }

        // Look for "Tablet" in name
        if (name[0] == 'Q' && name[5] == 'V' && name[12] == 'T') {
            mouse_device_index = i;
            return base;
        }
    }

    return NULL;
}

int mouse_init(void) {
    printf("[MOUSE] Initializing mouse...\n");

    mouse_base = find_virtio_tablet();
    if (!mouse_base) {
        // No virtio tablet - try HAL (USB mouse on Pi)
        printf("[MOUSE] No virtio tablet, trying HAL...\n");
        return hal_mouse_init();
    }

    // Reset device
    write32(mouse_base + VIRTIO_MMIO_STATUS/4, 0);
    while (read32(mouse_base + VIRTIO_MMIO_STATUS/4) != 0) {
        asm volatile("nop");
    }

    // Acknowledge
    write32(mouse_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK);
    write32(mouse_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    // Accept no special features
    write32(mouse_base + VIRTIO_MMIO_DRIVER_FEATURES/4, 0);
    write32(mouse_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    // Setup queue 0
    write32(mouse_base + VIRTIO_MMIO_QUEUE_SEL/4, 0);

    uint32_t max_queue = read32(mouse_base + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    if (max_queue < QUEUE_SIZE) {
        printf("[MOUSE] Queue too small\n");
        return -1;
    }

    write32(mouse_base + VIRTIO_MMIO_QUEUE_NUM/4, QUEUE_SIZE);

    // Setup queue memory
    desc = (virtq_desc_t *)queue_mem;
    avail = (virtq_avail_t *)(queue_mem + QUEUE_SIZE * sizeof(virtq_desc_t));
    used = (virtq_used_t *)(queue_mem + 2048);
    events = event_bufs;

    // Set queue addresses
    uint64_t desc_addr = (uint64_t)(uintptr_t)desc;
    uint64_t avail_addr = (uint64_t)(uintptr_t)avail;
    uint64_t used_addr = (uint64_t)(uintptr_t)used;

    write32(mouse_base + VIRTIO_MMIO_QUEUE_DESC_LOW/4, (uint32_t)desc_addr);
    write32(mouse_base + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, (uint32_t)(desc_addr >> 32));
    write32(mouse_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, (uint32_t)avail_addr);
    write32(mouse_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, (uint32_t)(avail_addr >> 32));
    write32(mouse_base + VIRTIO_MMIO_QUEUE_USED_LOW/4, (uint32_t)used_addr);
    write32(mouse_base + VIRTIO_MMIO_QUEUE_USED_HIGH/4, (uint32_t)(used_addr >> 32));

    // Initialize descriptors
    for (int i = 0; i < QUEUE_SIZE; i++) {
        desc[i].addr = (uint64_t)(uintptr_t)&events[i];
        desc[i].len = sizeof(virtio_input_event_t);
        desc[i].flags = DESC_F_WRITE;
        desc[i].next = 0;
    }

    // Fill available ring
    avail->flags = 0;
    for (int i = 0; i < QUEUE_SIZE; i++) {
        avail->ring[i] = i;
    }
    avail->idx = QUEUE_SIZE;

    // Queue ready
    write32(mouse_base + VIRTIO_MMIO_QUEUE_READY/4, 1);

    // Driver OK
    write32(mouse_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    // Notify device
    write32(mouse_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    // Check status
    uint32_t status = read32(mouse_base + VIRTIO_MMIO_STATUS/4);
    if (status & 0x40) {
        printf("[MOUSE] Device reported failure!\n");
        return -1;
    }

    // Center mouse initially
    mouse_x = 16384;
    mouse_y = 16384;

    printf("[MOUSE] Mouse initialized!\n");
    return 0;
}

void mouse_poll(void) {
    // Fall back to HAL on Pi (no virtio)
    if (!mouse_base || !used) {
        // HAL handles its own polling
        return;
    }

    mb();
    uint16_t current_used = used->idx;

    while (last_used_idx != current_used) {
        uint16_t idx = last_used_idx % QUEUE_SIZE;
        uint32_t desc_idx = used->ring[idx].id;

        virtio_input_event_t *ev = &events[desc_idx];

        // Process event
        if (ev->type == EV_ABS) {
            if (ev->code == ABS_X) {
                int new_x = ev->value;
                mouse_dx += new_x - mouse_x;
                mouse_x = new_x;
                mouse_event_pending = 1;
            } else if (ev->code == ABS_Y) {
                int new_y = ev->value;
                mouse_dy += new_y - mouse_y;
                mouse_y = new_y;
                mouse_event_pending = 1;
            }
        } else if (ev->type == EV_KEY) {
            // Mouse button events
            int pressed = (ev->value != 0);
            if (ev->code == BTN_LEFT) {
                if (pressed) mouse_buttons |= MOUSE_BTN_LEFT;
                else mouse_buttons &= ~MOUSE_BTN_LEFT;
                mouse_event_pending = 1;
            } else if (ev->code == BTN_RIGHT) {
                if (pressed) mouse_buttons |= MOUSE_BTN_RIGHT;
                else mouse_buttons &= ~MOUSE_BTN_RIGHT;
                mouse_event_pending = 1;
            } else if (ev->code == BTN_MIDDLE) {
                if (pressed) mouse_buttons |= MOUSE_BTN_MIDDLE;
                else mouse_buttons &= ~MOUSE_BTN_MIDDLE;
                mouse_event_pending = 1;
            }
        }

        // Re-add descriptor to available ring
        uint16_t avail_idx = avail->idx % QUEUE_SIZE;
        avail->ring[avail_idx] = desc_idx;
        avail->idx++;

        last_used_idx++;
    }

    // Notify device
    write32(mouse_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);
    write32(mouse_base + VIRTIO_MMIO_INTERRUPT_ACK/4,
            read32(mouse_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));
}

void mouse_get_pos(int *x, int *y) {
    mouse_poll();
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
}

void mouse_get_screen_pos(int *x, int *y) {
    // Fall back to HAL on Pi (no virtio)
    if (!mouse_base) {
        hal_mouse_get_state(x, y, NULL);
        return;
    }

    mouse_poll();

    // Scale from 0-32767 to screen dimensions
    // fb_width and fb_height are global variables from fb.h
    extern uint32_t fb_width, fb_height;

    if (x) *x = (mouse_x * (int)fb_width) / 32768;
    if (y) *y = (mouse_y * (int)fb_height) / 32768;
}

uint8_t mouse_get_buttons(void) {
    // Fall back to HAL on Pi (no virtio)
    if (!mouse_base) {
        int buttons = 0;
        hal_mouse_get_state(NULL, NULL, &buttons);
        return (uint8_t)buttons;
    }

    mouse_poll();
    return mouse_buttons;
}

int mouse_has_event(void) {
    mouse_poll();
    int pending = mouse_event_pending;
    mouse_event_pending = 0;
    return pending;
}

void mouse_set_pos(int x, int y) {
    // Convert screen coordinates to raw 0-32767 range
    extern uint32_t fb_width, fb_height;
    mouse_x = (x * 32768) / (int)fb_width;
    mouse_y = (y * 32768) / (int)fb_height;

    // Clamp to valid range
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > 32767) mouse_x = 32767;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > 32767) mouse_y = 32767;
}

void mouse_get_delta(int *dx, int *dy) {
    // For HAL/Pi path - track position ourselves
    static int hal_last_x = -1;
    static int hal_last_y = -1;

    // Fall back to HAL on Pi (no virtio)
    if (!mouse_base) {
        int hx, hy;
        hal_mouse_get_state(&hx, &hy, NULL);

        if (hal_last_x < 0) {
            // First call - initialize to center
            hal_last_x = 400;
            hal_last_y = 300;
            hal_mouse_set_pos(400, 300);
            if (dx) *dx = 0;
            if (dy) *dy = 0;
            return;
        }

        if (dx) *dx = hx - hal_last_x;
        if (dy) *dy = hy - hal_last_y;

        // Warp back to center for infinite movement
        hal_mouse_set_pos(400, 300);
        hal_last_x = 400;
        hal_last_y = 300;
        return;
    }

    // Virtio path - poll first to get latest
    mouse_poll();

    // Return accumulated deltas (raw, not scaled - let caller handle sensitivity)
    if (dx) *dx = mouse_dx;
    if (dy) *dy = mouse_dy;

    // Clear accumulated deltas
    mouse_dx = 0;
    mouse_dy = 0;
}

// Get the mouse's IRQ number
uint32_t mouse_get_irq(void) {
    if (mouse_device_index < 0) {
        return 0;  // Not initialized
    }
    return VIRTIO_IRQ_BASE + mouse_device_index;
}

// IRQ handler - called from irq.c
void mouse_irq_handler(void) {
    mouse_poll();
}
