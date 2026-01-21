/*
 * WiFi HAL Stub for QEMU
 * 
 * QEMU doesn't have WiFi - it uses VirtIO-net for networking.
 * This stub returns "WiFi not available".
 */

#include "../../wifi.h"
#include <stddef.h>

// QEMU doesn't have WiFi hardware
int hal_wifi_init(void) {
    return -1;  // Not available
}

int hal_wifi_available(void) {
    return 0;  // Not available
}

int hal_wifi_enable(void) {
    return -1;
}

int hal_wifi_disable(void) {
    return -1;
}

int hal_wifi_scan_start(void) {
    return -1;
}

int hal_wifi_scan_complete(void) {
    return -1;
}

int hal_wifi_scan_get_results(wifi_network_t *results, int max) {
    (void)results;
    (void)max;
    return -1;
}

int hal_wifi_connect(const char *ssid, const char *pass) {
    (void)ssid;
    (void)pass;
    return -1;
}

int hal_wifi_disconnect(void) {
    return -1;
}

int hal_wifi_get_state(void) {
    return WIFI_STATE_DISABLED;
}

int hal_wifi_get_connection(wifi_connection_t *info) {
    (void)info;
    return -1;
}

void hal_wifi_get_mac(uint8_t *mac) {
    if (mac) {
        // Return empty MAC
        for (int i = 0; i < 6; i++) {
            mac[i] = 0;
        }
    }
}

int hal_wifi_send(const void *data, uint32_t len) {
    (void)data;
    (void)len;
    return -1;
}

int hal_wifi_recv(void *buf, uint32_t maxlen) {
    (void)buf;
    (void)maxlen;
    return -1;
}

int hal_wifi_has_packet(void) {
    return 0;
}

void hal_wifi_poll(void) {
    // Nothing to do
}
