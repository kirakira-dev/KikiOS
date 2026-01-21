/*
 * KikiOS Initramfs
 *
 * Monolith kernel - no external programs to load!
 * Everything is built into the shell.
 */

#include "initramfs.h"
#include "printf.h"

void initramfs_init(void) {
    printf("[INITRAMFS] Monolith kernel - all commands built-in!\n");
}
