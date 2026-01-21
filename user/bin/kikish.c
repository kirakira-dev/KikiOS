/*
 * kikish - KikiOS Shell
 *
 * A userspace shell for KikiOS with readline-like editing.
 *
 * Features:
 *   - Command history (up/down arrows)
 *   - !! to repeat last command
 *   - Ctrl+U: Clear line before cursor
 *   - Ctrl+D: Exit shell (EOF)
 *   - Ctrl+L: Clear screen
 *   - Ctrl+C: Clear current line
 *   - Ctrl+R: Reverse search history
 *   - Tab: Command/path completion
 *
 * Builtins:
 *   cd <dir>    - Change directory
 *   exit        - Exit shell
 *   help        - Show help
 *   clear       - Clear screen
 */

#include "../lib/kiki.h"

// Shell limits
#define CMD_MAX     256
#define MAX_ARGS    16
#define PATH_MAX    256
#define HISTORY_SIZE 50

// Global API pointer
static kapi_t *k;

// Command buffer
static char cmd_buf[CMD_MAX];
static int cmd_pos;
static int cmd_len;  // Total length of command (for cursor movement)

// History
static char history[HISTORY_SIZE][CMD_MAX];
static int history_count = 0;
static int history_pos = 0;  // Current position when browsing history

// Reverse search state
static int search_mode = 0;
static char search_buf[CMD_MAX];
static int search_pos = 0;
static int search_match = -1;  // Index in history of current match

// ============ I/O Helpers (use stdio hooks if available) ============

static void sh_putc(char c) {
    if (k->stdio_putc) {
        k->stdio_putc(c);
    } else {
        k->putc(c);
    }
}

static void sh_puts(const char *s) {
    if (k->stdio_puts) {
        k->stdio_puts(s);
    } else {
        k->puts(s);
    }
}

static int sh_getc(void) {
    if (k->stdio_getc) {
        return k->stdio_getc();
    } else {
        return k->getc();
    }
}

static int sh_has_key(void) {
    if (k->stdio_has_key) {
        return k->stdio_has_key();
    } else {
        return k->has_key();
    }
}

static void sh_set_color(uint32_t fg, uint32_t bg) {
    // Only set color for console (not for terminal - it's B&W)
    if (!k->stdio_putc) {
        k->set_color(fg, bg);
    }
}

static void sh_clear(void) {
    if (!k->stdio_putc) {
        k->clear();
    } else {
        // For terminal, send clear escape or just print newlines
        // We'll have terminal handle \f (form feed) as clear
        sh_putc('\f');
    }
}

// ============ History Management ============

static void history_add(const char *cmd) {
    if (!cmd[0]) return;  // Don't add empty commands

    // Don't add duplicates of the last command
    if (history_count > 0 && strcmp(history[history_count - 1], cmd) == 0) {
        return;
    }

    // Add to history
    if (history_count < HISTORY_SIZE) {
        strncpy_safe(history[history_count], cmd, CMD_MAX);
        history_count++;
    } else {
        // Shift everything up and add at the end
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            strncpy_safe(history[i], history[i + 1], CMD_MAX);
        }
        strncpy_safe(history[HISTORY_SIZE - 1], cmd, CMD_MAX);
    }
}

static const char *history_get(int index) {
    if (index < 0 || index >= history_count) return NULL;
    return history[index];
}

// Search history backwards for a match
static int history_search(const char *pattern, int start_from) {
    if (!pattern[0]) return -1;

    for (int i = start_from; i >= 0; i--) {
        // Check if pattern is a substring of history[i]
        const char *h = history[i];
        const char *p = pattern;
        const char *found = NULL;

        for (const char *s = h; *s; s++) {
            if (*s == *p) {
                if (!found) found = s;
                p++;
                if (!*p) break;  // Found complete match
            } else if (found) {
                // Reset search
                found = NULL;
                p = pattern;
            }
        }

        if (found && !*p) {
            return i;  // Found match
        }
    }
    return -1;
}

// ============ Line Editing ============

