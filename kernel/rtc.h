/*
 * KikiOS RTC Driver
 *
 * PL031 Real Time Clock on QEMU virt machine.
 * Provides wall-clock time from the host system.
 */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

// Date/time structure
typedef struct {
    int year;    // e.g., 2025
    int month;   // 1-12
    int day;     // 1-31
    int hour;    // 0-23
    int minute;  // 0-59
    int second;  // 0-59
    int weekday; // 0-6 (Sunday = 0)
} datetime_t;

// Initialize RTC
void rtc_init(void);

// Get Unix timestamp (seconds since Jan 1, 1970)
uint32_t rtc_get_timestamp(void);

// Convert timestamp to date/time structure
void rtc_timestamp_to_datetime(uint32_t timestamp, datetime_t *dt);

// Get current date/time
void rtc_get_datetime(datetime_t *dt);

#endif
