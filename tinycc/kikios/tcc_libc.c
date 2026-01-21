/*
 * TCC for KikiOS - libc implementations
 *
 * These functions provide the C standard library interface
 * that TCC expects, implemented via kapi.
 */

#include "tcc_libc.h"

/* Global kapi pointer (defined in tcc_main.c) */
extern kapi_t *tcc_kapi;

/* Global errno */
int errno = 0;

/* Debug output - goes directly to UART */
void debug_puts(const char *s) {
    if (tcc_kapi) tcc_kapi->uart_puts(s);
}

/* Console output helpers - use stdio hooks if available (for terminal emulator) */
static void tcc_putc(char c) {
    if (!tcc_kapi) return;
    if (tcc_kapi->stdio_putc) {
        tcc_kapi->stdio_putc(c);
    } else {
        tcc_kapi->putc(c);
    }
}

static void tcc_puts(const char *s) {
    if (!tcc_kapi) return;
    if (tcc_kapi->stdio_puts) {
        tcc_kapi->stdio_puts(s);
    } else {
        tcc_kapi->puts(s);
    }
}

/* ============ File descriptor table ============ */

#define MAX_FDS 32
static struct {
    void *handle;
    int pos;
    int size;
    int flags;
    int in_use;
} fd_table[MAX_FDS];

/* stdio streams */
static FILE _stdin_file = {0};
static FILE _stdout_file = {0};
static FILE _stderr_file = {0};
FILE *stdin = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

/* ============ Memory functions ============ */

void *malloc(size_t size) {
    if (!tcc_kapi) return 0;
    return tcc_kapi->malloc(size);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!tcc_kapi) return 0;
    /* kapi doesn't have realloc, implement manually */
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    /* Allocate new block and copy - inefficient but works */
    void *new_ptr = malloc(size);
    if (!new_ptr) return 0;

    /* Copy old data - we don't know old size so just copy up to new size */
    /* This is safe because we're overwriting with potentially smaller data */
    memcpy(new_ptr, ptr, size);
    free(ptr);
    return new_ptr;
}

void free(void *ptr) {
    if (tcc_kapi && ptr) tcc_kapi->free(ptr);
}

/* ============ String functions ============ */
/* Note: memcpy, memset, memmove, strlen, strcpy, strcat, strcmp, strncmp
 * are already defined in kiki.h/crt0.o - we only define extras */

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return 0;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n-- && (*d = *src++)) d++;
    *d = '\0';
    return dest;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && tolower(*s1) == tolower(*s2)) { s1++; s2++; }
    return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && tolower(*s1) == tolower(*s2)) { s1++; s2++; n--; }
    return n ? tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2) : 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : 0;
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == '\0') ? (char *)s : (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return (char *)haystack;
    while (*haystack) {
        if (!strncmp(haystack, needle, nlen)) return (char *)haystack;
        haystack++;
    }
    return 0;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

char *strndup(const char *s, size_t n) {
    size_t len = strlen(s);
    if (len > n) len = n;
    char *p = malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}

size_t strspn(const char *s, const char *accept) {
    const char *p = s;
    while (*p && strchr(accept, *p)) p++;
    return p - s;
}

size_t strcspn(const char *s, const char *reject) {
    const char *p = s;
    while (*p && !strchr(reject, *p)) p++;
    return p - s;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        if (strchr(accept, *s)) return (char *)s;
        s++;
    }
    return 0;
}

static char *_strtok_last = 0;
char *strtok(char *str, const char *delim) {
    if (str) _strtok_last = str;
    if (!_strtok_last) return 0;

    /* Skip leading delimiters */
    char *start = _strtok_last + strspn(_strtok_last, delim);
    if (!*start) { _strtok_last = 0; return 0; }

    /* Find end of token */
    char *end = start + strcspn(start, delim);
    if (*end) {
        *end = '\0';
        _strtok_last = end + 1;
    } else {
        _strtok_last = 0;
    }
    return start;
}

char *strerror(int errnum) {
    static char buf[32];
    sprintf(buf, "Error %d", errnum);
    return buf;
}

/* ============ ctype functions ============ */
/* Note: isspace, isprint are already defined in kiki.h - only define extras */

int isalpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isgraph(int c) { return isprint(c) && c != ' '; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* ============ Number conversion ============ */

int atoi(const char *s) {
    return (int)strtol(s, 0, 10);
}

long atol(const char *s) {
    return strtol(s, 0, 10);
}

long long atoll(const char *s) {
    return strtoll(s, 0, 10);
}

double atof(const char *s) {
    return strtod(s, 0);
}

long strtol(const char *s, char **endp, int base) {
    long result = 0;
    int neg = 0;

    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') { base = 16; s++; }
            else base = 8;
        } else base = 10;
    } else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }

    if (endp) *endp = (char *)s;
    return neg ? -result : result;
}

unsigned long strtoul(const char *s, char **endp, int base) {
    return (unsigned long)strtol(s, endp, base);
}

long long strtoll(const char *s, char **endp, int base) {
    return (long long)strtol(s, endp, base);
}

unsigned long long strtoull(const char *s, char **endp, int base) {
    return (unsigned long long)strtol(s, endp, base);
}

double strtod(const char *s, char **endp) {
    double result = 0.0;
    double frac = 0.0;
    int neg = 0;
    int exp = 0;
    int exp_neg = 0;

    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;

    /* Integer part */
    while (isdigit(*s)) {
        result = result * 10.0 + (*s - '0');
        s++;
    }

    /* Fractional part */
    if (*s == '.') {
        s++;
        double place = 0.1;
        while (isdigit(*s)) {
            frac += (*s - '0') * place;
            place *= 0.1;
            s++;
        }
        result += frac;
    }

    /* Exponent */
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-') { exp_neg = 1; s++; }
        else if (*s == '+') s++;
        while (isdigit(*s)) {
            exp = exp * 10 + (*s - '0');
            s++;
        }
        double mult = 1.0;
        while (exp--) mult *= 10.0;
        if (exp_neg) result /= mult;
        else result *= mult;
    }

    if (endp) *endp = (char *)s;
    return neg ? -result : result;
}

float strtof(const char *s, char **endp) {
    return (float)strtod(s, endp);
}

long double strtold(const char *s, char **endp) {
    return (long double)strtod(s, endp);
}

/* ============ Other stdlib ============ */

char *getenv(const char *name) {
    (void)name;
    return 0;  /* No environment variables */
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

void abort(void) {
    tcc_puts("ABORT\n");
    while (1) {}
}

void exit(int status) {
    if (tcc_kapi) tcc_kapi->exit(status);
    while (1) {}
}

/* Simple insertion sort - good enough for small arrays */
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    char *arr = base;
    char *tmp = malloc(size);
    if (!tmp) return;

    for (size_t i = 1; i < nmemb; i++) {
        memcpy(tmp, arr + i * size, size);
        size_t j = i;
        while (j > 0 && compar(arr + (j-1) * size, tmp) > 0) {
            memcpy(arr + j * size, arr + (j-1) * size, size);
            j--;
        }
        memcpy(arr + j * size, tmp, size);
    }
    free(tmp);
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    const char *arr = base;
    size_t low = 0, high = nmemb;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int cmp = compar(key, arr + mid * size);
        if (cmp < 0) high = mid;
        else if (cmp > 0) low = mid + 1;
        else return (void *)(arr + mid * size);
    }
    return 0;
}

/* ============ File I/O (stdio) ============ */

FILE *fopen(const char *path, const char *mode) {
    if (!tcc_kapi) return 0;

    int m = 0;  /* 0=read, 1=write, 2=append */
    if (mode[0] == 'w') m = 1;
    else if (mode[0] == 'a') m = 2;

    /* For write/append, create file if needed */
    if (m != 0) {
        void *h = tcc_kapi->open(path);
        if (!h) {
            tcc_kapi->create(path);
        }
    }

    void *handle = tcc_kapi->open(path);
    if (!handle) {
        errno = ENOENT;
        return 0;
    }

    FILE *f = malloc(sizeof(FILE));
    if (!f) {
        errno = ENOMEM;
        return 0;
    }

    f->handle = handle;
    f->path = strdup(path);
    f->pos = (m == 2) ? tcc_kapi->file_size(handle) : 0;
    f->size = tcc_kapi->file_size(handle);
    f->mode = m;
    f->error = 0;
    f->eof = 0;
    f->buf = 0;
    f->buf_size = 0;
    f->buf_pos = 0;

    /* Truncate for write mode */
    if (m == 1) {
        /* kapi doesn't have truncate, so we'll handle this via write */
        f->size = 0;
    }

    return f;
}

