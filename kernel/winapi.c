/*
 * Windows API Stubs for KikiOS
 * 
 * Provides emulation of common Windows API functions
 * for running simple Windows console applications.
 */

#include "winapi.h"
#include "x86emu.h"
#include "irq.h"
#include "printf.h"
#include "memory.h"
#include "string.h"
#include <stddef.h>

// External declarations
extern void uart_puts(const char *s);

// Console output callback
static void (*console_output)(const char *str, int len) = NULL;

// Exit code
static int exit_code = 0;

// Heap allocations tracking
#define MAX_HEAP_ALLOCS 1024
static struct {
    uint32_t addr;
    uint32_t size;
} heap_allocs[MAX_HEAP_ALLOCS];
static int heap_alloc_count = 0;

// Environment strings
static const char *env_strings[] = {
    "PATH=C:\\Windows\\System32",
    "COMSPEC=C:\\Windows\\System32\\cmd.exe",
    "OS=Windows_NT",
    NULL
};

void winapi_init(void) {
    heap_alloc_count = 0;
    exit_code = 0;
}

void winapi_set_console_output(void (*func)(const char *str, int len)) {
    console_output = func;
}

int winapi_get_exit_code(void) {
    return exit_code;
}

// Helper: Read string from emulated memory
static void read_string(x86emu_state_t *emu, uint32_t addr, char *buf, int maxlen) {
    int i = 0;
    while (i < maxlen - 1) {
        char c = x86emu_read8(emu, addr + i);
        if (c == 0) break;
        buf[i++] = c;
    }
    buf[i] = 0;
}

// Helper: Get string length from emulated memory
static int get_string_len(x86emu_state_t *emu, uint32_t addr) {
    int len = 0;
    while (x86emu_read8(emu, addr + len) != 0 && len < 65536) {
        len++;
    }
    return len;
}

