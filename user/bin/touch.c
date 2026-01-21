/*
 * touch - create empty file
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;

    if (argc < 2) {
        out_puts("usage: touch <file>\n");
        return 1;
    }

    void *file = k->create(argv[1]);
    if (!file) {
        out_puts("touch: cannot create '");
        out_puts(argv[1]);
        out_puts("'\n");
        return 1;
    }

    return 0;
}
