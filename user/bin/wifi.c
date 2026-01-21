/*
 * KikiOS WiFi Management Tool
 * 
 * Commands:
 *   wifi status      - Show WiFi status
 *   wifi scan        - Scan for networks
 *   wifi connect <ssid> [password] - Connect to network
 *   wifi disconnect  - Disconnect from network
 *   wifi enable      - Enable WiFi radio
 *   wifi disable     - Disable WiFi radio
 */

#include "kiki.h"

static kapi_t *api;

// Print a string
static void print(const char *s) {
    while (*s) {
        api->putc(*s++);
    }
}

// Print integer
static void print_int(int n) {
    if (n < 0) {
        api->putc('-');
        n = -n;
    }
    if (n == 0) {
        api->putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        api->putc(buf[--i]);
    }
}

// Print IP address
static void print_ip(uint32_t ip) {
    print_int((ip >> 24) & 0xFF);
    api->putc('.');
    print_int((ip >> 16) & 0xFF);
    api->putc('.');
    print_int((ip >> 8) & 0xFF);
    api->putc('.');
    print_int(ip & 0xFF);
}

// Print MAC address
static void print_mac(const uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        if (i > 0) api->putc(':');
        uint8_t b = mac[i];
        char hex[] = "0123456789ABCDEF";
        api->putc(hex[(b >> 4) & 0xF]);
        api->putc(hex[b & 0xF]);
    }
}

// Get state name
static const char *state_name(int state) {
    switch (state) {
        case WIFI_STATE_DISABLED:     return "Disabled";
        case WIFI_STATE_DISCONNECTED: return "Disconnected";
        case WIFI_STATE_SCANNING:     return "Scanning";
        case WIFI_STATE_CONNECTING:   return "Connecting";
        case WIFI_STATE_CONNECTED:    return "Connected";
        case WIFI_STATE_ERROR:        return "Error";
        default:                      return "Unknown";
    }
}

// Get security name
static const char *security_name(uint8_t sec) {
    switch (sec) {
        case WIFI_SECURITY_OPEN:  return "Open";
        case WIFI_SECURITY_WEP:   return "WEP";
        case WIFI_SECURITY_WPA:   return "WPA";
        case WIFI_SECURITY_WPA2:  return "WPA2";
        case WIFI_SECURITY_WPA3:  return "WPA3";
        default:                  return "Unknown";
    }
}

// Signal quality bars
static void print_signal(int8_t rssi) {
    // RSSI typically -100 (weak) to -30 (strong)
    int bars;
    if (rssi >= -50) bars = 4;
    else if (rssi >= -60) bars = 3;
    else if (rssi >= -70) bars = 2;
    else if (rssi >= -80) bars = 1;
    else bars = 0;
    
    for (int i = 0; i < 4; i++) {
        api->putc(i < bars ? '#' : '-');
    }
    print(" (");
    print_int(rssi);
    print(" dBm)");
}

static void show_status(void) {
    if (!api->wifi_available()) {
        print("WiFi: Not available (using Ethernet)\n");
        return;
    }
    
    int state = api->wifi_get_state();
    print("WiFi Status: ");
    print(state_name(state));
    print("\n");
    
    uint8_t mac[6];
    api->wifi_get_mac(mac);
    print("MAC Address: ");
    print_mac(mac);
    print("\n");
    
    if (state == WIFI_STATE_CONNECTED) {
        wifi_conn_info_t info;
        if (api->wifi_get_connection(&info) == 0) {
            print("\nConnected Network:\n");
            print("  SSID:    ");
            print(info.ssid);
            print("\n");
            print("  BSSID:   ");
            print_mac(info.bssid);
            print("\n");
            print("  Channel: ");
            print_int(info.channel);
            print("\n");
            print("  Signal:  ");
            print_signal(info.rssi);
            print("\n");
            print("\nNetwork Configuration:\n");
            print("  IP:      ");
            print_ip(info.ip_addr);
            print("\n");
            print("  Gateway: ");
            print_ip(info.gateway);
            print("\n");
            print("  Netmask: ");
            print_ip(info.netmask);
            print("\n");
            print("  DNS:     ");
            print_ip(info.dns);
            print("\n");
        }
    }
}

