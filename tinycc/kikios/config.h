/*
 * TCC configuration for KikiOS
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Target: AArch64 (ARM64) */
#define TCC_TARGET_ARM64 1

/* KikiOS uses Unix-style CRT linking (crt1.o, crti.o, crtn.o) */
#define TCC_TARGET_UNIX 1

/* Version */
#define TCC_VERSION "0.9.28-kikios"

/* Installation paths - will be /lib/tcc on KikiOS */
#define CONFIG_TCCDIR "/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "/lib/tcc/include"
#define CONFIG_TCC_LIBPATHS "/lib/tcc/lib"
#define CONFIG_TCC_CRTPREFIX "/lib/tcc/lib"
#define CONFIG_TCC_ELFINTERP ""

/* Cross-compiler prefix (none for native) */
#define CONFIG_TCC_CROSSPREFIX ""

/* Runtime library name */
#define TCC_LIBTCC1 "libtcc1.a"

/* Static build - no dlopen */
#define CONFIG_TCC_STATIC 1

/* Generate PIE (Position Independent Executables) by default */
/* KikiOS requires PIE for dynamic loading at any address */
#define CONFIG_TCC_PIE 1

/* Disable features we don't need */
#define CONFIG_TCC_BCHECK 0      /* No bounds checking */
#define CONFIG_TCC_BACKTRACE 0   /* No backtrace */
#define CONFIG_TCC_SEMLOCK 0     /* No threading/semaphores */

/* ONE_SOURCE is set via command line */
/* #define ONE_SOURCE 1 */

/* KikiOS-specific defines */
#define TARGETOS_KikiOS 1

/* No threading */
#undef _REENTRANT

#endif /* CONFIG_H */
