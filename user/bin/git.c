/*
 * git - KikiOS Version Control System
 *
 * A simplified git implementation for KikiOS.
 * Supports basic operations for local version control.
 *
 * Commands:
 *   init          - Initialize a new repository
 *   status        - Show working tree status
 *   add <file>    - Add file to staging area
 *   commit -m     - Record changes to repository
 *   log           - Show commit history
 *   branch        - List or create branches
 *   checkout      - Switch branches
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

// Git directory paths
#define GIT_DIR      ".git"
#define GIT_OBJECTS  ".git/objects"
#define GIT_REFS     ".git/refs"
#define GIT_HEADS    ".git/refs/heads"
#define GIT_HEAD     ".git/HEAD"
#define GIT_INDEX    ".git/index"
#define GIT_CONFIG   ".git/config"

// Simple hash function (not real SHA1)
static void simple_hash(const char *data, size_t len, char *out) {
    uint32_t h1 = 0x811c9dc5;
    uint32_t h2 = 0x12345678;
    uint32_t h3 = 0xdeadbeef;
    uint32_t h4 = 0xcafebabe;
    uint32_t h5 = 0xfeedface;
    
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        h1 = (h1 ^ c) * 0x01000193;
        h2 = (h2 ^ c) * 0x01000193 + i;
        h3 = ((h3 << 5) + h3) ^ c;
        h4 = (h4 * 31) + c;
        h5 = (h5 ^ (c << (i % 24))) + c;
    }
    
    const char *hex = "0123456789abcdef";
    uint32_t vals[5] = {h1, h2, h3, h4, h5};
    
    for (int i = 0; i < 5; i++) {
        for (int j = 7; j >= 0; j--) {
            *out++ = hex[(vals[i] >> (j * 4)) & 0xF];
        }
    }
    *out = '\0';
}

// Check if in a git repository
static int is_git_repo(void) {
    void *f = api->open(GIT_DIR);
    if (!f) return 0;
    return api->is_dir(f);
}

// Get current branch name
static int get_current_branch(char *branch, int maxlen) {
    void *f = api->open(GIT_HEAD);
    if (!f) return -1;
    
    char buf[128];
    int len = api->read(f, buf, sizeof(buf) - 1, 0);
    if (len <= 0) return -1;
    buf[len] = '\0';
    
    // Remove trailing newline
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    
    // Check if it's a ref
    if (len > 16 && strncmp(buf, "ref: refs/heads/", 16) == 0) {
        strncpy_safe(branch, buf + 16, maxlen);
        return 0;
    }
    
    strncpy_safe(branch, buf, maxlen);
    return 1;
}

// Initialize a new repository
static int cmd_init(void) {
    if (is_git_repo()) {
        out_puts("Reinitialized existing Git repository\n");
        return 0;
    }
    
    api->mkdir(GIT_DIR);
    api->mkdir(GIT_OBJECTS);
    api->mkdir(GIT_REFS);
    api->mkdir(GIT_HEADS);
    
    void *f = api->create(GIT_HEAD);
    if (f) {
        const char *ref = "ref: refs/heads/master\n";
        api->write(f, ref, strlen(ref));
    }
    
    f = api->create(GIT_CONFIG);
    if (f) {
        const char *config = 
            "[core]\n"
            "\trepositoryformatversion = 0\n"
            "[user]\n"
            "\tname = KikiOS User\n"
            "\temail = user@kikios\n";
        api->write(f, config, strlen(config));
    }
    
    api->create(GIT_INDEX);
    
    out_puts("Initialized empty Git repository in .git/\n");
    return 0;
}

// Index entry
typedef struct {
    char path[128];
    char hash[41];
} index_entry_t;

#define MAX_INDEX 256
static index_entry_t index_entries[MAX_INDEX];
static int index_count = 0;

static void load_index(void) {
    index_count = 0;
    void *f = api->open(GIT_INDEX);
    if (!f) return;
    
    char buf[8192];
    int len = api->read(f, buf, sizeof(buf) - 1, 0);
    if (len <= 0) return;
    buf[len] = '\0';
    
    char *p = buf;
    while (*p && index_count < MAX_INDEX) {
        if (strlen(p) < 42) break;
        strncpy_safe(index_entries[index_count].hash, p, 41);
        p += 41;
        if (*p == ' ') p++;
        
        char *nl = p;
        while (*nl && *nl != '\n') nl++;
        
        int pathlen = nl - p;
        if (pathlen > 127) pathlen = 127;
        strncpy_safe(index_entries[index_count].path, p, pathlen + 1);
        index_entries[index_count].path[pathlen] = '\0';
        
        index_count++;
        p = (*nl) ? nl + 1 : nl;
    }
}

static void save_index(void) {
    void *f = api->create(GIT_INDEX);
    if (!f) return;
    
    char buf[8192];
    char *p = buf;
    
    for (int i = 0; i < index_count; i++) {
        memcpy(p, index_entries[i].hash, 40);
        p += 40;
        *p++ = ' ';
        int len = strlen(index_entries[i].path);
        memcpy(p, index_entries[i].path, len);
        p += len;
        *p++ = '\n';
    }
    
    api->write(f, buf, p - buf);
}

static int find_index_entry(const char *path) {
    for (int i = 0; i < index_count; i++) {
        if (strcmp(index_entries[i].path, path) == 0) return i;
    }
    return -1;
}

// Add file to staging
static int cmd_add(int argc, char **argv) {
    if (argc < 2) {
        out_puts("usage: git add <file>...\n");
        return 1;
    }
    
    if (!is_git_repo()) {
        out_puts("fatal: not a git repository\n");
        return 1;
    }
    
    load_index();
    
    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
        
        void *f = api->open(path);
        if (!f) {
            out_puts("fatal: pathspec '");
            out_puts(path);
            out_puts("' did not match any files\n");
            continue;
        }
        
        if (api->is_dir(f)) {
            out_puts("fatal: '");
            out_puts(path);
            out_puts("' is a directory\n");
            continue;
        }
        
        int size = api->file_size(f);
        char *content = api->malloc(size + 1);
        if (!content) continue;
        
        api->read(f, content, size, 0);
        content[size] = '\0';
        
        char hash[41];
        simple_hash(content, size, hash);
        api->free(content);
        
        int idx = find_index_entry(path);
        if (idx >= 0) {
            strncpy_safe(index_entries[idx].hash, hash, 41);
        } else if (index_count < MAX_INDEX) {
            strncpy_safe(index_entries[index_count].path, path, 128);
            strncpy_safe(index_entries[index_count].hash, hash, 41);
            index_count++;
        }
    }
    
    save_index();
    return 0;
}

// Show status
static int cmd_status(void) {
    if (!is_git_repo()) {
        out_puts("fatal: not a git repository\n");
        return 1;
    }
    
    char branch[64];
    int detached = get_current_branch(branch, sizeof(branch));
    
    out_puts("On branch ");
    out_puts(branch);
    out_putc('\n');
    
    if (detached) out_puts("(HEAD detached)\n");
    
    load_index();
    
    if (index_count > 0) {
        out_puts("\nChanges to be committed:\n");
        for (int i = 0; i < index_count; i++) {
            out_puts("\tnew file:   ");
            out_puts(index_entries[i].path);
            out_putc('\n');
        }
    }
    
    // Check for untracked files
    char cwd[256];
    api->get_cwd(cwd, sizeof(cwd));
    
    void *dir = api->open(cwd);
    if (dir && api->is_dir(dir)) {
        int has_untracked = 0;
        char name[64];
        uint8_t type;
        int idx = 0;
        
        while (api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
            idx++;
            if (name[0] == '.') continue;
            if (type != 1) continue;
            
            if (find_index_entry(name) < 0) {
                if (!has_untracked) {
                    out_puts("\nUntracked files:\n");
                    has_untracked = 1;
                }
                out_puts("\t");
                out_puts(name);
                out_putc('\n');
            }
        }
    }
    
    if (index_count == 0) {
        out_puts("\nNo changes added to commit (use \"git add\")\n");
    }
    
    return 0;
}

// Commit changes
static int cmd_commit(int argc, char **argv) {
    if (!is_git_repo()) {
        out_puts("fatal: not a git repository\n");
        return 1;
    }
    
    const char *message = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            message = argv[i + 1];
            break;
        }
    }
    
    if (!message) {
        out_puts("error: commit message required (-m \"message\")\n");
        return 1;
    }
    
    load_index();
    
    if (index_count == 0) {
        out_puts("nothing to commit\n");
        return 1;
    }
    
    // Build commit hash from message + files
    char commit_data[4096];
    int len = 0;
    
    for (int i = 0; i < index_count; i++) {
        len += strlen(index_entries[i].hash);
        len += strlen(index_entries[i].path);
    }
    len += strlen(message);
    
    char *p = commit_data;
    for (int i = 0; i < index_count && p - commit_data < 4000; i++) {
        strcpy(p, index_entries[i].hash);
        p += strlen(index_entries[i].hash);
        strcpy(p, index_entries[i].path);
        p += strlen(index_entries[i].path);
    }
    strcpy(p, message);
    p += strlen(message);
    
    char commit_hash[41];
    simple_hash(commit_data, p - commit_data, commit_hash);
    
    // Store commit
    char obj_dir[64];
    strcpy(obj_dir, GIT_OBJECTS);
    strcat(obj_dir, "/");
    int olen = strlen(obj_dir);
    obj_dir[olen] = commit_hash[0];
    obj_dir[olen + 1] = commit_hash[1];
    obj_dir[olen + 2] = '\0';
    
    api->mkdir(obj_dir);
    
    char obj_path[80];
    strcpy(obj_path, obj_dir);
    strcat(obj_path, "/");
    strcat(obj_path, commit_hash + 2);
    
    void *f = api->create(obj_path);
    if (f) {
        api->write(f, commit_data, p - commit_data);
    }
    
    // Update branch ref
    char branch[64];
    get_current_branch(branch, sizeof(branch));
    
    char ref_path[128];
    strcpy(ref_path, GIT_HEADS);
    strcat(ref_path, "/");
    strcat(ref_path, branch);
    
    f = api->create(ref_path);
    if (f) {
        char ref[48];
        strcpy(ref, commit_hash);
        strcat(ref, "\n");
        api->write(f, ref, strlen(ref));
    }
    
    // Output
    out_puts("[");
    out_puts(branch);
    out_puts(" ");
    for (int i = 0; i < 7; i++) out_putc(commit_hash[i]);
    out_puts("] ");
    out_puts(message);
    out_putc('\n');
    
    out_puts(" ");
    print_num(index_count);
    out_puts(" file(s) changed\n");
    
    // Clear index
    index_count = 0;
    save_index();
    
    return 0;
}

// Show log
static int cmd_log(void) {
    if (!is_git_repo()) {
        out_puts("fatal: not a git repository\n");
        return 1;
    }
    
    char branch[64];
    get_current_branch(branch, sizeof(branch));
    
    char ref_path[128];
    strcpy(ref_path, GIT_HEADS);
    strcat(ref_path, "/");
    strcat(ref_path, branch);
    
    void *f = api->open(ref_path);
    if (!f) {
        out_puts("No commits yet on ");
        out_puts(branch);
        out_putc('\n');
        return 0;
    }
    
    char hash[48];
    int len = api->read(f, hash, 40, 0);
    if (len < 40) {
        out_puts("No commits yet\n");
        return 0;
    }
    hash[40] = '\0';
    
    out_puts("commit ");
    out_puts(hash);
    out_puts("\n\n");
    
    return 0;
}

// List/create branches
static int cmd_branch(int argc, char **argv) {
    if (!is_git_repo()) {
        out_puts("fatal: not a git repository\n");
        return 1;
    }
    
    if (argc < 2) {
        char current[64];
        get_current_branch(current, sizeof(current));
        
        void *dir = api->open(GIT_HEADS);
        if (!dir || !api->is_dir(dir)) {
            out_puts("* master\n");
            return 0;
        }
        
        char name[64];
        uint8_t type;
        int idx = 0;
        while (api->readdir(dir, idx, name, sizeof(name), &type) == 0) {
            idx++;
            if (name[0] == '.') continue;
            
            if (strcmp(name, current) == 0) out_puts("* ");
            else out_puts("  ");
            out_puts(name);
            out_putc('\n');
        }
    } else {
        const char *branch_name = argv[1];
        
        char current_branch[64];
        get_current_branch(current_branch, sizeof(current_branch));
        
        char ref_path[128];
        strcpy(ref_path, GIT_HEADS);
        strcat(ref_path, "/");
        strcat(ref_path, current_branch);
        
        void *f = api->open(ref_path);
        char hash[48] = "0000000000000000000000000000000000000000\n";
        if (f) api->read(f, hash, 41, 0);
        
        strcpy(ref_path, GIT_HEADS);
        strcat(ref_path, "/");
        strcat(ref_path, branch_name);
        
        f = api->create(ref_path);
        if (f) {
            api->write(f, hash, strlen(hash));
            out_puts("Created branch '");
            out_puts(branch_name);
            out_puts("'\n");
        }
    }
    
    return 0;
}

// Checkout branch
static int cmd_checkout(int argc, char **argv) {
    if (argc < 2) {
        out_puts("usage: git checkout <branch>\n");
        return 1;
    }
    
    if (!is_git_repo()) {
        out_puts("fatal: not a git repository\n");
        return 1;
    }
    
    const char *target = argv[1];
    
    char ref_path[128];
    strcpy(ref_path, GIT_HEADS);
    strcat(ref_path, "/");
    strcat(ref_path, target);
    
    void *f = api->open(ref_path);
    if (!f) {
        out_puts("error: branch '");
        out_puts(target);
        out_puts("' not found\n");
        return 1;
    }
    
    f = api->create(GIT_HEAD);
    if (f) {
        char head[128];
        strcpy(head, "ref: refs/heads/");
        strcat(head, target);
        strcat(head, "\n");
        api->write(f, head, strlen(head));
    }
    
    out_puts("Switched to branch '");
    out_puts(target);
    out_puts("'\n");
    
    return 0;
}

static void print_help(void) {
    out_puts("usage: git <command> [<args>]\n\n");
    out_puts("Commands:\n");
    out_puts("   init       Create an empty Git repository\n");
    out_puts("   status     Show the working tree status\n");
    out_puts("   add        Add file contents to the index\n");
    out_puts("   commit     Record changes to the repository\n");
    out_puts("   log        Show commit logs\n");
    out_puts("   branch     List or create branches\n");
    out_puts("   checkout   Switch branches\n");
}

int main(kapi_t *k, int argc, char **argv) {
    api = k;
    
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "init") == 0) return cmd_init();
    if (strcmp(cmd, "status") == 0) return cmd_status();
    if (strcmp(cmd, "add") == 0) return cmd_add(argc - 1, argv + 1);
    if (strcmp(cmd, "commit") == 0) return cmd_commit(argc - 1, argv + 1);
    if (strcmp(cmd, "log") == 0) return cmd_log();
    if (strcmp(cmd, "branch") == 0) return cmd_branch(argc - 1, argv + 1);
    if (strcmp(cmd, "checkout") == 0) return cmd_checkout(argc - 1, argv + 1);
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "help") == 0) {
        print_help();
        return 0;
    }
    
    out_puts("git: '");
    out_puts(cmd);
    out_puts("' is not a git command.\n");
    return 1;
}
