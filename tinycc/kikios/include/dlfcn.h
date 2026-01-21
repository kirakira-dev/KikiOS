/*
 * dlfcn.h wrapper for TCC on KikiOS
 */

#ifndef _DLFCN_H
#define _DLFCN_H

#include "../tcc_libc.h"

/* dlopen flags */
#define RTLD_LAZY   0x0001
#define RTLD_NOW    0x0002
#define RTLD_GLOBAL 0x0100
#define RTLD_LOCAL  0x0000

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
char *dlerror(void);

#endif /* _DLFCN_H */