// Redraw the current line from cursor position
static void redraw_from_cursor(void) {
    // Print from cmd_pos to end
    for (int i = cmd_pos; i < cmd_len; i++) {
        sh_putc(cmd_buf[i]);
    }
    // Clear any leftover characters and move cursor back
    sh_putc(' ');
    for (int i = cmd_len; i >= cmd_pos; i--) {
        sh_putc('\b');
    }
}

// Clear the current line display and redraw
static void redraw_line(const char *prompt) {
    // Move to start of line
    sh_putc('\r');
    // Print prompt
    sh_puts(prompt);
    // Print command
    for (int i = 0; i < cmd_len; i++) {
        sh_putc(cmd_buf[i]);
    }
    // Clear rest of line (in case old content was longer)
    for (int i = 0; i < 10; i++) sh_putc(' ');
    for (int i = 0; i < 10; i++) sh_putc('\b');
    // Move cursor to correct position
    for (int i = cmd_len; i > cmd_pos; i--) {
        sh_putc('\b');
    }
}

// Clear line contents (Ctrl+U behavior)
static void clear_line(void) {
    // Move cursor to start
    while (cmd_pos > 0) {
        sh_putc('\b');
        cmd_pos--;
    }
    // Clear display
    for (int i = 0; i < cmd_len; i++) {
        sh_putc(' ');
    }
    for (int i = 0; i < cmd_len; i++) {
        sh_putc('\b');
    }
    cmd_len = 0;
    cmd_buf[0] = '\0';
}

// Set line to a string (for history navigation)
static void set_line(const char *str, const char *prompt) {
    clear_line();
    strncpy_safe(cmd_buf, str, CMD_MAX);
    cmd_len = strlen(cmd_buf);
    cmd_pos = cmd_len;
    // Redraw
    redraw_line(prompt);
}

// ============ Tab Completion ============

// Find common prefix length of two strings
static int common_prefix(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return i;
}

