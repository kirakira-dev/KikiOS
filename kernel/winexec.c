/*
 * Windows Executable Runner for KikiOS
 * 
 * Ties together PE loader, x86 emulator, and Windows API stubs
 * to run Windows .exe files on KikiOS.
 */

#include "pe.h"
#include "x86emu.h"
#include "winapi.h"
#include "vfs.h"
#include "printf.h"
#include <stddef.h>

// External declarations
#include "memory.h"
extern void *memset(void *s, int c, size_t n);
extern void uart_puts(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);

// Console output function pointer (set by caller)
static void (*console_output_fn)(const char *str, int len) = NULL;

// Default console output via uart
static void default_console_output(const char *str, int len) {
    (void)len;
    uart_puts(str);
}

// Console output wrapper for Windows API
static void winexec_console_output(const char *str, int len) {
    if (console_output_fn) {
        console_output_fn(str, len);
    } else {
        default_console_output(str, len);
    }
}

// Set console output function
void winexec_set_output(void (*func)(const char *str, int len)) {
    console_output_fn = func;
}

// Process PE imports and set up IAT
static int process_imports(pe_image_t *image, x86emu_state_t *emu) {
    if (!image || !image->base) return -1;
    
    // Get DOS header
    uint8_t *base = image->base;
    uint16_t dos_magic = base[0] | (base[1] << 8);
    if (dos_magic != 0x5A4D) return -1;
    
    uint32_t pe_offset = base[0x3C] | (base[0x3D] << 8) | (base[0x3E] << 16) | (base[0x3F] << 24);
    
    // Skip to optional header
    uint8_t *opt_header = base + pe_offset + 4 + 20;  // PE sig + COFF header
    
    uint16_t opt_magic = opt_header[0] | (opt_header[1] << 8);
    if (opt_magic != 0x10B) return -1;  // Not PE32
    
    // Get data directories
    uint32_t num_dirs = opt_header[92] | (opt_header[93] << 8) | (opt_header[94] << 16) | (opt_header[95] << 24);
    if (num_dirs <= 1) return 0;  // No imports
    
    // Import directory is at offset 104 in optional header (index 1)
    uint8_t *import_dir_entry = opt_header + 104;
    uint32_t import_rva = import_dir_entry[0] | (import_dir_entry[1] << 8) | 
                          (import_dir_entry[2] << 16) | (import_dir_entry[3] << 24);
    
    if (import_rva == 0) return 0;  // No imports
    
    printf("[WinExec] Processing imports at RVA 0x%08X\r\n", import_rva);
    
    // Process import descriptors
    uint8_t *import_desc = base + import_rva;
    
    while (1) {
        // Import descriptor: ILT RVA, TimeDateStamp, ForwarderChain, Name RVA, IAT RVA
        uint32_t ilt_rva = import_desc[0] | (import_desc[1] << 8) | 
                          (import_desc[2] << 16) | (import_desc[3] << 24);
        uint32_t name_rva = import_desc[12] | (import_desc[13] << 8) | 
                           (import_desc[14] << 16) | (import_desc[15] << 24);
        uint32_t iat_rva = import_desc[16] | (import_desc[17] << 8) | 
                          (import_desc[18] << 16) | (import_desc[19] << 24);
        
        // End of imports
        if (name_rva == 0) break;
        
        // Get DLL name
        const char *dll_name = (const char *)(base + name_rva);
        
        uart_puts("[WinExec] Import DLL: ");
        uart_puts(dll_name);
        uart_puts("\r\n");
        
        // Use ILT if available, otherwise use IAT
        uint32_t lookup_rva = ilt_rva ? ilt_rva : iat_rva;
        uint8_t *lookup = base + lookup_rva;
        uint8_t *iat = base + iat_rva;
        
        int entry_idx = 0;
        while (1) {
            uint32_t entry = lookup[0] | (lookup[1] << 8) | 
                            (lookup[2] << 16) | (lookup[3] << 24);
            
            if (entry == 0) break;
            
            const char *func_name;
            
            if (entry & 0x80000000) {
                // Import by ordinal - not supported
                uart_puts("[WinExec] WARNING: Ordinal import not supported\r\n");
                func_name = "ordinal";
            } else {
                // Import by name - name is at RVA, skip 2-byte hint
                func_name = (const char *)(base + entry + 2);
            }
            
            // Calculate IAT entry address (absolute)
            uint32_t iat_addr = (uint32_t)(uintptr_t)(iat + entry_idx * 4);
            
            // Register this import
            x86emu_add_import(emu, iat_addr, dll_name, func_name);
            
            // Write a placeholder to IAT (the address itself for indirect call)
            // When CALL [iat_addr] is executed, it will call iat_addr
            // which we intercept in the emulator
            iat[entry_idx * 4 + 0] = iat_addr & 0xFF;
            iat[entry_idx * 4 + 1] = (iat_addr >> 8) & 0xFF;
            iat[entry_idx * 4 + 2] = (iat_addr >> 16) & 0xFF;
            iat[entry_idx * 4 + 3] = (iat_addr >> 24) & 0xFF;
            
            lookup += 4;
            entry_idx++;
        }
        
        import_desc += 20;  // Next import descriptor
    }
    
    return 0;
}

