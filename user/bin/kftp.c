/*
 * KFTP - KikiOS FTP Server
 *
 * Starts an FTP server that allows file transfers
 * Compatible with macOS Finder and Windows File Explorer
 *
 * Usage:
 *   kftp start [port]  - Start FTP server (default port 21)
 *   kftp stop          - Stop FTP server
 *   kftp status        - Show server status
 *
 * Access:
 *   ftp://192.168.X.X (or your KikiOS IP)
 *   Username: any (anonymous login supported)
 *   Password: any
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

static void out_putc(char c) {
    if (api->stdio_putc) api->stdio_putc(c);
    else api->putc(c);
}

static void print_num(int n) {
    if (n < 0) { out_putc('-'); n = -n; }
    char buf[12];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    else while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) out_putc(buf[--i]);
}

// Simple atoi
static int parse_int(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;
    
    if (argc < 2) {
        out_puts("KFTP - KikiOS FTP Server\n\n");
        out_puts("Usage:\n");
        out_puts("  kftp start [port]  - Start FTP server (default: 21)\n");
        out_puts("  kftp stop          - Stop FTP server\n");
        out_puts("  kftp status        - Show server status\n\n");
        out_puts("Access:\n");
        out_puts("  ftp://<your-ip>\n");
        out_puts("  Username: any\n");
        out_puts("  Password: any\n");
        return 1;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "start") == 0) {
        uint16_t port = 21;
        
        if (argc >= 3) {
            port = (uint16_t)parse_int(argv[2]);
            if (port == 0) {
                out_puts("kftp: Invalid port number\n");
                return 1;
            }
        }
        
        if (api->ftp_is_running && api->ftp_is_running()) {
            out_puts("kftp: Server already running\n");
            return 1;
        }
        
        if (!api->ftp_start) {
            out_puts("kftp: FTP server not available\n");
            return 1;
        }
        
        api->ftp_start(port);
        
        // Get IP address
        uint32_t ip = api->net_get_ip();
        // IP is in network byte order, convert to host order for display
        uint8_t *ip_bytes = (uint8_t *)&ip;
        // Network byte order: first byte is MSB
        int ip0 = ip_bytes[3];
        int ip1 = ip_bytes[2];
        int ip2 = ip_bytes[1];
        int ip3 = ip_bytes[0];
        
        out_puts("KFTP server started on port ");
        print_num(port);
        out_puts("\n");
        out_puts("Access via: ftp://");
        print_num(ip0);
        out_putc('.');
        print_num(ip1);
        out_putc('.');
        print_num(ip2);
        out_putc('.');
        print_num(ip3);
        out_puts("\n");
        out_puts("Username: any\n");
        out_puts("Password: any\n");
        out_puts("\n");
        out_puts("Server is running. Press Ctrl+C to stop.\n");
        
        // Keep running and poll FTP server
        while (api->ftp_is_running && api->ftp_is_running()) {
            if (api->ftp_poll) {
                api->ftp_poll();
            }
            api->net_poll();  // Process network packets
            api->sleep_ms(10);   // Small delay
        }
        
        return 0;
    }
    else if (strcmp(cmd, "stop") == 0) {
        if (!api->ftp_is_running || !api->ftp_is_running()) {
            out_puts("kftp: Server is not running\n");
            return 1;
        }
        
        if (api->ftp_stop) {
            api->ftp_stop();
            out_puts("KFTP server stopped\n");
        }
        return 0;
    }
    else if (strcmp(cmd, "status") == 0) {
        if (api->ftp_is_running && api->ftp_is_running()) {
            uint32_t ip = api->net_get_ip();
            uint8_t *ip_bytes = (uint8_t *)&ip;
            int ip0 = ip_bytes[3];
            int ip1 = ip_bytes[2];
            int ip2 = ip_bytes[1];
            int ip3 = ip_bytes[0];
            
            out_puts("KFTP server is running\n");
            out_puts("Access: ftp://");
            print_num(ip0);
            out_putc('.');
            print_num(ip1);
            out_putc('.');
            print_num(ip2);
            out_putc('.');
            print_num(ip3);
            out_puts("\n");
        } else {
            out_puts("KFTP server is not running\n");
        }
        return 0;
    }
    else {
        out_puts("kftp: Unknown command: ");
        out_puts(cmd);
        out_puts("\n");
        return 1;
    }
    
    return 0;
}
