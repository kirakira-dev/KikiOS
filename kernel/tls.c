/*
 * KikiOS TLS Implementation
 *
 * Wraps TLSe library for HTTPS support.
 * Uses our TCP stack for transport.
 */

// TLSe configuration - must come before includes
#define TLS_AMALGAMATION          // Use bundled libtomcrypt
#define NO_SSL_COMPATIBLE_INTERFACE  // Don't need SSL_* API
// #define NO_TLS_LEGACY_SUPPORT     // Need TLS 1.2 support
#define LTM_DESC                  // Use libtommath
#define LTC_NO_FILE               // Don't use file-based RNG (no /dev/random)

// Provide our own random source - must fill buffer AND return 1
#define TLS_USE_RANDOM_SOURCE(key, len) do { vibe_random(key, len); } while(0); return 1

// ============ Safe unaligned memory access helpers ============
// ARM requires aligned access for multi-byte stores. TLSe does
// things like *(unsigned short *)(buf+1) which crashes on ARM.
// These helpers do byte-by-byte access instead.

static inline void store_u16_unaligned(void *ptr, unsigned short val) {
    unsigned char *p = (unsigned char *)ptr;
    p[0] = (val >> 8) & 0xFF;   // Big endian (network byte order)
    p[1] = val & 0xFF;
}

static inline void store_u16_le_unaligned(void *ptr, unsigned short val) {
    unsigned char *p = (unsigned char *)ptr;
    p[0] = val & 0xFF;          // Little endian
    p[1] = (val >> 8) & 0xFF;
}

static inline void store_u32_unaligned(void *ptr, unsigned int val) {
    unsigned char *p = (unsigned char *)ptr;
    p[0] = (val >> 24) & 0xFF;  // Big endian
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
}

static inline unsigned short load_u16_unaligned(const void *ptr) {
    const unsigned char *p = (const unsigned char *)ptr;
    return (p[0] << 8) | p[1];  // Big endian
}

static inline unsigned int load_u32_unaligned(const void *ptr) {
    const unsigned char *p = (const unsigned char *)ptr;
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];  // Big endian
}

// Standard types (from compiler)
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// Libc stubs (in kernel/libc/)
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>

// Our kernel headers
#include "memory.h"
#include "rtc.h"
#include "irq.h"
#include "net.h"
#include "tls.h"

// Forward declaration
extern void sleep_ms(uint32_t ms);

// Forward declarations for kernel functions
extern void uart_puts(const char *s);
extern unsigned long timer_get_ticks(void);

// errno global (declared in errno.h)
int errno = 0;

// ============ Implementations for time.h ============

time_t time(time_t *t) {
    uint32_t ts = rtc_get_timestamp();
    if (t) *t = ts;
    return ts;
}

static struct tm _tm_buf;
struct tm *gmtime(const time_t *t) {
    uint32_t ts = t ? *t : 0;
    _tm_buf.tm_sec = ts % 60; ts /= 60;
    _tm_buf.tm_min = ts % 60; ts /= 60;
    _tm_buf.tm_hour = ts % 24; ts /= 24;
    _tm_buf.tm_year = 70 + (ts / 365);
    _tm_buf.tm_mon = 0;
    _tm_buf.tm_mday = 1;
    _tm_buf.tm_wday = 0;
    _tm_buf.tm_yday = 0;
    _tm_buf.tm_isdst = 0;
    return &_tm_buf;
}

// ============ Random number generator for TLS ============

void vibe_random(unsigned char *key, int len) {
    static uint32_t seed = 0;
    if (seed == 0) {
        seed = rtc_get_timestamp() ^ timer_get_ticks();
    }
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        key[i] = (seed >> 16) & 0xFF;
        if ((i & 0xF) == 0) seed ^= timer_get_ticks();
    }
}

// We need to provide random bytes to libtomcrypt.
// The sprng PRNG calls rng_get_bytes -> rng_nix -> fopen("/dev/random") which fails.
// Solution: We'll patch libtomcrypt.c to use our vibe_random directly.
// For now, let's define LTC_TEST to skip some code paths and provide rng via callback.

// ============ Now include TLSe ============

// Suppress warnings from third-party code
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wpointer-sign"
#pragma GCC diagnostic ignored "-Wold-style-declaration"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#include "../vendor/tlse/tlse.c"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// ============ KikiOS TLS API ============

#define MAX_TLS_SOCKETS 4

typedef struct {
    int tcp_sock;
    struct TLSContext *ctx;
    int connected;
    int closed;
} tls_socket_internal_t;

static tls_socket_internal_t tls_sockets[MAX_TLS_SOCKETS];
static int tls_initialized = 0;