// Run a Windows executable
int winexec_run(const char *path) {
    uart_puts("[WinExec] Loading: ");
    uart_puts(path);
    uart_puts("\r\n");
    
    // Look up file
    vfs_node_t *file = vfs_lookup(path);
    if (!file) {
        uart_puts("[WinExec] Failed to open file\r\n");
        return -1;
    }
    
    if (vfs_is_dir(file)) {
        uart_puts("[WinExec] Path is a directory\r\n");
        return -1;
    }
    
    // Get file size
    int file_size = (int)file->size;
    if (file_size <= 0) {
        uart_puts("[WinExec] Empty or invalid file\r\n");
        return -1;
    }
    
    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        uart_puts("[WinExec] Failed to allocate memory for file\r\n");
        return -1;
    }
    
    if (vfs_read(file, (char *)file_data, file_size, 0) != file_size) {
        uart_puts("[WinExec] Failed to read file\r\n");
        free(file_data);
        return -1;
    }
    
    // Load PE
    pe_image_t image;
    memset(&image, 0, sizeof(image));
    
    if (pe_load(file_data, file_size, &image) != 0) {
        uart_puts("[WinExec] Failed to load PE image\r\n");
        free(file_data);
        return -1;
    }
    
    // Free file data (image is now in separate memory)
    free(file_data);
    
    printf("[WinExec] Image loaded at 0x%08X, entry point: 0x%08X\r\n", 
           (uint32_t)(uintptr_t)image.base, image.entry_point);
    
    // Check if it's a DLL
    if (image.is_dll) {
        uart_puts("[WinExec] Cannot execute DLLs directly\r\n");
        pe_unload(&image);
        return -1;
    }
    
    // Initialize x86 emulator
    x86emu_state_t emu;
    if (x86emu_init(&emu, image.base, image.image_size) != 0) {
        uart_puts("[WinExec] Failed to initialize emulator\r\n");
        pe_unload(&image);
        return -1;
    }
    
    // Process imports
    if (process_imports(&image, &emu) != 0) {
        uart_puts("[WinExec] Failed to process imports\r\n");
        pe_unload(&image);
        return -1;
    }
    
    // Initialize Windows API
    winapi_init();
    winapi_set_console_output(winexec_console_output);
    
    // Set up API callback
    emu.winapi_call = winapi_call;
    emu.console_write = winexec_console_output;
    
    // Set entry point
    x86emu_set_entry(&emu, image.entry_point);
    
    // Push a return address that will cause clean exit
    // When the program returns, it will try to return to this address
    // which we'll detect as end of execution
    x86emu_push32(&emu, 0xDEADBEEF);
    
    uart_puts("[WinExec] Starting execution...\r\n");
    
    // Run the emulator
    int result = x86emu_run(&emu);
    
    // Get exit code
    int exit_code = winapi_get_exit_code();
    
    printf("[WinExec] Execution finished with code %d (0x%08X)\r\n", exit_code, exit_code);
    
    // Dump final register state
    x86emu_dump_regs(&emu);
    
    // Clean up
    if (emu.stack) {
        free(emu.stack);
    }
    pe_unload(&image);
    
    return exit_code;
}

// Check if Windows executable support is available
int winexec_supported(void) {
    return 1;  // Always available
}
