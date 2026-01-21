/*
 * KikiOS Virtual File System
 *
 * Now backed by FAT32 on persistent storage with read/write support.
 * Falls back to in-memory if no disk is available.
 */

#include "vfs.h"
#include "fat32.h"
#include "string.h"
#include "memory.h"
#include "printf.h"

// Current working directory path
static char cwd_path[VFS_MAX_PATH] = "/";

// Is FAT32 available?
static int use_fat32 = 0;

// For in-memory fallback (minimal, just /tmp)
static vfs_node_t inodes[VFS_MAX_INODES];
static int inode_count = 0;
static vfs_node_t *mem_root = NULL;

// Allocate a new in-memory inode
static vfs_node_t *alloc_inode(void) {
    if (inode_count >= VFS_MAX_INODES) {
        return NULL;
    }
    vfs_node_t *node = &inodes[inode_count++];
    memset(node, 0, sizeof(vfs_node_t));
    return node;
}

// Create an in-memory directory node
static vfs_node_t *create_mem_dir(const char *name, vfs_node_t *parent) {
    vfs_node_t *dir = alloc_inode();
    if (!dir) return NULL;

    int i;
    for (i = 0; name[i] && i < VFS_MAX_NAME - 1; i++) {
        dir->name[i] = name[i];
    }
    dir->name[i] = '\0';

    dir->type = VFS_DIRECTORY;
    dir->parent = parent;
    dir->child_count = 0;

    if (parent) {
        if (parent->child_count >= VFS_MAX_CHILDREN) {
            return NULL;
        }
        parent->children[parent->child_count++] = dir;
    }

    return dir;
}

// Create an in-memory file node
static vfs_node_t *create_mem_file(const char *name, vfs_node_t *parent) {
    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }

    vfs_node_t *file = alloc_inode();
    if (!file) return NULL;

    int i;
    for (i = 0; name[i] && i < VFS_MAX_NAME - 1; i++) {
        file->name[i] = name[i];
    }
    file->name[i] = '\0';

    file->type = VFS_FILE;
    file->parent = parent;
    file->data = NULL;
    file->size = 0;
    file->capacity = 0;

    if (parent->child_count >= VFS_MAX_CHILDREN) {
        return NULL;
    }
    parent->children[parent->child_count++] = file;

    return file;
}

// Find child by name in an in-memory directory
static vfs_node_t *find_mem_child(vfs_node_t *dir, const char *name) {
    if (!dir || dir->type != VFS_DIRECTORY) {
        return NULL;
    }

    for (int i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0) {
            return dir->children[i];
        }
    }
    return NULL;
}

// Lookup in-memory filesystem
static vfs_node_t *mem_lookup(const char *path) {
    if (!path || !path[0]) {
        return mem_root;
    }

    vfs_node_t *current;
    char pathbuf[VFS_MAX_PATH];
    char *token;
    char *rest;

    if (path[0] == '/') {
        current = mem_root;
        path++;
    } else {
        // Relative path - not supported for in-memory, just use root
        current = mem_root;
    }

    if (!path[0]) {
        return current;
    }

    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    rest = pathbuf;
    while ((token = strtok_r(rest, "/", &rest)) != NULL) {
        if (token[0] == '\0') continue;

        if (strcmp(token, ".") == 0) continue;

        if (strcmp(token, "..") == 0) {
            if (current->parent) {
                current = current->parent;
            }
            continue;
        }

        vfs_node_t *child = find_mem_child(current, token);
        if (!child) {
            return NULL;
        }
        current = child;
    }

    return current;
}

