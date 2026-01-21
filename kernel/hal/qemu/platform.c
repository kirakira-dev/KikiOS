/*
 * QEMU virt machine Platform Info
 */

#include "../hal.h"

const char *hal_platform_name(void) {
    return "QEMU virt (aarch64)";
}

void hal_wfi(void) {
    asm volatile("wfi");
}

// Microsecond timer using ARM generic timer
uint32_t hal_get_time_us(void) {
    uint64_t cnt, freq;
    asm volatile("mrs %0, cntpct_el0" : "=r"(cnt));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    // Convert to microseconds: cnt * 1000000 / freq
    // To avoid overflow, divide first (loses some precision but ok for timeouts)
    return (uint32_t)((cnt * 1000000ULL) / freq);
}

// QEMU uses virtio for input, not USB
int hal_usb_init(void) {
    return -1;  // Not supported on QEMU virt
}

int hal_usb_keyboard_poll(uint8_t *report, int report_len) {
    (void)report;
    (void)report_len;
    return -1;
}

void hal_usb_keyboard_tick(void) {
    // Not supported on QEMU
}

// QEMU has no physical LED
void hal_led_init(void) {
    // No LED on QEMU virt
}

void hal_led_on(void) {
    // No LED on QEMU virt
}

void hal_led_off(void) {
    // No LED on QEMU virt
}

void hal_led_toggle(void) {
    // No LED on QEMU virt
}

int hal_led_status(void) {
    return 0;  // No LED on QEMU
}

// GPIO stubs - QEMU virt has no GPIO
void gpio_set_function(int pin, int func) {
    (void)pin;
    (void)func;
}

int gpio_get_function(int pin) {
    (void)pin;
    return -1;
}

void gpio_set(int pin, int high) {
    (void)pin;
    (void)high;
}

int gpio_get(int pin) {
    (void)pin;
    return 0;
}

void gpio_set_pull(int pin, int pull) {
    (void)pin;
    (void)pull;
}

void gpio_set_pull_mask(uint32_t pins_mask, int bank, int pull) {
    (void)pins_mask;
    (void)bank;
    (void)pull;
}

// CPU Info
const char *hal_get_cpu_name(void) {
    return "Cortex-A72";
}

uint32_t hal_get_cpu_freq_mhz(void) {
    return 1500;  // QEMU virt default
}

int hal_get_cpu_cores(void) {
    return 1;  // Single core in QEMU by default
}

// USB Device List - QEMU uses virtio, no USB
int hal_usb_get_device_count(void) {
    return 0;
}

int hal_usb_get_device_info(int idx, uint16_t *vid, uint16_t *pid,
                            char *name, int name_len) {
    (void)idx;
    (void)vid;
    (void)pid;
    (void)name;
    (void)name_len;
    return -1;
}

// Mouse - QEMU uses virtio-tablet, these are stubs
int hal_mouse_init(void) {
    return 0;  // Not needed, virtio handles it
}

void hal_mouse_get_state(int *x, int *y, int *buttons) {
    // Never called - virtio mouse handles it
    if (x) *x = 0;
    if (y) *y = 0;
    if (buttons) *buttons = 0;
}

void hal_mouse_set_pos(int x, int y) {
    // Not used on QEMU - virtio handles it
    (void)x;
    (void)y;
}

uint32_t hal_mouse_get_irq(void) {
    return 0;
}

void hal_mouse_irq_handler(void) {
    // Not used on QEMU
}