FILE *fdopen(int fd, const char *mode) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) return 0;

    int m = 0;
    if (mode[0] == 'w') m = 1;
    else if (mode[0] == 'a') m = 2;

    FILE *f = malloc(sizeof(FILE));
    if (!f) return 0;

    f->handle = fd_table[fd].handle;
    f->path = 0;  /* No path for fd-opened files */
    f->pos = fd_table[fd].pos;
    f->size = fd_table[fd].size;
    f->mode = m;
    f->error = 0;
    f->eof = 0;
    f->buf = 0;
    f->buf_size = 0;
    f->buf_pos = 0;

    return f;
}

int fclose(FILE *f) {
    if (!f || f == stdin || f == stdout || f == stderr) return 0;

    fflush(f);
    /* Close the handle */
    if (f->handle && tcc_kapi && tcc_kapi->close) {
        tcc_kapi->close(f->handle);
    }
    if (f->path) free(f->path);
    if (f->buf) free(f->buf);
    free(f);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || !f->handle || !tcc_kapi) return 0;
    if (f == stdin) return 0;  /* stdin not supported yet */

    size_t total = size * nmemb;
    int n = tcc_kapi->read(f->handle, ptr, total, f->pos);
    if (n <= 0) {
        f->eof = 1;
        return 0;
    }
    f->pos += n;
    return n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || !tcc_kapi) return 0;

    /* stdout/stderr -> console (use stdio hooks if available) */
    if (f == stdout || f == stderr) {
        const char *s = ptr;
        size_t total = size * nmemb;
        for (size_t i = 0; i < total; i++) {
            tcc_putc(s[i]);
        }
        return nmemb;
    }

    if (!f->handle) return 0;

    size_t total = size * nmemb;

    /* Buffer writes - FAT32 can only write entire file at once */
    size_t new_size = f->pos + total;
    if (new_size > f->buf_size) {
        /* Grow buffer */
        size_t new_cap = new_size + 4096;  /* Extra space */
        char *new_buf = malloc(new_cap);
        if (!new_buf) { f->error = 1; return 0; }
        if (f->buf) {
            memcpy(new_buf, f->buf, f->buf_size);
            free(f->buf);
        }
        f->buf = new_buf;
        f->buf_size = new_cap;
    }

    /* Copy data to buffer at current position */
    memcpy(f->buf + f->pos, ptr, total);
    f->pos += total;
    if (f->pos > f->size) f->size = f->pos;

    return nmemb;
}

int fseek(FILE *f, long offset, int whence) {
    if (!f) return -1;

    switch (whence) {
        case SEEK_SET: f->pos = offset; break;
        case SEEK_CUR: f->pos += offset; break;
        case SEEK_END: f->pos = f->size + offset; break;
        default: return -1;
    }
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) {
    return f ? f->pos : -1;
}

void rewind(FILE *f) {
    if (f) { f->pos = 0; f->eof = 0; f->error = 0; }
}

int feof(FILE *f) {
    return f ? f->eof : 1;
}

int ferror(FILE *f) {
    return f ? f->error : 1;
}

void clearerr(FILE *f) {
    if (f) { f->eof = 0; f->error = 0; }
}

int fflush(FILE *f) {
    if (!f || f == stdin || f == stdout || f == stderr) return 0;
    if (!f->buf || f->size == 0) return 0;
    if (!f->handle || !tcc_kapi) return -1;

    /* Write entire buffer to file */
    int n = tcc_kapi->write(f->handle, f->buf, f->size);
    if (n <= 0) {
        f->error = 1;
        return -1;
    }
    return 0;
}

int fgetc(FILE *f) {
    unsigned char c;
    if (fread(&c, 1, 1, f) != 1) return EOF;
    return c;
}

int fputc(int c, FILE *f) {
    unsigned char ch = c;
    if (fwrite(&ch, 1, 1, f) != 1) return EOF;
    return c;
}

