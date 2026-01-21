/*
 * PE (Portable Executable) Loader for KikiOS
 * 
 * Parses and loads Windows .exe files into memory
 */

#include "pe.h"
#include "memory.h"
#include "string.h"
#include "printf.h"
#include <stddef.h>

// External declarations
extern void uart_puts(const char *s);

// Validate PE file
int pe_validate(const uint8_t *data, uint32_t size) {
    if (size < sizeof(dos_header_t)) {
        uart_puts("[PE] File too small for DOS header\r\n");
        return -1;
    }
    
    dos_header_t *dos = (dos_header_t *)data;
    
    // Check DOS magic
    if (dos->e_magic != DOS_MAGIC) {
        uart_puts("[PE] Invalid DOS magic (not MZ)\r\n");
        return -1;
    }
    
    // Check PE header offset
    if (dos->e_lfanew + 4 > size) {
        uart_puts("[PE] Invalid PE header offset\r\n");
        return -1;
    }
    
    // Check PE signature
    uint32_t *pe_sig = (uint32_t *)(data + dos->e_lfanew);
    if (*pe_sig != PE_SIGNATURE) {
        uart_puts("[PE] Invalid PE signature\r\n");
        return -1;
    }
    
    // Get COFF header
    coff_header_t *coff = (coff_header_t *)(data + dos->e_lfanew + 4);
    
    // Check machine type - we only support x86 (i386)
    if (coff->machine != IMAGE_FILE_MACHINE_I386) {
        uart_puts("[PE] Unsupported machine type (only i386 supported)\r\n");
        return -1;
    }
    
    // Check it's an executable
    if (!(coff->characteristics & IMAGE_FILE_EXECUTABLE_IMAGE)) {
        uart_puts("[PE] Not an executable image\r\n");
        return -1;
    }
    
    return 0;
}

