// kiki module for KikiOS
// Python bindings to kernel API - FULL API

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "kiki.h"

// External reference to kernel API
extern kapi_t *mp_kikios_api;

// ============================================================================
// Console I/O
// ============================================================================

// kiki.putc(c)
static mp_obj_t mod_kiki_putc(mp_obj_t c_obj) {
    int c = mp_obj_get_int(c_obj);
    mp_kikios_api->putc((char)c);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_putc_obj, mod_kiki_putc);

// kiki.puts(s)
static mp_obj_t mod_kiki_puts(mp_obj_t s_obj) {
    const char *s = mp_obj_str_get_str(s_obj);
    mp_kikios_api->puts(s);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_puts_obj, mod_kiki_puts);

// vibe.uart_puts(s) - direct UART output for debugging
static mp_obj_t mod_kiki_uart_puts(mp_obj_t s_obj) {
    const char *s = mp_obj_str_get_str(s_obj);
    mp_kikios_api->uart_puts(s);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_uart_puts_obj, mod_kiki_uart_puts);

// vibe.clear()
static mp_obj_t mod_kiki_clear(void) {
    mp_kikios_api->clear();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_clear_obj, mod_kiki_clear);

// vibe.set_color(fg, bg)
static mp_obj_t mod_kiki_set_color(mp_obj_t fg_obj, mp_obj_t bg_obj) {
    uint32_t fg = mp_obj_get_int(fg_obj);
    uint32_t bg = mp_obj_get_int(bg_obj);
    mp_kikios_api->set_color(fg, bg);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_set_color_obj, mod_kiki_set_color);

// vibe.set_cursor(row, col)
static mp_obj_t mod_kiki_set_cursor(mp_obj_t row_obj, mp_obj_t col_obj) {
    int row = mp_obj_get_int(row_obj);
    int col = mp_obj_get_int(col_obj);
    mp_kikios_api->set_cursor(row, col);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_set_cursor_obj, mod_kiki_set_cursor);

// vibe.set_cursor_enabled(enabled)
static mp_obj_t mod_kiki_set_cursor_enabled(mp_obj_t enabled_obj) {
    int enabled = mp_obj_is_true(enabled_obj);
    mp_kikios_api->set_cursor_enabled(enabled);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_set_cursor_enabled_obj, mod_kiki_set_cursor_enabled);

// vibe.console_size() -> (rows, cols)
static mp_obj_t mod_kiki_console_size(void) {
    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(mp_kikios_api->console_rows());
    tuple[1] = mp_obj_new_int(mp_kikios_api->console_cols());
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_console_size_obj, mod_kiki_console_size);

// ============================================================================
// Keyboard Input
// ============================================================================

// kiki.has_key()
static mp_obj_t mod_kiki_has_key(void) {
    return mp_obj_new_bool(mp_kikios_api->has_key());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_has_key_obj, mod_kiki_has_key);

// vibe.getc()
static mp_obj_t mod_kiki_getc(void) {
    return mp_obj_new_int(mp_kikios_api->getc());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_getc_obj, mod_kiki_getc);

// ============================================================================
// Timing
// ============================================================================

// vibe.sleep_ms(ms)
static mp_obj_t mod_kiki_sleep_ms(mp_obj_t ms_obj) {
    uint32_t ms = mp_obj_get_int(ms_obj);
    mp_kikios_api->sleep_ms(ms);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_sleep_ms_obj, mod_kiki_sleep_ms);

// vibe.uptime_ms()
static mp_obj_t mod_kiki_uptime_ms(void) {
    return mp_obj_new_int(mp_kikios_api->get_uptime_ticks() * 10);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_uptime_ms_obj, mod_kiki_uptime_ms);

// vibe.sched_yield()
static mp_obj_t mod_kiki_sched_yield(void) {
    mp_kikios_api->yield();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_sched_yield_obj, mod_kiki_sched_yield);

// ============================================================================
// RTC / Date & Time
// ============================================================================

// vibe.timestamp() -> int (unix timestamp)
static mp_obj_t mod_kiki_timestamp(void) {
    return mp_obj_new_int(mp_kikios_api->get_timestamp());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_timestamp_obj, mod_kiki_timestamp);