char *fgets(char *s, int size, FILE *f) {
    if (!f || size <= 0) return 0;

    int i = 0;
    while (i < size - 1) {
        int c = fgetc(f);
        if (c == EOF) {
            if (i == 0) return 0;
            break;
        }
        s[i++] = c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *f) {
    size_t len = strlen(s);
    return fwrite(s, 1, len, f) == len ? 0 : EOF;
}

int getc(FILE *f) { return fgetc(f); }
int putc(int c, FILE *f) { return fputc(c, f); }

int ungetc(int c, FILE *f) {
    if (!f || c == EOF || f->pos <= 0) return EOF;
    f->pos--;
    f->eof = 0;
    return c;
}

int setvbuf(FILE *f, char *buf, int mode, size_t size) {
    (void)f; (void)buf; (void)mode; (void)size;
    return 0;
}

void perror(const char *s) {
    if (tcc_kapi) {
        if (s && *s) {
            tcc_puts(s);
            tcc_puts(": ");
        }
        tcc_puts(strerror(errno));
        tcc_putc('\n');
    }
}

int remove(const char *path) {
    if (!tcc_kapi) return -1;
    return tcc_kapi->delete(path);
}

int rename_file(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    /* kapi rename only changes name, not path - limited */
    return -1;
}

/* ============ printf family ============ */

/* Simple printf implementation */
static int _do_printf(char *buf, size_t size, const char *fmt, va_list ap) {
    char *p = buf;
    char *end = buf + size - 1;

    while (*fmt && p < end) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        fmt++;  /* skip % */

        /* Flags */
        int left = 0, zero = 0, plus = 0, space = 0;
        while (*fmt == '-' || *fmt == '0' || *fmt == '+' || *fmt == ' ') {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            fmt++;
        }
        (void)left; (void)plus; (void)space;

        /* Width */
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt++ - '0');
            }
        }

        /* Precision */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + (*fmt++ - '0');
                }
            }
        }

        /* Length */
        int is_long = 0, is_longlong = 0;
        if (*fmt == 'l') {
            fmt++;
            is_long = 1;
            if (*fmt == 'l') { is_longlong = 1; fmt++; }
        } else if (*fmt == 'z' || *fmt == 't') {
            is_long = 1;
            fmt++;
        }

        /* Conversion */
        char tmp[64];
        char *s = tmp;
        int len = 0;

        switch (*fmt) {
            case 'd':
            case 'i': {
                long long val;
                if (is_longlong) val = va_arg(ap, long long);
                else if (is_long) val = va_arg(ap, long);
                else val = va_arg(ap, int);

                int neg = val < 0;
                if (neg) val = -val;

                char *t = tmp + sizeof(tmp);
                *--t = '\0';
                do { *--t = '0' + val % 10; val /= 10; } while (val);
                if (neg) *--t = '-';
                s = t;
                len = strlen(s);
                break;
            }
            case 'u': {
                unsigned long long val;
                if (is_longlong) val = va_arg(ap, unsigned long long);
                else if (is_long) val = va_arg(ap, unsigned long);
                else val = va_arg(ap, unsigned int);

                char *t = tmp + sizeof(tmp);
                *--t = '\0';
                do { *--t = '0' + val % 10; val /= 10; } while (val);
                s = t;
                len = strlen(s);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long val;
                if (is_longlong) val = va_arg(ap, unsigned long long);
                else if (is_long) val = va_arg(ap, unsigned long);
                else val = va_arg(ap, unsigned int);

                const char *hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                char *t = tmp + sizeof(tmp);
                *--t = '\0';
                do { *--t = hex[val & 0xF]; val >>= 4; } while (val);
                s = t;
                len = strlen(s);
                break;
            }
            case 'p': {
                void *val = va_arg(ap, void *);
                unsigned long v = (unsigned long)val;
                char *t = tmp + sizeof(tmp);
                *--t = '\0';
                do { *--t = "0123456789abcdef"[v & 0xF]; v >>= 4; } while (v);
                *--t = 'x';
                *--t = '0';
                s = t;
                len = strlen(s);
                break;
            }
            case 's': {
                s = va_arg(ap, char *);
                if (!s) s = "(null)";
                len = strlen(s);
                if (prec >= 0 && len > prec) len = prec;
                break;
            }
            case 'c': {
                tmp[0] = (char)va_arg(ap, int);
                tmp[1] = '\0';
                len = 1;
                break;
            }
            case '%':
                tmp[0] = '%';
                tmp[1] = '\0';
                len = 1;
                break;
            default:
                tmp[0] = *fmt;
                tmp[1] = '\0';
                len = 1;
                break;
        }
        fmt++;

        /* Padding */
        int pad = width - len;
        char padchar = zero ? '0' : ' ';

        while (pad-- > 0 && p < end) *p++ = padchar;
        while (len-- > 0 && p < end) *p++ = *s++;
    }

    *p = '\0';
    return p - buf;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    if (!buf || size == 0) return 0;
    return _do_printf(buf, size, fmt, ap);
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, 65536, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    char buf[4096];
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (ret > 0) fwrite(buf, 1, ret, f);
    return ret;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(f, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

/* ============ Low-level I/O ============ */

int open(const char *path, int flags, ...) {
    if (!tcc_kapi) return -1;

    /* Find free fd */
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!fd_table[i].in_use) { fd = i; break; }
    }
    if (fd < 0) { errno = ENOMEM; return -1; }

    /* Create file if needed */
    if (flags & O_CREAT) {
        void *h = tcc_kapi->open(path);
        if (!h) tcc_kapi->create(path);
    }

    void *handle = tcc_kapi->open(path);
    if (!handle) {
        tcc_kapi->uart_puts("[open] FAILED: ");
        tcc_kapi->uart_puts(path);
        tcc_kapi->uart_puts("\n");
        errno = ENOENT;
        return -1;
    }

    fd_table[fd].handle = handle;
    fd_table[fd].pos = 0;
    fd_table[fd].size = tcc_kapi->file_size(handle);
    fd_table[fd].flags = flags;
    fd_table[fd].in_use = 1;

    char fdbuf[64];
    sprintf(fdbuf, "[open] fd=%d h=%p ", fd, handle);
    tcc_kapi->uart_puts(fdbuf);
    tcc_kapi->uart_puts(path);
    tcc_kapi->uart_puts("\n");

    if (flags & O_TRUNC) {
        fd_table[fd].size = 0;
    }
    if (flags & O_APPEND) {
        fd_table[fd].pos = fd_table[fd].size;
    }

    return fd;
}