static void do_scan(void) {
    if (!api->wifi_available()) {
        print("Error: WiFi not available\n");
        return;
    }
    
    print("Scanning for WiFi networks...\n\n");
    
    if (api->wifi_scan_start() != 0) {
        print("Error: Failed to start scan\n");
        return;
    }
    
    // Wait for scan to complete
    int timeout = 100;  // 10 seconds
    while (!api->wifi_scan_complete() && timeout > 0) {
        api->sleep_ms(100);
        timeout--;
    }
    
    if (timeout == 0) {
        print("Error: Scan timed out\n");
        return;
    }
    
    // Get results
    wifi_network_info_t networks[20];
    int count = api->wifi_scan_results(networks, 20);
    
    if (count < 0) {
        print("Error: Failed to get scan results\n");
        return;
    }
    
    if (count == 0) {
        print("No networks found\n");
        return;
    }
    
    print("Found ");
    print_int(count);
    print(" network(s):\n\n");
    
    print("SSID                              CH   SIGNAL      SECURITY\n");
    print("--------------------------------------------------------------\n");
    
    for (int i = 0; i < count; i++) {
        // SSID (padded to 32 chars)
        int len = strlen(networks[i].ssid);
        print(networks[i].ssid);
        for (int j = len; j < 34; j++) api->putc(' ');
        
        // Channel
        if (networks[i].channel < 10) api->putc(' ');
        print_int(networks[i].channel);
        print("   ");
        
        // Signal
        print_signal(networks[i].rssi);
        print("  ");
        
        // Security
        print(security_name(networks[i].security));
        print("\n");
    }
}

static void do_connect(const char *ssid, const char *password) {
    if (!api->wifi_available()) {
        print("Error: WiFi not available\n");
        return;
    }
    
    print("Connecting to '");
    print(ssid);
    print("'...\n");
    
    if (api->wifi_connect(ssid, password) != 0) {
        print("Error: Connection failed\n");
        return;
    }
    
    // Wait for connection
    int timeout = 100;  // 10 seconds
    while (api->wifi_get_state() == WIFI_STATE_CONNECTING && timeout > 0) {
        api->sleep_ms(100);
        timeout--;
    }
    
    int state = api->wifi_get_state();
    if (state == WIFI_STATE_CONNECTED) {
        print("Connected!\n\n");
        show_status();
    } else {
        print("Error: Connection failed (state: ");
        print(state_name(state));
        print(")\n");
    }
}

static void do_disconnect(void) {
    if (!api->wifi_available()) {
        print("Error: WiFi not available\n");
        return;
    }
    
    if (api->wifi_get_state() != WIFI_STATE_CONNECTED) {
        print("Not connected\n");
        return;
    }
    
    if (api->wifi_disconnect() == 0) {
        print("Disconnected\n");
    } else {
        print("Error: Disconnect failed\n");
    }
}

static void show_usage(void) {
    print("Usage: wifi <command> [options]\n\n");
    print("Commands:\n");
    print("  status                  Show WiFi status\n");
    print("  scan                    Scan for networks\n");
    print("  connect <ssid> [pass]   Connect to network\n");
    print("  disconnect              Disconnect from network\n");
    print("  enable                  Enable WiFi radio\n");
    print("  disable                 Disable WiFi radio\n");
}

int main(int argc, char **argv, kapi_t *kapi) {
    api = kapi;
    
    if (argc < 2) {
        show_usage();
        return 0;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "status") == 0) {
        show_status();
    } else if (strcmp(cmd, "scan") == 0) {
        do_scan();
    } else if (strcmp(cmd, "connect") == 0) {
        if (argc < 3) {
            print("Error: SSID required\n");
            print("Usage: wifi connect <ssid> [password]\n");
            return 1;
        }
        const char *ssid = argv[2];
        const char *pass = (argc > 3) ? argv[3] : NULL;
        do_connect(ssid, pass);
    } else if (strcmp(cmd, "disconnect") == 0) {
        do_disconnect();
    } else if (strcmp(cmd, "enable") == 0) {
        if (!api->wifi_available()) {
            print("Error: WiFi hardware not available\n");
            return 1;
        }
        if (api->wifi_enable() == 0) {
            print("WiFi enabled\n");
        } else {
            print("Error: Failed to enable WiFi\n");
            return 1;
        }
    } else if (strcmp(cmd, "disable") == 0) {
        if (!api->wifi_available()) {
            print("Error: WiFi hardware not available\n");
            return 1;
        }
        if (api->wifi_disable() == 0) {
            print("WiFi disabled\n");
        } else {
            print("Error: Failed to disable WiFi\n");
            return 1;
        }
    } else {
        print("Unknown command: ");
        print(cmd);
        print("\n\n");
        show_usage();
        return 1;
    }
    
    return 0;
}
