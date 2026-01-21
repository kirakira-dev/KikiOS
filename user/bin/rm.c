/*
 * rm - remove files and directories
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

// Simple strlen
static int str_len(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

// Simple strcmp
static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

// Simple strcpy
static void str_cpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

// Recursive delete implementation in userspace
static int delete_recursive(const char *path) {
    // First try deleting as a file
    if (api->delete(path) == 0) {
        return 0;
    }

    // Try deleting as an empty directory
    if (api->delete_dir(path) == 0) {
        return 0;
    }

    // If that failed, it might be a non-empty directory
    // Open directory and iterate
    void *dir = api->open(path);
    if (!dir) {
        return -1;
    }

    char name[256];
    unsigned char type;
    char child_path[512];
    int idx = 0;

    // Collect all names first, then delete
    // (Can't delete while iterating - shifts indices)
    char all_names[4096];
    int name_offsets[128];
    int name_count = 0;
    int buf_pos = 0;

    while (api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;

        // Skip . and ..
        if (str_cmp(name, ".") == 0 || str_cmp(name, "..") == 0) {
            continue;
        }

        // Store name
        if (name_count < 128 && buf_pos + str_len(name) + 1 < 4096) {
            name_offsets[name_count++] = buf_pos;
            str_cpy(all_names + buf_pos, name);
            buf_pos += str_len(name) + 1;
        }
    }

    // Now delete all collected entries
    for (int i = 0; i < name_count; i++) {
        // Build child path
        str_cpy(child_path, path);
        int plen = str_len(child_path);
        if (plen > 0 && child_path[plen - 1] != '/') {
            child_path[plen] = '/';
            child_path[plen + 1] = '\0';
        }
        int clen = str_len(child_path);
        str_cpy(child_path + clen, all_names + name_offsets[i]);

        // Recursively delete child
        delete_recursive(child_path);
    }

    // Now directory should be empty, delete it as directory
    return api->delete_dir(path);
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("Usage: rm [-r] <file> [...]\n");
        return 1;
    }

    int status = 0;
    int recursive = 0;
    int start_idx = 1;

    // Check for -r flag
    if (argc >= 2 && argv[1][0] == '-' && argv[1][1] == 'r' && argv[1][2] == '\0') {
        recursive = 1;
        start_idx = 2;
    }

    if (start_idx >= argc) {
        out_puts("Usage: rm [-r] <file> [...]\n");
        return 1;
    }

    for (int i = start_idx; i < argc; i++) {
        int result;
        if (recursive) {
            result = delete_recursive(argv[i]);
        } else {
            result = k->delete(argv[i]);
        }

        if (result < 0) {
            out_puts("rm: cannot remove '");
            out_puts(argv[i]);
            out_puts("'");
            if (!recursive) {
                out_puts(" (directory? use -r)");
            }
            out_puts("\n");
            status = 1;
        }
    }

    return status;
}
