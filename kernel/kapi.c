/*
 * KikiOS Kernel API Implementation
 */

#include "kapi.h"
#include "console.h"
#include "keyboard.h"
#include "memory.h"
#include "vfs.h"
#include "process.h"
#include "fb.h"
#include "mouse.h"
#include "irq.h"
#include "rtc.h"
#include "virtio_sound.h"
#include "fat32.h"
#include "net.h"
#include "tls.h"
#include "ttf.h"
#include "klog.h"
#include "ftp.h"
#include "winexec.h"
#include "hal/hal.h"

// Global kernel API instance
kapi_t kapi;

// RAM size helper
static size_t kapi_get_ram_total(void) {
    extern uint64_t ram_size;
    return (size_t)ram_size;
}

// Wrapper for exit (needs to match signature)
static void kapi_exit(int status) {
    process_exit(status);
}


// Print integer (simple implementation)
static void kapi_print_int(int n) {
    if (n < 0) {
        console_putc('-');
        n = -n;
    }
    if (n == 0) {
        console_putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        console_putc(buf[--i]);
    }
}

// Print hex
static void kapi_print_hex(uint32_t n) {
    const char *hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        console_putc(hex[(n >> (i * 4)) & 0xF]);
    }
}

// Wrapper for exec
static int kapi_exec(const char *path) {
    return process_exec(path);
}

// Wrapper for exec with arguments
static int kapi_exec_args(const char *path, int argc, char **argv) {
    return process_exec_args(path, argc, argv);
}

// Wrapper for spawn - create and start a new process
static int kapi_spawn(const char *path) {
    char *argv[1] = { (char *)path };
    int pid = process_create(path, 1, argv);
    if (pid > 0) {
        process_start(pid);
    }
    return pid;
}

// Wrapper for spawn with arguments - create and start a new process
static int kapi_spawn_args(const char *path, int argc, char **argv) {
    int pid = process_create(path, argc, argv);
    if (pid > 0) {
        process_start(pid);
    }
    return pid;
}

// Wrapper for console color
static void kapi_set_color(uint32_t fg, uint32_t bg) {
    console_set_color(fg, bg);
}

// Wrapper for VFS open (allocates handle)
static void *kapi_open(const char *path) {
    return (void *)vfs_open_handle(path);
}

// Wrapper for VFS close (frees handle)
static void kapi_close(void *file) {
    vfs_close_handle((vfs_node_t *)file);
}

// Wrapper for VFS read
static int kapi_read(void *file, char *buf, size_t size, size_t offset) {
    return vfs_read((vfs_node_t *)file, buf, size, offset);
}

// Wrapper for VFS write
static int kapi_write(void *file, const char *buf, size_t size) {
    return vfs_write((vfs_node_t *)file, buf, size);
}

// Wrapper for is_dir
static int kapi_is_dir(void *node) {
    return vfs_is_dir((vfs_node_t *)node);
}

// Wrapper for file_size
static int kapi_file_size(void *node) {
    if (!node) return -1;
    vfs_node_t *n = (vfs_node_t *)node;
    return (int)n->size;
}

// Wrapper for create
static void *kapi_create(const char *path) {
    return (void *)vfs_create(path);
}

// Wrapper for mkdir
static void *kapi_mkdir(const char *path) {
    return (void *)vfs_mkdir(path);
}

// Wrapper for delete
static int kapi_delete(const char *path) {
    return vfs_delete(path);
}

// Wrapper for delete_dir
static int kapi_delete_dir(const char *path) {
    return vfs_delete_dir(path);
}

// Wrapper for delete_recursive
static int kapi_delete_recursive(const char *path) {
    return vfs_delete_recursive(path);
}

// Wrapper for rename
static int kapi_rename(const char *path, const char *newname) {
    return vfs_rename(path, newname);
}

// Wrapper for readdir
static int kapi_readdir(void *dir, int index, char *name, size_t name_size, uint8_t *type) {
    return vfs_readdir((vfs_node_t *)dir, index, name, name_size, type);
}

// Wrapper for set_cwd
static int kapi_set_cwd(const char *path) {
    return vfs_set_cwd(path);
}

// Wrapper for get_cwd
static int kapi_get_cwd(char *buf, size_t size) {
    return vfs_get_cwd_path(buf, size);
}

// Wrapper for get_datetime (flattens the datetime_t struct)
static void kapi_get_datetime(int *year, int *month, int *day,
                              int *hour, int *minute, int *second, int *weekday) {
    datetime_t dt;
    rtc_get_datetime(&dt);
    if (year) *year = dt.year;
    if (month) *month = dt.month;
    if (day) *day = dt.day;
    if (hour) *hour = dt.hour;
    if (minute) *minute = dt.minute;
    if (second) *second = dt.second;
    if (weekday) *weekday = dt.weekday;
}

