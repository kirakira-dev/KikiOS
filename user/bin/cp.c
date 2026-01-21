/*
 * cp - copy files and directories
 *
 * Usage: cp [-r] <source> <dest>
 *        cp [-r] <source...> <destdir>
 *   -r  recursive (copy directories)
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

static int str_len(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void str_cpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

// Get the basename of a path
static const char *get_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

// Copy a single file
static int copy_file(const char *src, const char *dst) {
    void *src_file = api->open(src);
    if (!src_file) {
        out_puts("cp: cannot open '");
        out_puts(src);
        out_puts("'\n");
        return -1;
    }

    if (api->is_dir(src_file)) {
        out_puts("cp: '");
        out_puts(src);
        out_puts("' is a directory (use -r)\n");
        return -1;
    }

    // Create destination file
    void *dst_file = api->create(dst);
    if (!dst_file) {
        out_puts("cp: cannot create '");
        out_puts(dst);
        out_puts("'\n");
        return -1;
    }

    // Copy content in chunks
    char buf[4096];
    size_t offset = 0;
    int bytes;

    while ((bytes = api->read(src_file, buf, sizeof(buf), offset)) > 0) {
        // Write to destination (write appends, so no offset needed)
        // But our write() always appends, so we need to truncate first
        // Actually, create() makes empty file, so this should work
        if (offset == 0) {
            // First write - file is empty from create()
            api->write(dst_file, buf, bytes);
        } else {
            // Subsequent writes - append
            api->write(dst_file, buf, bytes);
        }
        offset += bytes;
    }

    return 0;
}

// Recursive directory copy
static int copy_recursive(const char *src, const char *dst) {
    void *src_node = api->open(src);
    if (!src_node) {
        out_puts("cp: cannot open '");
        out_puts(src);
        out_puts("'\n");
        return -1;
    }

    if (!api->is_dir(src_node)) {
        // Regular file
        return copy_file(src, dst);
    }

    // Create destination directory
    void *dst_node = api->open(dst);
    if (!dst_node) {
        api->mkdir(dst);
        dst_node = api->open(dst);
        if (!dst_node) {
            out_puts("cp: cannot create directory '");
            out_puts(dst);
            out_puts("'\n");
            return -1;
        }
    }

    // Iterate through source directory
    char name[256];
    uint8_t type;
    int idx = 0;
    int status = 0;

    while (api->readdir(src_node, idx, name, sizeof(name), &type) == 0) {
        idx++;

        // Skip . and ..
        if (str_cmp(name, ".") == 0 || str_cmp(name, "..") == 0) {
            continue;
        }

        // Build paths
        char src_path[512], dst_path[512];

        str_cpy(src_path, src);
        int slen = str_len(src_path);
        if (slen > 0 && src_path[slen - 1] != '/') {
            src_path[slen++] = '/';
            src_path[slen] = '\0';
        }
        str_cpy(src_path + slen, name);

        str_cpy(dst_path, dst);
        int dlen = str_len(dst_path);
        if (dlen > 0 && dst_path[dlen - 1] != '/') {
            dst_path[dlen++] = '/';
            dst_path[dlen] = '\0';
        }
        str_cpy(dst_path + dlen, name);

        // Recursively copy
        if (copy_recursive(src_path, dst_path) < 0) {
            status = -1;
        }
    }

    return status;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    int recursive = 0;
    const char *sources[16];
    int source_count = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'r') {
            recursive = 1;
        } else if (source_count < 16) {
            sources[source_count++] = argv[i];
        }
    }

    if (source_count < 2) {
        out_puts("Usage: cp [-r] <source...> <dest>\n");
        return 1;
    }

    // Last argument is destination
    const char *dest = sources[--source_count];

    // Check if destination is a directory
    void *dest_node = api->open(dest);
    int dest_is_dir = (dest_node && api->is_dir(dest_node));

    // Multiple sources require directory destination
    if (source_count > 1 && !dest_is_dir) {
        out_puts("cp: target '");
        out_puts(dest);
        out_puts("' is not a directory\n");
        return 1;
    }

    int status = 0;

    for (int i = 0; i < source_count; i++) {
        const char *src = sources[i];
        char final_dest[512];

        if (dest_is_dir) {
            // Copy into directory: dest/basename(src)
            str_cpy(final_dest, dest);
            int dlen = str_len(final_dest);
            if (dlen > 0 && final_dest[dlen - 1] != '/') {
                final_dest[dlen++] = '/';
                final_dest[dlen] = '\0';
            }
            str_cpy(final_dest + dlen, get_basename(src));
        } else {
            str_cpy(final_dest, dest);
        }

        // Check if source is directory
        void *src_node = api->open(src);
        if (src_node && api->is_dir(src_node)) {
            if (!recursive) {
                out_puts("cp: '");
                out_puts(src);
                out_puts("' is a directory (use -r)\n");
                status = 1;
                continue;
            }
            if (copy_recursive(src, final_dest) < 0) {
                status = 1;
            }
        } else {
            if (copy_file(src, final_dest) < 0) {
                status = 1;
            }
        }
    }

    return status;
}
