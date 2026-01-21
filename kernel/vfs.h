/*
 * KikiOS Virtual File System
 *
 * Simple in-memory filesystem with hierarchical directories
 */

#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

// File types
#define VFS_FILE      1
#define VFS_DIRECTORY 2

// Max limits
#define VFS_MAX_NAME     64
#define VFS_MAX_CHILDREN 32
#define VFS_MAX_PATH     256
#define VFS_MAX_INODES   256

// Forward declaration
struct vfs_node;

// Directory entry
typedef struct vfs_node {
    char name[VFS_MAX_NAME];
    uint8_t type;                           // VFS_FILE or VFS_DIRECTORY

    // For files
    char *data;                             // File contents
    size_t size;                            // File size
    size_t capacity;                        // Allocated capacity

    // For directories
    struct vfs_node *children[VFS_MAX_CHILDREN];
    int child_count;

    // Tree structure
    struct vfs_node *parent;
} vfs_node_t;

// Initialize the filesystem
void vfs_init(void);

// Path operations
vfs_node_t *vfs_lookup(const char *path);           // Returns static node - do NOT free
vfs_node_t *vfs_open_handle(const char *path);      // Allocates - must call vfs_close_handle
void vfs_close_handle(vfs_node_t *node);            // Free handle from vfs_open_handle
vfs_node_t *vfs_get_root(void);
vfs_node_t *vfs_get_cwd(void);
int vfs_set_cwd(const char *path);
int vfs_get_cwd_path(char *buf, size_t size);

// Directory operations
vfs_node_t *vfs_mkdir(const char *path);
int vfs_readdir(vfs_node_t *dir, int index, char *name, size_t name_size, uint8_t *type);

// File operations
vfs_node_t *vfs_create(const char *path);
int vfs_read(vfs_node_t *file, char *buf, size_t size, size_t offset);
int vfs_write(vfs_node_t *file, const char *buf, size_t size);
int vfs_append(vfs_node_t *file, const char *buf, size_t size);

// Delete file
int vfs_delete(const char *path);

// Delete empty directory
int vfs_delete_dir(const char *path);

// Delete file or directory recursively
int vfs_delete_recursive(const char *path);

// Rename (same directory only)
int vfs_rename(const char *path, const char *newname);

// Utility
int vfs_is_dir(vfs_node_t *node);
int vfs_is_file(vfs_node_t *node);

#endif
