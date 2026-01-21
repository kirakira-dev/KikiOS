/*
 * KikiOS WiFi Driver
 * 
 * Platform-independent WiFi management.
 * Uses HAL for hardware-specific operations.
 */

#include "wifi.h"
#include "printf.h"
#include "string.h"
#include "memory.h"

// External HAL functions (implemented per-platform)
extern int hal_wifi_init(void);
extern int hal_wifi_available(void);
extern int hal_wifi_enable(void);
extern int hal_wifi_disable(void);
extern int hal_wifi_scan_start(void);
extern int hal_wifi_scan_complete(void);
extern int hal_wifi_scan_get_results(wifi_network_t *results, int max);
extern int hal_wifi_connect(const char *ssid, const char *pass);
extern int hal_wifi_disconnect(void);
extern int hal_wifi_get_state(void);
extern int hal_wifi_get_connection(wifi_connection_t *info);
extern void hal_wifi_get_mac(uint8_t *mac);
extern int hal_wifi_send(const void *data, uint32_t len);
extern int hal_wifi_recv(void *buf, uint32_t maxlen);
extern int hal_wifi_has_packet(void);
extern void hal_wifi_poll(void);

// WiFi state
static int wifi_initialized = 0;

int wifi_init(void) {
    if (wifi_initialized) {
        return 0;
    }
    
    printf("[WiFi] Initializing...\n");
    
    if (hal_wifi_init() != 0) {
        printf("[WiFi] Hardware initialization failed\n");
        return -1;
    }
    
    wifi_initialized = 1;
    printf("[WiFi] Initialized successfully\n");
    return 0;
}

int wifi_available(void) {
    return hal_wifi_available();
}

int wifi_get_state(void) {
    if (!wifi_initialized) {
        return WIFI_STATE_DISABLED;
    }
    return hal_wifi_get_state();
}

int wifi_enable(void) {
    if (!wifi_initialized) {
        if (wifi_init() != 0) {
            return -1;
        }
    }
    return hal_wifi_enable();
}

int wifi_disable(void) {
    if (!wifi_initialized) {
        return 0;
    }
    return hal_wifi_disable();
}

int wifi_scan_start(void) {
    if (!wifi_initialized) {
        return -1;
    }
    printf("[WiFi] Starting scan...\n");
    return hal_wifi_scan_start();
}

int wifi_scan_complete(void) {
    if (!wifi_initialized) {
        return -1;
    }
    return hal_wifi_scan_complete();
}

int wifi_scan_get_results(wifi_network_t *results, int max_results) {
    if (!wifi_initialized || !results || max_results <= 0) {
        return -1;
    }
    return hal_wifi_scan_get_results(results, max_results);
}

int wifi_connect(const char *ssid, const char *password) {
    if (!wifi_initialized || !ssid) {
        return -1;
    }
    
    printf("[WiFi] Connecting to '%s'...\n", ssid);
    return hal_wifi_connect(ssid, password);
}

int wifi_disconnect(void) {
    if (!wifi_initialized) {
        return -1;
    }
    
    printf("[WiFi] Disconnecting...\n");
    return hal_wifi_disconnect();
}

int wifi_get_connection(wifi_connection_t *info) {
    if (!wifi_initialized || !info) {
        return -1;
    }
    return hal_wifi_get_connection(info);
}

void wifi_get_mac(uint8_t *mac) {
    if (mac) {
        hal_wifi_get_mac(mac);
    }
}

int wifi_send(const void *data, uint32_t len) {
    if (!wifi_initialized) {
        return -1;
    }
    return hal_wifi_send(data, len);
}

int wifi_recv(void *buf, uint32_t maxlen) {
    if (!wifi_initialized) {
        return -1;
    }
    return hal_wifi_recv(buf, maxlen);
}

int wifi_has_packet(void) {
    if (!wifi_initialized) {
        return 0;
    }
    return hal_wifi_has_packet();
}

void wifi_poll(void) {
    if (wifi_initialized) {
        hal_wifi_poll();
    }
}

// Helper: Convert security type to string
const char *wifi_security_str(uint8_t security) {
    switch (security) {
        case WIFI_SECURITY_OPEN:    return "Open";
        case WIFI_SECURITY_WEP:     return "WEP";
        case WIFI_SECURITY_WPA:     return "WPA";
        case WIFI_SECURITY_WPA2:    return "WPA2";
        case WIFI_SECURITY_WPA3:    return "WPA3";
        default:                    return "Unknown";
    }
}

// Helper: Convert RSSI to signal quality percentage
int wifi_rssi_to_quality(int8_t rssi) {
    // RSSI typically ranges from -100 (weak) to -30 (strong)
    if (rssi >= -30) return 100;
    if (rssi <= -100) return 0;
    // Linear interpolation
    return (int)((rssi + 100) * 100 / 70);
}
