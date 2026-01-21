/*
 * KikiOS Virtio Keyboard Driver
 *
 * Implements virtio-input for keyboard input on QEMU virt machine.
 * Virtio MMIO devices start at 0x0a000000 with 0x200 stride.
 */

#include "keyboard.h"
#include "printf.h"
#include "string.h"
#include "hal/hal.h"

// Virtio MMIO registers
#define VIRTIO_MMIO_BASE        0x0a000000
#define VIRTIO_MMIO_STRIDE      0x200

// Virtio MMIO register offsets
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c   // Legacy only
#define VIRTIO_MMIO_QUEUE_PFN       0x040   // Legacy only - page frame number
#define VIRTIO_MMIO_QUEUE_READY     0x044   // Modern only
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
// Modern only registers (version 2):
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4

// Virtio status bits
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

// Virtio device types
#define VIRTIO_DEV_INPUT        18

// Virtio input event types (Linux input event codes)
#define EV_KEY      0x01

// Key states
#define KEY_RELEASED 0
#define KEY_PRESSED  1

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

// Keyboard state
static volatile uint32_t *kbd_base = NULL;
static virtq_desc_t *desc = NULL;
static virtq_avail_t *avail = NULL;
static virtq_used_t *used = NULL;
static virtio_input_event_t *events = NULL;
static uint16_t last_used_idx = 0;
static int kbd_device_index = -1;  // Which virtio device slot (for IRQ calculation)

// Virtio MMIO IRQs start at 48 (SPI 16) on QEMU virt
#define VIRTIO_IRQ_BASE 48

#define QUEUE_SIZE 16
#define DESC_F_WRITE 2

// Key buffer (int to support extended keycodes > 127)
#define KEY_BUF_SIZE 32
static volatile int key_buffer[KEY_BUF_SIZE];
static volatile int key_buf_read = 0;
static volatile int key_buf_write = 0;

// Are we using interrupt-driven mode?
static int irq_mode = 0;

// Legacy virtio queue layout requires specific alignment and contiguous layout
// For queue size N:
//   Descriptor table: N * 16 bytes, 16-byte aligned
//   Available ring: 6 + 2*N bytes, 2-byte aligned
//   Used ring: 6 + 8*N bytes, 4-byte aligned (starts at page boundary for legacy)
// We use 4KB page for simplicity with queue size 16
static uint8_t queue_mem[4096] __attribute__((aligned(4096)));
static virtio_input_event_t event_bufs[QUEUE_SIZE] __attribute__((aligned(16)));

