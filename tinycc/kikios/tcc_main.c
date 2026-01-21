/*
 * TCC for KikiOS - Userspace Startup
 *
 * This provides the C runtime entry for TCC.
 * TCC's main() is compiled separately.
 */

#include "../../user/lib/kiki.h"
#include "tcc_libc.h"

/* kapi pointer used by tcc_libc.c */
kapi_t *tcc_kapi = 0;

/* TCC's main (compiled from tcc.c) */
extern int tcc_main(int argc, char **argv);

/* Userspace entry point */
int main(kapi_t *kapi, int argc, char **argv) {
    tcc_kapi = kapi;

    /* Debug: test both direct kapi and stderr */
    kapi->puts("[TCC] Starting (kapi direct)\n");
    fprintf(stderr, "[TCC] Starting (stderr)\n");

    if (argc < 2) {
        kapi->puts("Tiny C Compiler for KikiOS\n");
        kapi->puts("Usage: tcc [options] file.c\n");
        kapi->puts("       tcc -c file.c -o file.o\n");
        kapi->puts("       tcc file.c -o program\n");
        kapi->puts("\nOptions:\n");
        kapi->puts("  -c          Compile to object file\n");
        kapi->puts("  -o FILE     Output file\n");
        kapi->puts("  -I DIR      Add include path\n");
        kapi->puts("  -D SYM=VAL  Define macro\n");
        kapi->puts("  -v          Verbose\n");
        kapi->puts("  -h          More help\n");
        return 0;
    }

    return tcc_main(argc, argv);
}
