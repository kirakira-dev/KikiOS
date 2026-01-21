/*
 * Raspberry Pi Zero 2W GPIO Driver
 *
 * General-purpose GPIO control for all 54 GPIOs.
 * Supports input, output, alt functions, and pull-up/down configuration.
 */

#include "../hal.h"
#include "../../printf.h"

// ============================================================================
// GPIO Register Definitions
// ============================================================================

#define GPIO_BASE           0x3F200000

// Register offsets
#define GPFSEL0             0x00    // Function select GPIO 0-9
#define GPFSEL1             0x04    // Function select GPIO 10-19
#define GPFSEL2             0x08    // Function select GPIO 20-29
#define GPFSEL3             0x0C    // Function select GPIO 30-39
#define GPFSEL4             0x10    // Function select GPIO 40-49
#define GPFSEL5             0x14    // Function select GPIO 50-53
#define GPSET0              0x1C    // Output set GPIO 0-31
#define GPSET1              0x20    // Output set GPIO 32-53
#define GPCLR0              0x28    // Output clear GPIO 0-31
#define GPCLR1              0x2C    // Output clear GPIO 32-53
#define GPLEV0              0x34    // Pin level GPIO 0-31
#define GPLEV1              0x38    // Pin level GPIO 32-53
#define GPEDS0              0x40    // Event detect status GPIO 0-31
#define GPEDS1              0x44    // Event detect status GPIO 32-53
#define GPREN0              0x4C    // Rising edge detect GPIO 0-31
#define GPREN1              0x50    // Rising edge detect GPIO 32-53
#define GPFEN0              0x58    // Falling edge detect GPIO 0-31
#define GPFEN1              0x5C    // Falling edge detect GPIO 32-53
#define GPPUD               0x94    // Pull-up/down enable
#define GPPUDCLK0           0x98    // Pull-up/down clock GPIO 0-31
#define GPPUDCLK1           0x9C    // Pull-up/down clock GPIO 32-53

// Register access helpers
static inline uint32_t gpio_reg_read(uint32_t offset) {
    return *(volatile uint32_t *)(GPIO_BASE + offset);
}

static inline void gpio_reg_write(uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(GPIO_BASE + offset) = val;
}

// Memory barrier
static inline void gpio_dsb(void) {
    asm volatile("dsb sy" ::: "memory");
}

// Microsecond delay (for pull-up/down timing)
static void gpio_delay_us(uint32_t us) {
    for (uint32_t i = 0; i < us * 300; i++) {
        asm volatile("nop");
    }
}

// ACT LED is on GPIO 29 for Pi Zero 2W
#define ACT_LED_GPIO        29

// ============================================================================
// LED State
// ============================================================================

static int led_state = 0;

// Forward declaration
void led_off(void);

// ============================================================================
// General GPIO API
// ============================================================================

/*
 * Set the function of a GPIO pin.
 * pin: 0-53
 * func: GPIO_INPUT, GPIO_OUTPUT, GPIO_ALT0-5
 */
void gpio_set_function(int pin, int func) {
    if (pin < 0 || pin > 53) return;

    // Each GPFSEL register controls 10 pins, 3 bits each
    int reg_offset = GPFSEL0 + (pin / 10) * 4;
    int bit_offset = (pin % 10) * 3;

    uint32_t val = gpio_reg_read(reg_offset);
    val &= ~(7 << bit_offset);      // Clear 3 bits
    val |= (func << bit_offset);    // Set new function
    gpio_reg_write(reg_offset, val);
    gpio_dsb();
}

/*
 * Get the current function of a GPIO pin.
 */
int gpio_get_function(int pin) {
    if (pin < 0 || pin > 53) return -1;

    int reg_offset = GPFSEL0 + (pin / 10) * 4;
    int bit_offset = (pin % 10) * 3;

    return (gpio_reg_read(reg_offset) >> bit_offset) & 7;
}

