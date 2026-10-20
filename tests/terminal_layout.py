"""Exercise the real terminal renderer at multiple window sizes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
test = r'''
#include "src/system/graphics/terminal/terminal.c"
#include <assert.h>
static int seen, first_x, last_x, last_y;
static void fill(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
static void text(int32_t x,int32_t y,const char* s,uint32_t color) {
    (void)color;
    if (s[0] != 'X') return;
    if (!seen) first_x = x;
    ++seen; last_x = x; last_y = y;
    assert(x >= terminal_window.x + 12);
    assert(x + 8 <= terminal_window.x + terminal_window.w - 12);
    assert(y >= terminal_window.y + 26);
    assert(y + 12 <= terminal_window.y + terminal_window.h - 12);
}
void wm_bring_to_front(Window* w) { (void)w; }
uint32_t paging_current_directory(void) { return 0; }
void paging_activate_kernel(void) {}
void paging_activate_directory(uint32_t d) { (void)d; }
void shell_execute_command(const char* s,shell_output_fn o,shell_clear_fn c) {
    (void)s; (void)o; (void)c;
}
int main(void) {
    os_api_t api = {0};
    api.graphics.fill_rect = fill;
    api.graphics.draw_string = text;
    terminal_init(&api);
    terminal_clear_output();
    for (int i = 0; i < 80; ++i) terminal_append_output("X");
    terminal_window.w = 824; /* 100 columns */
    terminal_draw(&terminal_window);
    assert(seen == 80 && last_y == terminal_window.y + 26);
    terminal_window.w = 344; /* 40 columns: reflow existing output */
    seen = 0;
    terminal_draw(&terminal_window);
    assert(seen == 80 && last_y == terminal_window.y + 38);
    assert(last_x == first_x + 39 * 8);
    terminal_window.w = 824;
    seen = 0;
    terminal_draw(&terminal_window);
    assert(seen == 80 && last_y == terminal_window.y + 26);
    terminal_window.w = 184;
    terminal_window.h = 110;
    seen = 0;
    terminal_draw(&terminal_window);
    assert(seen > 0 && term_cursor_col == 80);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(test)
    subprocess.run(["gcc", "-I", str(root), str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Terminal reflow, clipping, and resize round-trip checks passed")
