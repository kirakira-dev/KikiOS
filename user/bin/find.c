/*
 * find - search for files in a directory tree
 *
 * Usage: find <path> [-name <pattern>] [-type f|d]
 *   -name  match filename pattern (simple glob with *)
 *   -type  f = files only, d = directories only
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

// Simple glob match with * wildcard
static int glob_match(const char *pattern, const char *str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;  // * at end matches everything

            // Try to match rest of pattern at each position
            while (*str) {
                if (glob_match(pattern, str)) return 1;
                str++;
            }
            return glob_match(pattern, str);
        } else if (*pattern == *str) {
            pattern++;
            str++;
        } else {
            return 0;
        }
    }

    // Skip trailing *
    while (*pattern == '*') pattern++;

    return !*pattern && !*str;
}

static int str_len(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void str_cpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

// Recursive find
static void find_recursive(const char *path, const char *name_pattern, int type_filter) {
    void *dir = api->open(path);
    if (!dir || !api->is_dir(dir)) {
        return;
    }

    char name[256];
    uint8_t type;
    int idx = 0;

    while (api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;

        // Skip . and ..
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        // Build full path
        char full_path[512];
        str_cpy(full_path, path);
        int plen = str_len(full_path);
        if (plen > 0 && full_path[plen - 1] != '/') {
            full_path[plen++] = '/';
            full_path[plen] = '\0';
        }
        str_cpy(full_path + plen, name);

        // Check if it's a directory
        void *node = api->open(full_path);
        int is_dir = node && api->is_dir(node);

        // Check type filter
        int type_ok = 1;
        if (type_filter == 'f' && is_dir) type_ok = 0;
        if (type_filter == 'd' && !is_dir) type_ok = 0;

        // Check name pattern
        int name_ok = 1;
        if (name_pattern && !glob_match(name_pattern, name)) {
            name_ok = 0;
        }

        // Print if both match
        if (type_ok && name_ok) {
            out_puts(full_path);
            out_putc('\n');
        }

        // Recurse into directories
        if (is_dir) {
            find_recursive(full_path, name_pattern, type_filter);
        }
    }
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    const char *start_path = ".";
    const char *name_pattern = NULL;
    int type_filter = 0;  // 0 = all, 'f' = files, 'd' = dirs

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'n' && argv[i][2] == 'a') {
            // -name
            if (i + 1 < argc) {
                name_pattern = argv[++i];
            }
        } else if (argv[i][0] == '-' && argv[i][1] == 't' && argv[i][2] == 'y') {
            // -type
            if (i + 1 < argc) {
                type_filter = argv[++i][0];
            }
        } else if (argv[i][0] != '-') {
            start_path = argv[i];
        }
    }

    // Normalize start path
    char path[512];
    if (start_path[0] == '/') {
        str_cpy(path, start_path);
    } else if (start_path[0] == '.' && start_path[1] == '\0') {
        k->get_cwd(path, sizeof(path));
    } else {
        k->get_cwd(path, sizeof(path));
        int plen = str_len(path);
        if (plen > 0 && path[plen - 1] != '/') {
            path[plen++] = '/';
            path[plen] = '\0';
        }
        str_cpy(path + plen, start_path);
    }

    // Check if start path exists
    void *start = k->open(path);
    if (!start) {
        out_puts("find: '");
        out_puts(path);
        out_puts("': No such file or directory\n");
        return 1;
    }

    // If start is a file, just check and print it
    if (!k->is_dir(start)) {
        // Extract filename for pattern matching
        const char *filename = path;
        for (const char *p = path; *p; p++) {
            if (*p == '/') filename = p + 1;
        }

        int type_ok = (type_filter == 0 || type_filter == 'f');
        int name_ok = (!name_pattern || glob_match(name_pattern, filename));

        if (type_ok && name_ok) {
            out_puts(path);
            out_putc('\n');
        }
        return 0;
    }

    // Print start directory if it matches
    if (type_filter == 0 || type_filter == 'd') {
        // Directory itself - only print if no name filter or matches
        const char *dirname = path;
        for (const char *p = path; *p; p++) {
            if (*p == '/') dirname = p + 1;
        }
        if (!name_pattern || glob_match(name_pattern, dirname)) {
            out_puts(path);
            out_putc('\n');
        }
    }

    // Recurse
    find_recursive(path, name_pattern, type_filter);

    return 0;
}
