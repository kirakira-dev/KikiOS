/*
 * KikiOS fetch command - HTTP/HTTPS client
 *
 * Usage: fetch <url>
 * Example: fetch http://example.com/
 *          fetch https://google.com/
 *
 * Features:
 * - HTTP and HTTPS support
 * - Follows redirects (301, 302, 307, 308)
 * - Parses HTTP headers
 * - Shows response info
 */

#include "../lib/kiki.h"

static kapi_t *k;

// Output helpers
static void out_puts(const char *s) {
    if (k->stdio_puts) k->stdio_puts(s);
    else k->puts(s);
}

static void out_putc(char c) {
    if (k->stdio_putc) k->stdio_putc(c);
    else k->putc(c);
}

static void out_num(int n) {
    if (n < 0) { out_putc('-'); n = -n; }
    if (n == 0) { out_putc('0'); return; }
    char buf[12];
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) out_putc(buf[--i]);
}

// String helpers
static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_eqn(const char *a, const char *b, int n) {
    while (n > 0 && *a && *b && *a == *b) { a++; b++; n--; }
    return n == 0;
}

static int str_ieqn(const char *a, const char *b, int n) {
    // Case-insensitive compare
    while (n > 0 && *a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++; n--;
    }
    return n == 0;
}

static void str_cpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void str_ncpy(char *dst, const char *src, int n) {
    while (n > 0 && *src) { *dst++ = *src++; n--; }
    *dst = '\0';
}

static int parse_int(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

// URL parsing
typedef struct {
    char host[256];
    char path[512];
    int port;
    int use_tls;  // 1 for https, 0 for http
} url_t;

static int parse_url(const char *url, url_t *out) {
    out->use_tls = 0;
    out->port = 80;

    // Check for https://
    if (str_eqn(url, "https://", 8)) {
        url += 8;
        out->use_tls = 1;
        out->port = 443;
    } else if (str_eqn(url, "http://", 7)) {
        url += 7;
    }

    // Find host end (/ or :)
    const char *host_start = url;
    const char *host_end = url;
    while (*host_end && *host_end != '/' && *host_end != ':') host_end++;

    int host_len = host_end - host_start;
    if (host_len >= 256) return -1;
    str_ncpy(out->host, host_start, host_len);

    // Parse port if present
    if (*host_end == ':') {
        host_end++;
        out->port = parse_int(host_end);
        while (*host_end >= '0' && *host_end <= '9') host_end++;
    }

    // Path (default to /)
    if (*host_end == '/') {
        str_cpy(out->path, host_end);
    } else {
        str_cpy(out->path, "/");
    }

    return 0;
}

// HTTP response
typedef struct {
    int status_code;
    int content_length;
    char location[512];
    char content_type[128];
    int header_len;
} http_response_t;

// Find \r\n\r\n (end of headers)
static int find_header_end(const char *buf, int len) {
    for (int i = 0; i < len - 3; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return i + 4;
        }
    }
    return -1;
}

// Parse HTTP response headers
static int parse_headers(const char *buf, int len, http_response_t *resp) {
    memset(resp, 0, sizeof(*resp));
    resp->content_length = -1;

    // Find end of headers
    resp->header_len = find_header_end(buf, len);
    if (resp->header_len < 0) return -1;

    // Parse status line: HTTP/1.x NNN ...
    const char *p = buf;
    if (!str_eqn(p, "HTTP/1.", 7)) return -1;
    p += 7;
    while (*p && *p != ' ') p++;  // Skip version
    while (*p == ' ') p++;
    resp->status_code = parse_int(p);

    // Parse headers (line by line)
    while (*p && *p != '\r') p++;
    if (*p == '\r') p += 2;  // Skip status line

    while (p < buf + resp->header_len - 2) {
        // Find end of line
        const char *line_end = p;
        while (line_end < buf + resp->header_len && *line_end != '\r') line_end++;
        int line_len = line_end - p;

        // Parse header
        if (str_ieqn(p, "Content-Length:", 15)) {
            const char *val = p + 15;
            while (*val == ' ') val++;
            resp->content_length = parse_int(val);
        } else if (str_ieqn(p, "Location:", 9)) {
            const char *val = p + 9;
            while (*val == ' ') val++;
            int loc_len = line_end - val;
            if (loc_len >= 512) loc_len = 511;
            str_ncpy(resp->location, val, loc_len);
        } else if (str_ieqn(p, "Content-Type:", 13)) {
            const char *val = p + 13;
            while (*val == ' ') val++;
            int ct_len = line_end - val;
            if (ct_len >= 128) ct_len = 127;
            str_ncpy(resp->content_type, val, ct_len);
        }

        // Next line
        p = line_end + 2;
    }

    return 0;
}

