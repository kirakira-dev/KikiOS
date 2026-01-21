/*
 * FTP Server for KikiOS
 * 
 * Implements FTP protocol (RFC 959) server
 * Supports file upload/download, directory listing
 * Compatible with macOS Finder and Windows File Explorer
 */

#include "ftp.h"
#include "net.h"
#include "vfs.h"
#include "printf.h"
#include "string.h"
#include "memory.h"
#include <stddef.h>
#include <stdarg.h>

// External declarations
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strncpy(char *dest, const char *src, size_t n);
extern char *strstr(const char *haystack, const char *needle);
extern char *strchr(const char *s, int c);
extern const char *ip_to_str(uint32_t ip);
extern int tcp_get_peer_info(tcp_socket_t sock, uint32_t *ip, uint16_t *port);

// Helper: Safe strncpy that always null-terminates
static char *strncpy_safe(char *dest, const char *src, size_t n) {
    if (n == 0) return dest;
    strncpy(dest, src, n - 1);
    dest[n - 1] = '\0';
    return dest;
}

// FTP connection states
#define FTP_STATE_NONE      0
#define FTP_STATE_WAIT_USER 1
#define FTP_STATE_WAIT_PASS 2
#define FTP_STATE_LOGGED_IN 3

// FTP connection structure
#define FTP_MAX_CONNECTIONS 4
typedef struct {
    int active;
    tcp_socket_t control_sock;  // Control connection (port 21)
    tcp_socket_t data_sock;      // Data connection (for transfers)
    int state;
    char current_dir[256];
    uint32_t client_ip;
    uint16_t client_port;
    int pasv_mode;              // 1 = PASV mode, 0 = PORT mode
    uint32_t pasv_ip;
    uint16_t pasv_port;
} ftp_connection_t;

static ftp_connection_t ftp_connections[FTP_MAX_CONNECTIONS];
static int ftp_listening = 0;
static uint16_t ftp_port = 21;
static tcp_socket_t ftp_listen_sock = -1;

// Helper: Send FTP response
static void ftp_send_response(tcp_socket_t sock, int code, const char *msg) {
    char buf[512];
    int len = sprintf(buf, "%d %s\r\n", code, msg);
    tcp_send(sock, buf, len);
}

// Helper: Parse command from buffer
static int parse_command(const char *buf, int len, char *cmd, char *arg) {
    // Find end of line
    int i = 0;
    while (i < len && buf[i] != '\r' && buf[i] != '\n') i++;
    if (i == 0) return 0;
    
    // Find space
    int space = -1;
    for (int j = 0; j < i; j++) {
        if (buf[j] == ' ' || buf[j] == '\t') {
            space = j;
            break;
        }
    }
    
    if (space >= 0) {
        // Command and argument
        int cmd_len = space < 64 ? space : 64;
        memcpy(cmd, buf, cmd_len);
        cmd[cmd_len] = '\0';
        
        // Skip whitespace
        while (space < i && (buf[space] == ' ' || buf[space] == '\t')) space++;
        
        int arg_len = i - space;
        if (arg_len > 255) arg_len = 255;
        memcpy(arg, buf + space, arg_len);
        arg[arg_len] = '\0';
    } else {
        // Just command
        int cmd_len = i < 64 ? i : 64;
        memcpy(cmd, buf, cmd_len);
        cmd[cmd_len] = '\0';
        arg[0] = '\0';
    }
    
    // Convert to uppercase
    for (int j = 0; cmd[j]; j++) {
        if (cmd[j] >= 'a' && cmd[j] <= 'z') {
            cmd[j] = cmd[j] - 'a' + 'A';
        }
    }
    
    return i + 1;  // Return bytes consumed
}

// Helper: Get file size
static int get_file_size(const char *path) {
    vfs_node_t *f = vfs_lookup(path);
    if (!f) return -1;
    if (vfs_is_dir(f)) {
        return -1;
    }
    return f->size;
}

// Helper: Format file listing (UNIX-style)
static int format_listing(char *buf, const char *name, int is_dir, int size) {
    // Format: -rw-r--r-- 1 user group size date name
    // Simplified: drwxr-xr-x or -rw-r--r--
    if (is_dir) {
        return sprintf(buf, "drwxr-xr-x 1 user group %8d Jan  1 1970 %s\r\n", size, name);
    } else {
        return sprintf(buf, "-rw-r--r-- 1 user group %8d Jan  1 1970 %s\r\n", size, name);
    }
}

