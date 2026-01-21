/*
 * ls - list directory contents
 *
 * Uses the proper readdir API instead of accessing VFS internals.
 */

#include "../lib/kiki.h"

int main(kapi_t *k, int argc, char **argv) {
    const char *path = ".";

    if (argc > 1) {
        path = argv[1];
    }

    void *dir = k->open(path);
    if (!dir) {
        vibe_puts(k, "ls: ");
        vibe_puts(k, path);
        vibe_puts(k, ": No such file or directory\n");
        return 1;
    }

    if (!k->is_dir(dir)) {
        // It's a file, just print the name
        vibe_puts(k, path);
        vibe_putc(k, '\n');
        return 0;
    }

    // List directory contents using readdir
    char name[256];
    uint8_t type;
    int index = 0;

    while (k->readdir(dir, index, name, sizeof(name), &type) >= 0) {
        vibe_puts(k, name);
        if (type == 2) {
            // Directory
            vibe_putc(k, '/');
        }
        vibe_putc(k, '\n');
        index++;
    }

    return 0;
}
