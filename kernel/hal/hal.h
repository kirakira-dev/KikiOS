/*
 * KikiOS Hardware Abstraction Layer
 *
 * Common interface for platform-specific hardware.
 * Implementations: qemu/, pizero2w/
 */

#ifndef HAL_H
#define HAL_H

#include <stdint.h>

/*
 * Serial (UART)
 * Used for early boot debug output
 */
void hal_serial_init(void);
void hal_serial_putc(char c);
int hal_serial_getc(void);      // Returns -1 if no data

/*
 * Framebuffer
 * Platform provides a linear framebuffer for graphics
 */
typedef struct {
    uint32_t *base;     // Pointer to pixel memory
    uint32_t width;     // Width in pixels
    uint32_t height;    // Height in pixels
    uint32_t pitch;     // Bytes per row (may include padding)
} hal_fb_info_t;

int hal_fb_init(uint32_t width, uint32_t height);
hal_fb_info_t *hal_fb_get_info(void);
int hal_fb_set_scroll_offset(uint32_t y);  // Hardware scroll (returns 0 if supported)
uint32_t hal_fb_get_virtual_height(void);  // Get total virtual height (for wraparound)

/*
 * Interrupts
 * Platform-specific interrupt controller
 */
void hal_irq_init(void);
void hal_irq_enable(void);
void hal_irq_disable(void);
void hal_irq_enable_irq(uint32_t irq);
void hal_irq_disable_irq(uint32_t irq);
void hal_irq_register_handler(uint32_t irq, void (*handler)(void));

/*
 * Timer
 * ARM Generic Timer is shared, but IRQ routing differs
 */
void hal_timer_init(uint32_t interval_ms);
uint64_t hal_timer_get_ticks(void);
void hal_timer_set_interval(uint32_t interval_ms);

/*
 * Block Device (Storage)
 * Abstract disk access
 */
int hal_blk_init(void);
int hal_blk_read(uint32_t sector, void *buf, uint32_t count);
int hal_blk_write(uint32_t sector, const void *buf, uint32_t count);

/*
 * Input Devices
 * Keyboard and mouse/touch
 */
int hal_keyboard_init(void);
int hal_keyboard_getc(void);    // Returns -1 if no key
uint32_t hal_keyboard_get_irq(void);
void hal_keyboard_irq_handler(void);

int hal_mouse_init(void);
void hal_mouse_get_state(int *x, int *y, int *buttons);
void hal_mouse_set_pos(int x, int y);
uint32_t hal_mouse_get_irq(void);
void hal_mouse_irq_handler(void);

/*
 * Platform Info
 */
const char *hal_platform_name(void);
uint64_t hal_get_ram_size(void);

/*
 * Power Management
 */
void hal_wfi(void);             // Wait for interrupt

/*
 * Microsecond Timer (for timeouts)
 * Returns current time in microseconds from a free-running counter
 * Available very early in boot (before kernel timer is initialized)
 */
uint32_t hal_get_time_us(void);

/*
 * USB (Optional - not all platforms support this)
 * Returns 0 on success, -1 if not supported/failed
 */
int hal_usb_init(void);
int hal_usb_keyboard_poll(uint8_t *report, int report_len);
void hal_usb_keyboard_tick(void);  // Call from timer tick to schedule polls

#ifdef PI_DEBUG_MODE
/*
 * USB Debug Mode
 * Minimal debug loop for USB keyboard troubleshooting
 */
void usb_keyboard_debug_loop(void);
#endif

/*
 * GPIO LED
 * Platform-specific activity LED control
 */
void hal_led_init(void);
void hal_led_on(void);
void hal_led_off(void);
void hal_led_toggle(void);
int hal_led_status(void);

/*
 * GPIO (General Purpose I/O)
 * Direct GPIO pin control (Pi only, QEMU stubs return 0/-1)
 *
 * Function values:
 *   GPIO_INPUT  = 0
 *   GPIO_OUTPUT = 1
 *   GPIO_ALT0   = 4 (I2C, SPI, etc.)
 *   GPIO_ALT1   = 5
 *   GPIO_ALT2   = 6
 *   GPIO_ALT3   = 7 (SD card)
 *   GPIO_ALT4   = 3
 *   GPIO_ALT5   = 2 (UART)
 *
 * Pull values:
 *   GPIO_PULL_NONE = 0
 *   GPIO_PULL_DOWN = 1
 *   GPIO_PULL_UP   = 2
 */
#define GPIO_INPUT      0
#define GPIO_OUTPUT     1
#define GPIO_ALT0       4
#define GPIO_ALT1       5
#define GPIO_ALT2       6
#define GPIO_ALT3       7
#define GPIO_ALT4       3
#define GPIO_ALT5       2

#define GPIO_PULL_NONE  0
#define GPIO_PULL_DOWN  1
#define GPIO_PULL_UP    2

void gpio_set_function(int pin, int func);
int gpio_get_function(int pin);
void gpio_set(int pin, int high);
int gpio_get(int pin);
void gpio_set_pull(int pin, int pull);
void gpio_set_pull_mask(uint32_t pins_mask, int bank, int pull);

/*
 * CPU Info
 * Platform-specific CPU information
 */
const char *hal_get_cpu_name(void);     // e.g., "Cortex-A72"
uint32_t hal_get_cpu_freq_mhz(void);    // e.g., 1500 for 1.5GHz
int hal_get_cpu_cores(void);            // e.g., 4

/*
 * USB Device Info
 * List of enumerated USB devices (Pi only, QEMU returns 0)
 */
int hal_usb_get_device_count(void);
int hal_usb_get_device_info(int idx, uint16_t *vid, uint16_t *pid,
                            char *name, int name_len);

/*
 * DMA (Direct Memory Access)
 * Hardware-accelerated memory transfers (Pi only, QEMU uses CPU fallback)
 */
int hal_dma_init(void);
int hal_dma_available(void);
int hal_dma_copy(void *dst, const void *src, uint32_t len);
int hal_dma_copy_2d(void *dst, uint32_t dst_pitch,
                    const void *src, uint32_t src_pitch,
                    uint32_t width, uint32_t height);
int hal_dma_fb_copy(uint32_t *dst, const uint32_t *src, uint32_t width, uint32_t height);
int hal_dma_fill(void *dst, uint32_t value, uint32_t len);

#endif // HAL_H