// Handle FTP command
static void handle_ftp_command(ftp_connection_t *conn) {
    char recv_buf[512];
    int len = tcp_recv(conn->control_sock, recv_buf, sizeof(recv_buf) - 1);
    
    if (len <= 0) {
        // Connection closed or error
        conn->active = 0;
        if (conn->control_sock >= 0) {
            tcp_close(conn->control_sock);
            conn->control_sock = -1;
        }
        if (conn->data_sock >= 0) {
            tcp_close(conn->data_sock);
            conn->data_sock = -1;
        }
        return;
    }
    
    recv_buf[len] = '\0';
    
    char cmd[64];
    char arg[256];
    int consumed = parse_command(recv_buf, len, cmd, arg);
    
    if (consumed == 0) return;
    
    printf("[FTP] Command: %s %s\n", cmd, arg);
    
    // Handle commands
    if (strcmp(cmd, "USER") == 0) {
        // Accept any username
        ftp_send_response(conn->control_sock, 331, "Password required");
        conn->state = FTP_STATE_WAIT_PASS;
    }
    else if (strcmp(cmd, "PASS") == 0) {
        // Accept any password
        ftp_send_response(conn->control_sock, 230, "User logged in");
        conn->state = FTP_STATE_LOGGED_IN;
        strcpy(conn->current_dir, "/");
    }
    else if (strcmp(cmd, "SYST") == 0) {
        ftp_send_response(conn->control_sock, 215, "UNIX Type: L8");
    }
    else if (strcmp(cmd, "FEAT") == 0) {
        // Features - send empty list
        ftp_send_response(conn->control_sock, 211, "Features:");
        ftp_send_response(conn->control_sock, 211, "End");
    }
    else if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
        char resp[512];
        sprintf(resp, "\"%s\"", conn->current_dir);
        ftp_send_response(conn->control_sock, 257, resp);
    }
    else if (strcmp(cmd, "CWD") == 0 || strcmp(cmd, "XCWD") == 0) {
        char new_dir[256];
        if (arg[0] == '/') {
            strncpy_safe(new_dir, arg, sizeof(new_dir));
        } else {
            sprintf(new_dir, "%s/%s", conn->current_dir, arg);
        }
        
        // Normalize path
        vfs_node_t *d = vfs_lookup(new_dir);
        if (d && vfs_is_dir(d)) {
            strncpy_safe(conn->current_dir, new_dir, sizeof(conn->current_dir));
            ftp_send_response(conn->control_sock, 250, "CWD command successful");
        } else {
            ftp_send_response(conn->control_sock, 550, "Directory not found");
        }
    }
    else if (strcmp(cmd, "CDUP") == 0 || strcmp(cmd, "XCUP") == 0) {
        // Go to parent directory
        int len = strlen(conn->current_dir);
        if (len > 1) {
            // Find last /
            int last_slash = -1;
            for (int i = len - 1; i >= 0; i--) {
                if (conn->current_dir[i] == '/') {
                    last_slash = i;
                    break;
                }
            }
            if (last_slash > 0) {
                conn->current_dir[last_slash] = '\0';
            } else {
                conn->current_dir[1] = '\0';
            }
        }
        ftp_send_response(conn->control_sock, 250, "CDUP command successful");
    }
    else if (strcmp(cmd, "TYPE") == 0) {
        // Type I (binary) or A (ASCII)
        if (arg[0] == 'I' || arg[0] == 'A') {
            ftp_send_response(conn->control_sock, 200, "Type set");
        } else {
            ftp_send_response(conn->control_sock, 504, "Type not supported");
        }
    }
    else if (strcmp(cmd, "MODE") == 0) {
        if (arg[0] == 'S') {
            ftp_send_response(conn->control_sock, 200, "Mode set");
        } else {
            ftp_send_response(conn->control_sock, 504, "Mode not supported");
        }
    }
    else if (strcmp(cmd, "STRU") == 0) {
        if (arg[0] == 'F') {
            ftp_send_response(conn->control_sock, 200, "Structure set");
        } else {
            ftp_send_response(conn->control_sock, 504, "Structure not supported");
        }
    }
    else if (strcmp(cmd, "PASV") == 0) {
        // Passive mode - we'll use a high port
        conn->pasv_mode = 1;
        conn->pasv_ip = net_get_ip();
        conn->pasv_port = 50000 + (conn - ftp_connections);  // Different port per connection
        
        // Format PASV response: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
        uint8_t *ip = (uint8_t *)&conn->pasv_ip;
        uint16_t port = conn->pasv_port;
        char resp[128];
        sprintf(resp, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
                ip[0], ip[1], ip[2], ip[3],
                (port >> 8) & 0xFF, port & 0xFF);
        ftp_send_response(conn->control_sock, 227, resp);
        
        // TODO: Actually listen on pasv_port
        // For now, we'll accept connections on the data socket
    }
    else if (strcmp(cmd, "PORT") == 0) {
        // Active mode - parse IP and port
        // Format: PORT h1,h2,h3,h4,p1,p2
        uint8_t ip[4];
        uint8_t p1, p2;
        
        // Simple parser (no sscanf)
        const char *p = arg;
        int values[6];
        int idx = 0;
        int current = 0;
        
        for (; *p && idx < 6; p++) {
            if (*p >= '0' && *p <= '9') {
                current = current * 10 + (*p - '0');
            } else if (*p == ',' || *p == '\0') {
                if (current > 255) {
                    ftp_send_response(conn->control_sock, 501, "Invalid PORT command");
                    return;
                }
                values[idx++] = current;
                current = 0;
                if (*p == '\0') break;
            } else {
                ftp_send_response(conn->control_sock, 501, "Invalid PORT command");
                return;
            }
        }
        
        if (idx == 6) {
            conn->pasv_mode = 0;
            conn->client_ip = MAKE_IP(values[0], values[1], values[2], values[3]);
            conn->client_port = (values[4] << 8) | values[5];
            ftp_send_response(conn->control_sock, 200, "PORT command successful");
        } else {
            ftp_send_response(conn->control_sock, 501, "Invalid PORT command");
        }
    }
    else if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0) {
        if (conn->state != FTP_STATE_LOGGED_IN) {
            ftp_send_response(conn->control_sock, 530, "Not logged in");
            return;
        }
        
        // Open data connection
        if (conn->pasv_mode) {
            // Wait for client to connect to pasv_port
            // For now, use control socket (simplified)
            conn->data_sock = conn->control_sock;  // Temporary
        } else {
            // Connect to client
            conn->data_sock = tcp_connect(conn->client_ip, conn->client_port);
            if (conn->data_sock < 0) {
                ftp_send_response(conn->control_sock, 425, "Cannot open data connection");
                return;
            }
        }
        
        ftp_send_response(conn->control_sock, 150, "Opening ASCII mode data connection");
        
        // List directory
        vfs_node_t *dir = vfs_lookup(conn->current_dir);
        if (dir && vfs_is_dir(dir)) {
            char listing[4096];
            int listing_len = 0;
            char name[64];
            uint8_t type;
            int idx = 0;
            
            while (vfs_readdir(dir, idx, name, sizeof(name), &type) == 0 && listing_len < 4000) {
                idx++;
                if (name[0] == '.') continue;  // Skip hidden files
                
                char line[256];
                int line_len;
                
                if (type == VFS_FILE) {
                    // File
                    char file_path[512];
                    sprintf(file_path, "%s/%s", conn->current_dir, name);
                    int size = get_file_size(file_path);
                    if (size < 0) size = 0;
                    line_len = format_listing(line, name, 0, size);
                } else {
                    // Directory
                    line_len = format_listing(line, name, 1, 4096);
                }
                
                if ((size_t)(listing_len + line_len) < sizeof(listing) - 1) {
                    memcpy(listing + listing_len, line, line_len);
                    listing_len += line_len;
                }
            }
            
            listing[listing_len] = '\0';
            tcp_send(conn->data_sock, listing, listing_len);
        }
        
        if (conn->data_sock != conn->control_sock) {
            tcp_close(conn->data_sock);
            conn->data_sock = -1;
        }
        
        ftp_send_response(conn->control_sock, 226, "Transfer complete");
    }
    else if (strcmp(cmd, "RETR") == 0) {
        // Download file
        if (conn->state != FTP_STATE_LOGGED_IN) {
            ftp_send_response(conn->control_sock, 530, "Not logged in");
            return;
        }
        
        char file_path[512];
        if (arg[0] == '/') {
            strncpy_safe(file_path, arg, sizeof(file_path));
        } else {
            sprintf(file_path, "%s/%s", conn->current_dir, arg);
        }
        
        vfs_node_t *f = vfs_lookup(file_path);
        if (!f || vfs_is_dir(f)) {
            ftp_send_response(conn->control_sock, 550, "File not found");
            return;
        }
        
        // Open data connection
        if (conn->pasv_mode) {
            conn->data_sock = conn->control_sock;  // Temporary
        } else {
            conn->data_sock = tcp_connect(conn->client_ip, conn->client_port);
            if (conn->data_sock < 0) {
                ftp_send_response(conn->control_sock, 425, "Cannot open data connection");
                return;
            }
        }
        
        int file_size = f->size;
        ftp_send_response(conn->control_sock, 150, "Opening BINARY mode data connection");
        
        // Read and send file in chunks
        char buf[4096];
        int offset = 0;
        while (offset < file_size) {
            int chunk = file_size - offset;
            if ((size_t)chunk > sizeof(buf)) chunk = (int)sizeof(buf);
            
            int read = vfs_read(f, buf, chunk, offset);
            if (read <= 0) break;
            
            tcp_send(conn->data_sock, buf, read);
            offset += read;
        }
        
        if (conn->data_sock != conn->control_sock) {
            tcp_close(conn->data_sock);
            conn->data_sock = -1;
        }
        
        ftp_send_response(conn->control_sock, 226, "Transfer complete");
    }
    else if (strcmp(cmd, "STOR") == 0 || strcmp(cmd, "APPE") == 0) {
        // Upload file
        if (conn->state != FTP_STATE_LOGGED_IN) {
            ftp_send_response(conn->control_sock, 530, "Not logged in");
            return;
        }
        
        char file_path[512];
        if (arg[0] == '/') {
            strncpy_safe(file_path, arg, sizeof(file_path));
        } else {
            sprintf(file_path, "%s/%s", conn->current_dir, arg);
        }
        
        // Open data connection
        if (conn->pasv_mode) {
            conn->data_sock = conn->control_sock;  // Temporary
        } else {
            conn->data_sock = tcp_connect(conn->client_ip, conn->client_port);
            if (conn->data_sock < 0) {
                ftp_send_response(conn->control_sock, 425, "Cannot open data connection");
                return;
            }
        }
        
        vfs_node_t *f = vfs_create(file_path);
        if (!f) {
            ftp_send_response(conn->control_sock, 550, "Cannot create file");
            if (conn->data_sock != conn->control_sock) {
                tcp_close(conn->data_sock);
                conn->data_sock = -1;
            }
            return;
        }
        
        ftp_send_response(conn->control_sock, 150, "Opening BINARY mode data connection");
        
        // Receive and write file
        char buf[4096];
        int total = 0;
        while (1) {
            int len = tcp_recv(conn->data_sock, buf, sizeof(buf));
            if (len <= 0) break;
            
            vfs_write(f, buf, len);
            total += len;
        }
        
        if (conn->data_sock != conn->control_sock) {
            tcp_close(conn->data_sock);
            conn->data_sock = -1;
        }
        
        ftp_send_response(conn->control_sock, 226, "Transfer complete");
    }
    else if (strcmp(cmd, "DELE") == 0) {
        if (conn->state != FTP_STATE_LOGGED_IN) {
            ftp_send_response(conn->control_sock, 530, "Not logged in");
            return;
        }
        
        char file_path[512];
        if (arg[0] == '/') {
            strncpy_safe(file_path, arg, sizeof(file_path));
        } else {
            sprintf(file_path, "%s/%s", conn->current_dir, arg);
        }
        
        if (vfs_delete(file_path) == 0) {
            ftp_send_response(conn->control_sock, 250, "DELE command successful");
        } else {
            ftp_send_response(conn->control_sock, 550, "File not found");
        }
    }
    else if (strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
        if (conn->state != FTP_STATE_LOGGED_IN) {
            ftp_send_response(conn->control_sock, 530, "Not logged in");
            return;
        }
        
        char dir_path[512];
        if (arg[0] == '/') {
            strncpy_safe(dir_path, arg, sizeof(dir_path));
        } else {
            sprintf(dir_path, "%s/%s", conn->current_dir, arg);
        }
        
        if (vfs_mkdir(dir_path) == 0) {
            char resp[512];
            sprintf(resp, "\"%s\" created", dir_path);
            ftp_send_response(conn->control_sock, 257, resp);
        } else {
            ftp_send_response(conn->control_sock, 550, "Cannot create directory");
        }
    }
    else if (strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
        if (conn->state != FTP_STATE_LOGGED_IN) {
            ftp_send_response(conn->control_sock, 530, "Not logged in");
            return;
        }
        
        char dir_path[512];
        if (arg[0] == '/') {
            strncpy_safe(dir_path, arg, sizeof(dir_path));
        } else {
            sprintf(dir_path, "%s/%s", conn->current_dir, arg);
        }
        
        if (vfs_delete_dir(dir_path) == 0) {
            ftp_send_response(conn->control_sock, 250, "RMD command successful");
        } else {
            ftp_send_response(conn->control_sock, 550, "Directory not found");
        }
    }
    else if (strcmp(cmd, "SIZE") == 0) {
        if (conn->state != FTP_STATE_LOGGED_IN) {
            ftp_send_response(conn->control_sock, 530, "Not logged in");
            return;
        }
        
        char file_path[512];
        if (arg[0] == '/') {
            strncpy_safe(file_path, arg, sizeof(file_path));
        } else {
            sprintf(file_path, "%s/%s", conn->current_dir, arg);
        }
        
        int size = get_file_size(file_path);
        if (size >= 0) {
            char resp[64];
            sprintf(resp, "%d", size);
            ftp_send_response(conn->control_sock, 213, resp);
        } else {
            ftp_send_response(conn->control_sock, 550, "File not found");
        }
    }
    else if (strcmp(cmd, "QUIT") == 0) {
        ftp_send_response(conn->control_sock, 221, "Goodbye");
        conn->active = 0;
    }
    else if (strcmp(cmd, "NOOP") == 0) {
        ftp_send_response(conn->control_sock, 200, "NOOP command successful");
    }
    else {
        // Unknown command
        ftp_send_response(conn->control_sock, 502, "Command not implemented");
    }
}