static void do_tab_completion(const char *prompt) {
    // Find the word being completed
    int word_start = cmd_pos;
    while (word_start > 0 && cmd_buf[word_start - 1] != ' ') {
        word_start--;
    }

    char word[PATH_MAX];
    int word_len = cmd_pos - word_start;
    for (int i = 0; i < word_len && i < PATH_MAX - 1; i++) {
        word[i] = cmd_buf[word_start + i];
    }
    word[word_len] = '\0';

    // Determine if this is a path or command completion
    int is_path = (word[0] == '/' || word[0] == '.' || word_start > 0);

    // Get directory and prefix to match
    char dir_path[PATH_MAX];
    char prefix[PATH_MAX];

    if (is_path) {
        // Find last /
        int last_slash = -1;
        for (int i = 0; word[i]; i++) {
            if (word[i] == '/') last_slash = i;
        }

        if (last_slash >= 0) {
            // Copy directory part
            for (int i = 0; i <= last_slash; i++) {
                dir_path[i] = word[i];
            }
            dir_path[last_slash + 1] = '\0';
            // Copy prefix part
            strcpy(prefix, word + last_slash + 1);
        } else {
            // Current directory
            k->get_cwd(dir_path, PATH_MAX);
            strcpy(prefix, word);
        }
    } else {
        // Command completion - look in /bin
        strcpy(dir_path, "/bin");
        strcpy(prefix, word);
    }

    // Open directory
    void *dir = k->open(dir_path);
    if (!dir || !k->is_dir(dir)) return;

    // Find matches
    char matches[10][PATH_MAX];  // Up to 10 matches
    int match_count = 0;
    int prefix_len = strlen(prefix);

    char name[256];
    uint8_t type;
    int idx = 0;

    while (match_count < 10 && k->readdir(dir, idx, name, sizeof(name), &type) == 0) {
        idx++;
        if (name[0] == '.') continue;  // Skip hidden files

        // Check if name starts with prefix
        if (strncmp(name, prefix, prefix_len) == 0) {
            strcpy(matches[match_count], name);
            match_count++;
        }
    }

    if (match_count == 0) {
        return;  // No matches
    }

    if (match_count == 1) {
        // Single match - complete it
        const char *match = matches[0];
        int match_len = strlen(match);

        // Insert the rest of the match
        for (int i = prefix_len; i < match_len; i++) {
            if (cmd_len < CMD_MAX - 1) {
                // Shift rest of line right
                for (int j = cmd_len; j >= cmd_pos; j--) {
                    cmd_buf[j + 1] = cmd_buf[j];
                }
                cmd_buf[cmd_pos] = match[i];
                cmd_pos++;
                cmd_len++;
                sh_putc(match[i]);
            }
        }

        // Add trailing / for directories or space for files/commands
        // Check if it's a directory
        char full_path[PATH_MAX];
        strcpy(full_path, dir_path);
        if (full_path[strlen(full_path) - 1] != '/') strcat(full_path, "/");
        strcat(full_path, match);
        void *node = k->open(full_path);

        char suffix = (node && k->is_dir(node)) ? '/' : ' ';
        if (cmd_len < CMD_MAX - 1) {
            for (int j = cmd_len; j >= cmd_pos; j--) {
                cmd_buf[j + 1] = cmd_buf[j];
            }
            cmd_buf[cmd_pos] = suffix;
            cmd_pos++;
            cmd_len++;
            sh_putc(suffix);
        }

        redraw_from_cursor();
    } else {
        // Multiple matches - complete common prefix and show options

        // Find common prefix
        int common = strlen(matches[0]);
        for (int i = 1; i < match_count; i++) {
            int c = common_prefix(matches[0], matches[i]);
            if (c < common) common = c;
        }

        // Insert common prefix beyond what's typed
        for (int i = prefix_len; i < common; i++) {
            if (cmd_len < CMD_MAX - 1) {
                for (int j = cmd_len; j >= cmd_pos; j--) {
                    cmd_buf[j + 1] = cmd_buf[j];
                }
                cmd_buf[cmd_pos] = matches[0][i];
                cmd_pos++;
                cmd_len++;
                sh_putc(matches[0][i]);
            }
        }

        // Show all matches
        sh_putc('\n');
        for (int i = 0; i < match_count; i++) {
            sh_puts(matches[i]);
            sh_puts("  ");
        }
        sh_putc('\n');

        // Redraw prompt and line
        redraw_line(prompt);
    }
}

// ============ Command Parsing ============

// Parse command line into argc/argv
static int parse_command(char *cmd, char *argv[], int max_args) {
    int argc = 0;
    char *p = cmd;

    while (*p && argc < max_args) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') break;

        // Found start of argument
        argv[argc++] = p;

        // Find end of argument
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }

        // Null-terminate this argument
        if (*p) {
            *p++ = '\0';
        }
    }

    return argc;
}

// Build prompt string
static void get_prompt(char *prompt_buf, int size) {
    char cwd[PATH_MAX];
    k->get_cwd(cwd, PATH_MAX);

    // Simple prompt: "cwd $ "
    int i = 0;
    for (int j = 0; cwd[j] && i < size - 4; j++) {
        prompt_buf[i++] = cwd[j];
    }
    prompt_buf[i++] = ' ';
    prompt_buf[i++] = '$';
    prompt_buf[i++] = ' ';
    prompt_buf[i] = '\0';
}

// Print the shell prompt with colors
static void print_prompt(void) {
    char cwd[PATH_MAX];
    k->get_cwd(cwd, PATH_MAX);

    sh_set_color(COLOR_CYAN, COLOR_BLACK);
    sh_puts(cwd);
    sh_set_color(COLOR_WHITE, COLOR_BLACK);
    sh_puts(" $ ");
}

// ============ Builtins ============

