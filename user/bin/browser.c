/*
 * KikiOS Browser Launcher
 * Launches the Python browser via MicroPython
 */

#include "../lib/kiki.h"

int main(kapi_t *api, int argc, char **argv) {
    // Build args: micropython, browser.py, then any passed args
    char *args[16];
    int n = 0;
    args[n++] = "/bin/micropython";
    args[n++] = "/bin/browser.py";

    // Forward any arguments (skip argv[0] which is our own name)
    for (int i = 1; i < argc && n < 15; i++) {
        args[n++] = argv[i];
    }
    args[n] = 0;

    return api->exec_args("/bin/micropython", n, args);
}