// KERNEL32.DLL functions
static int handle_kernel32(x86emu_state_t *emu, const char *func) {
    // GetStdHandle(nStdHandle)
    if (strcmp(func, "GetStdHandle") == 0) {
        uint32_t handle_type = x86emu_read32(emu, emu->regs.esp + 4);
        
        if (handle_type == STD_INPUT_HANDLE) {
            emu->regs.eax = WINAPI_STDIN;
        } else if (handle_type == STD_OUTPUT_HANDLE) {
            emu->regs.eax = WINAPI_STDOUT;
        } else if (handle_type == STD_ERROR_HANDLE) {
            emu->regs.eax = WINAPI_STDERR;
        } else {
            emu->regs.eax = 0xFFFFFFFF;  // INVALID_HANDLE_VALUE
        }
        
        // stdcall: callee cleans up
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;  // 1 parameter
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped)
    if (strcmp(func, "WriteFile") == 0) {
        uint32_t handle = x86emu_read32(emu, emu->regs.esp + 4);
        uint32_t buffer = x86emu_read32(emu, emu->regs.esp + 8);
        uint32_t bytes_to_write = x86emu_read32(emu, emu->regs.esp + 12);
        uint32_t bytes_written_ptr = x86emu_read32(emu, emu->regs.esp + 16);
        
        if ((handle == WINAPI_STDOUT || handle == WINAPI_STDERR) && console_output) {
            // Read string from emulated memory
            char *str = malloc(bytes_to_write + 1);
            if (str) {
                for (uint32_t i = 0; i < bytes_to_write; i++) {
                    str[i] = x86emu_read8(emu, buffer + i);
                }
                str[bytes_to_write] = 0;
                console_output(str, bytes_to_write);
                free(str);
            }
            
            // Write bytes written
            if (bytes_written_ptr) {
                x86emu_write32(emu, bytes_written_ptr, bytes_to_write);
            }
            
            emu->regs.eax = 1;  // Success
        } else {
            emu->regs.eax = 0;  // Failure
        }
        
        // stdcall: callee cleans up 5 parameters (20 bytes)
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 20;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // WriteConsoleA(hConsoleOutput, lpBuffer, nNumberOfCharsToWrite, lpNumberOfCharsWritten, lpReserved)
    if (strcmp(func, "WriteConsoleA") == 0) {
        uint32_t handle = x86emu_read32(emu, emu->regs.esp + 4);
        uint32_t buffer = x86emu_read32(emu, emu->regs.esp + 8);
        uint32_t chars_to_write = x86emu_read32(emu, emu->regs.esp + 12);
        uint32_t chars_written_ptr = x86emu_read32(emu, emu->regs.esp + 16);
        
        (void)handle;
        
        if (console_output) {
            char *str = malloc(chars_to_write + 1);
            if (str) {
                for (uint32_t i = 0; i < chars_to_write; i++) {
                    str[i] = x86emu_read8(emu, buffer + i);
                }
                str[chars_to_write] = 0;
                console_output(str, chars_to_write);
                free(str);
            }
        }
        
        if (chars_written_ptr) {
            x86emu_write32(emu, chars_written_ptr, chars_to_write);
        }
        
        emu->regs.eax = 1;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 20;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // ExitProcess(uExitCode)
    if (strcmp(func, "ExitProcess") == 0) {
        exit_code = x86emu_read32(emu, emu->regs.esp + 4);
        printf("[WinAPI] ExitProcess called with code %d (0x%08X)\r\n", exit_code, exit_code);
        return -1;  // Signal exit
    }
    
    // GetCommandLineA()
    if (strcmp(func, "GetCommandLineA") == 0) {
        // Return a static command line string
        static const char cmdline[] = "program.exe";
        // We'd need to allocate this in emulated memory
        // For now, return 0
        emu->regs.eax = 0;
        return 1;
    }
    
    // GetModuleHandleA(lpModuleName)
    if (strcmp(func, "GetModuleHandleA") == 0) {
        uint32_t name_ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        // If NULL, return base of current module
        if (name_ptr == 0) {
            emu->regs.eax = (uint32_t)(uintptr_t)emu->mem_base;
        } else {
            // Return NULL for other modules
            emu->regs.eax = 0;
        }
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetProcAddress(hModule, lpProcName)
    if (strcmp(func, "GetProcAddress") == 0) {
        // Return NULL - we don't support dynamic function lookup
        emu->regs.eax = 0;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 8;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // HeapCreate(flOptions, dwInitialSize, dwMaximumSize)
    if (strcmp(func, "HeapCreate") == 0) {
        // Return a pseudo-handle
        emu->regs.eax = 0x10000000;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 12;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // HeapAlloc(hHeap, dwFlags, dwBytes)
    if (strcmp(func, "HeapAlloc") == 0) {
        uint32_t flags = x86emu_read32(emu, emu->regs.esp + 8);
        uint32_t bytes = x86emu_read32(emu, emu->regs.esp + 12);
        
        void *ptr = malloc(bytes);
        if (ptr) {
            if (flags & 0x08) {  // HEAP_ZERO_MEMORY
                memset(ptr, 0, bytes);
            }
            
            // Track allocation
            if (heap_alloc_count < MAX_HEAP_ALLOCS) {
                heap_allocs[heap_alloc_count].addr = (uint32_t)(uintptr_t)ptr;
                heap_allocs[heap_alloc_count].size = bytes;
                heap_alloc_count++;
            }
            
            emu->regs.eax = (uint32_t)(uintptr_t)ptr;
        } else {
            emu->regs.eax = 0;
        }
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 12;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // HeapFree(hHeap, dwFlags, lpMem)
    if (strcmp(func, "HeapFree") == 0) {
        uint32_t ptr = x86emu_read32(emu, emu->regs.esp + 12);
        
        if (ptr) {
            free((void *)(uintptr_t)ptr);
        }
        
        emu->regs.eax = 1;  // Success
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 12;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetProcessHeap()
    if (strcmp(func, "GetProcessHeap") == 0) {
        emu->regs.eax = 0x10000000;  // Return our pseudo heap handle
        return 1;
    }
    
    // VirtualAlloc(lpAddress, dwSize, flAllocationType, flProtect)
    if (strcmp(func, "VirtualAlloc") == 0) {
        uint32_t size = x86emu_read32(emu, emu->regs.esp + 8);
        
        void *ptr = malloc(size);
        if (ptr) {
            memset(ptr, 0, size);
            emu->regs.eax = (uint32_t)(uintptr_t)ptr;
        } else {
            emu->regs.eax = 0;
        }
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 16;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // VirtualFree(lpAddress, dwSize, dwFreeType)
    if (strcmp(func, "VirtualFree") == 0) {
        uint32_t ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        if (ptr) {
            free((void *)(uintptr_t)ptr);
        }
        
        emu->regs.eax = 1;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 12;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetLastError()
    if (strcmp(func, "GetLastError") == 0) {
        emu->regs.eax = 0;  // No error
        return 1;
    }
    
    // SetLastError(dwErrCode)
    if (strcmp(func, "SetLastError") == 0) {
        // Ignore
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetCurrentProcessId()
    if (strcmp(func, "GetCurrentProcessId") == 0) {
        emu->regs.eax = 1;  // PID 1
        return 1;
    }
    
    // GetCurrentThreadId()
    if (strcmp(func, "GetCurrentThreadId") == 0) {
        emu->regs.eax = 1;  // Thread ID 1
        return 1;
    }
    
    // GetTickCount()
    if (strcmp(func, "GetTickCount") == 0) {
        // Return a fake tick count
        emu->regs.eax = (uint32_t)(timer_get_ticks() * 10);  // Convert to ms
        return 1;
    }
    
    // QueryPerformanceCounter(lpPerformanceCount)
    if (strcmp(func, "QueryPerformanceCounter") == 0) {
        uint32_t ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        uint64_t count = timer_get_ticks() * 10000;
        
        x86emu_write32(emu, ptr, (uint32_t)count);
        x86emu_write32(emu, ptr + 4, (uint32_t)(count >> 32));
        
        emu->regs.eax = 1;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // QueryPerformanceFrequency(lpFrequency)
    if (strcmp(func, "QueryPerformanceFrequency") == 0) {
        uint32_t ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        uint64_t freq = 1000000;  // 1 MHz
        x86emu_write32(emu, ptr, (uint32_t)freq);
        x86emu_write32(emu, ptr + 4, (uint32_t)(freq >> 32));
        
        emu->regs.eax = 1;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetEnvironmentStringsA()
    if (strcmp(func, "GetEnvironmentStringsA") == 0 || 
        strcmp(func, "GetEnvironmentStrings") == 0) {
        // Return NULL for now
        emu->regs.eax = 0;
        return 1;
    }
    
    // FreeEnvironmentStringsA(lpszEnvironmentBlock)
    if (strcmp(func, "FreeEnvironmentStringsA") == 0 ||
        strcmp(func, "FreeEnvironmentStrings") == 0) {
        emu->regs.eax = 1;
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetVersion()
    if (strcmp(func, "GetVersion") == 0) {
        // Return Windows XP version (5.1)
        emu->regs.eax = 0x80000A05;  // Windows XP
        return 1;
    }
    
    // GetVersionExA(lpVersionInfo)
    if (strcmp(func, "GetVersionExA") == 0) {
        uint32_t info_ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        // Windows XP
        x86emu_write32(emu, info_ptr + 4, 5);   // dwMajorVersion
        x86emu_write32(emu, info_ptr + 8, 1);   // dwMinorVersion
        x86emu_write32(emu, info_ptr + 12, 2600); // dwBuildNumber
        x86emu_write32(emu, info_ptr + 16, 2);  // dwPlatformId (VER_PLATFORM_WIN32_NT)
        
        emu->regs.eax = 1;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetSystemInfo(lpSystemInfo)
    if (strcmp(func, "GetSystemInfo") == 0) {
        uint32_t info_ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        memset((void *)(uintptr_t)info_ptr, 0, 36);  // SYSTEM_INFO size
        x86emu_write32(emu, info_ptr + 0, 0);     // wProcessorArchitecture (x86)
        x86emu_write32(emu, info_ptr + 4, 4096);  // dwPageSize
        x86emu_write32(emu, info_ptr + 20, 1);    // dwNumberOfProcessors
        x86emu_write32(emu, info_ptr + 24, 586);  // dwProcessorType (Pentium)
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // IsDebuggerPresent()
    if (strcmp(func, "IsDebuggerPresent") == 0) {
        emu->regs.eax = 0;  // No debugger
        return 1;
    }
    
    // Sleep(dwMilliseconds)
    if (strcmp(func, "Sleep") == 0) {
        // We could implement actual sleeping, but for simplicity, just return
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 4;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // GetConsoleMode(hConsoleHandle, lpMode)
    if (strcmp(func, "GetConsoleMode") == 0) {
        uint32_t mode_ptr = x86emu_read32(emu, emu->regs.esp + 8);
        
        x86emu_write32(emu, mode_ptr, 0x7);  // ENABLE_PROCESSED_INPUT etc.
        
        emu->regs.eax = 1;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 8;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    // SetConsoleMode(hConsoleHandle, dwMode)
    if (strcmp(func, "SetConsoleMode") == 0) {
        emu->regs.eax = 1;
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 8;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    return 0;  // Not handled
}

// USER32.DLL functions
static int handle_user32(x86emu_state_t *emu, const char *func) {
    // MessageBoxA(hWnd, lpText, lpCaption, uType)
    if (strcmp(func, "MessageBoxA") == 0) {
        uint32_t text_ptr = x86emu_read32(emu, emu->regs.esp + 8);
        uint32_t caption_ptr = x86emu_read32(emu, emu->regs.esp + 12);
        
        char text[256], caption[256];
        read_string(emu, text_ptr, text, 256);
        read_string(emu, caption_ptr, caption, 256);
        
        uart_puts("[MessageBox] ");
        uart_puts(caption);
        uart_puts(": ");
        uart_puts(text);
        uart_puts("\r\n");
        
        if (console_output) {
            console_output("[MessageBox] ", 13);
            console_output(caption, strlen(caption));
            console_output(": ", 2);
            console_output(text, strlen(text));
            console_output("\n", 1);
        }
        
        emu->regs.eax = 1;  // IDOK
        
        uint32_t ret = x86emu_pop32(emu);
        emu->regs.esp += 16;
        x86emu_push32(emu, ret);
        return 1;
    }
    
    return 0;
}

// MSVCRT.DLL functions
static int handle_msvcrt(x86emu_state_t *emu, const char *func) {
    // printf(format, ...)
    if (strcmp(func, "printf") == 0 || strcmp(func, "_printf") == 0) {
        uint32_t format_ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        char format[256];
        read_string(emu, format_ptr, format, 256);
        
        // Simple printf - just print the format string for now
        // A full implementation would parse format specifiers
        if (console_output) {
            console_output(format, strlen(format));
        }
        
        emu->regs.eax = strlen(format);
        return 1;  // cdecl - caller cleans up
    }
    
    // puts(str)
    if (strcmp(func, "puts") == 0 || strcmp(func, "_puts") == 0) {
        uint32_t str_ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        char str[1024];
        read_string(emu, str_ptr, str, 1024);
        
        if (console_output) {
            console_output(str, strlen(str));
            console_output("\n", 1);
        }
        
        emu->regs.eax = 0;  // Success
        return 1;
    }
    
    // putchar(c)
    if (strcmp(func, "putchar") == 0 || strcmp(func, "_putchar") == 0) {
        uint32_t c = x86emu_read32(emu, emu->regs.esp + 4);
        
        if (console_output) {
            char ch = (char)c;
            console_output(&ch, 1);
        }
        
        emu->regs.eax = c;
        return 1;
    }
    
    // exit(status)
    if (strcmp(func, "exit") == 0 || strcmp(func, "_exit") == 0) {
        exit_code = x86emu_read32(emu, emu->regs.esp + 4);
        return -1;  // Signal exit
    }
    
    // malloc(size)
    if (strcmp(func, "malloc") == 0 || strcmp(func, "_malloc") == 0) {
        uint32_t size = x86emu_read32(emu, emu->regs.esp + 4);
        
        void *ptr = malloc(size);
        emu->regs.eax = ptr ? (uint32_t)(uintptr_t)ptr : 0;
        return 1;
    }
    
    // free(ptr)
    if (strcmp(func, "free") == 0 || strcmp(func, "_free") == 0) {
        uint32_t ptr = x86emu_read32(emu, emu->regs.esp + 4);
        
        if (ptr) {
            free((void *)(uintptr_t)ptr);
        }
        return 1;
    }
    
    // strlen(str)
    if (strcmp(func, "strlen") == 0) {
        uint32_t str_ptr = x86emu_read32(emu, emu->regs.esp + 4);
        emu->regs.eax = get_string_len(emu, str_ptr);
        return 1;
    }
    
    // __getmainargs, __p___argc, __p___argv - CRT initialization
    if (strncmp(func, "__", 2) == 0) {
        // Return NULL/0 for most CRT internal functions
        emu->regs.eax = 0;
        return 1;
    }
    
    return 0;
}

// Main API call handler
int winapi_call(x86emu_state_t *emu, const char *dll, const char *func) {
    uart_puts("[WinAPI] Call: ");
    uart_puts(dll);
    uart_puts("!");
    uart_puts(func);
    uart_puts("\r\n");
    
    // Try KERNEL32
    if (strcmp(dll, "KERNEL32.dll") == 0 || strcmp(dll, "kernel32.dll") == 0 ||
        strcmp(dll, "KERNEL32.DLL") == 0) {
        int result = handle_kernel32(emu, func);
        if (result != 0) return result;
    }
    
    // Try USER32
    if (strcmp(dll, "USER32.dll") == 0 || strcmp(dll, "user32.dll") == 0 ||
        strcmp(dll, "USER32.DLL") == 0) {
        int result = handle_user32(emu, func);
        if (result != 0) return result;
    }
    
    // Try MSVCRT
    if (strcmp(dll, "MSVCRT.dll") == 0 || strcmp(dll, "msvcrt.dll") == 0 ||
        strcmp(dll, "MSVCRT.DLL") == 0 || strcmp(dll, "msvcrt.DLL") == 0) {
        int result = handle_msvcrt(emu, func);
        if (result != 0) return result;
    }
    
    // Unhandled function
    uart_puts("[WinAPI] WARNING: Unhandled function ");
    uart_puts(dll);
    uart_puts("!");
    uart_puts(func);
    uart_puts("\r\n");
    
    // Return 0 and hope for the best
    emu->regs.eax = 0;
    return 1;
}
