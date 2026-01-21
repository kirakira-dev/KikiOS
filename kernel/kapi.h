/*
 * KikiOS Kernel API
 *
 * Function pointers passed to userspace programs.
 * Programs call kernel functions directly - no syscalls needed.
 * Win3.1 style!
 */

#ifndef KAPI_H
#define KAPI_H

#include <stdint.h>
#include <stddef.h>

// Kernel API version
#define KAPI_VERSION 1

// The kernel API structure - passed to every program
typedef struct {
    uint32_t version;

    // Console I/O
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*uart_puts)(const char *s);  // Direct UART output
    int  (*getc)(void);              // Non-blocking, returns -1 if no input
    void (*set_color)(uint32_t fg, uint32_t bg);
    void (*clear)(void);             // Clear screen
    void (*set_cursor)(int row, int col);  // Set cursor position
    void (*set_cursor_enabled)(int enabled);  // Enable/disable text cursor
    void (*print_int)(int n);        // Print integer
    void (*print_hex)(uint32_t n);   // Print hex
    void (*clear_to_eol)(void);      // Fast clear from cursor to end of line
    void (*clear_region)(int row, int col, int w, int h);  // Fast rectangular clear

    // Keyboard
    int  (*has_key)(void);           // Check if key available

    // Memory
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    // Filesystem
    void *(*open)(const char *path);  // Returns vfs_node_t*
    void  (*close)(void *file);       // Free vfs_node_t*
    int   (*read)(void *file, char *buf, size_t size, size_t offset);
    int   (*write)(void *file, const char *buf, size_t size);
    int   (*is_dir)(void *node);
    int   (*file_size)(void *node);   // Get file size in bytes
    void *(*create)(const char *path);
    void *(*mkdir)(const char *path);
    int   (*delete)(const char *path);
    int   (*delete_dir)(const char *path);  // Delete empty directory
    int   (*delete_recursive)(const char *path);  // Delete file or dir recursively
    int   (*rename)(const char *path, const char *newname);
    int   (*readdir)(void *dir, int index, char *name, size_t name_size, uint8_t *type);
    int   (*set_cwd)(const char *path);
    int   (*get_cwd)(char *buf, size_t size);

    // Process
    void (*exit)(int status);
    int  (*exec)(const char *path);   // Run another program (waits for completion)
    int  (*exec_args)(const char *path, int argc, char **argv);  // Run with arguments
    void (*yield)(void);              // Give up CPU to other processes
    int  (*spawn)(const char *path);  // Start a new process (returns immediately)
    int  (*spawn_args)(const char *path, int argc, char **argv);  // Spawn with arguments

    // Console info
    int  (*console_rows)(void);       // Get number of console rows
    int  (*console_cols)(void);       // Get number of console columns

    // Framebuffer (for GUI programs)
    uint32_t *fb_base;               // Direct framebuffer pointer
    uint32_t fb_width;               // Screen width in pixels
    uint32_t fb_height;              // Screen height in pixels
    void (*fb_put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fb_fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*fb_draw_char)(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
    void (*fb_draw_string)(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

    // Font access (for custom rendering)
    const uint8_t *font_data;        // 256 chars, 16 bytes each (8x16 bitmap)

    // Mouse (for GUI programs)
    void (*mouse_get_pos)(int *x, int *y);         // Get screen position
    uint8_t (*mouse_get_buttons)(void);            // Get button state
    void (*mouse_poll)(void);                      // Poll for updates
    void (*mouse_set_pos)(int x, int y);           // Set position (for capture)
    void (*mouse_get_delta)(int *dx, int *dy);     // Get and clear accumulated delta

    // Window management (for desktop apps)
    // These are set by the desktop window server, not the kernel
    int  (*window_create)(int x, int y, int w, int h, const char *title);
    void (*window_destroy)(int wid);
    uint32_t *(*window_get_buffer)(int wid, int *w, int *h);
    int  (*window_poll_event)(int wid, int *event_type, int *data1, int *data2, int *data3);
    void (*window_invalidate)(int wid);
    void (*window_set_title)(int wid, const char *title);

    // Stdio hooks (for terminal emulator)
    // If set, shell uses these instead of console I/O
    void (*stdio_putc)(char c);          // Write a character
    void (*stdio_puts)(const char *s);   // Write a string
    int  (*stdio_getc)(void);            // Read a character (-1 if none)
    int  (*stdio_has_key)(void);         // Check if input available

    // System info
    uint64_t (*get_uptime_ticks)(void);  // Get timer tick count (100 ticks/sec)
    size_t (*get_mem_used)(void);        // Get used memory in bytes
    size_t (*get_mem_free)(void);        // Get free memory in bytes

    // RTC (Real Time Clock)
    uint32_t (*get_timestamp)(void);     // Unix timestamp (seconds since 1970)
    void (*get_datetime)(int *year, int *month, int *day,
                         int *hour, int *minute, int *second, int *weekday);

    // Power management / timing
    void (*wfi)(void);                   // Wait for interrupt (low power sleep)
    void (*sleep_ms)(uint32_t ms);       // Sleep for at least ms milliseconds

    // Sound
    int (*sound_play_wav)(const void *data, uint32_t size);  // Play WAV from memory (legacy)
    void (*sound_stop)(void);                                 // Stop playback
    int (*sound_is_playing)(void);                           // Check if playing
    int (*sound_play_pcm)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);  // Play raw S16LE PCM (blocking)
    int (*sound_play_pcm_async)(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);  // Play raw S16LE PCM (non-blocking)
    void (*sound_pause)(void);                               // Pause playback (can resume)
    int (*sound_resume)(void);                               // Resume paused playback
    int (*sound_is_paused)(void);                            // Check if paused

    // Process info (for sysmon)
    int (*get_process_count)(void);                          // Number of active processes
    int (*get_process_info)(int index, char *name, int name_size, int *state);  // Get process info by index

    // Disk info
    int (*get_disk_total)(void);                             // Total disk space in KB
    int (*get_disk_free)(void);                              // Free disk space in KB

    // RAM info
    size_t (*get_ram_total)(void);                           // Total RAM in bytes

    // Debug memory info
    uint64_t (*get_heap_start)(void);                        // Heap start address
    uint64_t (*get_heap_end)(void);                          // Heap end address
    uint64_t (*get_stack_ptr)(void);                         // Current stack pointer
    int (*get_alloc_count)(void);                            // Number of allocations

    // Networking
    int (*net_ping)(uint32_t ip, uint16_t seq, uint32_t timeout_ms);  // Ping an IP, returns 0 on success
    void (*net_poll)(void);                                           // Process incoming packets
    uint32_t (*net_get_ip)(void);                                     // Get our IP address
    void (*net_get_mac)(uint8_t *mac);                               // Get our MAC address (6 bytes)
    uint32_t (*dns_resolve)(const char *hostname);                   // Resolve hostname to IP, returns 0 on failure

    // TCP sockets
    int (*tcp_connect)(uint32_t ip, uint16_t port);                  // Connect to server, returns socket or -1
    int (*tcp_send)(int sock, const void *data, uint32_t len);       // Send data, returns bytes sent or -1
    int (*tcp_recv)(int sock, void *buf, uint32_t maxlen);           // Receive data, returns bytes or 0/-1
    void (*tcp_close)(int sock);                                      // Close connection
    int (*tcp_is_connected)(int sock);                               // Check if connected

    // TLS (HTTPS) sockets
    int (*tls_connect)(uint32_t ip, uint16_t port, const char *hostname);  // Connect with TLS, returns socket or -1
    int (*tls_send)(int sock, const void *data, uint32_t len);             // Send encrypted data
    int (*tls_recv)(int sock, void *buf, uint32_t maxlen);                 // Receive decrypted data
    void (*tls_close)(int sock);                                           // Close TLS connection
    int (*tls_is_connected)(int sock);                                     // Check if connected

    // TrueType font rendering
    // Returns pointer to glyph struct: { uint8_t *bitmap, int width, height, xoff, yoff, advance }
    // style: 0=normal, 1=bold, 2=italic, 3=bold+italic
    // Bitmap is grayscale (0-255), cached internally - do not free
    void *(*ttf_get_glyph)(int codepoint, int size, int style);
    int (*ttf_get_advance)(int codepoint, int size);                       // Get advance width
    int (*ttf_get_kerning)(int cp1, int cp2, int size);                    // Get kerning between chars
    void (*ttf_get_metrics)(int size, int *ascent, int *descent, int *line_gap);
    int (*ttf_is_ready)(void);                                             // Check if TTF system is loaded

    // GPIO LED (for Pi only, no-op on QEMU)
    void (*led_on)(void);
    void (*led_off)(void);
    void (*led_toggle)(void);
    int (*led_status)(void);

    // Process control
    int (*kill_process)(int pid);                                // Kill a process by PID

    // CPU info
    const char *(*get_cpu_name)(void);                           // CPU model name
    uint32_t (*get_cpu_freq_mhz)(void);                          // CPU frequency in MHz
    int (*get_cpu_cores)(void);                                  // Number of CPU cores

    // USB device list
    int (*usb_device_count)(void);                               // Number of USB devices
    int (*usb_device_info)(int idx, uint16_t *vid, uint16_t *pid,
                           char *name, int name_len);            // Get USB device info

    // Kernel log (dmesg)
    size_t (*klog_read)(char *buf, size_t offset, size_t size);  // Read from kernel log
    size_t (*klog_size)(void);                                   // Get log size

    // Hardware double buffering (Pi only)
    int (*fb_has_hw_double_buffer)(void);    // Returns 1 if hardware double buffering available
    int (*fb_flip)(int buffer);              // Switch visible buffer (0 or 1), returns 0 on success
    uint32_t *(*fb_get_backbuffer)(void);    // Get pointer to current backbuffer

    // DMA (Direct Memory Access) - hardware accelerated memory copies (Pi only)
    int (*dma_available)(void);              // Returns 1 if DMA is available
    int (*dma_copy)(void *dst, const void *src, uint32_t len);  // 1D copy
    int (*dma_copy_2d)(void *dst, uint32_t dst_pitch,           // 2D blit
                       const void *src, uint32_t src_pitch,
                       uint32_t width, uint32_t height);
    int (*dma_fb_copy)(uint32_t *dst, const uint32_t *src,      // Full framebuffer copy
                       uint32_t width, uint32_t height);
    int (*dma_fill)(void *dst, uint32_t value, uint32_t len);   // Fill with 32-bit value
    
    // Windows executable support (x86 emulation)
    int (*winexec_run)(const char *path);  // Run a Windows .exe file, returns exit code
    int (*winexec_supported)(void);        // Check if Windows exe support is available
    
    // FTP server support
    void (*ftp_start)(uint16_t port);       // Start FTP server on port
    void (*ftp_stop)(void);                 // Stop FTP server
    int (*ftp_is_running)(void);            // Check if FTP server is running
    void (*ftp_poll)(void);                 // Poll FTP server (call from main loop)
    
    // WiFi support
    int (*wifi_available)(void);            // Check if WiFi hardware is available
    int (*wifi_enable)(void);               // Enable WiFi radio
    int (*wifi_disable)(void);              // Disable WiFi radio
    int (*wifi_get_state)(void);            // Get WiFi state
    int (*wifi_scan_start)(void);           // Start scanning for networks
    int (*wifi_scan_complete)(void);        // Check if scan is done
    int (*wifi_scan_results)(void *results, int max);  // Get scan results
    int (*wifi_connect)(const char *ssid, const char *pass);  // Connect to network
    int (*wifi_disconnect)(void);           // Disconnect from network
    int (*wifi_get_connection)(void *info); // Get connection info
    void (*wifi_get_mac)(uint8_t *mac);     // Get WiFi MAC address

} kapi_t;

// TTF font style flags (for ttf_get_glyph)
#define TTF_STYLE_NORMAL  0
#define TTF_STYLE_BOLD    1
#define TTF_STYLE_ITALIC  2

// Window event types
#define WIN_EVENT_NONE       0
#define WIN_EVENT_MOUSE_DOWN 1
#define WIN_EVENT_MOUSE_UP   2
#define WIN_EVENT_MOUSE_MOVE 3
#define WIN_EVENT_KEY        4
#define WIN_EVENT_CLOSE      5
#define WIN_EVENT_FOCUS      6
#define WIN_EVENT_UNFOCUS    7
#define WIN_EVENT_RESIZE     8

// Global kernel API instance
extern kapi_t kapi;

// Initialize the kernel API
void kapi_init(void);

#endif