static int builtin_cd(int argc, char *argv[]) {
    if (argc < 2) {
        if (k->set_cwd("/home/user") < 0) {
            sh_set_color(COLOR_RED, COLOR_BLACK);
            sh_puts("cd: failed\n");
            sh_set_color(COLOR_WHITE, COLOR_BLACK);
            return 1;
        }
    } else {
        if (k->set_cwd(argv[1]) < 0) {
            sh_set_color(COLOR_RED, COLOR_BLACK);
            sh_puts("cd: ");
            sh_puts(argv[1]);
            sh_puts(": No such directory\n");
            sh_set_color(COLOR_WHITE, COLOR_BLACK);
            return 1;
        }
    }
    return 0;
}

static void builtin_help(void) {
    sh_puts("kikish - KikiOS Shell\n\n");
    sh_puts("Builtins:\n");
    sh_puts("  cd <dir>    Change directory\n");
    sh_puts("  exit        Exit shell\n");
    sh_puts("  clear       Clear screen\n");
    sh_puts("  time <cmd>  Time command execution\n");
    sh_puts("  mpy [file]  Run MicroPython (REPL or script)\n");
    sh_puts("  help        Show this help\n");
    sh_puts("\nLine editing:\n");
    sh_puts("  Up/Down     Browse command history\n");
    sh_puts("  Tab         Complete command or path\n");
    sh_puts("  Ctrl+C      Clear current line\n");
    sh_puts("  Ctrl+U      Clear line before cursor\n");
    sh_puts("  Ctrl+L      Clear screen\n");
    sh_puts("  Ctrl+R      Reverse search history\n");
    sh_puts("  Ctrl+D      Exit shell\n");
    sh_puts("  !!          Repeat last command\n");
    sh_puts("\nExternal commands in /bin:\n");
    sh_puts("  echo, ls, cat, pwd, mkdir, touch, rm, ...\n");
}

// ============ Command Execution ============

// Check if string ends with suffix
static int ends_with(const char *str, const char *suffix) {
    int str_len = strlen(str);
    int suf_len = strlen(suffix);
    if (suf_len > str_len) return 0;
    return strcmp(str + str_len - suf_len, suffix) == 0;
}

static int exec_external(int argc, char *argv[]) {
    char path[PATH_MAX];

    if (argv[0][0] == '/' || argv[0][0] == '.') {
        strncpy_safe(path, argv[0], PATH_MAX);
    } else {
        strcpy(path, "/bin/");
        strcat(path, argv[0]);
    }

    void *file = k->open(path);
    if (!file) {
        sh_set_color(COLOR_RED, COLOR_BLACK);
        sh_puts(argv[0]);
        sh_puts(": command not found\n");
        sh_set_color(COLOR_WHITE, COLOR_BLACK);
        return 127;
    }

    // Auto-detect .py files and run with micropython
    if (ends_with(path, ".py")) {
        char *new_argv[MAX_ARGS + 1];
        new_argv[0] = "/bin/micropython";
        for (int i = 0; i < argc && i < MAX_ARGS; i++) {
            new_argv[i + 1] = argv[i];
        }
        return k->exec_args("/bin/micropython", argc + 1, new_argv);
    }

    int result = k->exec_args(path, argc, argv);
    return result;
}