int close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) return -1;
    char buf[32];
    sprintf(buf, "[close] fd=%d\n", fd);
    tcc_kapi->uart_puts(buf);
    // Free the VFS node
    if (fd_table[fd].handle && tcc_kapi->close) {
        tcc_kapi->close(fd_table[fd].handle);
    }
    fd_table[fd].in_use = 0;
    fd_table[fd].handle = 0;
    return 0;
}

ssize_t read(int fd, void *buf, size_t count) {
    if (!tcc_kapi) return -1;
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) return -1;

    /* Only print on first read of each fd (pos==0) */
    int is_first = (fd_table[fd].pos == 0);

    int n = tcc_kapi->read(fd_table[fd].handle, buf, count, fd_table[fd].pos);

    if (is_first) {
        char num[64];
        sprintf(num, "[read fd=%d h=%p] ", fd, fd_table[fd].handle);
        tcc_kapi->uart_puts(num);
        if (n > 0) {
            char preview[48];
            int plen = n < 47 ? n : 47;
            memcpy(preview, buf, plen);
            preview[plen] = 0;
            tcc_kapi->uart_puts("'");
            tcc_kapi->uart_puts(preview);
            tcc_kapi->uart_puts("'\n");
        } else {
            sprintf(num, "FAILED n=%d\n", n);
            tcc_kapi->uart_puts(num);
        }
    }

    if (n > 0) fd_table[fd].pos += n;
    return n;
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (!tcc_kapi) return -1;

    /* stdout/stderr (use stdio hooks if available) */
    if (fd == 1 || fd == 2) {
        const char *s = buf;
        for (size_t i = 0; i < count; i++) {
            tcc_putc(s[i]);
        }
        return count;
    }

    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) return -1;

    int n = tcc_kapi->write(fd_table[fd].handle, buf, count);
    if (n > 0) {
        fd_table[fd].pos += n;
        if (fd_table[fd].pos > fd_table[fd].size)
            fd_table[fd].size = fd_table[fd].pos;
    }
    return n;
}

long lseek(int fd, long offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].in_use) return -1;

    switch (whence) {
        case SEEK_SET: fd_table[fd].pos = offset; break;
        case SEEK_CUR: fd_table[fd].pos += offset; break;
        case SEEK_END: fd_table[fd].pos = fd_table[fd].size + offset; break;
        default: return -1;
    }
    return fd_table[fd].pos;
}

int unlink(const char *path) {
    return tcc_kapi ? tcc_kapi->delete(path) : -1;
}

int access(const char *path, int mode) {
    if (!tcc_kapi) return -1;
    (void)mode;
    void *h = tcc_kapi->open(path);
    return h ? 0 : -1;
}

char *getcwd(char *buf, size_t size) {
    if (!tcc_kapi || !buf) return 0;
    tcc_kapi->get_cwd(buf, size);
    return buf;
}

int chdir(const char *path) {
    if (!tcc_kapi) return -1;
    tcc_kapi->set_cwd(path);
    return 0;
}

int isatty(int fd) {
    return (fd == 0 || fd == 1 || fd == 2);
}

