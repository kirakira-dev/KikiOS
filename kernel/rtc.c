/*
 * KikiOS RTC Driver
 *
 * PL031 Real Time Clock driver for QEMU virt machine.
 * Address: 0x09010000
 */

#include "rtc.h"
#include "printf.h"

// PL031 RTC registers (QEMU virt machine)
#define RTC_BASE    0x09010000UL
#define RTC_DR      (*(volatile uint32_t *)(RTC_BASE + 0x000))  // Data Register (read = current time)
#define RTC_MR      (*(volatile uint32_t *)(RTC_BASE + 0x004))  // Match Register
#define RTC_LR      (*(volatile uint32_t *)(RTC_BASE + 0x008))  // Load Register (write to set time)
#define RTC_CR      (*(volatile uint32_t *)(RTC_BASE + 0x00C))  // Control Register
#define RTC_IMSC    (*(volatile uint32_t *)(RTC_BASE + 0x010))  // Interrupt Mask
#define RTC_RIS     (*(volatile uint32_t *)(RTC_BASE + 0x014))  // Raw Interrupt Status
#define RTC_MIS     (*(volatile uint32_t *)(RTC_BASE + 0x018))  // Masked Interrupt Status
#define RTC_ICR     (*(volatile uint32_t *)(RTC_BASE + 0x01C))  // Interrupt Clear

// Days in each month (non-leap year)
static const int days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

// Check if year is a leap year
static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Get days in a given month
static int get_days_in_month(int year, int month) {
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days_in_month[month - 1];
}

void rtc_init(void) {
    // PL031 is simple - just enable it
    RTC_CR = 1;  // Enable RTC

    uint32_t ts = RTC_DR;
    printf("[RTC] Initialized, timestamp: %u\n", ts);
}

uint32_t rtc_get_timestamp(void) {
    return RTC_DR;
}

void rtc_timestamp_to_datetime(uint32_t timestamp, datetime_t *dt) {
    // Calculate days since Unix epoch (Jan 1, 1970)
    uint32_t days = timestamp / 86400;
    uint32_t remaining_secs = timestamp % 86400;

    // Time of day
    dt->hour = remaining_secs / 3600;
    dt->minute = (remaining_secs % 3600) / 60;
    dt->second = remaining_secs % 60;

    // Day of week (Jan 1, 1970 was Thursday = 4)
    dt->weekday = (days + 4) % 7;

    // Calculate year (with bounds to prevent infinite loop on corrupted RTC)
    int year = 1970;
    int max_years = 10000;  // Reasonable upper bound (year 11970)
    while (max_years-- > 0) {
        int days_in_year = is_leap_year(year) ? 366 : 365;
        if (days < (uint32_t)days_in_year) break;
        days -= days_in_year;
        year++;
    }
    if (max_years == 0) {
        printf("[RTC] Warning: year calculation overflow\n");
        year = 1970;  // Reset to epoch on overflow
    }
    dt->year = year;

    // Calculate month and day (with bounds)
    int month = 1;
    int max_months = 12;
    while (max_months-- > 0) {
        int dim = get_days_in_month(year, month);
        if (days < (uint32_t)dim) break;
        days -= dim;
        month++;
    }
    if (max_months == 0) {
        printf("[RTC] Warning: month calculation overflow\n");
        month = 1;
        days = 0;
    }
    dt->month = month;
    dt->day = days + 1;  // Days are 1-indexed
}

void rtc_get_datetime(datetime_t *dt) {
    uint32_t ts = rtc_get_timestamp();
    rtc_timestamp_to_datetime(ts, dt);
}