// Scancode to ASCII (simple US layout, lowercase)
// Scancode 1 = ESC (ASCII 27)
static const char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Scancode to ASCII with shift (US layout)
static const char scancode_to_ascii_shift[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Modifier key scancodes
#define KEY_LEFTSHIFT  42
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTCTRL   29
#define KEY_RIGHTCTRL  97

// Arrow key scancodes (Linux input event codes)
#define KEY_UP_ARROW    103
#define KEY_LEFT_ARROW  105
#define KEY_RIGHT_ARROW 106
#define KEY_DOWN_ARROW  108
#define KEY_HOME        102
#define KEY_END         107
#define KEY_DELETE      111
#define KEY_PAGE_UP     104
#define KEY_PAGE_DOWN   109

// Special key codes returned by keyboard_getc() (values >= 128)
#define SPECIAL_KEY_UP     0x100
#define SPECIAL_KEY_DOWN   0x101
#define SPECIAL_KEY_LEFT   0x102
#define SPECIAL_KEY_RIGHT  0x103
#define SPECIAL_KEY_HOME   0x104
#define SPECIAL_KEY_END    0x105
#define SPECIAL_KEY_DELETE 0x106
#define SPECIAL_KEY_PGUP   0x107
#define SPECIAL_KEY_PGDN   0x108
#define SPECIAL_KEY_CTRL   0x109
#define SPECIAL_KEY_SHIFT  0x10A

// Modifier state
static int shift_held = 0;
static int ctrl_held = 0;

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

// Virtio-input config registers (offset from base + 0x100)
#define VIRTIO_INPUT_CFG_SELECT  0x100
#define VIRTIO_INPUT_CFG_SUBSEL  0x101
#define VIRTIO_INPUT_CFG_SIZE    0x102
#define VIRTIO_INPUT_CFG_DATA    0x108

// Config select values
#define VIRTIO_INPUT_CFG_ID_NAME    0x01
#define VIRTIO_INPUT_CFG_ID_DEVIDS  0x03

static volatile uint32_t *find_virtio_input(void) {
    printf("[KBD] Scanning virtio devices...\n");

    // Scan virtio MMIO devices
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);
        volatile uint8_t *base8 = (volatile uint8_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);

        if (magic == 0x74726976 && device_id != 0) {
            printf("[KBD]   Device %d: type=%d", i, device_id);

            if (device_id == VIRTIO_DEV_INPUT) {
                // Query the input device name
                base8[VIRTIO_INPUT_CFG_SELECT] = VIRTIO_INPUT_CFG_ID_NAME;
                base8[VIRTIO_INPUT_CFG_SUBSEL] = 0;
                mb();

                uint8_t size = base8[VIRTIO_INPUT_CFG_SIZE];
                if (size > 0 && size < 64) {
                    printf(" name=\"");
                    for (int j = 0; j < size && j < 32; j++) {
                        char c = base8[VIRTIO_INPUT_CFG_DATA + j];
                        if (c >= 32 && c < 127) printf("%c", c);
                    }
                    printf("\"");
                }
            }
            printf("\n");
        }
    }

    // Now find the keyboard specifically (not tablet!)
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);
        volatile uint8_t *base8 = (volatile uint8_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);

        if (magic == 0x74726976 && device_id == VIRTIO_DEV_INPUT) {
            // Check if this is a keyboard by looking at the name
            base8[VIRTIO_INPUT_CFG_SELECT] = VIRTIO_INPUT_CFG_ID_NAME;
            base8[VIRTIO_INPUT_CFG_SUBSEL] = 0;
            mb();

            // Read full name
            char name[32] = {0};
            uint8_t size = base8[VIRTIO_INPUT_CFG_SIZE];
            for (int j = 0; j < 31 && j < size; j++) {
                name[j] = base8[VIRTIO_INPUT_CFG_DATA + j];
            }

            // Look for "Keyboard" in the name (QEMU Virtio Keyboard)
            // Skip if it's a Tablet
            int is_keyboard = 0;
            for (int j = 0; name[j] && name[j+7]; j++) {
                if (name[j] == 'K' && name[j+1] == 'e' && name[j+2] == 'y' &&
                    name[j+3] == 'b' && name[j+4] == 'o' && name[j+5] == 'a' &&
                    name[j+6] == 'r' && name[j+7] == 'd') {
                    is_keyboard = 1;
                    break;
                }
            }

            if (is_keyboard) {
                printf("[KBD] Selected: %s (device %d)\n", name, i);
                kbd_device_index = i;
                return base;
            }
        }
    }

    // Fallback: return first virtio-input device
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);
        uint32_t magic = read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = read32(base + VIRTIO_MMIO_DEVICE_ID/4);
        if (magic == 0x74726976 && device_id == VIRTIO_DEV_INPUT) {
            kbd_device_index = i;
            return base;
        }
    }

    return NULL;
}