// Initialize the filesystem
void vfs_init(void) {
    // Try to use FAT32
    if (fat32_init() == 0) {
        use_fat32 = 1;

        // Set initial cwd to /home/user if it exists, else /
        if (fat32_is_dir("/home/user") == 1) {
            strcpy(cwd_path, "/home/user");
        } else if (fat32_is_dir("/home") == 1) {
            strcpy(cwd_path, "/home");
        } else {
            strcpy(cwd_path, "/");
        }
    } else {
        use_fat32 = 0;

        // Create minimal in-memory filesystem
        inode_count = 0;
        mem_root = alloc_inode();
        mem_root->name[0] = '/';
        mem_root->name[1] = '\0';
        mem_root->type = VFS_DIRECTORY;
        mem_root->parent = mem_root;
        mem_root->child_count = 0;

        // Create /tmp for temporary files
        create_mem_dir("tmp", mem_root);

        strcpy(cwd_path, "/");
    }

    printf("[VFS] %s, cwd=%s\n", use_fat32 ? "FAT32" : "in-memory", cwd_path);
}

// Resolve a path to a node (returns static/cached node - do NOT free)
// For FAT32, returns a static temp node
// For in-memory, returns the actual node
vfs_node_t *vfs_lookup(const char *path) {
    static vfs_node_t temp_node;
    static char stored_path[VFS_MAX_PATH];
    char fullpath[VFS_MAX_PATH];

    // Build full path
    if (!path || !path[0]) {
        strcpy(fullpath, cwd_path);
    } else if (path[0] == '/') {
        strncpy(fullpath, path, VFS_MAX_PATH - 1);
        fullpath[VFS_MAX_PATH - 1] = '\0';
    } else {
        // Relative path
        if (strcmp(cwd_path, "/") == 0) {
            snprintf(fullpath, VFS_MAX_PATH, "/%s", path);
        } else {
            snprintf(fullpath, VFS_MAX_PATH, "%s/%s", cwd_path, path);
        }
    }

    // Normalize . and ..
    char normalized[VFS_MAX_PATH];
    char *parts[32];
    int depth = 0;

    char *rest = fullpath;
    char *token;
    if (*rest == '/') rest++;

    while ((token = strtok_r(rest, "/", &rest)) != NULL) {
        if (token[0] == '\0' || strcmp(token, ".") == 0) {
            continue;
        }
        if (strcmp(token, "..") == 0) {
            if (depth > 0) depth--;
            continue;
        }
        parts[depth++] = token;
    }

    // Rebuild normalized path
    normalized[0] = '\0';
    for (int i = 0; i < depth; i++) {
        strcat(normalized, "/");
        strcat(normalized, parts[i]);
    }
    if (normalized[0] == '\0') {
        strcpy(normalized, "/");
    }

    if (use_fat32) {
        int is_dir = fat32_is_dir(normalized);
        if (is_dir < 0) {
            return NULL;  // Not found
        }

        // Use static node for lookup (not for file handles!)
        memset(&temp_node, 0, sizeof(temp_node));

        // Extract name from path
        char *last_slash = NULL;
        for (char *p = normalized; *p; p++) {
            if (*p == '/') last_slash = p;
        }
        if (last_slash && last_slash[1]) {
            strncpy(temp_node.name, last_slash + 1, VFS_MAX_NAME - 1);
        } else {
            strcpy(temp_node.name, "/");
        }

        temp_node.type = is_dir ? VFS_DIRECTORY : VFS_FILE;
        if (!is_dir) {
            temp_node.size = fat32_file_size(normalized);
        }

        // Store path in static buffer
        strcpy(stored_path, normalized);
        temp_node.data = stored_path;

        return &temp_node;
    } else {
        return mem_lookup(normalized);
    }
}

// Open a file handle (allocates - caller must free with vfs_close_handle)
// This is for kapi->open, NOT for internal kernel lookups
vfs_node_t *vfs_open_handle(const char *path) {
    // First do a lookup to check if file exists and get info
    vfs_node_t *temp = vfs_lookup(path);
    if (!temp) return NULL;

    // Allocate a new node for this handle
    vfs_node_t *node = malloc(sizeof(vfs_node_t));
    if (!node) return NULL;

    // Copy the temp node data
    memcpy(node, temp, sizeof(vfs_node_t));

    // Allocate and copy the path
    if (temp->data) {
        char *path_copy = malloc(VFS_MAX_PATH);
        if (!path_copy) { free(node); return NULL; }
        strcpy(path_copy, (char*)temp->data);
        node->data = path_copy;
    }

    return node;
}

