/*
 * echo - print arguments
 *
 * Supports output redirection: echo hello > file.txt
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
    api = k;

    // Check for output redirection
    int redir_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0 && i + 1 < argc) {
            redir_idx = i;
            break;
        }
    }

    if (redir_idx > 0) {
        // Redirect to file
        char *filename = argv[redir_idx + 1];
        void *file = k->create(filename);
        if (!file) {
            out_puts("echo: cannot create ");
            out_puts(filename);
            out_putc('\n');
            return 1;
        }

        // Build content from args before >
        char content[512];
        int pos = 0;
        for (int i = 1; i < redir_idx && pos < 510; i++) {
            int len = strlen(argv[i]);
            for (int j = 0; j < len && pos < 510; j++) {
                content[pos++] = argv[i][j];
            }
            if (i < redir_idx - 1 && pos < 510) {
                content[pos++] = ' ';
            }
        }
        content[pos++] = '\n';
        content[pos] = '\0';

        k->write(file, content, pos);
    } else {
        // Print to console
        for (int i = 1; i < argc; i++) {
            out_puts(argv[i]);
            if (i < argc - 1) {
                out_putc(' ');
            }
        }
        out_putc('\n');
    }

    return 0;
}