void tls_init_lib(void) {
    if (!tls_initialized) {
        tls_init();
        memset(tls_sockets, 0, sizeof(tls_sockets));
        tls_initialized = 1;
        uart_puts("TLS: Initialized\r\n");
        // Test if DEBUG_PRINT works
        DEBUG_PRINT("DEBUG TEST: If you see this, DEBUG_PRINT works!\n");
    }
}

int tls_connect(uint32_t ip, uint16_t port, const char *hostname) {
    if (!tls_initialized) tls_init_lib();

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_TLS_SOCKETS; i++) {
        if (tls_sockets[i].ctx == NULL) { slot = i; break; }
    }
    if (slot < 0) return -1;

    // TCP connect
    int tcp = tcp_connect(ip, port);
    if (tcp < 0) return -1;

    // Create TLS context
    struct TLSContext *ctx = tls_create_context(0, TLS_V12);
    if (!ctx) { tcp_close(tcp); return -1; }

    if (hostname && hostname[0]) {
        tls_sni_set(ctx, hostname);
    }

    tls_sockets[slot].tcp_sock = tcp;
    tls_sockets[slot].ctx = ctx;
    tls_sockets[slot].connected = 0;
    tls_sockets[slot].closed = 0;

    // Start handshake
    tls_client_connect(ctx);

    // Send ClientHello
    unsigned int out_len = 0;
    const unsigned char *out_buf = tls_get_write_buffer(ctx, &out_len);
    if (out_buf && out_len > 0) {
        uart_puts("[TLS] Sending ClientHello (");
        // Print length
        char lenbuf[16];
        int i = 0;
        unsigned int tmp = out_len;
        if (tmp == 0) lenbuf[i++] = '0';
        else {
            char rev[16];
            int j = 0;
            while (tmp > 0) { rev[j++] = '0' + (tmp % 10); tmp /= 10; }
            while (j > 0) lenbuf[i++] = rev[--j];
        }
        lenbuf[i] = 0;
        uart_puts(lenbuf);
        uart_puts(" bytes)\r\n");

        int sent = tcp_send(tcp, out_buf, out_len);
        if (sent < 0) {
            uart_puts("[TLS] Failed to send ClientHello!\r\n");
        }
        tls_buffer_clear(ctx);
    } else {
        uart_puts("[TLS] No ClientHello generated!\r\n");
    }

    // Handshake loop - increased timeout for slow connections
    unsigned char recv_buf[4096];
    int max_rounds = 500;  // Increased from 50 to 500 (5 seconds total)
    int empty_rounds = 0;  // Track consecutive empty receives

    uart_puts("[TLS] Starting handshake...\r\n");

    while (!tls_established(ctx) && max_rounds-- > 0) {
        net_poll();

        int recv_len = tcp_recv(tcp, recv_buf, sizeof(recv_buf));
        if (recv_len > 0) {
            empty_rounds = 0;  // Reset empty counter on data
            // Print received length
            uart_puts("[TLS] Got ");
            char lbuf[16];
            int v = recv_len; int i = 0;
            if (v == 0) lbuf[i++] = '0';
            else { char r[16]; int j=0; while(v>0){r[j++]='0'+(v%10);v/=10;} while(j>0)lbuf[i++]=r[--j]; }
            lbuf[i] = 0;
            uart_puts(lbuf);
            uart_puts(" bytes, consuming...\r\n");

            // Dump first 16 bytes of received data
            uart_puts("[TLS] First 16 bytes: ");
            for (int d = 0; d < 16 && d < recv_len; d++) {
                char hex[4];
                hex[0] = "0123456789ABCDEF"[(recv_buf[d] >> 4) & 0xF];
                hex[1] = "0123456789ABCDEF"[recv_buf[d] & 0xF];
                hex[2] = ' ';
                hex[3] = 0;
                uart_puts(hex);
            }
            uart_puts("\r\n");

            int consumed = tls_consume_stream(ctx, recv_buf, recv_len, NULL);

            // Print consume result and error code if any
            if (consumed < 0) {
                uart_puts("[TLS] ERROR! critical_error=");
                char eb[8];
                eb[0] = '0' + (ctx->critical_error % 10);
                eb[1] = 0;
                uart_puts(eb);
                uart_puts(" error_code=");
                int ec = ctx->error_code;
                eb[0] = '0' + (ec / 100) % 10;
                eb[1] = '0' + (ec / 10) % 10;
                eb[2] = '0' + ec % 10;
                eb[3] = 0;
                uart_puts(eb);
                uart_puts(" conn_status=");
                eb[0] = '0' + (ctx->connection_status / 100) % 10;
                eb[1] = '0' + (ctx->connection_status / 10) % 10;
                eb[2] = '0' + ctx->connection_status % 10;
                eb[3] = 0;
                uart_puts(eb);
                uart_puts("\r\n");
            }
            uart_puts("[TLS] consume returned: ");
            v = consumed < 0 ? -consumed : consumed; i = 0;
            if (v == 0) lbuf[i++] = '0';
            else { char r[16]; int j=0; while(v>0){r[j++]='0'+(v%10);v/=10;} while(j>0)lbuf[i++]=r[--j]; }
            lbuf[i] = 0;
            if (consumed < 0) uart_puts("-");
            uart_puts(lbuf);
            uart_puts("\r\n");

            if (consumed < 0) {
                tls_destroy_context(ctx);
                tcp_close(tcp);
                tls_sockets[slot].ctx = NULL;
                return -1;
            }

            out_buf = tls_get_write_buffer(ctx, &out_len);
            if (out_buf && out_len > 0) {
                uart_puts("[TLS] Sending handshake response...\r\n");
                tcp_send(tcp, out_buf, out_len);
                tls_buffer_clear(ctx);
            }

            // Print handshake state
            if (tls_established(ctx)) {
                uart_puts("[TLS] Connection established!\r\n");
            }
        } else if (recv_len < 0) {
            uart_puts("[TLS] TCP recv failed during handshake\r\n");
            tls_destroy_context(ctx);
            tcp_close(tcp);
            tls_sockets[slot].ctx = NULL;
            return -1;
        } else {
            // No data received
            empty_rounds++;
            // If we've had too many empty rounds after sending, the server might not be responding
            if (empty_rounds > 200) {  // 2 seconds with no data
                uart_puts("[TLS] No response from server\r\n");
                break;
            }
        }

        // Small delay - poll network more frequently
        sleep_ms(10);
        net_poll();  // Additional poll
    }

    if (!tls_established(ctx)) {
        uart_puts("[TLS] Handshake timed out\r\n");
        tls_destroy_context(ctx);
        tcp_close(tcp);
        tls_sockets[slot].ctx = NULL;
        return -1;
    }

    uart_puts("[TLS] Handshake complete!\r\n");

    tls_sockets[slot].connected = 1;
    return slot;
}

