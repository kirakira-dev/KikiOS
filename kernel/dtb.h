/*
 * KikiOS Device Tree Blob (DTB) Parser
 *
 * Parses the flattened device tree to detect hardware configuration,
 * most importantly RAM size.
 */

#ifndef DTB_H
#define DTB_H

#include <stdint.h>

// DTB location - QEMU places it at start of RAM (0x40000000)
// Our linker script starts data/BSS at 0x40010000 to avoid overwriting it
#define DTB_ADDR 0x40000000

// DTB magic number (big-endian: 0xd00dfeed)
#define DTB_MAGIC 0xd00dfeed

// DTB tokens
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

// DTB header structure (all fields are big-endian)
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

// Memory region info
struct dtb_memory_info {
    uint64_t base;
    uint64_t size;
};

// Parse DTB and extract memory information
// Returns 0 on success, -1 on failure
int dtb_parse(void *dtb_addr, struct dtb_memory_info *mem_info);

// Get a human-readable description of parsing result
const char *dtb_get_error(void);

#endif
