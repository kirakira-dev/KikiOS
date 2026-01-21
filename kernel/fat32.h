/*
 * KikiOS FAT32 Filesystem Driver
 *
 * Read/write FAT32 implementation for persistent storage.
 */

#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>

// FAT32 directory entry attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F  // Long filename entry

// Special cluster values
#define FAT32_EOC           0x0FFFFFF8  // End of cluster chain (>= this value)
#define FAT32_FREE          0x00000000
#define FAT32_BAD           0x0FFFFFF7

// FAT32 directory entry (32 bytes)
typedef struct __attribute__((packed)) {
    char name[11];          // 8.3 filename (space-padded)
    uint8_t attr;           // File attributes
    uint8_t nt_reserved;    // Reserved for Windows NT
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;    // High 16 bits of first cluster
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_lo;    // Low 16 bits of first cluster
    uint32_t size;          // File size in bytes
} fat32_dirent_t;

// FAT32 long filename entry (32 bytes)
typedef struct __attribute__((packed)) {
    uint8_t order;          // Sequence number
    uint16_t name1[5];      // Characters 1-5
    uint8_t attr;           // Always 0x0F
    uint8_t type;           // Always 0
    uint8_t checksum;       // Checksum of short name
    uint16_t name2[6];      // Characters 6-11
    uint16_t cluster;       // Always 0
    uint16_t name3[2];      // Characters 12-13
} fat32_lfn_t;

// FAT32 filesystem info
typedef struct {
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t fat_size;          // Sectors per FAT
    uint32_t root_cluster;      // First cluster of root directory
    uint32_t data_start;        // First data sector
    uint32_t total_clusters;
} fat32_fs_t;

// Initialize FAT32 filesystem (reads from virtio-blk)
int fat32_init(void);

// Read a file's contents into buffer
// path: absolute path like "/bin/hello.elf"
// buf: buffer to read into
// size: max bytes to read
// Returns: bytes read, or -1 on error
int fat32_read_file(const char *path, void *buf, size_t size);

// Read file with offset support (efficient partial reads)
// offset: byte offset to start reading from
// Returns: bytes read, or -1 on error
int fat32_read_file_offset(const char *path, void *buf, size_t size, size_t offset);

// Get file size
// Returns: file size in bytes, or -1 on error
int fat32_file_size(const char *path);

// Check if path is a directory
// Returns: 1 if directory, 0 if file, -1 if not found
int fat32_is_dir(const char *path);

// List directory contents
// path: directory path
// callback: called for each entry with (name, is_dir, size, user_data)
// Returns: 0 on success, -1 on error
typedef void (*fat32_dir_callback)(const char *name, int is_dir, uint32_t size, void *user_data);
int fat32_list_dir(const char *path, fat32_dir_callback callback, void *user_data);

// Get filesystem info
fat32_fs_t *fat32_get_fs_info(void);

// Write operations (new!)
// Create an empty file
// Returns 0 on success, -1 on error
int fat32_create_file(const char *path);

// Create a directory
// Returns 0 on success, -1 on error
int fat32_mkdir(const char *path);

// Write data to a file (creates if not exists, overwrites if exists)
// Returns bytes written, or -1 on error
int fat32_write_file(const char *path, const void *buf, size_t size);

// Delete a file (not directories)
// Returns 0 on success, -1 on error
int fat32_delete(const char *path);

// Delete an empty directory
// Returns 0 on success, -1 on error (including if not empty)
int fat32_delete_dir(const char *path);

// Delete a file or directory recursively
// Returns 0 on success, -1 on error
int fat32_delete_recursive(const char *path);

// Rename a file or directory (same directory only)
// oldpath: full path to existing file/dir
// newname: new filename (just the name, not full path)
// Returns 0 on success, -1 on error
int fat32_rename(const char *oldpath, const char *newname);

// Get disk space stats
// Returns total disk space in KB
int fat32_get_total_kb(void);

// Returns free disk space in KB (counts free clusters)
int fat32_get_free_kb(void);

#endif