void kapi_init(void) {
    kapi.version = KAPI_VERSION;

    // Console
    extern void uart_puts(const char *s);
    kapi.putc = console_putc;
    kapi.puts = console_puts;
    kapi.uart_puts = uart_puts;
    kapi.getc = keyboard_getc;
    kapi.set_color = kapi_set_color;
    kapi.clear = console_clear;
    kapi.set_cursor = console_set_cursor;
    kapi.set_cursor_enabled = console_set_cursor_enabled;
    kapi.print_int = kapi_print_int;
    kapi.print_hex = kapi_print_hex;
    kapi.clear_to_eol = console_clear_to_eol;
    kapi.clear_region = console_clear_region;

    // Keyboard
    kapi.has_key = keyboard_has_key;

    // Memory
    kapi.malloc = malloc;
    kapi.free = free;

    // Filesystem
    kapi.open = kapi_open;
    kapi.close = kapi_close;
    kapi.read = kapi_read;
    kapi.write = kapi_write;
    kapi.is_dir = kapi_is_dir;
    kapi.file_size = kapi_file_size;
    kapi.create = kapi_create;
    kapi.mkdir = kapi_mkdir;
    kapi.delete = kapi_delete;
    kapi.delete_dir = kapi_delete_dir;
    kapi.delete_recursive = kapi_delete_recursive;
    kapi.rename = kapi_rename;
    kapi.readdir = kapi_readdir;
    kapi.set_cwd = kapi_set_cwd;
    kapi.get_cwd = kapi_get_cwd;

    // Process
    kapi.exit = kapi_exit;
    kapi.exec = kapi_exec;
    kapi.exec_args = kapi_exec_args;
    kapi.yield = process_yield;  // Voluntary yield + preemptive backup
    kapi.spawn = kapi_spawn;
    kapi.spawn_args = kapi_spawn_args;

    // Console info
    kapi.console_rows = console_rows;
    kapi.console_cols = console_cols;

    // Framebuffer
    kapi.fb_base = fb_base;
    kapi.fb_width = fb_width;
    kapi.fb_height = fb_height;
    kapi.fb_put_pixel = fb_put_pixel;
    kapi.fb_fill_rect = fb_fill_rect;
    kapi.fb_draw_char = fb_draw_char;
    kapi.fb_draw_string = fb_draw_string;

    // Font access
    extern const uint8_t font_data[256][16];
    kapi.font_data = (const uint8_t *)font_data;

    // Mouse
    kapi.mouse_get_pos = mouse_get_screen_pos;
    kapi.mouse_get_buttons = mouse_get_buttons;
    kapi.mouse_poll = mouse_poll;
    kapi.mouse_set_pos = mouse_set_pos;
    kapi.mouse_get_delta = mouse_get_delta;

    // Window management (provided by desktop, not kernel)
    kapi.window_create = 0;
    kapi.window_destroy = 0;
    kapi.window_get_buffer = 0;
    kapi.window_poll_event = 0;
    kapi.window_invalidate = 0;
    kapi.window_set_title = 0;

    // Stdio hooks (provided by terminal emulator, not kernel)
    kapi.stdio_putc = 0;
    kapi.stdio_puts = 0;
    kapi.stdio_getc = 0;
    kapi.stdio_has_key = 0;

    // System info
    kapi.get_uptime_ticks = timer_get_ticks;
    kapi.get_mem_used = memory_used;
    kapi.get_mem_free = memory_free;

    // RTC
    kapi.get_timestamp = rtc_get_timestamp;
    kapi.get_datetime = kapi_get_datetime;

    // Power management / timing
    kapi.wfi = wfi;
    kapi.sleep_ms = sleep_ms;

    // Sound
    kapi.sound_play_wav = virtio_sound_play_wav;
    kapi.sound_stop = virtio_sound_stop;
    kapi.sound_is_playing = virtio_sound_is_playing;
    kapi.sound_play_pcm = (int (*)(const void *, uint32_t, uint8_t, uint32_t))virtio_sound_play_pcm;
    kapi.sound_play_pcm_async = (int (*)(const void *, uint32_t, uint8_t, uint32_t))virtio_sound_play_pcm_async;
    kapi.sound_pause = virtio_sound_pause;
    kapi.sound_resume = virtio_sound_resume;
    kapi.sound_is_paused = virtio_sound_is_paused;

    // Process info
    kapi.get_process_count = process_count_ready;
    kapi.get_process_info = process_get_info;

    // Disk info
    kapi.get_disk_total = fat32_get_total_kb;
    kapi.get_disk_free = fat32_get_free_kb;

    // RAM info
    kapi.get_ram_total = kapi_get_ram_total;

    // Debug memory info
    kapi.get_heap_start = memory_heap_start;
    kapi.get_heap_end = memory_heap_end;
    kapi.get_stack_ptr = memory_get_sp;
    kapi.get_alloc_count = memory_alloc_count;

    // Networking
    kapi.net_ping = net_ping;
    kapi.net_poll = net_poll;
    kapi.net_get_ip = net_get_ip;
    kapi.net_get_mac = net_get_mac;
    kapi.dns_resolve = dns_resolve;

    // TCP sockets
    kapi.tcp_connect = tcp_connect;
    kapi.tcp_send = tcp_send;
    kapi.tcp_recv = tcp_recv;
    kapi.tcp_close = tcp_close;
    kapi.tcp_is_connected = tcp_is_connected;

    // TLS (HTTPS) sockets
    kapi.tls_connect = tls_connect;
    kapi.tls_send = tls_send;
    kapi.tls_recv = tls_recv;
    kapi.tls_close = tls_close;
    kapi.tls_is_connected = tls_is_connected;

    // TrueType font rendering
    kapi.ttf_get_glyph = (void *(*)(int, int, int))ttf_get_glyph;
    kapi.ttf_get_advance = ttf_get_advance;
    kapi.ttf_get_kerning = ttf_get_kerning;
    kapi.ttf_get_metrics = ttf_get_metrics;
    kapi.ttf_is_ready = ttf_is_ready;

    // GPIO LED
    kapi.led_on = hal_led_on;
    kapi.led_off = hal_led_off;
    kapi.led_toggle = hal_led_toggle;
    kapi.led_status = hal_led_status;

    // Process control
    kapi.kill_process = process_kill;

    // CPU info
    kapi.get_cpu_name = hal_get_cpu_name;
    kapi.get_cpu_freq_mhz = hal_get_cpu_freq_mhz;
    kapi.get_cpu_cores = hal_get_cpu_cores;

    // USB device list
    kapi.usb_device_count = hal_usb_get_device_count;
    kapi.usb_device_info = hal_usb_get_device_info;

    // Kernel log (dmesg)
    kapi.klog_read = klog_read;
    kapi.klog_size = klog_size;

    // Hardware double buffering
    kapi.fb_has_hw_double_buffer = fb_has_hw_double_buffer;
    kapi.fb_flip = fb_flip;
    kapi.fb_get_backbuffer = fb_get_backbuffer;

    // DMA (hardware accelerated memory copies)
    kapi.dma_available = hal_dma_available;
    kapi.dma_copy = hal_dma_copy;
    kapi.dma_copy_2d = hal_dma_copy_2d;
    kapi.dma_fb_copy = hal_dma_fb_copy;
    kapi.dma_fill = hal_dma_fill;
    
    // Windows executable support
    kapi.winexec_run = winexec_run;
    kapi.winexec_supported = winexec_supported;
    
    // FTP server support
    kapi.ftp_start = ftp_start;
    kapi.ftp_stop = ftp_stop;
    kapi.ftp_is_running = ftp_is_running;
    kapi.ftp_poll = ftp_poll;
    
    // WiFi support
    extern int wifi_available(void);
    extern int wifi_enable(void);
    extern int wifi_disable(void);
    extern int wifi_get_state(void);
    extern int wifi_scan_start(void);
    extern int wifi_scan_complete(void);
    extern int wifi_scan_get_results(void *results, int max);
    extern int wifi_connect(const char *ssid, const char *pass);
    extern int wifi_disconnect(void);
    extern int wifi_get_connection(void *info);
    extern void wifi_get_mac(uint8_t *mac);
    
    kapi.wifi_available = wifi_available;
    kapi.wifi_enable = wifi_enable;
    kapi.wifi_disable = wifi_disable;
    kapi.wifi_get_state = wifi_get_state;
    kapi.wifi_scan_start = wifi_scan_start;
    kapi.wifi_scan_complete = wifi_scan_complete;
    kapi.wifi_scan_results = wifi_scan_get_results;
    kapi.wifi_connect = wifi_connect;
    kapi.wifi_disconnect = wifi_disconnect;
    kapi.wifi_get_connection = wifi_get_connection;
    kapi.wifi_get_mac = wifi_get_mac;
}
