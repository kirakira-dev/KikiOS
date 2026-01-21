/*
 * Kuav - KikiOS Archive Utility
 *
 * List and extract ZIP archives.
 * Terminal-based tool.
 *
 * Usage:
 *   kuav list <archive>          - List contents
 *   kuav extract <archive> [dir] - Extract to directory
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

static void print_num(unsigned long n) {
    char buf[20];
    int i = 0;
    if (n == 0) { out_putc('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) out_putc(buf[--i]);
}

// ZIP format constants
#define ZIP_LOCAL_SIG    0x04034B50
#define ZIP_CENTRAL_SIG  0x02014B50
#define ZIP_END_SIG      0x06054B50

// Read 16-bit little-endian
static uint16_t read16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}

// Read 32-bit little-endian
static uint32_t read32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

// Check if file is a ZIP
static int is_zip(void *file) {
    uint8_t sig[4];
    if (api->read(file, (char *)sig, 4, 0) != 4) return 0;
    return read32(sig) == ZIP_LOCAL_SIG;
}

// Find end of central directory
static int find_end_central(void *file, int file_size, uint8_t *end_record) {
    // Search backwards for signature
    int search_start = file_size > 65557 ? file_size - 65557 : 0;
    
    for (int pos = file_size - 22; pos >= search_start; pos--) {
        uint8_t sig[4];
        if (api->read(file, (char *)sig, 4, pos) != 4) continue;
        
        if (read32(sig) == ZIP_END_SIG) {
            if (api->read(file, (char *)end_record, 22, pos) == 22) {
                return pos;
            }
        }
    }
    return -1;
}

// List ZIP contents
static int list_zip(void *file, int file_size) {
    uint8_t end_record[22];
    if (find_end_central(file, file_size, end_record) < 0) {
        out_puts("Error: Cannot find ZIP central directory\n");
        return 1;
    }
    
    uint16_t total_entries = read16(end_record + 10);
    uint32_t central_offset = read32(end_record + 16);
    
    out_puts("  Size       Name\n");
    out_puts("----------  ----\n");
    
    uint32_t offset = central_offset;
    int count = 0;
    
    while (count < total_entries) {
        uint8_t header[46];
        if (api->read(file, (char *)header, 46, offset) != 46) break;
        
        if (read32(header) != ZIP_CENTRAL_SIG) break;
        
        uint32_t uncompressed = read32(header + 24);
        uint16_t name_len = read16(header + 28);
        uint16_t extra_len = read16(header + 30);
        uint16_t comment_len = read16(header + 32);
        
        // Read filename
        char filename[256];
        int read_len = name_len < 255 ? name_len : 255;
        if (api->read(file, filename, read_len, offset + 46) != read_len) break;
        filename[read_len] = '\0';
        
        // Print size (10 chars right-aligned)
        char size_str[12];
        int si = 0;
        unsigned long n = uncompressed;
        if (n == 0) size_str[si++] = '0';
        else {
            char tmp[12];
            int ti = 0;
            while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
            while (ti > 0) size_str[si++] = tmp[--ti];
        }
        size_str[si] = '\0';
        
        for (int i = 0; i < 10 - si; i++) out_putc(' ');
        out_puts(size_str);
        out_puts("  ");
        out_puts(filename);
        out_putc('\n');
        
        offset += 46 + name_len + extra_len + comment_len;
        count++;
    }
    
    out_puts("----------  ----\n");
    print_num(count);
    out_puts(" file(s)\n");
    
    return 0;
}

// Extract a single file from ZIP
static int extract_one(void *archive, uint32_t local_offset, const char *dest_dir, const char *filename) {
    uint8_t local[30];
    if (api->read(archive, (char *)local, 30, local_offset) != 30) return -1;
    
    if (read32(local) != ZIP_LOCAL_SIG) return -1;
    
    uint16_t compression = read16(local + 8);
    uint32_t compressed = read32(local + 18);
    uint32_t uncompressed = read32(local + 22);
    uint16_t name_len = read16(local + 26);
    uint16_t extra_len = read16(local + 28);
    
    // Skip directories
    if (filename[strlen(filename) - 1] == '/') {
        char dir_path[512];
        strcpy(dir_path, dest_dir);
        int len = strlen(dir_path);
        if (len > 0 && dir_path[len - 1] != '/') dir_path[len++] = '/';
        strcpy(dir_path + len, filename);
        api->mkdir(dir_path);
        out_puts("  Creating: ");
        out_puts(filename);
        out_putc('\n');
        return 0;
    }
    
    // Only support uncompressed (store) for now
    if (compression != 0) {
        out_puts("  Skipping: ");
        out_puts(filename);
        out_puts(" (compressed)\n");
        return 0;
    }
    
    out_puts("  Extracting: ");
    out_puts(filename);
    
    // Build output path
    char out_path[512];
    strcpy(out_path, dest_dir);
    int len = strlen(out_path);
    if (len > 0 && out_path[len - 1] != '/') out_path[len++] = '/';
    strcpy(out_path + len, filename);
    
    // Create parent directories
    for (int i = len; out_path[i]; i++) {
        if (out_path[i] == '/') {
            out_path[i] = '\0';
            api->mkdir(out_path);
            out_path[i] = '/';
        }
    }
    
    // Calculate data offset
    uint32_t data_offset = local_offset + 30 + name_len + extra_len;
    
    if (uncompressed == 0) {
        // Empty file
        void *f = api->create(out_path);
        if (f) {
            out_puts(" (empty)\n");
            return 0;
        }
        out_puts(" FAILED\n");
        return -1;
    }
    
    // Read and write file data
    void *out_file = api->create(out_path);
    if (!out_file) {
        out_puts(" FAILED\n");
        return -1;
    }
    
    // Read in chunks
    uint8_t buf[4096];
    uint32_t remaining = uncompressed;
    uint32_t read_offset = data_offset;
    
    while (remaining > 0) {
        int chunk = remaining > 4096 ? 4096 : remaining;
        if (api->read(archive, (char *)buf, chunk, read_offset) != chunk) {
            out_puts(" read error\n");
            return -1;
        }
        api->write(out_file, (char *)buf, chunk);
        remaining -= chunk;
        read_offset += chunk;
    }
    
    out_puts(" (");
    print_num(uncompressed);
    out_puts(" bytes)\n");
    
    return 0;
}

// Extract ZIP archive
static int extract_zip(void *file, int file_size, const char *dest_dir) {
    uint8_t end_record[22];
    if (find_end_central(file, file_size, end_record) < 0) {
        out_puts("Error: Cannot find ZIP central directory\n");
        return 1;
    }
    
    uint16_t total_entries = read16(end_record + 10);
    uint32_t central_offset = read32(end_record + 16);
    
    out_puts("Extracting to: ");
    out_puts(dest_dir);
    out_putc('\n');
    
    api->mkdir(dest_dir);
    
    uint32_t offset = central_offset;
    int count = 0, errors = 0;
    
    while (count < total_entries) {
        uint8_t header[46];
        if (api->read(file, (char *)header, 46, offset) != 46) break;
        
        if (read32(header) != ZIP_CENTRAL_SIG) break;
        
        uint16_t name_len = read16(header + 28);
        uint16_t extra_len = read16(header + 30);
        uint16_t comment_len = read16(header + 32);
        uint32_t local_offset = read32(header + 42);
        
        // Read filename
        char filename[256];
        int read_len = name_len < 255 ? name_len : 255;
        if (api->read(file, filename, read_len, offset + 46) != read_len) break;
        filename[read_len] = '\0';
        
        if (extract_one(file, local_offset, dest_dir, filename) != 0) {
            errors++;
        }
        
        offset += 46 + name_len + extra_len + comment_len;
        count++;
    }
    
    out_puts("\nExtracted ");
    print_num(count);
    out_puts(" file(s)");
    if (errors > 0) {
        out_puts(", ");
        print_num(errors);
        out_puts(" error(s)");
    }
    out_putc('\n');
    
    return errors > 0 ? 1 : 0;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;
    
    if (argc < 2) {
        out_puts("Kuav - KikiOS Archive Utility\n\n");
        out_puts("Usage:\n");
        out_puts("  kuav list <archive>           - List archive contents\n");
        out_puts("  kuav extract <archive> [dir]  - Extract files\n");
        out_puts("\nSupported: ZIP (uncompressed entries)\n");
        return 1;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "l") == 0) {
        if (argc < 3) {
            out_puts("Usage: kuav list <archive>\n");
            return 1;
        }
        
        const char *archive = argv[2];
        void *f = api->open(archive);
        if (!f) {
            out_puts("Error: Cannot open ");
            out_puts(archive);
            out_putc('\n');
            return 1;
        }
        
        if (api->is_dir(f)) {
            out_puts("Error: ");
            out_puts(archive);
            out_puts(" is a directory\n");
            return 1;
        }
        
        int fsize = api->file_size(f);
        
        if (is_zip(f)) {
            return list_zip(f, fsize);
        } else {
            out_puts("Error: Not a ZIP file\n");
            return 1;
        }
    }
    else if (strcmp(cmd, "extract") == 0 || strcmp(cmd, "x") == 0) {
        if (argc < 3) {
            out_puts("Usage: kuav extract <archive> [directory]\n");
            return 1;
        }
        
        const char *archive = argv[2];
        const char *dest = argc > 3 ? argv[3] : ".";
        
        void *f = api->open(archive);
        if (!f) {
            out_puts("Error: Cannot open ");
            out_puts(archive);
            out_putc('\n');
            return 1;
        }
        
        if (api->is_dir(f)) {
            out_puts("Error: ");
            out_puts(archive);
            out_puts(" is a directory\n");
            return 1;
        }
        
        int fsize = api->file_size(f);
        
        if (is_zip(f)) {
            return extract_zip(f, fsize, dest);
        } else {
            out_puts("Error: Not a ZIP file\n");
            return 1;
        }
    }
    else {
        out_puts("Unknown command: ");
        out_puts(cmd);
        out_puts("\nUse 'kuav' without arguments for help.\n");
        return 1;
    }
    
    return 0;
}