// Load PE file into memory
int pe_load(const uint8_t *data, uint32_t size, pe_image_t *image) {
    if (pe_validate(data, size) != 0) {
        return -1;
    }
    
    dos_header_t *dos = (dos_header_t *)data;
    coff_header_t *coff = (coff_header_t *)(data + dos->e_lfanew + 4);
    pe32_optional_header_t *opt = (pe32_optional_header_t *)((uint8_t *)coff + sizeof(coff_header_t));
    
    // Check optional header magic
    if (opt->magic != PE32_MAGIC) {
        uart_puts("[PE] Only PE32 (32-bit) executables supported\r\n");
        return -1;
    }
    
    // Fill in image info
    image->machine = coff->machine;
    image->subsystem = opt->subsystem;
    image->is_dll = (coff->characteristics & IMAGE_FILE_DLL) ? 1 : 0;
    image->image_size = opt->size_of_image;
    
    // Allocate memory for image
    image->base = malloc(opt->size_of_image);
    if (!image->base) {
        uart_puts("[PE] Failed to allocate memory for image\r\n");
        return -1;
    }
    
    // Zero the memory
    memset(image->base, 0, opt->size_of_image);
    
    // Copy headers
    memcpy(image->base, data, opt->size_of_headers);
    
    // Get section headers
    section_header_t *sections = (section_header_t *)((uint8_t *)opt + coff->optional_header_size);
    image->num_sections = coff->num_sections;
    if (image->num_sections > 16) image->num_sections = 16;
    
    uart_puts("[PE] Loading sections:\r\n");
    
    // Load each section
    for (int i = 0; i < coff->num_sections && i < 16; i++) {
        section_header_t *sec = &sections[i];
        
        // Copy section info
        memcpy(image->sections[i].name, sec->name, 8);
        image->sections[i].name[8] = '\0';
        image->sections[i].vaddr = sec->virtual_address;
        image->sections[i].vsize = sec->virtual_size;
        image->sections[i].flags = sec->characteristics;
        
        uart_puts("  ");
        uart_puts(image->sections[i].name);
        uart_puts("\r\n");
        
        // Copy section data if it has raw data
        if (sec->raw_data_size > 0 && sec->raw_data_ptr > 0) {
            if (sec->raw_data_ptr + sec->raw_data_size <= size) {
                uint32_t copy_size = sec->raw_data_size;
                if (copy_size > sec->virtual_size && sec->virtual_size > 0) {
                    copy_size = sec->virtual_size;
                }
                memcpy(image->base + sec->virtual_address,
                       data + sec->raw_data_ptr,
                       copy_size);
            }
        }
    }
    
    // Calculate entry point
    image->entry_point = (uint32_t)(uintptr_t)image->base + opt->entry_point;
    
    // Process relocations if image is loaded at different base
    uint32_t load_delta = (uint32_t)(uintptr_t)image->base - opt->image_base;
    
    if (load_delta != 0 && opt->num_data_dirs > IMAGE_DIRECTORY_ENTRY_BASERELOC) {
        data_directory_t *data_dirs = (data_directory_t *)((uint8_t *)opt + 
            offsetof(pe32_optional_header_t, stack_reserve) + 16);  // After standard fields
        
        data_directory_t *reloc_dir = &data_dirs[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        
        if (reloc_dir->virtual_address != 0 && reloc_dir->size > 0) {
            uart_puts("[PE] Applying relocations...\r\n");
            
            uint8_t *reloc_ptr = image->base + reloc_dir->virtual_address;
            uint8_t *reloc_end = reloc_ptr + reloc_dir->size;
            
            while (reloc_ptr < reloc_end) {
                base_reloc_block_t *block = (base_reloc_block_t *)reloc_ptr;
                
                if (block->block_size == 0) break;
                
                int num_entries = (block->block_size - sizeof(base_reloc_block_t)) / 2;
                uint16_t *entries = (uint16_t *)(reloc_ptr + sizeof(base_reloc_block_t));
                
                for (int i = 0; i < num_entries; i++) {
                    uint16_t entry = entries[i];
                    uint8_t type = entry >> 12;
                    uint16_t offset = entry & 0xFFF;
                    
                    if (type == IMAGE_REL_BASED_HIGHLOW) {
                        uint32_t *patch = (uint32_t *)(image->base + block->page_rva + offset);
                        *patch += load_delta;
                    }
                    // Skip IMAGE_REL_BASED_ABSOLUTE (padding)
                }
                
                reloc_ptr += block->block_size;
            }
        }
    }
    
    uart_puts("[PE] Image loaded successfully\r\n");
    return 0;
}

// Unload PE image
void pe_unload(pe_image_t *image) {
    if (image && image->base) {
        free(image->base);
        image->base = NULL;
    }
}

// Get import DLL name by index
const char *pe_get_import_dll(pe_image_t *image, int index) {
    if (!image || !image->base) return NULL;
    
    dos_header_t *dos = (dos_header_t *)image->base;
    coff_header_t *coff = (coff_header_t *)(image->base + dos->e_lfanew + 4);
    pe32_optional_header_t *opt = (pe32_optional_header_t *)((uint8_t *)coff + sizeof(coff_header_t));
    
    if (opt->num_data_dirs <= IMAGE_DIRECTORY_ENTRY_IMPORT) return NULL;
    
    data_directory_t *data_dirs = (data_directory_t *)((uint8_t *)opt + 
        offsetof(pe32_optional_header_t, stack_reserve) + 16);
    
    data_directory_t *import_dir = &data_dirs[IMAGE_DIRECTORY_ENTRY_IMPORT];
    
    if (import_dir->virtual_address == 0) return NULL;
    
    import_directory_t *imports = (import_directory_t *)(image->base + import_dir->virtual_address);
    
    int i = 0;
    while (imports->name_rva != 0) {
        if (i == index) {
            return (const char *)(image->base + imports->name_rva);
        }
        imports++;
        i++;
    }
    
    return NULL;
}