static int execute_command(char *cmd) {
    // Handle !! expansion
    if (cmd[0] == '!' && cmd[1] == '!') {
        if (history_count == 0) {
            sh_puts("!!: no previous command\n");
            return 1;
        }
        // Replace !! with last command
        char expanded[CMD_MAX];
        strncpy_safe(expanded, history[history_count - 1], CMD_MAX);
        // Append anything after !!
        if (cmd[2]) {
            strcat(expanded, cmd + 2);
        }
        strncpy_safe(cmd, expanded, CMD_MAX);
        // Show what we're running
        sh_puts(cmd);
        sh_putc('\n');
    }

    char *argv[MAX_ARGS];
    int argc = parse_command(cmd, argv, MAX_ARGS);

    if (argc == 0) {
        return 0;
    }

    // Builtins
    if (strcmp(argv[0], "cd") == 0) {
        return builtin_cd(argc, argv);
    }

    if (strcmp(argv[0], "exit") == 0) {
        return -1;  // Signal to exit
    }

    if (strcmp(argv[0], "help") == 0) {
        builtin_help();
        return 0;
    }

    if (strcmp(argv[0], "clear") == 0) {
        sh_clear();
        return 0;
    }

    // mpy - alias for micropython
    if (strcmp(argv[0], "mpy") == 0) {
        // Rewrite argv to use /bin/micropython
        argv[0] = "/bin/micropython";
        return k->exec_args("/bin/micropython", argc, argv);
    }

    // time <command> - measure execution time
    if (strcmp(argv[0], "time") == 0) {
        if (argc < 2) {
            sh_puts("usage: time <command> [args...]\n");
            return 1;
        }

        // Get start time
        uint64_t start_ticks = k->get_uptime_ticks();

        // Execute the command (argv+1, argc-1)
        int result;
        if (strcmp(argv[1], "cd") == 0) {
            result = builtin_cd(argc - 1, argv + 1);
        } else if (strcmp(argv[1], "help") == 0) {
            builtin_help();
            result = 0;
        } else if (strcmp(argv[1], "clear") == 0) {
            sh_clear();
            result = 0;
        } else {
            result = exec_external(argc - 1, argv + 1);
        }

        // Get end time
        uint64_t end_ticks = k->get_uptime_ticks();
        uint64_t elapsed = end_ticks - start_ticks;

        // Convert to seconds and milliseconds (100 ticks/sec)
        uint64_t secs = elapsed / 100;
        uint64_t ms = (elapsed % 100) * 10;

        // Print timing
        sh_putc('\n');
        sh_set_color(COLOR_CYAN, COLOR_BLACK);
        sh_puts("real\t");
        sh_set_color(COLOR_WHITE, COLOR_BLACK);

        // Print seconds
        char num[32];
        int i = 0;
        if (secs == 0) {
            num[i++] = '0';
        } else {
            uint64_t tmp = secs;
            int start = i;
            while (tmp > 0) {
                num[i++] = '0' + (tmp % 10);
                tmp /= 10;
            }
            // Reverse
            for (int j = start; j < (start + i) / 2; j++) {
                char c = num[j];
                num[j] = num[start + i - 1 - j];
                num[start + i - 1 - j] = c;
            }
        }
        num[i] = '\0';
        sh_puts(num);
        sh_putc('.');

        // Print ms with leading zeros (3 digits)
        num[0] = '0' + (ms / 100);
        num[1] = '0' + ((ms / 10) % 10);
        num[2] = '0' + (ms % 10);
        num[3] = '\0';
        sh_puts(num);
        sh_puts("s\n");

        return result;
    }

    return exec_external(argc, argv);
}

// ============ Input Handling ============

