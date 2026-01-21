/*
 * mv - move (rename) files
 *
 * Usage: mv <source> <dest>
 *        mv <source...> <destdir>
 *
 * If source and dest are in same directory, uses rename.
 * Otherwise, copies and deletes.
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

static int str_ncmp(const char *a, const char *b, int n) {
    while (n > 0 && *a && *b && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
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

// Get directory part of path (returns length)
static int get_dirname_len(const char *path) {
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    if (last_slash < 0) return 0;  // No directory part
    return last_slash + 1;
}

// Check if two paths are in the same directory
static int same_directory(const char *src, const char *dst) {
    int src_dir_len = get_dirname_len(src);
    int dst_dir_len = get_dirname_len(dst);

    if (src_dir_len != dst_dir_len) return 0;
    if (src_dir_len == 0) return 1;  // Both in current dir

    return str_ncmp(src, dst, src_dir_len) == 0;
}

// Copy file content
static int copy_file(const char *src, const char *dst) {
    void *src_file = api->open(src);
    if (!src_file) return -1;

    void *dst_file = api->create(dst);
    if (!dst_file) return -1;

    char buf[4096];
    size_t offset = 0;
    int bytes;

    while ((bytes = api->read(src_file, buf, sizeof(buf), offset)) > 0) {
        api->write(dst_file, buf, bytes);
        offset += bytes;
    }

    return 0;
}

// Recursive copy (for directories)
static int copy_recursive(const char *src, const char *dst);

// Delete recursively
static int delete_recursive(const char *path) {
    // Try file first
    if (api->delete(path) == 0) return 0;

    // Try empty dir
    if (api->delete_dir(path) == 0) return 0;

    // Must be non-empty dir
    void *dir = api->open(path);
    if (!dir) return -1;

    char name[256];
    uint8_t type;
    int idx = 0;

    // Collect children
    char names[2048];
    int offsets[64];
    int count = 0;
    int pos = 0;

    while (api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (str_cmp(name, ".") == 0 || str_cmp(name, "..") == 0) continue;

        if (count < 64 && pos + str_len(name) + 1 < 2048) {
            offsets[count++] = pos;
            str_cpy(names + pos, name);
            pos += str_len(name) + 1;
        }
    }

    // Delete children
    for (int i = 0; i < count; i++) {
        char child[512];
        str_cpy(child, path);
        int plen = str_len(child);
        if (plen > 0 && child[plen - 1] != '/') {
            child[plen++] = '/';
            child[plen] = '\0';
        }
        str_cpy(child + plen, names + offsets[i]);
        delete_recursive(child);
    }

    return api->delete_dir(path);
}

// Recursive directory copy
static int copy_recursive(const char *src, const char *dst) {
    void *src_node = api->open(src);
    if (!src_node) return -1;

    if (!api->is_dir(src_node)) {
        return copy_file(src, dst);
    }

    // Create destination dir
    api->mkdir(dst);

    char name[256];
    uint8_t type;
    int idx = 0;

    while (api->readdir(src_node, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (str_cmp(name, ".") == 0 || str_cmp(name, "..") == 0) continue;

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

        copy_recursive(src_path, dst_path);
    }

    return 0;
}

// Move a single file/directory
static int move_one(const char *src, const char *dst) {
    // Check if source exists
    void *src_node = api->open(src);
    if (!src_node) {
        out_puts("mv: cannot stat '");
        out_puts(src);
        out_puts("'\n");
        return -1;
    }

    int src_is_dir = api->is_dir(src_node);

    // Check if destination exists
    void *dst_node = api->open(dst);
    int dst_exists = (dst_node != NULL);
    int dst_is_dir = dst_exists && api->is_dir(dst_node);

    // If dest is existing dir, move into it
    if (dst_is_dir && !src_is_dir) {
        char new_dst[512];
        str_cpy(new_dst, dst);
        int dlen = str_len(new_dst);
        if (dlen > 0 && new_dst[dlen - 1] != '/') {
            new_dst[dlen++] = '/';
            new_dst[dlen] = '\0';
        }
        str_cpy(new_dst + dlen, get_basename(src));
        return move_one(src, new_dst);
    }

    // If same directory, try rename
    if (same_directory(src, dst)) {
        // Extract just the new name
        const char *new_name = get_basename(dst);
        if (api->rename(src, new_name) == 0) {
            return 0;
        }
        // Fall through to copy+delete
    }

    // Cross-directory or rename failed: copy and delete
    int result;
    if (src_is_dir) {
        result = copy_recursive(src, dst);
    } else {
        result = copy_file(src, dst);
    }

    if (result == 0) {
        // Delete source
        if (src_is_dir) {
            delete_recursive(src);
        } else {
            api->delete(src);
        }
    } else {
        out_puts("mv: failed to copy '");
        out_puts(src);
        out_puts("'\n");
        return -1;
    }

    return 0;
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    const char *sources[16];
    int source_count = 0;

    // Parse arguments (no flags for now)
    for (int i = 1; i < argc; i++) {
        if (source_count < 16) {
            sources[source_count++] = argv[i];
        }
    }

    if (source_count < 2) {
        out_puts("Usage: mv <source...> <dest>\n");
        return 1;
    }

    // Last argument is destination
    const char *dest = sources[--source_count];

    // Check if destination is a directory
    void *dest_node = api->open(dest);
    int dest_is_dir = (dest_node && api->is_dir(dest_node));

    // Multiple sources require directory destination
    if (source_count > 1 && !dest_is_dir) {
        out_puts("mv: target '");
        out_puts(dest);
        out_puts("' is not a directory\n");
        return 1;
    }

    int status = 0;

    for (int i = 0; i < source_count; i++) {
        const char *src = sources[i];
        char final_dest[512];

        if (dest_is_dir) {
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

        if (move_one(src, final_dest) < 0) {
            status = 1;
        }
    }

    return status;
}