// Close/free a handle returned by vfs_open_handle
void vfs_close_handle(vfs_node_t *node) {
    if (!node) return;
    if (node->data) free(node->data);
    free(node);
}

vfs_node_t *vfs_get_root(void) {
    return vfs_lookup("/");
}

vfs_node_t *vfs_get_cwd(void) {
    return vfs_lookup(cwd_path);
}

int vfs_set_cwd(const char *path) {
    char fullpath[VFS_MAX_PATH];

    if (!path || !path[0]) {
        return -1;
    }

    // Build full path
    if (path[0] == '/') {
        strncpy(fullpath, path, VFS_MAX_PATH - 1);
        fullpath[VFS_MAX_PATH - 1] = '\0';
    } else {
        if (strcmp(cwd_path, "/") == 0) {
            snprintf(fullpath, VFS_MAX_PATH, "/%s", path);
        } else {
            snprintf(fullpath, VFS_MAX_PATH, "%s/%s", cwd_path, path);
        }
    }

    // Normalize the path
    char normalized[VFS_MAX_PATH];
    char *parts[32];
    int depth = 0;

    char pathcopy[VFS_MAX_PATH];
    strcpy(pathcopy, fullpath);

    char *rest = pathcopy;
    char *token;
    if (*rest == '/') rest++;

    while ((token = strtok_r(rest, "/", &rest)) != NULL) {
        if (token[0] == '\0' || strcmp(token, ".") == 0) {
            continue;
        }
        if (strcmp(token, "..") == 0) {
            if (depth > 0) depth--;
            continue;
        }
        parts[depth++] = token;
    }

    normalized[0] = '\0';
    for (int i = 0; i < depth; i++) {
        strcat(normalized, "/");
        strcat(normalized, parts[i]);
    }
    if (normalized[0] == '\0') {
        strcpy(normalized, "/");
    }

    // Check if it exists and is a directory
    if (use_fat32) {
        if (fat32_is_dir(normalized) != 1) {
            return -1;
        }
    } else {
        vfs_node_t *node = mem_lookup(normalized);
        if (!node || node->type != VFS_DIRECTORY) {
            return -1;
        }
    }

    strcpy(cwd_path, normalized);
    return 0;
}

int vfs_get_cwd_path(char *buf, size_t size) {
    if (!buf || size == 0) return -1;
    strncpy(buf, cwd_path, size - 1);
    buf[size - 1] = '\0';
    return 0;
}

// Directory listing callback context
typedef struct {
    int index;
    int target_index;
    char *name;
    size_t name_size;
    uint8_t *type;
    int found;
} readdir_ctx_t;

static void readdir_callback(const char *name, int is_dir, uint32_t size, void *user_data) {
    (void)size;
    readdir_ctx_t *ctx = (readdir_ctx_t *)user_data;

    if (ctx->index == ctx->target_index) {
        strncpy(ctx->name, name, ctx->name_size - 1);
        ctx->name[ctx->name_size - 1] = '\0';
        if (ctx->type) {
            *ctx->type = is_dir ? VFS_DIRECTORY : VFS_FILE;
        }
        ctx->found = 1;
    }
    ctx->index++;
}