// Check for new connections
static void ftp_check_new_connections(void) {
    if (ftp_listen_sock < 0) return;
    
    // Try to accept a new connection
    tcp_socket_t new_sock = tcp_accept(ftp_listen_sock);
    if (new_sock < 0) return;
    
    // Find free connection slot
    int idx = -1;
    for (int i = 0; i < FTP_MAX_CONNECTIONS; i++) {
        if (!ftp_connections[i].active) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        printf("[FTP] No free connection slots, closing new connection\n");
        tcp_close(new_sock);
        return;
    }
    
    // Initialize connection
    ftp_connection_t *conn = &ftp_connections[idx];
    memset(conn, 0, sizeof(*conn));
    conn->active = 1;
    conn->control_sock = new_sock;
    conn->state = FTP_STATE_WAIT_USER;
    conn->data_sock = -1;
    strcpy(conn->current_dir, "/");
    
    // Get client info from socket
    extern int tcp_get_peer_info(tcp_socket_t sock, uint32_t *ip, uint16_t *port);
    tcp_get_peer_info(new_sock, &conn->client_ip, &conn->client_port);
    
    // Send welcome message
    ftp_send_response(new_sock, 220, "KikiOS FTP server ready");
    
    printf("[FTP] New connection accepted from %s:%d (slot %d)\n", 
           ip_to_str(conn->client_ip), conn->client_port, idx);
}