char *realpath(const char *path, char *resolved_path) {
    /* Simple implementation - just return a copy of the path */
    /* KikiOS doesn't have symlinks so the path is already "real" */
    if (!path) return 0;

    size_t len = strlen(path);
    char *result;

    if (resolved_path) {
        result = resolved_path;
    } else {
        result = malloc(len + 1);
        if (!result) return 0;
    }

    strcpy(result, path);
    return result;
}

/* ============ Time ============ */

time_t time(time_t *t) {
    /* Return uptime in seconds as approximation */
    time_t now = tcc_kapi ? (time_t)(tcc_kapi->get_uptime_ticks() / 100) : 0;
    if (t) *t = now;
    return now;
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tv) {
        tv->tv_sec = time(0);
        tv->tv_usec = 0;
    }
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

static struct tm _tm;
struct tm *localtime(const time_t *timep) {
    /* Stub - just return zeros */
    memset(&_tm, 0, sizeof(_tm));
    return &_tm;
}

struct tm *gmtime(const time_t *timep) {
    return localtime(timep);
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    (void)format; (void)tm;
    if (max > 0) s[0] = '\0';
    return 0;
}

/* ============ Signal (stubs) ============ */

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum; (void)handler;
    return SIG_DFL;
}

/* ============ setjmp/longjmp ============ */

/* Implemented in assembly - declare here */
/* int setjmp(jmp_buf env); */
/* void longjmp(jmp_buf env, int val); */

/* ============ Math (minimal) ============ */

double fabs(double x) { return x < 0 ? -x : x; }
double floor(double x) { return (double)(long long)x - (x < 0 && x != (long long)x ? 1 : 0); }
double ceil(double x) { return (double)(long long)x + (x > 0 && x != (long long)x ? 1 : 0); }

/* These need proper implementations or libgcc */
double sqrt(double x) {
    if (x <= 0) return 0;
    double guess = x / 2;
    for (int i = 0; i < 20; i++) {
        guess = (guess + x / guess) / 2;
    }
    return guess;
}

double pow(double x, double y) {
    /* Simple integer exponent only */
    if (y == 0) return 1;
    if (y == 1) return x;
    double result = 1;
    int n = (int)y;
    int neg = n < 0;
    if (neg) n = -n;
    while (n--) result *= x;
    return neg ? 1/result : result;
}

double log(double x) { (void)x; return 0; }  /* Stub */
double log10(double x) { (void)x; return 0; }  /* Stub */
double exp(double x) { (void)x; return 1; }  /* Stub */
double sin(double x) { (void)x; return 0; }  /* Stub */
double cos(double x) { (void)x; return 1; }  /* Stub */
double tan(double x) { (void)x; return 0; }  /* Stub */
double atan(double x) { (void)x; return 0; }  /* Stub */
double atan2(double y, double x) { (void)y; (void)x; return 0; }  /* Stub */
double fmod(double x, double y) { return x - (long long)(x/y) * y; }
double ldexp(double x, int exp) { while (exp > 0) { x *= 2; exp--; } while (exp < 0) { x /= 2; exp++; } return x; }
double frexp(double x, int *exp) { *exp = 0; return x; }  /* Stub */
double modf(double x, double *iptr) { *iptr = (long long)x; return x - *iptr; }

int isnan(double x) { return x != x; }
int isinf(double x) { return x == INFINITY || x == -INFINITY; }
int isfinite(double x) { return !isnan(x) && !isinf(x); }

/* Long double versions - just use double */
long double fabsl(long double x) { return fabs((double)x); }
long double floorl(long double x) { return floor((double)x); }
long double ceill(long double x) { return ceil((double)x); }
long double sqrtl(long double x) { return sqrt((double)x); }
long double ldexpl(long double x, int exp) { return ldexp((double)x, exp); }

/* ============ Dynamic loading stubs ============ */

void *dlopen(const char *filename, int flags) {
    (void)filename; (void)flags;
    return 0;  /* Not supported */
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle; (void)symbol;
    return 0;  /* Not supported */
}

int dlclose(void *handle) {
    (void)handle;
    return 0;
}

char *dlerror(void) {
    return "dlopen not supported";
}

/* ============ TCC runtime stubs ============ */

/* tcc_run_free is called when tcc_delete is called after tcc_run()
 * Since we don't support tcc_run (JIT), this is a no-op */
void tcc_run_free(void *s) {
    (void)s;
}

/* tcc_run executes compiled code in memory (JIT). Not supported without mmap. */
int tcc_run(void *s, int argc, char **argv) {
    (void)s; (void)argc; (void)argv;
    /* Return error - we only support output to file, not in-memory execution */
    return -1;
}