// Make HTTP/HTTPS request
static int http_get(url_t *url, char *response, int max_response, http_response_t *resp) {
    // Resolve hostname
    uint32_t ip = k->dns_resolve(url->host);
    if (ip == 0) {
        out_puts("DNS resolution failed\n");
        return -1;
    }

    // Connect (TLS or plain TCP)
    int sock;
    if (url->use_tls) {
        out_puts("Connecting with TLS...\n");
        sock = k->tls_connect(ip, url->port, url->host);
    } else {
        sock = k->tcp_connect(ip, url->port);
    }

    if (sock < 0) {
        out_puts("Connection failed\n");
        return -1;
    }

    // Build request
    char request[1024];
    char *p = request;

    // GET /path HTTP/1.0\r\n
    const char *s = "GET ";
    while (*s) *p++ = *s++;
    s = url->path;
    while (*s) *p++ = *s++;
    s = " HTTP/1.0\r\n";
    while (*s) *p++ = *s++;

    // Host: hostname\r\n
    s = "Host: ";
    while (*s) *p++ = *s++;
    s = url->host;
    while (*s) *p++ = *s++;
    *p++ = '\r'; *p++ = '\n';

    // User-Agent
    s = "User-Agent: Mozilla/5.0 (compatible; KikiOS)\r\n";
    while (*s) *p++ = *s++;

    // Connection: close\r\n\r\n
    s = "Connection: close\r\n\r\n";
    while (*s) *p++ = *s++;
    *p = '\0';

    // Send request
    int sent;
    if (url->use_tls) {
        sent = k->tls_send(sock, request, p - request);
    } else {
        sent = k->tcp_send(sock, request, p - request);
    }

    if (sent < 0) {
        out_puts("Send failed\n");
        if (url->use_tls) k->tls_close(sock);
        else k->tcp_close(sock);
        return -1;
    }

    // Receive response
    int total = 0;
    int timeout = 0;

    while (total < max_response - 1 && timeout < 500) {
        int n;
        if (url->use_tls) {
            n = k->tls_recv(sock, response + total, max_response - 1 - total);
        } else {
            n = k->tcp_recv(sock, response + total, max_response - 1 - total);
        }

        if (n < 0) break;  // Connection closed
        if (n == 0) {
            k->net_poll();
            k->sleep_ms(10);
            timeout++;
            continue;
        }
        total += n;
        timeout = 0;

        // Check if we got headers yet
        if (resp->header_len == 0) {
            response[total] = '\0';
            parse_headers(response, total, resp);

            // If we have Content-Length and got all content, we're done
            if (resp->header_len > 0 && resp->content_length >= 0) {
                int body_received = total - resp->header_len;
                if (body_received >= resp->content_length) break;
            }
        }
    }

    response[total] = '\0';

    if (url->use_tls) k->tls_close(sock);
    else k->tcp_close(sock);

    // Parse headers if not done yet
    if (resp->header_len == 0) {
        parse_headers(response, total, resp);
    }

    return total;
}

// Check if redirect status
static int is_redirect(int status) {
    return status == 301 || status == 302 || status == 307 || status == 308;
}

int main(kapi_t *kapi, int argc, char **argv) {
    k = kapi;

    if (argc < 2) {
        out_puts("Usage: fetch <url>\n");
        out_puts("Example: fetch http://example.com/\n");
        out_puts("         fetch https://google.com/\n");
        return 1;
    }

    // Parse URL
    url_t url;
    if (parse_url(argv[1], &url) < 0) {
        out_puts("Invalid URL\n");
        return 1;
    }

    // Allocate response buffer
    char *response = k->malloc(65536);
    if (!response) {
        out_puts("Out of memory\n");
        return 1;
    }

    http_response_t resp;
    int redirects = 0;
    const int max_redirects = 5;

    while (1) {
        out_puts("Fetching ");
        out_puts(url.use_tls ? "https://" : "http://");
        out_puts(url.host);
        out_puts(url.path);
        out_puts("\n");

        memset(&resp, 0, sizeof(resp));
        int len = http_get(&url, response, 65536, &resp);
        if (len < 0) {
            k->free(response);
            return 1;
        }

        out_puts("HTTP ");
        out_num(resp.status_code);
        out_puts(" - ");
        out_num(len);
        out_puts(" bytes\n");

        if (resp.content_type[0]) {
            out_puts("Content-Type: ");
            out_puts(resp.content_type);
            out_puts("\n");
        }

        // Handle redirect
        if (is_redirect(resp.status_code) && resp.location[0]) {
            redirects++;
            if (redirects > max_redirects) {
                out_puts("Too many redirects\n");
                k->free(response);
                return 1;
            }

            out_puts("Redirecting to: ");
            out_puts(resp.location);
            out_puts("\n\n");

            // Check if it's a relative URL (starts with /)
            if (resp.location[0] == '/') {
                // Just update path, keep same host and protocol
                str_cpy(url.path, resp.location);
            } else {
                // Parse new URL (might switch http->https)
                if (parse_url(resp.location, &url) < 0) {
                    out_puts("Invalid redirect URL\n");
                    k->free(response);
                    return 1;
                }
            }

            continue;
        }

        // Print body
        out_puts("\n");
        if (resp.header_len > 0 && resp.header_len < len) {
            out_puts(response + resp.header_len);
        }
        out_puts("\n");

        break;
    }

    k->free(response);
    return 0;
}