// vibe.datetime() -> (year, month, day, hour, minute, second, weekday)
static mp_obj_t mod_kiki_datetime(void) {
    int year, month, day, hour, minute, second, weekday;
    mp_kikios_api->get_datetime(&year, &month, &day, &hour, &minute, &second, &weekday);
    mp_obj_t tuple[7];
    tuple[0] = mp_obj_new_int(year);
    tuple[1] = mp_obj_new_int(month);
    tuple[2] = mp_obj_new_int(day);
    tuple[3] = mp_obj_new_int(hour);
    tuple[4] = mp_obj_new_int(minute);
    tuple[5] = mp_obj_new_int(second);
    tuple[6] = mp_obj_new_int(weekday);
    return mp_obj_new_tuple(7, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_datetime_obj, mod_kiki_datetime);

// ============================================================================
// Filesystem
// ============================================================================

// vibe.open(path) -> file handle (int) or None
static mp_obj_t mod_kiki_open(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    void *file = mp_kikios_api->open(path);
    if (!file) return mp_const_none;
    return mp_obj_new_int((mp_int_t)file);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_open_obj, mod_kiki_open);

// vibe.read(file, size, offset=0) -> bytes or None
static mp_obj_t mod_kiki_read(size_t n_args, const mp_obj_t *args) {
    void *file = (void *)mp_obj_get_int(args[0]);
    int size = mp_obj_get_int(args[1]);
    int offset = n_args > 2 ? mp_obj_get_int(args[2]) : 0;

    char *buf = m_new(char, size);
    int bytes_read = mp_kikios_api->read(file, buf, size, offset);
    if (bytes_read < 0) {
        m_del(char, buf, size);
        return mp_const_none;
    }
    mp_obj_t result = mp_obj_new_bytes((const byte *)buf, bytes_read);
    m_del(char, buf, size);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_read_obj, 2, 3, mod_kiki_read);

// vibe.write(file, data) -> bytes written
static mp_obj_t mod_kiki_write(mp_obj_t file_obj, mp_obj_t data_obj) {
    void *file = (void *)mp_obj_get_int(file_obj);
    size_t len;
    const char *data = mp_obj_str_get_data(data_obj, &len);
    int written = mp_kikios_api->write(file, data, len);
    return mp_obj_new_int(written);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_write_obj, mod_kiki_write);

// vibe.file_size(file) -> int
static mp_obj_t mod_kiki_file_size(mp_obj_t file_obj) {
    void *file = (void *)mp_obj_get_int(file_obj);
    return mp_obj_new_int(mp_kikios_api->file_size(file));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_file_size_obj, mod_kiki_file_size);

// vibe.is_dir(file) -> bool
static mp_obj_t mod_kiki_is_dir(mp_obj_t file_obj) {
    void *file = (void *)mp_obj_get_int(file_obj);
    return mp_obj_new_bool(mp_kikios_api->is_dir(file));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_is_dir_obj, mod_kiki_is_dir);

// vibe.create(path) -> file handle or None
static mp_obj_t mod_kiki_create(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    void *file = mp_kikios_api->create(path);
    if (!file) return mp_const_none;
    return mp_obj_new_int((mp_int_t)file);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_create_obj, mod_kiki_create);

// vibe.mkdir(path) -> file handle or None
static mp_obj_t mod_kiki_mkdir(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    void *dir = mp_kikios_api->mkdir(path);
    if (!dir) return mp_const_none;
    return mp_obj_new_int((mp_int_t)dir);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_mkdir_obj, mod_kiki_mkdir);

// vibe.delete(path) -> bool
static mp_obj_t mod_kiki_delete(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    return mp_obj_new_bool(mp_kikios_api->delete(path) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_delete_obj, mod_kiki_delete);

// vibe.rename(path, newname) -> bool
static mp_obj_t mod_kiki_rename(mp_obj_t path_obj, mp_obj_t newname_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    const char *newname = mp_obj_str_get_str(newname_obj);
    return mp_obj_new_bool(mp_kikios_api->rename(path, newname) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_rename_obj, mod_kiki_rename);

// vibe.listdir(path) -> list of (name, is_dir) tuples
static mp_obj_t mod_kiki_listdir(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    void *dir = mp_kikios_api->open(path);
    if (!dir || !mp_kikios_api->is_dir(dir)) {
        return mp_obj_new_list(0, NULL);
    }

    mp_obj_t list = mp_obj_new_list(0, NULL);
    char name[256];
    uint8_t type;
    int i = 0;
    while (mp_kikios_api->readdir(dir, i, name, sizeof(name), &type) == 0) {
        mp_obj_t tuple[2];
        tuple[0] = mp_obj_new_str(name, strlen(name));
        tuple[1] = mp_obj_new_bool(type == 1);  // 1 = directory
        mp_obj_list_append(list, mp_obj_new_tuple(2, tuple));
        i++;
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_listdir_obj, mod_kiki_listdir);

// vibe.getcwd() -> str
static mp_obj_t mod_kiki_getcwd(void) {
    char buf[256];
    mp_kikios_api->get_cwd(buf, sizeof(buf));
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_getcwd_obj, mod_kiki_getcwd);

// vibe.chdir(path) -> bool
static mp_obj_t mod_kiki_chdir(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    return mp_obj_new_bool(mp_kikios_api->set_cwd(path) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_chdir_obj, mod_kiki_chdir);

// ============================================================================
// Process Management
// ============================================================================

// vibe.exit(code=0)
static mp_obj_t mod_kiki_exit(size_t n_args, const mp_obj_t *args) {
    int code = n_args > 0 ? mp_obj_get_int(args[0]) : 0;
    mp_kikios_api->exit(code);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_exit_obj, 0, 1, mod_kiki_exit);

// vibe.exec(path) -> exit code
static mp_obj_t mod_kiki_exec(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    return mp_obj_new_int(mp_kikios_api->exec(path));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_exec_obj, mod_kiki_exec);

// vibe.spawn(path) -> pid or -1
static mp_obj_t mod_kiki_spawn(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    return mp_obj_new_int(mp_kikios_api->spawn(path));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_spawn_obj, mod_kiki_spawn);

// vibe.kill(pid) -> bool
static mp_obj_t mod_kiki_kill(mp_obj_t pid_obj) {
    int pid = mp_obj_get_int(pid_obj);
    return mp_obj_new_bool(mp_kikios_api->kill_process(pid) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_kill_obj, mod_kiki_kill);

// vibe.ps() -> list of (pid, name, state) tuples
static mp_obj_t mod_kiki_ps(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    int count = mp_kikios_api->get_process_count();
    for (int i = 0; i < count; i++) {
        char name[64];
        int state;
        if (mp_kikios_api->get_process_info(i, name, sizeof(name), &state) == 0) {
            mp_obj_t tuple[3];
            tuple[0] = mp_obj_new_int(i);
            tuple[1] = mp_obj_new_str(name, strlen(name));
            tuple[2] = mp_obj_new_int(state);
            mp_obj_list_append(list, mp_obj_new_tuple(3, tuple));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_ps_obj, mod_kiki_ps);

// ============================================================================
// Graphics
// ============================================================================

// vibe.put_pixel(x, y, color)
static mp_obj_t mod_kiki_put_pixel(mp_obj_t x_obj, mp_obj_t y_obj, mp_obj_t c_obj) {
    uint32_t x = mp_obj_get_int(x_obj);
    uint32_t y = mp_obj_get_int(y_obj);
    uint32_t c = mp_obj_get_int(c_obj);
    mp_kikios_api->fb_put_pixel(x, y, c);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(mod_kiki_put_pixel_obj, mod_kiki_put_pixel);

// vibe.fill_rect(x, y, w, h, color)
static mp_obj_t mod_kiki_fill_rect(size_t n_args, const mp_obj_t *args) {
    uint32_t x = mp_obj_get_int(args[0]);
    uint32_t y = mp_obj_get_int(args[1]);
    uint32_t w = mp_obj_get_int(args[2]);
    uint32_t h = mp_obj_get_int(args[3]);
    uint32_t c = mp_obj_get_int(args[4]);
    mp_kikios_api->fb_fill_rect(x, y, w, h, c);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_fill_rect_obj, 5, 5, mod_kiki_fill_rect);

// vibe.draw_char(x, y, c, fg, bg)
static mp_obj_t mod_kiki_draw_char(size_t n_args, const mp_obj_t *args) {
    uint32_t x = mp_obj_get_int(args[0]);
    uint32_t y = mp_obj_get_int(args[1]);
    char c = mp_obj_get_int(args[2]);
    uint32_t fg = mp_obj_get_int(args[3]);
    uint32_t bg = mp_obj_get_int(args[4]);
    mp_kikios_api->fb_draw_char(x, y, c, fg, bg);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_draw_char_obj, 5, 5, mod_kiki_draw_char);

// vibe.draw_string(x, y, s, fg, bg)
static mp_obj_t mod_kiki_draw_string(size_t n_args, const mp_obj_t *args) {
    uint32_t x = mp_obj_get_int(args[0]);
    uint32_t y = mp_obj_get_int(args[1]);
    const char *s = mp_obj_str_get_str(args[2]);
    uint32_t fg = mp_obj_get_int(args[3]);
    uint32_t bg = mp_obj_get_int(args[4]);
    mp_kikios_api->fb_draw_string(x, y, s, fg, bg);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_draw_string_obj, 5, 5, mod_kiki_draw_string);

// vibe.screen_size() -> (width, height)
static mp_obj_t mod_kiki_screen_size(void) {
    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(mp_kikios_api->fb_width);
    tuple[1] = mp_obj_new_int(mp_kikios_api->fb_height);
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_screen_size_obj, mod_kiki_screen_size);

// ============================================================================
// Mouse
// ============================================================================

// vibe.mouse_pos() -> (x, y)
static mp_obj_t mod_kiki_mouse_pos(void) {
    int x, y;
    mp_kikios_api->mouse_get_pos(&x, &y);
    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(x);
    tuple[1] = mp_obj_new_int(y);
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_mouse_pos_obj, mod_kiki_mouse_pos);

// vibe.mouse_buttons() -> int
static mp_obj_t mod_kiki_mouse_buttons(void) {
    return mp_obj_new_int(mp_kikios_api->mouse_get_buttons());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_mouse_buttons_obj, mod_kiki_mouse_buttons);

// ============================================================================
// Window Management
// ============================================================================

// vibe.window_create(x, y, w, h, title) -> wid or -1
static mp_obj_t mod_kiki_window_create(size_t n_args, const mp_obj_t *args) {
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int w = mp_obj_get_int(args[2]);
    int h = mp_obj_get_int(args[3]);
    const char *title = mp_obj_str_get_str(args[4]);
    return mp_obj_new_int(mp_kikios_api->window_create(x, y, w, h, title));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_create_obj, 5, 5, mod_kiki_window_create);

// vibe.window_destroy(wid)
static mp_obj_t mod_kiki_window_destroy(mp_obj_t wid_obj) {
    int wid = mp_obj_get_int(wid_obj);
    mp_kikios_api->window_destroy(wid);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_window_destroy_obj, mod_kiki_window_destroy);

// vibe.window_poll(wid) -> (event_type, data1, data2, data3) or None
static mp_obj_t mod_kiki_window_poll(mp_obj_t wid_obj) {
    int wid = mp_obj_get_int(wid_obj);
    int event_type, data1, data2, data3;
    int result = mp_kikios_api->window_poll_event(wid, &event_type, &data1, &data2, &data3);
    if (result == 0 || event_type == 0) {
        return mp_const_none;
    }
    mp_obj_t tuple[4];
    tuple[0] = mp_obj_new_int(event_type);
    tuple[1] = mp_obj_new_int(data1);
    tuple[2] = mp_obj_new_int(data2);
    tuple[3] = mp_obj_new_int(data3);
    return mp_obj_new_tuple(4, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_window_poll_obj, mod_kiki_window_poll);

// vibe.window_invalidate(wid)
static mp_obj_t mod_kiki_window_invalidate(mp_obj_t wid_obj) {
    int wid = mp_obj_get_int(wid_obj);
    mp_kikios_api->window_invalidate(wid);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_window_invalidate_obj, mod_kiki_window_invalidate);

// vibe.window_set_title(wid, title)
static mp_obj_t mod_kiki_window_set_title(mp_obj_t wid_obj, mp_obj_t title_obj) {
    int wid = mp_obj_get_int(wid_obj);
    const char *title = mp_obj_str_get_str(title_obj);
    mp_kikios_api->window_set_title(wid, title);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_window_set_title_obj, mod_kiki_window_set_title);

// vibe.window_size(wid) -> (w, h)
static mp_obj_t mod_kiki_window_size(mp_obj_t wid_obj) {
    int wid = mp_obj_get_int(wid_obj);
    int w, h;
    mp_kikios_api->window_get_buffer(wid, &w, &h);
    mp_obj_t tuple[2];
    tuple[0] = mp_obj_new_int(w);
    tuple[1] = mp_obj_new_int(h);
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_window_size_obj, mod_kiki_window_size);

// vibe.window_fill_rect(wid, x, y, w, h, color)
static mp_obj_t mod_kiki_window_fill_rect(size_t n_args, const mp_obj_t *args) {
    int wid = mp_obj_get_int(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int w = mp_obj_get_int(args[3]);
    int h = mp_obj_get_int(args[4]);
    uint32_t color = mp_obj_get_int(args[5]);

    int bw, bh;
    uint32_t *buf = mp_kikios_api->window_get_buffer(wid, &bw, &bh);
    if (!buf) return mp_const_none;

    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > bw) w = bw - x;
    if (y + h > bh) h = bh - y;
    if (w <= 0 || h <= 0) return mp_const_none;

    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            buf[py * bw + px] = color;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_fill_rect_obj, 6, 6, mod_kiki_window_fill_rect);

// vibe.window_put_pixel(wid, x, y, color)
static mp_obj_t mod_kiki_window_put_pixel(size_t n_args, const mp_obj_t *args) {
    int wid = mp_obj_get_int(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    uint32_t color = mp_obj_get_int(args[3]);

    int bw, bh;
    uint32_t *buf = mp_kikios_api->window_get_buffer(wid, &bw, &bh);
    if (!buf) return mp_const_none;

    if (x >= 0 && x < bw && y >= 0 && y < bh) {
        buf[y * bw + x] = color;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_put_pixel_obj, 4, 4, mod_kiki_window_put_pixel);

// vibe.window_draw_char(wid, x, y, char, fg, bg)
static mp_obj_t mod_kiki_window_draw_char(size_t n_args, const mp_obj_t *args) {
    int wid = mp_obj_get_int(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int c = mp_obj_get_int(args[3]);
    uint32_t fg = mp_obj_get_int(args[4]);
    uint32_t bg = mp_obj_get_int(args[5]);

    int bw, bh;
    uint32_t *buf = mp_kikios_api->window_get_buffer(wid, &bw, &bh);
    if (!buf) return mp_const_none;

    const uint8_t *font = mp_kikios_api->font_data;
    const uint8_t *glyph = &font[(unsigned char)c * 16];

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            uint32_t color = (glyph[row] & (0x80 >> col)) ? fg : bg;
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < bw && py >= 0 && py < bh) {
                buf[py * bw + px] = color;
            }
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_draw_char_obj, 6, 6, mod_kiki_window_draw_char);

// vibe.window_draw_string(wid, x, y, str, fg, bg)
static mp_obj_t mod_kiki_window_draw_string(size_t n_args, const mp_obj_t *args) {
    int wid = mp_obj_get_int(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    const char *s = mp_obj_str_get_str(args[3]);
    uint32_t fg = mp_obj_get_int(args[4]);
    uint32_t bg = mp_obj_get_int(args[5]);

    int bw, bh;
    uint32_t *buf = mp_kikios_api->window_get_buffer(wid, &bw, &bh);
    if (!buf) return mp_const_none;

    const uint8_t *font = mp_kikios_api->font_data;
    int cx = x;

    while (*s) {
        const uint8_t *glyph = &font[(unsigned char)*s * 16];
        for (int row = 0; row < 16; row++) {
            for (int col = 0; col < 8; col++) {
                uint32_t color = (glyph[row] & (0x80 >> col)) ? fg : bg;
                int px = cx + col;
                int py = y + row;
                if (px >= 0 && px < bw && py >= 0 && py < bh) {
                    buf[py * bw + px] = color;
                }
            }
        }
        cx += 8;
        s++;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_draw_string_obj, 6, 6, mod_kiki_window_draw_string);

// vibe.window_draw_rect(wid, x, y, w, h, color) - outline only
static mp_obj_t mod_kiki_window_draw_rect(size_t n_args, const mp_obj_t *args) {
    int wid = mp_obj_get_int(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int w = mp_obj_get_int(args[3]);
    int h = mp_obj_get_int(args[4]);
    uint32_t color = mp_obj_get_int(args[5]);

    int bw, bh;
    uint32_t *buf = mp_kikios_api->window_get_buffer(wid, &bw, &bh);
    if (!buf) return mp_const_none;

    // Draw horizontal lines (top and bottom)
    for (int px = x; px < x + w; px++) {
        if (px >= 0 && px < bw) {
            if (y >= 0 && y < bh) buf[y * bw + px] = color;
            if (y + h - 1 >= 0 && y + h - 1 < bh) buf[(y + h - 1) * bw + px] = color;
        }
    }
    // Draw vertical lines (left and right)
    for (int py = y; py < y + h; py++) {
        if (py >= 0 && py < bh) {
            if (x >= 0 && x < bw) buf[py * bw + x] = color;
            if (x + w - 1 >= 0 && x + w - 1 < bw) buf[py * bw + x + w - 1] = color;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_draw_rect_obj, 6, 6, mod_kiki_window_draw_rect);

// vibe.window_draw_hline(wid, x, y, w, color)
static mp_obj_t mod_kiki_window_draw_hline(size_t n_args, const mp_obj_t *args) {
    int wid = mp_obj_get_int(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    int w = mp_obj_get_int(args[3]);
    uint32_t color = mp_obj_get_int(args[4]);

    int bw, bh;
    uint32_t *buf = mp_kikios_api->window_get_buffer(wid, &bw, &bh);
    if (!buf) return mp_const_none;

    if (y < 0 || y >= bh) return mp_const_none;
    if (x < 0) { w += x; x = 0; }
    if (x + w > bw) w = bw - x;
    if (w <= 0) return mp_const_none;

    for (int px = x; px < x + w; px++) {
        buf[y * bw + px] = color;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_draw_hline_obj, 5, 5, mod_kiki_window_draw_hline);

// ============================================================================
// TTF Font Rendering
// ============================================================================

// vibe.ttf_is_ready() -> bool
static mp_obj_t mod_kiki_ttf_is_ready(void) {
    return mp_obj_new_bool(mp_kikios_api->ttf_is_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_ttf_is_ready_obj, mod_kiki_ttf_is_ready);

// vibe.ttf_get_glyph(codepoint, size, style) -> dict or None
// Returns: {bitmap: bytes, width: int, height: int, xoff: int, yoff: int, advance: int}
static mp_obj_t mod_kiki_ttf_get_glyph(mp_obj_t cp_obj, mp_obj_t size_obj, mp_obj_t style_obj) {
    int cp = mp_obj_get_int(cp_obj);
    int size = mp_obj_get_int(size_obj);
    int style = mp_obj_get_int(style_obj);

    ttf_glyph_t *glyph = (ttf_glyph_t *)mp_kikios_api->ttf_get_glyph(cp, size, style);
    if (!glyph || !glyph->bitmap) return mp_const_none;

    mp_obj_t dict = mp_obj_new_dict(6);

    // Copy bitmap to bytes object
    int bmp_size = glyph->width * glyph->height;
    mp_obj_t bmp_bytes = mp_obj_new_bytes(glyph->bitmap, bmp_size);

    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_bitmap), bmp_bytes);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_width), mp_obj_new_int(glyph->width));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_height), mp_obj_new_int(glyph->height));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_xoff), mp_obj_new_int(glyph->xoff));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_yoff), mp_obj_new_int(glyph->yoff));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_advance), mp_obj_new_int(glyph->advance));

    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_3(mod_kiki_ttf_get_glyph_obj, mod_kiki_ttf_get_glyph);

// vibe.ttf_get_metrics(size) -> (ascent, descent, line_gap)
static mp_obj_t mod_kiki_ttf_get_metrics(mp_obj_t size_obj) {
    int size = mp_obj_get_int(size_obj);
    int ascent, descent, line_gap;
    mp_kikios_api->ttf_get_metrics(size, &ascent, &descent, &line_gap);

    mp_obj_t tuple[3];
    tuple[0] = mp_obj_new_int(ascent);
    tuple[1] = mp_obj_new_int(descent);
    tuple[2] = mp_obj_new_int(line_gap);
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_ttf_get_metrics_obj, mod_kiki_ttf_get_metrics);

// vibe.ttf_get_advance(codepoint, size) -> int
static mp_obj_t mod_kiki_ttf_get_advance(mp_obj_t cp_obj, mp_obj_t size_obj) {
    int cp = mp_obj_get_int(cp_obj);
    int size = mp_obj_get_int(size_obj);
    return mp_obj_new_int(mp_kikios_api->ttf_get_advance(cp, size));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_ttf_get_advance_obj, mod_kiki_ttf_get_advance);

// vibe.ttf_get_kerning(cp1, cp2, size) -> int
static mp_obj_t mod_kiki_ttf_get_kerning(mp_obj_t cp1_obj, mp_obj_t cp2_obj, mp_obj_t size_obj) {
    int cp1 = mp_obj_get_int(cp1_obj);
    int cp2 = mp_obj_get_int(cp2_obj);
    int size = mp_obj_get_int(size_obj);
    return mp_obj_new_int(mp_kikios_api->ttf_get_kerning(cp1, cp2, size));
}
static MP_DEFINE_CONST_FUN_OBJ_3(mod_kiki_ttf_get_kerning_obj, mod_kiki_ttf_get_kerning);

// vibe.window_draw_glyph(wid, x, y, bitmap, w, h, fg, bg)
// Draws a TTF glyph bitmap with alpha blending
static mp_obj_t mod_kiki_window_draw_glyph(size_t n_args, const mp_obj_t *args) {
    int wid = mp_obj_get_int(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[3], &bufinfo, MP_BUFFER_READ);
    const uint8_t *bitmap = bufinfo.buf;

    int gw = mp_obj_get_int(args[4]);
    int gh = mp_obj_get_int(args[5]);
    uint32_t fg = mp_obj_get_int(args[6]);
    uint32_t bg = mp_obj_get_int(args[7]);

    int bw, bh;
    uint32_t *buf = mp_kikios_api->window_get_buffer(wid, &bw, &bh);
    if (!buf) return mp_const_none;

    // Extract RGB components
    uint8_t fg_r = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fg_b = fg & 0xFF;
    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_b = bg & 0xFF;

    for (int row = 0; row < gh; row++) {
        for (int col = 0; col < gw; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < bw && py >= 0 && py < bh) {
                uint8_t alpha = bitmap[row * gw + col];
                // Blend: result = fg * alpha + bg * (255 - alpha)
                uint8_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
                uint8_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
                uint8_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
                buf[py * bw + px] = (r << 16) | (g << 8) | b;
            }
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_window_draw_glyph_obj, 8, 8, mod_kiki_window_draw_glyph);

// ============================================================================
// Sound
// ============================================================================

// vibe.sound_play(path) -> bool (plays WAV file)
static mp_obj_t mod_kiki_sound_play(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    // Open and read the file
    void *file = mp_kikios_api->open(path);
    if (!file) return mp_const_false;
    int size = mp_kikios_api->file_size(file);
    if (size <= 0) return mp_const_false;

    char *buf = mp_kikios_api->malloc(size);
    if (!buf) return mp_const_false;
    mp_kikios_api->read(file, buf, size, 0);

    int result = mp_kikios_api->sound_play_wav(buf, size);
    mp_kikios_api->free(buf);
    return mp_obj_new_bool(result == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_sound_play_obj, mod_kiki_sound_play);

// vibe.sound_stop()
static mp_obj_t mod_kiki_sound_stop(void) {
    mp_kikios_api->sound_stop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_sound_stop_obj, mod_kiki_sound_stop);

// vibe.sound_pause()
static mp_obj_t mod_kiki_sound_pause(void) {
    mp_kikios_api->sound_pause();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_sound_pause_obj, mod_kiki_sound_pause);

// vibe.sound_resume() -> bool
static mp_obj_t mod_kiki_sound_resume(void) {
    return mp_obj_new_bool(mp_kikios_api->sound_resume() == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_sound_resume_obj, mod_kiki_sound_resume);

// vibe.sound_is_playing() -> bool
static mp_obj_t mod_kiki_sound_is_playing(void) {
    return mp_obj_new_bool(mp_kikios_api->sound_is_playing());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_sound_is_playing_obj, mod_kiki_sound_is_playing);

// ============================================================================
// Networking
// ============================================================================

// vibe.dns_resolve(hostname) -> ip (int) or 0
static mp_obj_t mod_kiki_dns_resolve(mp_obj_t host_obj) {
    const char *host = mp_obj_str_get_str(host_obj);
    return mp_obj_new_int(mp_kikios_api->dns_resolve(host));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_dns_resolve_obj, mod_kiki_dns_resolve);

// vibe.ping(ip, timeout_ms=1000) -> bool
static mp_obj_t mod_kiki_ping(size_t n_args, const mp_obj_t *args) {
    uint32_t ip = mp_obj_get_int(args[0]);
    uint32_t timeout = n_args > 1 ? mp_obj_get_int(args[1]) : 1000;
    return mp_obj_new_bool(mp_kikios_api->net_ping(ip, 1, timeout) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_kiki_ping_obj, 1, 2, mod_kiki_ping);

// vibe.tcp_connect(ip, port) -> socket or -1
static mp_obj_t mod_kiki_tcp_connect(mp_obj_t ip_obj, mp_obj_t port_obj) {
    uint32_t ip = mp_obj_get_int(ip_obj);
    uint16_t port = mp_obj_get_int(port_obj);
    return mp_obj_new_int(mp_kikios_api->tcp_connect(ip, port));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_tcp_connect_obj, mod_kiki_tcp_connect);

// vibe.tcp_send(sock, data) -> bytes sent
static mp_obj_t mod_kiki_tcp_send(mp_obj_t sock_obj, mp_obj_t data_obj) {
    int sock = mp_obj_get_int(sock_obj);
    size_t len;
    const char *data = mp_obj_str_get_data(data_obj, &len);
    return mp_obj_new_int(mp_kikios_api->tcp_send(sock, data, len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_tcp_send_obj, mod_kiki_tcp_send);

// vibe.tcp_recv(sock, maxlen) -> bytes or None
static mp_obj_t mod_kiki_tcp_recv(mp_obj_t sock_obj, mp_obj_t maxlen_obj) {
    int sock = mp_obj_get_int(sock_obj);
    int maxlen = mp_obj_get_int(maxlen_obj);
    char *buf = m_new(char, maxlen);
    int received = mp_kikios_api->tcp_recv(sock, buf, maxlen);
    if (received <= 0) {
        m_del(char, buf, maxlen);
        return mp_const_none;
    }
    mp_obj_t result = mp_obj_new_bytes((const byte *)buf, received);
    m_del(char, buf, maxlen);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_tcp_recv_obj, mod_kiki_tcp_recv);

// vibe.tcp_close(sock)
static mp_obj_t mod_kiki_tcp_close(mp_obj_t sock_obj) {
    int sock = mp_obj_get_int(sock_obj);
    mp_kikios_api->tcp_close(sock);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_tcp_close_obj, mod_kiki_tcp_close);

// vibe.tls_connect(ip, port, hostname) -> socket or -1
static mp_obj_t mod_kiki_tls_connect(mp_obj_t ip_obj, mp_obj_t port_obj, mp_obj_t host_obj) {
    uint32_t ip = mp_obj_get_int(ip_obj);
    uint16_t port = mp_obj_get_int(port_obj);
    const char *host = mp_obj_str_get_str(host_obj);
    return mp_obj_new_int(mp_kikios_api->tls_connect(ip, port, host));
}
static MP_DEFINE_CONST_FUN_OBJ_3(mod_kiki_tls_connect_obj, mod_kiki_tls_connect);

// vibe.tls_send(sock, data) -> bytes sent
static mp_obj_t mod_kiki_tls_send(mp_obj_t sock_obj, mp_obj_t data_obj) {
    int sock = mp_obj_get_int(sock_obj);
    size_t len;
    const char *data = mp_obj_str_get_data(data_obj, &len);
    return mp_obj_new_int(mp_kikios_api->tls_send(sock, data, len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_tls_send_obj, mod_kiki_tls_send);

// vibe.tls_recv(sock, maxlen) -> bytes or None
static mp_obj_t mod_kiki_tls_recv(mp_obj_t sock_obj, mp_obj_t maxlen_obj) {
    int sock = mp_obj_get_int(sock_obj);
    int maxlen = mp_obj_get_int(maxlen_obj);
    char *buf = m_new(char, maxlen);
    int received = mp_kikios_api->tls_recv(sock, buf, maxlen);
    if (received <= 0) {
        m_del(char, buf, maxlen);
        return mp_const_none;
    }
    mp_obj_t result = mp_obj_new_bytes((const byte *)buf, received);
    m_del(char, buf, maxlen);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_kiki_tls_recv_obj, mod_kiki_tls_recv);

// vibe.tls_close(sock)
static mp_obj_t mod_kiki_tls_close(mp_obj_t sock_obj) {
    int sock = mp_obj_get_int(sock_obj);
    mp_kikios_api->tls_close(sock);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_kiki_tls_close_obj, mod_kiki_tls_close);

// vibe.get_ip() -> int
static mp_obj_t mod_kiki_get_ip(void) {
    return mp_obj_new_int(mp_kikios_api->net_get_ip());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_get_ip_obj, mod_kiki_get_ip);

// ============================================================================
// System Info
// ============================================================================

// vibe.mem_free()
static mp_obj_t mod_kiki_mem_free(void) {
    return mp_obj_new_int(mp_kikios_api->get_mem_free());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_mem_free_obj, mod_kiki_mem_free);

// vibe.mem_used()
static mp_obj_t mod_kiki_mem_used(void) {
    return mp_obj_new_int(mp_kikios_api->get_mem_used());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_mem_used_obj, mod_kiki_mem_used);

// vibe.ram_total()
static mp_obj_t mod_kiki_ram_total(void) {
    return mp_obj_new_int(mp_kikios_api->get_ram_total());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_ram_total_obj, mod_kiki_ram_total);

// vibe.disk_total()
static mp_obj_t mod_kiki_disk_total(void) {
    return mp_obj_new_int(mp_kikios_api->get_disk_total());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_disk_total_obj, mod_kiki_disk_total);

// vibe.disk_free()
static mp_obj_t mod_kiki_disk_free(void) {
    return mp_obj_new_int(mp_kikios_api->get_disk_free());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_disk_free_obj, mod_kiki_disk_free);

// vibe.cpu_info() -> (name, freq_mhz, cores)
static mp_obj_t mod_kiki_cpu_info(void) {
    mp_obj_t tuple[3];
    const char *name = mp_kikios_api->get_cpu_name();
    tuple[0] = mp_obj_new_str(name, strlen(name));
    tuple[1] = mp_obj_new_int(mp_kikios_api->get_cpu_freq_mhz());
    tuple[2] = mp_obj_new_int(mp_kikios_api->get_cpu_cores());
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_cpu_info_obj, mod_kiki_cpu_info);

// ============================================================================
// USB
// ============================================================================

// vibe.usb_devices() -> list of (vid, pid, name) tuples
static mp_obj_t mod_kiki_usb_devices(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    int count = mp_kikios_api->usb_device_count();
    for (int i = 0; i < count; i++) {
        uint16_t vid, pid;
        char name[64];
        if (mp_kikios_api->usb_device_info(i, &vid, &pid, name, sizeof(name)) == 0) {
            mp_obj_t tuple[3];
            tuple[0] = mp_obj_new_int(vid);
            tuple[1] = mp_obj_new_int(pid);
            tuple[2] = mp_obj_new_str(name, strlen(name));
            mp_obj_list_append(list, mp_obj_new_tuple(3, tuple));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_usb_devices_obj, mod_kiki_usb_devices);

// ============================================================================
// LED (Pi only)
// ============================================================================

// vibe.led_on()
static mp_obj_t mod_kiki_led_on(void) {
    mp_kikios_api->led_on();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_led_on_obj, mod_kiki_led_on);

// vibe.led_off()
static mp_obj_t mod_kiki_led_off(void) {
    mp_kikios_api->led_off();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_led_off_obj, mod_kiki_led_off);

// vibe.led_toggle()
static mp_obj_t mod_kiki_led_toggle(void) {
    mp_kikios_api->led_toggle();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_kiki_led_toggle_obj, mod_kiki_led_toggle);

// ============================================================================
// Module Registration
// ============================================================================

static const mp_rom_map_elem_t mp_module_vibe_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_vibe) },

    // Console I/O
    { MP_ROM_QSTR(MP_QSTR_putc), MP_ROM_PTR(&mod_kiki_putc_obj) },
    { MP_ROM_QSTR(MP_QSTR_puts), MP_ROM_PTR(&mod_kiki_puts_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_puts), MP_ROM_PTR(&mod_kiki_uart_puts_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&mod_kiki_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_color), MP_ROM_PTR(&mod_kiki_set_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_cursor), MP_ROM_PTR(&mod_kiki_set_cursor_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_cursor_enabled), MP_ROM_PTR(&mod_kiki_set_cursor_enabled_obj) },
    { MP_ROM_QSTR(MP_QSTR_console_size), MP_ROM_PTR(&mod_kiki_console_size_obj) },

    // Keyboard
    { MP_ROM_QSTR(MP_QSTR_has_key), MP_ROM_PTR(&mod_kiki_has_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_getc), MP_ROM_PTR(&mod_kiki_getc_obj) },

    // Timing
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&mod_kiki_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_uptime_ms), MP_ROM_PTR(&mod_kiki_uptime_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sched_yield), MP_ROM_PTR(&mod_kiki_sched_yield_obj) },

    // RTC
    { MP_ROM_QSTR(MP_QSTR_timestamp), MP_ROM_PTR(&mod_kiki_timestamp_obj) },
    { MP_ROM_QSTR(MP_QSTR_datetime), MP_ROM_PTR(&mod_kiki_datetime_obj) },

    // Filesystem
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mod_kiki_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mod_kiki_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mod_kiki_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_file_size), MP_ROM_PTR(&mod_kiki_file_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_dir), MP_ROM_PTR(&mod_kiki_is_dir_obj) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&mod_kiki_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&mod_kiki_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_delete), MP_ROM_PTR(&mod_kiki_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&mod_kiki_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&mod_kiki_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&mod_kiki_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&mod_kiki_chdir_obj) },

    // Process
    { MP_ROM_QSTR(MP_QSTR_exit), MP_ROM_PTR(&mod_kiki_exit_obj) },
    { MP_ROM_QSTR(MP_QSTR_exec), MP_ROM_PTR(&mod_kiki_exec_obj) },
    { MP_ROM_QSTR(MP_QSTR_spawn), MP_ROM_PTR(&mod_kiki_spawn_obj) },
    { MP_ROM_QSTR(MP_QSTR_kill), MP_ROM_PTR(&mod_kiki_kill_obj) },
    { MP_ROM_QSTR(MP_QSTR_ps), MP_ROM_PTR(&mod_kiki_ps_obj) },

    // Graphics
    { MP_ROM_QSTR(MP_QSTR_put_pixel), MP_ROM_PTR(&mod_kiki_put_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect), MP_ROM_PTR(&mod_kiki_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_char), MP_ROM_PTR(&mod_kiki_draw_char_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw_string), MP_ROM_PTR(&mod_kiki_draw_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_screen_size), MP_ROM_PTR(&mod_kiki_screen_size_obj) },

    // Mouse
    { MP_ROM_QSTR(MP_QSTR_mouse_pos), MP_ROM_PTR(&mod_kiki_mouse_pos_obj) },
    { MP_ROM_QSTR(MP_QSTR_mouse_buttons), MP_ROM_PTR(&mod_kiki_mouse_buttons_obj) },

    // Window management
    { MP_ROM_QSTR(MP_QSTR_window_create), MP_ROM_PTR(&mod_kiki_window_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_destroy), MP_ROM_PTR(&mod_kiki_window_destroy_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_poll), MP_ROM_PTR(&mod_kiki_window_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_invalidate), MP_ROM_PTR(&mod_kiki_window_invalidate_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_set_title), MP_ROM_PTR(&mod_kiki_window_set_title_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_size), MP_ROM_PTR(&mod_kiki_window_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_fill_rect), MP_ROM_PTR(&mod_kiki_window_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_put_pixel), MP_ROM_PTR(&mod_kiki_window_put_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_draw_char), MP_ROM_PTR(&mod_kiki_window_draw_char_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_draw_string), MP_ROM_PTR(&mod_kiki_window_draw_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_draw_rect), MP_ROM_PTR(&mod_kiki_window_draw_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_draw_hline), MP_ROM_PTR(&mod_kiki_window_draw_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_window_draw_glyph), MP_ROM_PTR(&mod_kiki_window_draw_glyph_obj) },

    // TTF Font Rendering
    { MP_ROM_QSTR(MP_QSTR_ttf_is_ready), MP_ROM_PTR(&mod_kiki_ttf_is_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_ttf_get_glyph), MP_ROM_PTR(&mod_kiki_ttf_get_glyph_obj) },
    { MP_ROM_QSTR(MP_QSTR_ttf_get_metrics), MP_ROM_PTR(&mod_kiki_ttf_get_metrics_obj) },
    { MP_ROM_QSTR(MP_QSTR_ttf_get_advance), MP_ROM_PTR(&mod_kiki_ttf_get_advance_obj) },
    { MP_ROM_QSTR(MP_QSTR_ttf_get_kerning), MP_ROM_PTR(&mod_kiki_ttf_get_kerning_obj) },

    // TTF style constants
    { MP_ROM_QSTR(MP_QSTR_TTF_NORMAL), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_TTF_BOLD), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_TTF_ITALIC), MP_ROM_INT(2) },

    // Sound
    { MP_ROM_QSTR(MP_QSTR_sound_play), MP_ROM_PTR(&mod_kiki_sound_play_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound_stop), MP_ROM_PTR(&mod_kiki_sound_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound_pause), MP_ROM_PTR(&mod_kiki_sound_pause_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound_resume), MP_ROM_PTR(&mod_kiki_sound_resume_obj) },
    { MP_ROM_QSTR(MP_QSTR_sound_is_playing), MP_ROM_PTR(&mod_kiki_sound_is_playing_obj) },

    // Networking
    { MP_ROM_QSTR(MP_QSTR_dns_resolve), MP_ROM_PTR(&mod_kiki_dns_resolve_obj) },
    { MP_ROM_QSTR(MP_QSTR_ping), MP_ROM_PTR(&mod_kiki_ping_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp_connect), MP_ROM_PTR(&mod_kiki_tcp_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp_send), MP_ROM_PTR(&mod_kiki_tcp_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp_recv), MP_ROM_PTR(&mod_kiki_tcp_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp_close), MP_ROM_PTR(&mod_kiki_tcp_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_tls_connect), MP_ROM_PTR(&mod_kiki_tls_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_tls_send), MP_ROM_PTR(&mod_kiki_tls_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_tls_recv), MP_ROM_PTR(&mod_kiki_tls_recv_obj) },
    { MP_ROM_QSTR(MP_QSTR_tls_close), MP_ROM_PTR(&mod_kiki_tls_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ip), MP_ROM_PTR(&mod_kiki_get_ip_obj) },

    // System info
    { MP_ROM_QSTR(MP_QSTR_mem_free), MP_ROM_PTR(&mod_kiki_mem_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_used), MP_ROM_PTR(&mod_kiki_mem_used_obj) },
    { MP_ROM_QSTR(MP_QSTR_ram_total), MP_ROM_PTR(&mod_kiki_ram_total_obj) },
    { MP_ROM_QSTR(MP_QSTR_disk_total), MP_ROM_PTR(&mod_kiki_disk_total_obj) },
    { MP_ROM_QSTR(MP_QSTR_disk_free), MP_ROM_PTR(&mod_kiki_disk_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_cpu_info), MP_ROM_PTR(&mod_kiki_cpu_info_obj) },

    // USB
    { MP_ROM_QSTR(MP_QSTR_usb_devices), MP_ROM_PTR(&mod_kiki_usb_devices_obj) },

    // LED
    { MP_ROM_QSTR(MP_QSTR_led_on), MP_ROM_PTR(&mod_kiki_led_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_led_off), MP_ROM_PTR(&mod_kiki_led_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_led_toggle), MP_ROM_PTR(&mod_kiki_led_toggle_obj) },

    // Color constants
    { MP_ROM_QSTR(MP_QSTR_BLACK), MP_ROM_INT(COLOR_BLACK) },
    { MP_ROM_QSTR(MP_QSTR_WHITE), MP_ROM_INT(COLOR_WHITE) },
    { MP_ROM_QSTR(MP_QSTR_RED), MP_ROM_INT(COLOR_RED) },
    { MP_ROM_QSTR(MP_QSTR_GREEN), MP_ROM_INT(COLOR_GREEN) },
    { MP_ROM_QSTR(MP_QSTR_BLUE), MP_ROM_INT(COLOR_BLUE) },
    { MP_ROM_QSTR(MP_QSTR_YELLOW), MP_ROM_INT(COLOR_YELLOW) },
    { MP_ROM_QSTR(MP_QSTR_CYAN), MP_ROM_INT(COLOR_CYAN) },
    { MP_ROM_QSTR(MP_QSTR_MAGENTA), MP_ROM_INT(COLOR_MAGENTA) },
    { MP_ROM_QSTR(MP_QSTR_AMBER), MP_ROM_INT(COLOR_AMBER) },

    // Window event constants
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_NONE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_MOUSE_DOWN), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_MOUSE_UP), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_MOUSE_MOVE), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_KEY), MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_CLOSE), MP_ROM_INT(5) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_FOCUS), MP_ROM_INT(6) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_UNFOCUS), MP_ROM_INT(7) },
    { MP_ROM_QSTR(MP_QSTR_WIN_EVENT_RESIZE), MP_ROM_INT(8) },

    // Mouse button constants
    { MP_ROM_QSTR(MP_QSTR_MOUSE_LEFT), MP_ROM_INT(0x01) },
    { MP_ROM_QSTR(MP_QSTR_MOUSE_RIGHT), MP_ROM_INT(0x02) },
    { MP_ROM_QSTR(MP_QSTR_MOUSE_MIDDLE), MP_ROM_INT(0x04) },
};

static MP_DEFINE_CONST_DICT(mp_module_vibe_globals, mp_module_vibe_globals_table);

const mp_obj_module_t mp_module_vibe = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_vibe_globals,
};

MP_REGISTER_MODULE(MP_QSTR_vibe, mp_module_vibe);