// Initialize FTP server
void ftp_init(void) {
    memset(ftp_connections, 0, sizeof(ftp_connections));
    ftp_listening = 0;
    ftp_listen_sock = -1;
}

// Start FTP server
void ftp_start(uint16_t port) {
    if (ftp_listening) {
        printf("[FTP] Server already running\n");
        return;
    }
    
    ftp_port = port;
    ftp_init();
    
    // Start listening on FTP port
    ftp_listen_sock = tcp_listen(port);
    if (ftp_listen_sock < 0) {
        printf("[FTP] Failed to start listening on port %d\n", port);
        return;
    }
    
    printf("[FTP] Server started on port %d\n", port);
    uint32_t ip = net_get_ip();
    printf("[FTP] Access via: ftp://%s\n", ip_to_str(ip));
    
    ftp_listening = 1;
}

// Stop FTP server
void ftp_stop(void) {
    if (!ftp_listening) return;
    
    // Close all connections
    for (int i = 0; i < FTP_MAX_CONNECTIONS; i++) {
        if (ftp_connections[i].active) {
            if (ftp_connections[i].control_sock >= 0) {
                tcp_close(ftp_connections[i].control_sock);
            }
            if (ftp_connections[i].data_sock >= 0) {
                tcp_close(ftp_connections[i].data_sock);
            }
            ftp_connections[i].active = 0;
        }
    }
    
    if (ftp_listen_sock >= 0) {
        tcp_close(ftp_listen_sock);
        ftp_listen_sock = -1;
    }
    
    ftp_listening = 0;
    printf("[FTP] Server stopped\n");
}

// Check if server is running
int ftp_is_running(void) {
    return ftp_listening;
}

// Poll FTP server (call from main loop)
void ftp_poll(void) {
    if (!ftp_listening) return;
    
    // Check for new connections
    ftp_check_new_connections();
    
    // Process existing connections
    for (int i = 0; i < FTP_MAX_CONNECTIONS; i++) {
        if (ftp_connections[i].active) {
            handle_ftp_command(&ftp_connections[i]);
        }
    }
}