int tls_send(int sock, const void *data, uint32_t len) {
    if (sock < 0 || sock >= MAX_TLS_SOCKETS) return -1;
    tls_socket_internal_t *s = &tls_sockets[sock];
    if (!s->ctx || !s->connected || s->closed) return -1;

    tls_write(s->ctx, data, len);

    unsigned int out_len = 0;
    const unsigned char *out_buf = tls_get_write_buffer(s->ctx, &out_len);
    if (out_buf && out_len > 0) {
        int sent = tcp_send(s->tcp_sock, out_buf, out_len);
        tls_buffer_clear(s->ctx);
        if (sent < 0) return -1;
    }

    return len;
}

int tls_recv(int sock, void *buf, uint32_t maxlen) {
    if (sock < 0 || sock >= MAX_TLS_SOCKETS) return -1;
    tls_socket_internal_t *s = &tls_sockets[sock];
    if (!s->ctx || s->closed) return -1;

    // Check for buffered data
    int decrypted = tls_read(s->ctx, buf, maxlen);
    if (decrypted > 0) return decrypted;

    net_poll();

    unsigned char recv_buf[4096];
    int recv_len = tcp_recv(s->tcp_sock, recv_buf, sizeof(recv_buf));

    if (recv_len > 0) {
        int consumed = tls_consume_stream(s->ctx, recv_buf, recv_len, NULL);
        if (consumed < 0) { s->closed = 1; return -1; }

        unsigned int out_len = 0;
        const unsigned char *out_buf = tls_get_write_buffer(s->ctx, &out_len);
        if (out_buf && out_len > 0) {
            tcp_send(s->tcp_sock, out_buf, out_len);
            tls_buffer_clear(s->ctx);
        }

        decrypted = tls_read(s->ctx, buf, maxlen);
        if (decrypted > 0) return decrypted;
    } else if (recv_len < 0) {
        s->closed = 1;
        return -1;
    }

    return 0;
}

void tls_close(int sock) {
    if (sock < 0 || sock >= MAX_TLS_SOCKETS) return;
    tls_socket_internal_t *s = &tls_sockets[sock];
    if (!s->ctx) return;

    if (s->connected && !s->closed) {
        tls_close_notify(s->ctx);
        unsigned int out_len = 0;
        const unsigned char *out_buf = tls_get_write_buffer(s->ctx, &out_len);
        if (out_buf && out_len > 0) {
            tcp_send(s->tcp_sock, out_buf, out_len);
            tls_buffer_clear(s->ctx);
        }
    }

    tcp_close(s->tcp_sock);
    tls_destroy_context(s->ctx);
    memset(s, 0, sizeof(*s));
}

int tls_is_connected(int sock) {
    if (sock < 0 || sock >= MAX_TLS_SOCKETS) return 0;
    tls_socket_internal_t *s = &tls_sockets[sock];
    return s->ctx && s->connected && !s->closed && tcp_is_connected(s->tcp_sock);
}