int vfs_readdir(vfs_node_t *dir, int index, char *name, size_t name_size, uint8_t *type) {
    if (!dir || dir->type != VFS_DIRECTORY || !name) {
        return -1;
    }

    if (use_fat32) {
        // Get the path from the node
        const char *dirpath = (const char *)dir->data;
        if (!dirpath) dirpath = "/";

        readdir_ctx_t ctx = {
            .index = 0,
            .target_index = index,
            .name = name,
            .name_size = name_size,
            .type = type,
            .found = 0
        };

        fat32_list_dir(dirpath, readdir_callback, &ctx);

        return ctx.found ? 0 : -1;
    } else {
        if (index < 0 || index >= dir->child_count) {
            return -1;
        }

        vfs_node_t *child = dir->children[index];
        strncpy(name, child->name, name_size - 1);
        name[name_size - 1] = '\0';
        if (type) *type = child->type;

        return 0;
    }
}

vfs_node_t *vfs_mkdir(const char *path) {
    if (use_fat32) {
        // Build full path
        char fullpath[VFS_MAX_PATH];
        if (path[0] == '/') {
            strncpy(fullpath, path, VFS_MAX_PATH - 1);
            fullpath[VFS_MAX_PATH - 1] = '\0';
        } else {
            if (strcmp(cwd_path, "/") == 0) {
                snprintf(fullpath, VFS_MAX_PATH, "/%s", path);
            } else {
                snprintf(fullpath, VFS_MAX_PATH, "%s/%s", cwd_path, path);
            }
        }

        if (fat32_mkdir(fullpath) < 0) {
            return NULL;
        }
        return vfs_lookup(path);
    }

    // In-memory mkdir
    if (!path || !path[0]) return NULL;

    char pathbuf[VFS_MAX_PATH];
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    char *last_slash = NULL;
    for (char *p = pathbuf; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent;
    char *dirname;

    if (last_slash == NULL) {
        parent = mem_lookup(cwd_path);
        dirname = pathbuf;
    } else if (last_slash == pathbuf) {
        parent = mem_root;
        dirname = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent = mem_lookup(pathbuf);
        dirname = last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }

    if (find_mem_child(parent, dirname)) {
        return NULL;  // Already exists
    }

    return create_mem_dir(dirname, parent);
}

vfs_node_t *vfs_create(const char *path) {
    if (use_fat32) {
        // Build full path
        char fullpath[VFS_MAX_PATH];
        if (path[0] == '/') {
            strncpy(fullpath, path, VFS_MAX_PATH - 1);
            fullpath[VFS_MAX_PATH - 1] = '\0';
        } else {
            if (strcmp(cwd_path, "/") == 0) {
                snprintf(fullpath, VFS_MAX_PATH, "/%s", path);
            } else {
                snprintf(fullpath, VFS_MAX_PATH, "%s/%s", cwd_path, path);
            }
        }

        if (fat32_create_file(fullpath) < 0) {
            return NULL;
        }
        return vfs_lookup(path);
    }

    if (!path || !path[0]) return NULL;

    char pathbuf[VFS_MAX_PATH];
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    char *last_slash = NULL;
    for (char *p = pathbuf; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent;
    char *filename;

    if (last_slash == NULL) {
        parent = mem_lookup(cwd_path);
        filename = pathbuf;
    } else if (last_slash == pathbuf) {
        parent = mem_root;
        filename = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent = mem_lookup(pathbuf);
        filename = last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }

    vfs_node_t *existing = find_mem_child(parent, filename);
    if (existing) {
        return existing;
    }

    return create_mem_file(filename, parent);
}

int vfs_read(vfs_node_t *file, char *buf, size_t size, size_t offset) {
    if (!file || file->type != VFS_FILE || !buf) {
        return -1;
    }

    if (use_fat32) {
        // Get path from node
        const char *filepath = (const char *)file->data;
        if (!filepath) return -1;

        // Use offset-aware read - only reads what's needed
        return fat32_read_file_offset(filepath, buf, size, offset);
    } else {
        if (offset >= file->size) {
            return 0;
        }

        size_t to_read = file->size - offset;
        if (to_read > size) to_read = size;

        memcpy(buf, file->data + offset, to_read);
        return (int)to_read;
    }
}

int vfs_write(vfs_node_t *file, const char *buf, size_t size) {
    if (!file || file->type != VFS_FILE) {
        return -1;
    }

    if (use_fat32) {
        // Get path from node
        const char *filepath = (const char *)file->data;
        if (!filepath) return -1;

        return fat32_write_file(filepath, buf, size);
    }

    // In-memory write
    if (size > file->capacity) {
        size_t new_cap = size + 64;
        char *new_data = malloc(new_cap);
        if (!new_data) return -1;

        if (file->data) {
            free(file->data);
        }
        file->data = new_data;
        file->capacity = new_cap;
    }

    memcpy(file->data, buf, size);
    file->size = size;
    return (int)size;
}

int vfs_append(vfs_node_t *file, const char *buf, size_t size) {
    if (!file || file->type != VFS_FILE) {
        return -1;
    }

    if (use_fat32) {
        // Get path from node
        const char *filepath = (const char *)file->data;
        if (!filepath) return -1;

        // For append, we need to read existing content, add new data, and write back
        int file_size = fat32_file_size(filepath);
        if (file_size < 0) file_size = 0;

        char *new_buf = malloc(file_size + size);
        if (!new_buf) return -1;

        // Read existing content
        if (file_size > 0) {
            if (fat32_read_file(filepath, new_buf, file_size) < 0) {
                free(new_buf);
                return -1;
            }
        }

        // Append new data
        memcpy(new_buf + file_size, buf, size);

        // Write back
        int result = fat32_write_file(filepath, new_buf, file_size + size);
        free(new_buf);
        return result >= 0 ? (int)size : -1;
    }

    size_t new_size = file->size + size;

    if (new_size > file->capacity) {
        size_t new_cap = new_size + 64;
        char *new_data = malloc(new_cap);
        if (!new_data) return -1;

        if (file->data) {
            memcpy(new_data, file->data, file->size);
            free(file->data);
        }
        file->data = new_data;
        file->capacity = new_cap;
    }

    memcpy(file->data + file->size, buf, size);
    file->size = new_size;
    return (int)size;
}

// Helper to build full path from possibly relative path
static void build_fullpath(const char *path, char *fullpath) {
    if (path[0] == '/') {
        strncpy(fullpath, path, VFS_MAX_PATH - 1);
        fullpath[VFS_MAX_PATH - 1] = '\0';
    } else {
        if (strcmp(cwd_path, "/") == 0) {
            snprintf(fullpath, VFS_MAX_PATH, "/%s", path);
        } else {
            snprintf(fullpath, VFS_MAX_PATH, "%s/%s", cwd_path, path);
        }
    }
}

int vfs_delete(const char *path) {
    if (use_fat32) {
        char fullpath[VFS_MAX_PATH];
        build_fullpath(path, fullpath);
        return fat32_delete(fullpath);
    }

    // In-memory delete
    if (!path || !path[0]) return -1;

    char pathbuf[VFS_MAX_PATH];
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    char *last_slash = NULL;
    for (char *p = pathbuf; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent;
    char *filename;

    if (last_slash == NULL) {
        parent = mem_lookup(cwd_path);
        filename = pathbuf;
    } else if (last_slash == pathbuf) {
        parent = mem_root;
        filename = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent = mem_lookup(pathbuf);
        filename = last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIRECTORY) {
        return -1;
    }

    // Find the child
    int found_idx = -1;
    for (int i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, filename) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx < 0) return -1;

    vfs_node_t *node = parent->children[found_idx];

    // Don't delete directories with this function
    if (node->type == VFS_DIRECTORY) {
        return -1;
    }

    // Free file data if any
    if (node->data && node->type == VFS_FILE) {
        free(node->data);
    }

    // Remove from parent's children array
    for (int i = found_idx; i < parent->child_count - 1; i++) {
        parent->children[i] = parent->children[i + 1];
    }
    parent->child_count--;

    return 0;
}

int vfs_delete_dir(const char *path) {
    if (use_fat32) {
        char fullpath[VFS_MAX_PATH];
        build_fullpath(path, fullpath);
        return fat32_delete_dir(fullpath);
    }

    // In-memory delete directory
    if (!path || !path[0]) return -1;

    char pathbuf[VFS_MAX_PATH];
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    char *last_slash = NULL;
    for (char *p = pathbuf; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent;
    char *dirname;

    if (last_slash == NULL) {
        parent = mem_lookup(cwd_path);
        dirname = pathbuf;
    } else if (last_slash == pathbuf) {
        parent = mem_root;
        dirname = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent = mem_lookup(pathbuf);
        dirname = last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIRECTORY) {
        return -1;
    }

    // Find the child
    int found_idx = -1;
    for (int i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, dirname) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx < 0) return -1;

    vfs_node_t *node = parent->children[found_idx];

    // Must be a directory
    if (node->type != VFS_DIRECTORY) {
        return -1;
    }

    // Must be empty
    if (node->child_count > 0) {
        return -1;
    }

    // Remove from parent's children array
    for (int i = found_idx; i < parent->child_count - 1; i++) {
        parent->children[i] = parent->children[i + 1];
    }
    parent->child_count--;

    return 0;
}

int vfs_delete_recursive(const char *path) {
    if (use_fat32) {
        char fullpath[VFS_MAX_PATH];
        build_fullpath(path, fullpath);
        return fat32_delete_recursive(fullpath);
    }

    // In-memory recursive delete - for now just try delete_dir (doesn't recurse)
    // This is fine since in-memory fs is rarely used
    vfs_node_t *node = vfs_lookup(path);
    if (!node) return -1;

    if (node->type == VFS_DIRECTORY) {
        return vfs_delete_dir(path);
    } else {
        return vfs_delete(path);
    }
}

int vfs_rename(const char *path, const char *newname) {
    if (use_fat32) {
        // Build full path for old file
        char fullpath[VFS_MAX_PATH];
        if (path[0] == '/') {
            strncpy(fullpath, path, VFS_MAX_PATH - 1);
            fullpath[VFS_MAX_PATH - 1] = '\0';
        } else {
            if (strcmp(cwd_path, "/") == 0) {
                snprintf(fullpath, VFS_MAX_PATH, "/%s", path);
            } else {
                snprintf(fullpath, VFS_MAX_PATH, "%s/%s", cwd_path, path);
            }
        }

        // Extract just the filename from newname (fat32_rename expects basename only)
        const char *basename = newname;
        for (const char *p = newname; *p; p++) {
            if (*p == '/') basename = p + 1;
        }

        return fat32_rename(fullpath, basename);
    }

    // In-memory rename
    if (!path || !path[0] || !newname || !newname[0]) return -1;

    char pathbuf[VFS_MAX_PATH];
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    char *last_slash = NULL;
    for (char *p = pathbuf; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent;
    char *filename;

    if (last_slash == NULL) {
        parent = mem_lookup(cwd_path);
        filename = pathbuf;
    } else if (last_slash == pathbuf) {
        parent = mem_root;
        filename = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent = mem_lookup(pathbuf);
        filename = last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIRECTORY) {
        return -1;
    }

    // Find the child
    for (int i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, filename) == 0) {
            // Rename it
            int j;
            for (j = 0; newname[j] && j < VFS_MAX_NAME - 1; j++) {
                parent->children[i]->name[j] = newname[j];
            }
            parent->children[i]->name[j] = '\0';
            return 0;
        }
    }

    return -1;
}

int vfs_is_dir(vfs_node_t *node) {
    return node && node->type == VFS_DIRECTORY;
}

int vfs_is_file(vfs_node_t *node) {
    return node && node->type == VFS_FILE;
}