int keyboard_init(void) {
    printf("[KBD] Initializing keyboard...\n");

    kbd_base = find_virtio_input();
    if (!kbd_base) {
        printf("[KBD] No virtio-input device found\n");
        return -1;
    }

    printf("[KBD] Found virtio-input at %p\n", kbd_base);

    // Check virtio version
    uint32_t version = read32(kbd_base + VIRTIO_MMIO_VERSION/4);
    printf("[KBD] Virtio version: %d\n", version);

    // Reset device
    write32(kbd_base + VIRTIO_MMIO_STATUS/4, 0);

    // Wait for reset
    while (read32(kbd_base + VIRTIO_MMIO_STATUS/4) != 0) {
        asm volatile("nop");
    }
    printf("[KBD] Device reset complete\n");

    // Acknowledge
    write32(kbd_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK);

    // Driver loaded
    write32(kbd_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    // Read device features
    uint32_t dev_features = read32(kbd_base + VIRTIO_MMIO_DEVICE_FEATURES/4);
    printf("[KBD] Device features: 0x%x\n", dev_features);

    // Accept no special features for now
    write32(kbd_base + VIRTIO_MMIO_DRIVER_FEATURES/4, 0);

    // Features OK (modern virtio v2)
    write32(kbd_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    // Select queue 0 (eventq)
    write32(kbd_base + VIRTIO_MMIO_QUEUE_SEL/4, 0);

    uint32_t max_queue = read32(kbd_base + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    printf("[KBD] Max queue size: %d\n", max_queue);

    if (max_queue < QUEUE_SIZE) {
        printf("[KBD] Queue too small\n");
        return -1;
    }

    // Set queue size
    write32(kbd_base + VIRTIO_MMIO_QUEUE_NUM/4, QUEUE_SIZE);
    printf("[KBD] Set queue size to %d\n", QUEUE_SIZE);

    // Legacy virtio queue layout (contiguous in memory):
    // Offset 0: Descriptor table (N * 16 bytes)
    // Offset 256 (16*16): Available ring (6 + 2*N bytes)
    // Offset 512 (aligned): Padding
    // Offset 2048 (page aligned): Used ring (6 + 8*N bytes)
    printf("[KBD] Using queue memory at %p\n", queue_mem);

    desc = (virtq_desc_t *)queue_mem;
    // Available ring right after descriptors
    avail = (virtq_avail_t *)(queue_mem + QUEUE_SIZE * sizeof(virtq_desc_t));
    // Used ring at offset that maintains alignment (legacy uses QUEUE_ALIGN)
    // For legacy MMIO, used ring should be at next page-aligned offset
    // With QUEUE_SIZE=16: desc=256 bytes, avail=6+32=38 bytes
    // Used ring starts at aligned offset (typically align to QUEUE_ALIGN which is usually 4096)
    used = (virtq_used_t *)(queue_mem + 2048);
    events = event_bufs;
    printf("[KBD] desc=%p avail=%p used=%p events=%p\n", desc, avail, used, events);

    // For MODERN mode (version 2), use separate address registers
    uint64_t desc_addr = (uint64_t)desc;
    uint64_t avail_addr = (uint64_t)avail;
    uint64_t used_addr = (uint64_t)used;
    printf("[KBD] Setting queue addresses (modern mode)...\n");

    write32(kbd_base + VIRTIO_MMIO_QUEUE_DESC_LOW/4, (uint32_t)desc_addr);
    write32(kbd_base + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, (uint32_t)(desc_addr >> 32));
    write32(kbd_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, (uint32_t)avail_addr);
    write32(kbd_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, (uint32_t)(avail_addr >> 32));
    write32(kbd_base + VIRTIO_MMIO_QUEUE_USED_LOW/4, (uint32_t)used_addr);
    write32(kbd_base + VIRTIO_MMIO_QUEUE_USED_HIGH/4, (uint32_t)(used_addr >> 32));
    printf("[KBD] Queue addresses set\n");

    // Initialize descriptors with buffers for receiving events
    printf("[KBD] Initializing descriptors...\n");
    for (int i = 0; i < QUEUE_SIZE; i++) {
        desc[i].addr = (uint64_t)&events[i];
        desc[i].len = sizeof(virtio_input_event_t);
        desc[i].flags = DESC_F_WRITE;  // Device writes to this buffer
        desc[i].next = 0;
    }
    printf("[KBD] Descriptors initialized\n");

    // Add all descriptors to available ring
    printf("[KBD] Setting avail->flags...\n");
    avail->flags = 0;
    printf("[KBD] avail->flags = 0 done\n");

    printf("[KBD] Filling available ring...\n");
    for (int i = 0; i < QUEUE_SIZE; i++) {
        avail->ring[i] = i;
        printf("[KBD]   ring[%d] = %d\n", i, i);
    }
    printf("[KBD] Ring filled\n");

    avail->idx = QUEUE_SIZE;
    printf("[KBD] Available ring set up\n");

    // Queue ready (modern virtio v2)
    printf("[KBD] Setting queue ready...\n");
    write32(kbd_base + VIRTIO_MMIO_QUEUE_READY/4, 1);

    printf("[KBD] Setting driver OK...\n");
    // Driver OK (modern mode - includes FEATURES_OK)
    write32(kbd_base + VIRTIO_MMIO_STATUS/4,
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    printf("[KBD] Notifying device...\n");
    // Notify device
    write32(kbd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    // Check final status
    uint32_t status = read32(kbd_base + VIRTIO_MMIO_STATUS/4);
    printf("[KBD] Final status: 0x%x\n", status);

    if (status & 0x40) {
        printf("[KBD] ERROR: Device reported failure!\n");
        return -1;
    }

    printf("[KBD] Keyboard initialized!\n");
    return 0;
}

static void process_events(void) {
    if (!kbd_base) return;
    if (!used) return;  // Safety check

    // Check for new events
    mb();  // Ensure we see device updates
    uint16_t current_used = used->idx;
    while (last_used_idx != current_used) {
        uint16_t idx = last_used_idx % QUEUE_SIZE;
        uint32_t desc_idx = used->ring[idx].id;

        virtio_input_event_t *ev = &events[desc_idx];

        // Process key event
        if (ev->type == EV_KEY) {
            uint16_t code = ev->code;

            // Track modifier key state and send as special keys
            if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
                shift_held = (ev->value != KEY_RELEASED);
                // Send shift key event
                if (ev->value == KEY_PRESSED) {
                    int next = (key_buf_write + 1) % KEY_BUF_SIZE;
                    if (next != key_buf_read) {
                        key_buffer[key_buf_write] = SPECIAL_KEY_SHIFT;
                        key_buf_write = next;
                    }
                }
            }
            else if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
                ctrl_held = (ev->value != KEY_RELEASED);
                // Send ctrl key event
                if (ev->value == KEY_PRESSED) {
                    int next = (key_buf_write + 1) % KEY_BUF_SIZE;
                    if (next != key_buf_read) {
                        key_buffer[key_buf_write] = SPECIAL_KEY_CTRL;
                        key_buf_write = next;
                    }
                }
            }
            // Key press
            else if (ev->value == KEY_PRESSED) {
                int key = 0;

                // Check for special keys first
                switch (code) {
                    case KEY_UP_ARROW:    key = SPECIAL_KEY_UP; break;
                    case KEY_DOWN_ARROW:  key = SPECIAL_KEY_DOWN; break;
                    case KEY_LEFT_ARROW:  key = SPECIAL_KEY_LEFT; break;
                    case KEY_RIGHT_ARROW: key = SPECIAL_KEY_RIGHT; break;
                    case KEY_HOME:        key = SPECIAL_KEY_HOME; break;
                    case KEY_END:         key = SPECIAL_KEY_END; break;
                    case KEY_DELETE:      key = SPECIAL_KEY_DELETE; break;
                    case KEY_PAGE_UP:     key = SPECIAL_KEY_PGUP; break;
                    case KEY_PAGE_DOWN:   key = SPECIAL_KEY_PGDN; break;
                    default:
                        // Regular key
                        if (code < 128) {
                            if (shift_held) {
                                key = scancode_to_ascii_shift[code];
                            } else {
                                key = scancode_to_ascii[code];
                            }
                            // Apply Ctrl modifier (Ctrl+A = 1, Ctrl+S = 19, etc.)
                            if (ctrl_held && key >= 'a' && key <= 'z') {
                                key = key - 'a' + 1;
                            } else if (ctrl_held && key >= 'A' && key <= 'Z') {
                                key = key - 'A' + 1;
                            }
                        }
                        break;
                }

                if (key != 0) {
                    // Add to buffer
                    int next = (key_buf_write + 1) % KEY_BUF_SIZE;
                    if (next != key_buf_read) {
                        key_buffer[key_buf_write] = key;
                        key_buf_write = next;
                    }
                }
            }
        }

        // Re-add descriptor to available ring
        uint16_t avail_idx = avail->idx % QUEUE_SIZE;
        avail->ring[avail_idx] = desc_idx;
        avail->idx++;

        last_used_idx++;
    }

    // Notify device we added buffers
    write32(kbd_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, 0);

    // Ack interrupt (in case we use interrupts later)
    write32(kbd_base + VIRTIO_MMIO_INTERRUPT_ACK/4, read32(kbd_base + VIRTIO_MMIO_INTERRUPT_STATUS/4));
}

int keyboard_has_key(void) {
    // If no virtio keyboard, use HAL (USB keyboard on Pi)
    if (!kbd_base) {
        int c = hal_keyboard_getc();
        if (c >= 0) {
            // Put it back in our buffer so keyboard_getc can get it
            int next = (key_buf_write + 1) % KEY_BUF_SIZE;
            if (next != key_buf_read) {
                key_buffer[key_buf_write] = c;
                key_buf_write = next;
            }
        }
        return key_buf_read != key_buf_write;
    }

    process_events();
    return key_buf_read != key_buf_write;
}

int keyboard_getc(void) {
    // If no virtio keyboard, use HAL (USB keyboard on Pi)
    if (!kbd_base) {
        // First check our buffer (from keyboard_has_key)
        if (key_buf_read != key_buf_write) {
            int c = key_buffer[key_buf_read];
            key_buf_read = (key_buf_read + 1) % KEY_BUF_SIZE;
            return c;
        }
        // Then poll HAL directly
        return hal_keyboard_getc();
    }

    process_events();

    if (key_buf_read == key_buf_write) {
        return -1;
    }

    int c = key_buffer[key_buf_read];
    key_buf_read = (key_buf_read + 1) % KEY_BUF_SIZE;
    return c;
}

// Get the keyboard's IRQ number
uint32_t keyboard_get_irq(void) {
    if (kbd_device_index < 0) {
        return 0;  // Not initialized
    }
    return VIRTIO_IRQ_BASE + kbd_device_index;
}

// IRQ handler - called from irq.c
static int irq_count = 0;
void keyboard_irq_handler(void) {
    irq_count++;
    if (irq_count <= 5) {
        printf("[KBD] IRQ! (count=%d)\n", irq_count);
    }
    process_events();
}