static int read_line(void) {
    cmd_pos = 0;
    cmd_len = 0;
    cmd_buf[0] = '\0';
    history_pos = history_count;  // Start at end of history
    search_mode = 0;

    char prompt[PATH_MAX + 8];
    get_prompt(prompt, sizeof(prompt));

    while (1) {
        int c = sh_getc();

        if (c < 0) {
            k->yield();
            continue;
        }

        // Handle reverse search mode
        if (search_mode) {
            if (c == '\r' || c == '\n') {
                // Accept the match
                search_mode = 0;
                sh_putc('\n');
                return 0;
            } else if (c == 27 || c == 3) {  // Escape or Ctrl+C
                // Cancel search
                search_mode = 0;
                clear_line();
                sh_putc('\r');
                print_prompt();
                continue;
            } else if (c == 18) {  // Ctrl+R again - search further back
                if (search_match > 0) {
                    search_match = history_search(search_buf, search_match - 1);
                    if (search_match >= 0) {
                        strncpy_safe(cmd_buf, history[search_match], CMD_MAX);
                        cmd_len = strlen(cmd_buf);
                        cmd_pos = cmd_len;
                    }
                }
                // Redraw search line
                sh_putc('\r');
                sh_puts("(reverse-i-search)`");
                sh_puts(search_buf);
                sh_puts("': ");
                sh_puts(cmd_buf);
                sh_puts("   ");  // Clear extra
                continue;
            } else if (c == '\b' || c == 127) {  // Backspace
                if (search_pos > 0) {
                    search_pos--;
                    search_buf[search_pos] = '\0';
                    // Re-search
                    search_match = history_search(search_buf, history_count - 1);
                    if (search_match >= 0) {
                        strncpy_safe(cmd_buf, history[search_match], CMD_MAX);
                        cmd_len = strlen(cmd_buf);
                        cmd_pos = cmd_len;
                    } else {
                        cmd_buf[0] = '\0';
                        cmd_len = 0;
                        cmd_pos = 0;
                    }
                }
                // Redraw
                sh_putc('\r');
                sh_puts("(reverse-i-search)`");
                sh_puts(search_buf);
                sh_puts("': ");
                sh_puts(cmd_buf);
                sh_puts("      ");
                continue;
            } else if (c >= 32 && c < 127) {
                // Add to search
                if (search_pos < CMD_MAX - 1) {
                    search_buf[search_pos++] = c;
                    search_buf[search_pos] = '\0';
                    // Search
                    search_match = history_search(search_buf, history_count - 1);
                    if (search_match >= 0) {
                        strncpy_safe(cmd_buf, history[search_match], CMD_MAX);
                        cmd_len = strlen(cmd_buf);
                        cmd_pos = cmd_len;
                    }
                }
                // Redraw
                sh_putc('\r');
                sh_puts("(reverse-i-search)`");
                sh_puts(search_buf);
                sh_puts("': ");
                sh_puts(cmd_buf);
                sh_puts("   ");
                continue;
            }
            continue;
        }

        // Normal mode
        if (c == '\r' || c == '\n') {
            sh_putc('\n');
            cmd_buf[cmd_len] = '\0';
            return 0;
        }

        // Ctrl+C - clear line
        if (c == 3) {
            sh_puts("^C\n");
            print_prompt();
            cmd_pos = 0;
            cmd_len = 0;
            cmd_buf[0] = '\0';
            continue;
        }

        // Ctrl+D - exit (EOF)
        if (c == 4) {
            if (cmd_len == 0) {
                sh_puts("exit\n");
                strcpy(cmd_buf, "exit");
                return 0;
            }
            // If there's content, Ctrl+D does nothing (or could delete char)
            continue;
        }

        // Ctrl+L - clear screen
        if (c == 12) {
            sh_clear();
            print_prompt();
            for (int i = 0; i < cmd_len; i++) {
                sh_putc(cmd_buf[i]);
            }
            for (int i = cmd_len; i > cmd_pos; i--) {
                sh_putc('\b');
            }
            continue;
        }

        // Ctrl+U - clear line before cursor
        if (c == 21) {
            // Delete from start to cursor
            int to_delete = cmd_pos;
            if (to_delete > 0) {
                // Move cursor to start
                for (int i = 0; i < cmd_pos; i++) {
                    sh_putc('\b');
                }
                // Shift remaining content
                for (int i = cmd_pos; i <= cmd_len; i++) {
                    cmd_buf[i - to_delete] = cmd_buf[i];
                }
                cmd_len -= to_delete;
                cmd_pos = 0;
                // Redraw
                for (int i = 0; i < cmd_len; i++) {
                    sh_putc(cmd_buf[i]);
                }
                // Clear old chars
                for (int i = 0; i < to_delete; i++) {
                    sh_putc(' ');
                }
                // Move back
                for (int i = 0; i < cmd_len + to_delete; i++) {
                    sh_putc('\b');
                }
            }
            continue;
        }

        // Ctrl+R - reverse search
        if (c == 18) {
            search_mode = 1;
            search_pos = 0;
            search_buf[0] = '\0';
            search_match = -1;
            sh_putc('\r');
            sh_puts("(reverse-i-search)`': ");
            // Clear rest of line
            for (int i = 0; i < 40; i++) sh_putc(' ');
            for (int i = 0; i < 40; i++) sh_putc('\b');
            continue;
        }

        // Tab - completion
        if (c == '\t') {
            do_tab_completion(prompt);
            continue;
        }

        // Backspace
        if (c == '\b' || c == 127) {
            if (cmd_pos > 0) {
                cmd_pos--;
                // Shift everything after cursor left
                for (int i = cmd_pos; i < cmd_len; i++) {
                    cmd_buf[i] = cmd_buf[i + 1];
                }
                cmd_len--;
                sh_putc('\b');
                redraw_from_cursor();
            }
            continue;
        }

        // Arrow keys (special codes >= 0x100)
        if (c == KEY_UP) {
            // Go back in history
            if (history_pos > 0) {
                history_pos--;
                set_line(history[history_pos], prompt);
            }
            continue;
        }

        if (c == KEY_DOWN) {
            // Go forward in history
            if (history_pos < history_count - 1) {
                history_pos++;
                set_line(history[history_pos], prompt);
            } else if (history_pos == history_count - 1) {
                history_pos = history_count;
                set_line("", prompt);
            }
            continue;
        }

        if (c == KEY_LEFT) {
            if (cmd_pos > 0) {
                cmd_pos--;
                sh_putc('\b');
            }
            continue;
        }

        if (c == KEY_RIGHT) {
            if (cmd_pos < cmd_len) {
                sh_putc(cmd_buf[cmd_pos]);
                cmd_pos++;
            }
            continue;
        }

        if (c == KEY_HOME) {
            while (cmd_pos > 0) {
                sh_putc('\b');
                cmd_pos--;
            }
            continue;
        }

        if (c == KEY_END) {
            while (cmd_pos < cmd_len) {
                sh_putc(cmd_buf[cmd_pos]);
                cmd_pos++;
            }
            continue;
        }

        if (c == KEY_DELETE) {
            // Delete character under cursor
            if (cmd_pos < cmd_len) {
                for (int i = cmd_pos; i < cmd_len; i++) {
                    cmd_buf[i] = cmd_buf[i + 1];
                }
                cmd_len--;
                redraw_from_cursor();
            }
            continue;
        }

        // Escape - ignore (could handle escape sequences later)
        if (c == 27) {
            continue;
        }

        // Regular character
        if (c >= 32 && c < 127 && cmd_len < CMD_MAX - 1) {
            // Insert at cursor position
            for (int i = cmd_len; i >= cmd_pos; i--) {
                cmd_buf[i + 1] = cmd_buf[i];
            }
            cmd_buf[cmd_pos] = (char)c;
            cmd_pos++;
            cmd_len++;

            // Redraw from cursor
            for (int i = cmd_pos - 1; i < cmd_len; i++) {
                sh_putc(cmd_buf[i]);
            }
            // Move cursor back to position
            for (int i = cmd_len; i > cmd_pos; i--) {
                sh_putc('\b');
            }
        }
    }
}

// ============ Main ============

int main(kapi_t *api, int argc, char **argv) {
    (void)argc;
    (void)argv;

    k = api;

    // Print banner
    sh_set_color(COLOR_GREEN, COLOR_BLACK);
    sh_puts("kikish ");
    sh_set_color(COLOR_WHITE, COLOR_BLACK);
    sh_puts("- KikiOS Shell\n");
    sh_puts("Type 'help' for commands.\n\n");

    // Main loop
    while (1) {
        print_prompt();

        if (read_line() < 0) {
            break;
        }

        // Add to history before executing
        if (cmd_buf[0] && !(cmd_buf[0] == '!' && cmd_buf[1] == '!')) {
            history_add(cmd_buf);
        }

        int result = execute_command(cmd_buf);

        // If !! was used, add the expanded command to history
        if (cmd_buf[0] && cmd_buf[0] != '!') {
            // Already added or will be added next iteration
        }

        if (result == -1) {
            break;
        }
    }

    sh_puts("Goodbye!\n");
    return 0;
}
