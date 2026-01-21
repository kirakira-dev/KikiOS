/*
 * kikifetch - system information display
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

static void print_num(unsigned long n) {
    if (n == 0) {
        out_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        out_putc(buf[--i]);
    }
}

// ASCII art logo
static const char *logo[] = {
    " __   __ _  _            ",
    " \\ \\ / /(_)| |__   ___  ",
    "  \\ V / | || '_ \\ / _ \\ ",
    "   \\_/  | || |_) ||  __/ ",
    "        |_||_.__/  \\___| ",
    "    ___   ___            ",
    "   / _ \\ / __|           ",
    "  | (_) |\\__ \\           ",
    "   \\___/ |___/           ",
    NULL
};

int main(kapi_t *k, int argc, char **argv) {
    (void)argc;
    (void)argv;
    api = k;

    // Gather system info
    unsigned long ticks = k->get_uptime_ticks();
    unsigned long total_seconds = ticks / 100;
    unsigned long hours = total_seconds / 3600;
    unsigned long minutes = (total_seconds % 3600) / 60;
    unsigned long seconds = total_seconds % 60;

    size_t ram_total = k->get_ram_total();
    size_t ram_used = k->get_mem_used();
    unsigned long ram_total_mb = ram_total / (1024 * 1024);
    unsigned long ram_used_mb = ram_used / (1024 * 1024);

    const char *cpu_name = k->get_cpu_name();
    int cpu_cores = k->get_cpu_cores();

    // Print logo with info on the right
    int line = 0;
    for (int i = 0; logo[i] != NULL; i++) {
        out_puts(logo[i]);
        out_puts("   ");

        switch (line) {
            case 0:
                out_puts("OS: KikiOS 0.1");
                break;
            case 1:
                out_puts("Kernel: aarch64");
                break;
            case 2:
                out_puts("Uptime: ");
                if (hours > 0) {
                    print_num(hours);
                    out_puts("h ");
                }
                if (hours > 0 || minutes > 0) {
                    print_num(minutes);
                    out_puts("m ");
                }
                print_num(seconds);
                out_puts("s");
                break;
            case 3:
                out_puts("Shell: kikish");
                break;
            case 4:
                out_puts("CPU: ");
                out_puts(cpu_name);
                out_puts(" (");
                print_num(cpu_cores);
                out_puts(")");
                break;
            case 5:
                out_puts("Memory: ");
                print_num(ram_used_mb);
                out_puts(" MB / ");
                print_num(ram_total_mb);
                out_puts(" MB");
                break;
            case 6:
                out_puts("Display: ");
                print_num(k->fb_width);
                out_puts("x");
                print_num(k->fb_height);
                break;
            default:
                break;
        }

        out_putc('\n');
        line++;
    }

    out_putc('\n');
    return 0;
}