/*
 * Set output level of a GPIO pin.
 * high: 1 = high, 0 = low
 */
void gpio_set(int pin, int high) {
    if (pin < 0 || pin > 53) return;

    uint32_t bit = 1 << (pin % 32);
    uint32_t reg = high ? (pin < 32 ? GPSET0 : GPSET1) : (pin < 32 ? GPCLR0 : GPCLR1);
    gpio_reg_write(reg, bit);
    gpio_dsb();
}

/*
 * Read the level of a GPIO pin.
 * Returns: 1 = high, 0 = low
 */
int gpio_get(int pin) {
    if (pin < 0 || pin > 53) return 0;

    uint32_t reg = pin < 32 ? GPLEV0 : GPLEV1;
    uint32_t bit = 1 << (pin % 32);

    return (gpio_reg_read(reg) & bit) ? 1 : 0;
}

/*
 * Set pull-up/down resistor for a GPIO pin.
 * pull: GPIO_PULL_NONE, GPIO_PULL_DOWN, GPIO_PULL_UP
 */
void gpio_set_pull(int pin, int pull) {
    if (pin < 0 || pin > 53) return;

    // BCM2835 pull-up/down sequence:
    // 1. Write to GPPUD to set pull type
    // 2. Wait 150 cycles
    // 3. Write to GPPUDCLKn to clock it into specific pin
    // 4. Wait 150 cycles
    // 5. Clear GPPUD
    // 6. Clear GPPUDCLKn

    gpio_reg_write(GPPUD, pull & 3);
    gpio_delay_us(150);

    uint32_t clk_reg = pin < 32 ? GPPUDCLK0 : GPPUDCLK1;
    uint32_t bit = 1 << (pin % 32);
    gpio_reg_write(clk_reg, bit);
    gpio_delay_us(150);

    gpio_reg_write(GPPUD, 0);
    gpio_reg_write(clk_reg, 0);
}

/*
 * Set pull-up/down for multiple pins at once (more efficient).
 * pins_mask: bitmask of pins (bit 0 = GPIO 0, etc.)
 * bank: 0 for GPIO 0-31, 1 for GPIO 32-53
 * pull: GPIO_PULL_NONE, GPIO_PULL_DOWN, GPIO_PULL_UP
 */
void gpio_set_pull_mask(uint32_t pins_mask, int bank, int pull) {
    gpio_reg_write(GPPUD, pull & 3);
    gpio_delay_us(150);

    uint32_t clk_reg = bank == 0 ? GPPUDCLK0 : GPPUDCLK1;
    gpio_reg_write(clk_reg, pins_mask);
    gpio_delay_us(150);

    gpio_reg_write(GPPUD, 0);
    gpio_reg_write(clk_reg, 0);
}

// ============================================================================
// LED Functions (using general GPIO API)
// ============================================================================

void led_init(void) {
    gpio_set_function(ACT_LED_GPIO, GPIO_OUTPUT);
    led_off();
    printf("[GPIO] ACT LED (GPIO %d) initialized\n", ACT_LED_GPIO);
}

void led_on(void) {
    // GPIO 29: Active low - set pin LOW to turn LED ON
    gpio_set(ACT_LED_GPIO, 0);
    led_state = 1;
}

void led_off(void) {
    // GPIO 29: Active low - set pin HIGH to turn LED OFF
    gpio_set(ACT_LED_GPIO, 1);
    led_state = 0;
}

void led_toggle(void) {
    if (led_state) {
        led_off();
    } else {
        led_on();
    }
}

int led_get_state(void) {
    return led_state;
}

// ============================================================================
// HAL LED Interface
// ============================================================================

void hal_led_init(void) {
    led_init();
}

void hal_led_on(void) {
    led_on();
}

void hal_led_off(void) {
    led_off();
}

void hal_led_toggle(void) {
    led_toggle();
}

int hal_led_status(void) {
    return led_get_state();
}
