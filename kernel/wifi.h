/*
 * KikiOS WiFi Interface
 * 
 * Abstract interface for WiFi hardware.
 * Implementation: CYW43438 for Pi Zero 2W, stub for QEMU.
 */

#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stddef.h>

// WiFi security types
#define WIFI_SECURITY_OPEN      0
#define WIFI_SECURITY_WEP       1
#define WIFI_SECURITY_WPA       2
#define WIFI_SECURITY_WPA2      3
#define WIFI_SECURITY_WPA3      4
#define WIFI_SECURITY_UNKNOWN   255

// WiFi connection states
#define WIFI_STATE_DISABLED     0
#define WIFI_STATE_DISCONNECTED 1
#define WIFI_STATE_SCANNING     2
#define WIFI_STATE_CONNECTING   3
#define WIFI_STATE_CONNECTED    4
#define WIFI_STATE_ERROR        5

// Maximum SSID length (including null terminator)
#define WIFI_SSID_MAX_LEN       33

// Maximum password length
#define WIFI_PASS_MAX_LEN       65

// Maximum number of scan results
#define WIFI_MAX_SCAN_RESULTS   20

// WiFi network info (for scan results)
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];   // Network name
    uint8_t bssid[6];                // MAC address of AP
    int8_t rssi;                     // Signal strength (dBm, negative)
    uint8_t channel;                 // WiFi channel (1-14)
    uint8_t security;                // Security type
} wifi_network_t;

// WiFi connection info
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];   // Connected network name
    uint8_t bssid[6];                // Connected AP MAC
    int8_t rssi;                     // Current signal strength
    uint8_t channel;                 // Current channel
    uint32_t ip_addr;                // Assigned IP address
    uint32_t gateway;                // Gateway IP
    uint32_t netmask;                // Network mask
    uint32_t dns;                    // DNS server
} wifi_connection_t;

/*
 * Core WiFi functions
 */

// Initialize WiFi hardware
// Returns 0 on success, -1 if WiFi not available
int wifi_init(void);

// Check if WiFi hardware is available
int wifi_available(void);

// Get current WiFi state
int wifi_get_state(void);

// Enable WiFi radio
int wifi_enable(void);

// Disable WiFi radio
int wifi_disable(void);

/*
 * Scanning
 */

// Start scanning for networks (async)
// Returns 0 on success, -1 on error
int wifi_scan_start(void);

// Check if scan is complete
// Returns 1 if complete, 0 if in progress, -1 on error
int wifi_scan_complete(void);

// Get scan results
// Returns number of networks found, -1 on error
int wifi_scan_get_results(wifi_network_t *results, int max_results);

/*
 * Connection
 */

// Connect to a network
// Returns 0 if connection started, -1 on error
int wifi_connect(const char *ssid, const char *password);

// Disconnect from current network
int wifi_disconnect(void);

// Get connection info
// Returns 0 on success, -1 if not connected
int wifi_get_connection(wifi_connection_t *info);

// Get MAC address
void wifi_get_mac(uint8_t *mac);

/*
 * Network integration
 * These functions integrate WiFi with the existing network stack
 */

// Send raw ethernet frame over WiFi
int wifi_send(const void *data, uint32_t len);

// Receive raw ethernet frame from WiFi
// Returns length of received frame, 0 if none, -1 on error
int wifi_recv(void *buf, uint32_t maxlen);

// Check if packets are available
int wifi_has_packet(void);

// Poll WiFi (call periodically from main loop)
void wifi_poll(void);

#endif // WIFI_H
