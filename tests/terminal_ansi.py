"""Run the terminal's actual parser on the host, without the window manager."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/system/graphics/terminal/terminal.c").read_text()
parser = source[source.index("#define TERM_MAX_LINES"):source.index("void terminal_write(")]
checks = r'''
#include <assert.h>
#include <string.h>
int main(void) {
    terminal_clear_output();
    terminal_append_output("\033[0;1;36;47mA\033[0mB");
    assert(strcmp(term_lines[0], "AB") == 0);
    assert(term_fg[0][0] == 0x55FFFF && term_bg[0][0] == 0xAAAAAA);
    assert(term_fg[0][1] == 0xAAAAAA && term_bg[0][1] == 0);
    terminal_append_output("\033[3");
    terminal_append_output("1;4");
    terminal_append_output("4mC");
    assert(term_fg[0][2] == 0xAA0000 && term_bg[0][2] == 0x0000AA);
    terminal_append_output("\033[97;100mD\033[39;49mE");
    assert(term_fg[0][3] == 0xFFFFFF && term_bg[0][3] == 0x555555);
    assert(term_bg[0][4] == 0);
    terminal_append_output("\033[5;7mF\033[27mG");
    assert(term_fg[0][5] == 0 && term_bg[0][5] == 0xAAAAAA);
    assert(term_bg[0][6] == 0);
    terminal_append_output("\033[38;2;1;31;44mH");
    assert(term_fg[0][7] == 0xAAAAAA && term_bg[0][7] == 0);
    terminal_append_output("\033[999999999999999999999999mI\033[?25lJ");
    assert(strcmp(term_lines[0], "ABCDEFGHIJ") == 0);
    terminal_clear_output();
    terminal_append_output("\033[31;44m");
    for (int i = 0; i < 44; ++i) terminal_append_output("X");
    assert(term_cursor_row == 0 && term_cursor_col == 44);
    terminal_append_output("\033[0m\r\nY");
    assert(term_cursor_row == 1 && term_cursor_col == 1);
    terminal_scroll();
    assert(term_lines[0][0] == 'Y' && term_bg[0][0] == 0);
    terminal_clear_output();
    terminal_append_output("\033[32mZ\n");
    terminal_scroll();
    assert(term_lines[0][0] == 0);
    terminal_clear_output();
    terminal_append_output("\033]0;ignored title\007OK");
    assert(strcmp(term_lines[0], "OK") == 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(
        "#include <stdint.h>\ntypedef int Window; typedef int os_api_t;\n"
        + parser + checks
    )
    subprocess.run(["gcc", "-std=c99", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Terminal ANSI parser checks passed")
