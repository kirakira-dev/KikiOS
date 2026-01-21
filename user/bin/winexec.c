/*
 * winexec - Run Windows .exe files on KikiOS
 *
 * Uses real-time x86 instruction translation to execute
 * Windows PE (Portable Executable) programs.
 *
 * Usage: winexec <file.exe>
 *
 * Supported:
 *   - 32-bit (x86) Windows executables
 *   - Console applications (subsystem CONSOLE)
 *   - Basic Windows API functions (kernel32, user32, msvcrt)
 *
 * Limitations:
 *   - No GUI support
 *   - Limited Windows API coverage
 *   - No multithreading
 *   - Interpreted execution (slow)
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

static void print_num(int n) {
    if (n < 0) { out_putc('-'); n = -n; }
    char buf[12];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    else while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) out_putc(buf[--i]);
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;
    
    if (argc < 2) {
        out_puts("winexec - Run Windows .exe files on KikiOS\n\n");
        out_puts("Usage: winexec <file.exe>\n\n");
        out_puts("Supports 32-bit Windows console applications.\n");
        out_puts("Uses real-time x86 instruction translation.\n\n");
        out_puts("Supported Windows APIs:\n");
        out_puts("  kernel32: GetStdHandle, WriteFile, ExitProcess, ...\n");
        out_puts("  user32:   MessageBoxA\n");
        out_puts("  msvcrt:   printf, puts, malloc, free, ...\n");
        return 1;
    }
    
    // Check if winexec is supported
    if (!api->winexec_supported || !api->winexec_supported()) {
        out_puts("winexec: Windows executable support not available\n");
        return 1;
    }
    
    const char *exe_path = argv[1];
    
    // Check file extension
    int len = strlen(exe_path);
    if (len < 4) {
        out_puts("winexec: Invalid filename\n");
        return 1;
    }
    
    const char *ext = exe_path + len - 4;
    if (strcmp(ext, ".exe") != 0 && strcmp(ext, ".EXE") != 0) {
        out_puts("winexec: Warning: File does not have .exe extension\n");
    }
    
    // Check if file exists
    void *f = api->open(exe_path);
    if (!f) {
        out_puts("winexec: Cannot open '");
        out_puts(exe_path);
        out_puts("'\n");
        return 1;
    }
    
    if (api->is_dir(f)) {
        out_puts("winexec: '");
        out_puts(exe_path);
        out_puts("' is a directory\n");
        return 1;
    }
    
    int size = api->file_size(f);
    if (size < 64) {
        out_puts("winexec: File too small to be a valid executable\n");
        return 1;
    }
    
    // Check for MZ header
    char header[2];
    api->read(f, header, 2, 0);
    if (header[0] != 'M' || header[1] != 'Z') {
        out_puts("winexec: Not a valid Windows executable (no MZ header)\n");
        return 1;
    }
    
    out_puts("Running: ");
    out_puts(exe_path);
    out_puts("\n");
    out_puts("----------------------------------------\n");
    
    // Run the executable
    int exit_code = api->winexec_run(exe_path);
    
    out_puts("----------------------------------------\n");
    out_puts("Exit code: ");
    print_num(exit_code);
    out_puts("\n");
    
    return exit_code;
}
