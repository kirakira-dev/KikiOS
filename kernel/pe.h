/*
 * PE (Portable Executable) File Format Definitions
 * 
 * This header defines structures for parsing Windows .exe files
 */

#ifndef _PE_H
#define _PE_H

#include <stdint.h>

// DOS Header (MZ)
typedef struct {
    uint16_t e_magic;      // "MZ"
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;     // Offset to PE header
} __attribute__((packed)) dos_header_t;

#define DOS_MAGIC 0x5A4D  // "MZ"

// PE Signature
#define PE_SIGNATURE 0x00004550  // "PE\0\0"

// COFF File Header
typedef struct {
    uint16_t machine;
    uint16_t num_sections;
    uint32_t timestamp;
    uint32_t symbol_table_ptr;
    uint32_t num_symbols;
    uint16_t optional_header_size;
    uint16_t characteristics;
} __attribute__((packed)) coff_header_t;

// Machine types
#define IMAGE_FILE_MACHINE_I386   0x014c
#define IMAGE_FILE_MACHINE_AMD64  0x8664

// Characteristics
#define IMAGE_FILE_EXECUTABLE_IMAGE    0x0002
#define IMAGE_FILE_32BIT_MACHINE       0x0100
#define IMAGE_FILE_DLL                 0x2000

// Optional Header (PE32)
typedef struct {
    uint16_t magic;                // 0x10b for PE32, 0x20b for PE32+
    uint8_t  major_linker_ver;
    uint8_t  minor_linker_ver;
    uint32_t size_of_code;
    uint32_t size_of_init_data;
    uint32_t size_of_uninit_data;
    uint32_t entry_point;          // RVA of entry point
    uint32_t base_of_code;
    uint32_t base_of_data;         // PE32 only
    uint32_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_os_ver;
    uint16_t minor_os_ver;
    uint16_t major_image_ver;
    uint16_t minor_image_ver;
    uint16_t major_subsystem_ver;
    uint16_t minor_subsystem_ver;
    uint32_t win32_version;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint32_t stack_reserve;
    uint32_t stack_commit;
    uint32_t heap_reserve;
    uint32_t heap_commit;
    uint32_t loader_flags;
    uint32_t num_data_dirs;
} __attribute__((packed)) pe32_optional_header_t;

#define PE32_MAGIC  0x10b
#define PE32P_MAGIC 0x20b

// Subsystem values
#define IMAGE_SUBSYSTEM_CONSOLE 3
#define IMAGE_SUBSYSTEM_GUI     2

// Data Directory
typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} __attribute__((packed)) data_directory_t;

// Data directory indices
#define IMAGE_DIRECTORY_ENTRY_EXPORT     0
#define IMAGE_DIRECTORY_ENTRY_IMPORT     1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE   2
#define IMAGE_DIRECTORY_ENTRY_BASERELOC  5
#define IMAGE_DIRECTORY_ENTRY_IAT        12

// Section Header
typedef struct {
    char     name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t raw_data_size;
    uint32_t raw_data_ptr;
    uint32_t reloc_ptr;
    uint32_t linenum_ptr;
    uint16_t num_relocs;
    uint16_t num_linenums;
    uint32_t characteristics;
} __attribute__((packed)) section_header_t;

// Section characteristics
#define IMAGE_SCN_CNT_CODE          0x00000020
#define IMAGE_SCN_CNT_INIT_DATA     0x00000040
#define IMAGE_SCN_CNT_UNINIT_DATA   0x00000080
#define IMAGE_SCN_MEM_EXECUTE       0x20000000
#define IMAGE_SCN_MEM_READ          0x40000000
#define IMAGE_SCN_MEM_WRITE         0x80000000

// Import Directory
typedef struct {
    uint32_t import_lookup_table;
    uint32_t timestamp;
    uint32_t forwarder_chain;
    uint32_t name_rva;
    uint32_t import_address_table;
} __attribute__((packed)) import_directory_t;

// Import Lookup Table Entry (PE32)
#define IMAGE_ORDINAL_FLAG32 0x80000000

// Base Relocation Block
typedef struct {
    uint32_t page_rva;
    uint32_t block_size;
    // Followed by uint16_t entries
} __attribute__((packed)) base_reloc_block_t;

#define IMAGE_REL_BASED_ABSOLUTE    0
#define IMAGE_REL_BASED_HIGHLOW     3
#define IMAGE_REL_BASED_DIR64       10

// PE loaded image structure
typedef struct {
    uint8_t  *base;           // Base address of loaded image
    uint32_t  image_size;     // Total size of image in memory
    uint32_t  entry_point;    // Entry point (absolute address)
    uint16_t  machine;        // Machine type (I386, AMD64)
    uint16_t  subsystem;      // Console or GUI
    int       is_dll;         // Is this a DLL?
    
    // Sections
    int       num_sections;
    struct {
        char     name[9];
        uint32_t vaddr;
        uint32_t vsize;
        uint32_t flags;
    } sections[16];
    
} pe_image_t;

// PE loader functions
int pe_validate(const uint8_t *data, uint32_t size);
int pe_load(const uint8_t *data, uint32_t size, pe_image_t *image);
void pe_unload(pe_image_t *image);
const char *pe_get_import_dll(pe_image_t *image, int index);

#endif // _PE_H
