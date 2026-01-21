/*
 * pwd - print working directory
 */

#include "../lib/kiki.h"

static kapi_t *api;

static void out_putc(char c) {
    if (api->stdio_putc) api->stdio_putc(c);
    else api->putc(c);
}

static void out_puts(const char *s) {
    if (api->stdio_puts) api->stdio_puts(s);
    else api->puts(s);
}

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;
    api = k;

    char cwd[256];
    if (k->get_cwd(cwd, sizeof(cwd)) >= 0) {
        out_puts(cwd);
        out_putc('\n');
        return 0;
    }

    out_puts("pwd: error getting current directory\n");
    return 1;
}
