// MicroPython for KikiOS
// Entry point and runtime initialization

#include "kiki.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/stackctrl.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/nlr.h"
#include "shared/runtime/pyexec.h"

// Global kernel API pointer (used by mphalport.c)
kapi_t *mp_kikios_api;

// Heap for MicroPython's garbage collector
// Must be aligned for pointer-sized access (GC stores pointers in heap)
static char heap[MICROPY_HEAP_SIZE] __attribute__((aligned(16)));

// Stack tracking
static char *stack_top;

// GC collection - scan stack for roots
void gc_collect(void) {
    void *dummy;
    gc_collect_start();
    // Sanity check: stack grows down on ARM64, so stack_top should be > &dummy
    // Also limit scan to reasonable size (1MB max) to prevent corruption issues
    mp_uint_t top = (mp_uint_t)stack_top;
    mp_uint_t cur = (mp_uint_t)&dummy;
    if (top > cur && (top - cur) < (1024 * 1024)) {
        gc_collect_root(&dummy, (top - cur) / sizeof(mp_uint_t));
    }
    gc_collect_end();
}

// File operations for script execution
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *path = qstr_str(filename);

    // Open the file
    void *file = mp_kikios_api->open(path);
    if (!file) {
        mp_raise_OSError(MP_ENOENT);
    }

    // Check if it's a directory
    if (mp_kikios_api->is_dir(file)) {
        mp_raise_OSError(MP_EISDIR);
    }

    // Get file size
    int size = mp_kikios_api->file_size(file);
    if (size < 0) {
        mp_raise_OSError(MP_EIO);
    }

    // Allocate buffer using kernel malloc (simpler, small leak is OK for scripts)
    char *buf = mp_kikios_api->malloc(size + 1);
    if (!buf) {
        mp_raise_OSError(MP_ENOMEM);
    }

    // Read file contents
    int bytes_read = mp_kikios_api->read(file, buf, size, 0);
    if (bytes_read != size) {
        mp_kikios_api->free(buf);
        mp_raise_OSError(MP_EIO);
    }

    // Strip \r (CRLF -> LF) - FAT32 files from macOS/Windows have CRLF
    int j = 0;
    for (int k = 0; k < size; k++) {
        if (buf[k] != '\r') {
            buf[j++] = buf[k];
        }
    }
    size = j;
    buf[size] = '\0';

    // Create lexer from buffer (pass 0 for free_len - we won't free it)
    return mp_lexer_new_from_str_len(filename, buf, size, 0);
}

mp_import_stat_t mp_import_stat(const char *path) {
    void *file = mp_kikios_api->open(path);
    if (!file) {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    if (mp_kikios_api->is_dir(file)) {
        return MP_IMPORT_STAT_DIR;
    }
    return MP_IMPORT_STAT_FILE;
}

void nlr_jump_fail(void *val) {
    mp_kikios_api->puts("FATAL: nlr_jump_fail\n");
    mp_kikios_api->exit(1);
    for (;;) {}
}

void NORETURN __fatal_error(const char *msg) {
    mp_kikios_api->puts("FATAL: ");
    mp_kikios_api->puts(msg);
    mp_kikios_api->puts("\n");
    mp_kikios_api->exit(1);
    for (;;) {}
}

#ifndef NDEBUG
void __assert_func(const char *file, int line, const char *func, const char *expr) {
    mp_kikios_api->puts("Assertion failed: ");
    mp_kikios_api->puts(expr);
    mp_kikios_api->puts("\n");
    __fatal_error("assertion failed");
}
#endif

int main(kapi_t *api, int argc, char **argv) {
    mp_kikios_api = api;

    // Track stack for GC - capture SP at very start of main
    // Use inline asm to get actual stack pointer value
    char *sp_val;
    asm volatile("mov %0, sp" : "=r" (sp_val));
    stack_top = sp_val;

    // Initialize MicroPython
    mp_stack_ctrl_init();
    mp_stack_set_limit(64 * 1024);  // 64KB stack limit

    gc_init(heap, heap + sizeof(heap));
    mp_init();

    // Populate sys.argv with command line arguments
    #if MICROPY_PY_SYS_ARGV
    for (int i = 0; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
    }
    #endif

    int ret = 0;

    if (argc > 1) {
        // Run script file - read and execute directly
        void *file = api->open(argv[1]);
        if (!file) {
            api->puts("micropython: cannot open ");
            api->puts(argv[1]);
            api->puts("\n");
            mp_deinit();
            return 1;
        }
        int size = api->file_size(file);
        char *buf = api->malloc(size + 1);
        api->read(file, buf, size, 0);

        // Strip \r
        int j = 0;
        for (int k = 0; k < size; k++) {
            if (buf[k] != '\r') buf[j++] = buf[k];
        }
        buf[j] = '\0';

        // Execute using mp_parse_compile_execute with FILE_INPUT mode
        mp_lexer_t *lex = mp_lexer_new_from_str_len(qstr_from_str(argv[1]), buf, j, 0);

        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
            mp_obj_t module_fun = mp_compile(&parse_tree, lex->source_name, false);
            mp_call_function_0(module_fun);
            nlr_pop();
        } else {
            // Exception - print it
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
            ret = 1;
        }
        api->free(buf);
    } else {
        // Interactive REPL
        // Use stdio hooks if available (for terminal emulator)
        if (api->stdio_puts) {
            api->stdio_puts("MicroPython for KikiOS\n");
        } else {
            api->puts("MicroPython for KikiOS\n");
        }
        pyexec_friendly_repl();
    }

    mp_deinit();
    return ret;
}
