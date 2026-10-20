#include "../../kernel/types.h"
#include "../../kernel/os_api.h"
#include "../../kernel/window.h"
#include "../../system/user/sys_api.h"

#define COLOR_WIN_BG     0x00C0C0C0
#define COLOR_DARK_GRAY  0x00808080
#define COLOR_LIGHT_GRAY 0x00DFDFDF
#define COLOR_WHITE      0x00FFFFFF
#define COLOR_BLACK      0x00000000
#define COLOR_RED        0x00FF0000
#define COLOR_BLUE       0x000000AA
#define COLOR_GREEN      0x00008000
#define COLOR_NAVY       0x00000080

#define GRID_W 9
#define GRID_H 9
#define NUM_MINES 10
#define CELL_SIZE 18

#define STATE_PLAYING 0
#define STATE_WON     1
#define STATE_LOST    2

typedef struct {
    uint8_t has_mine;
    uint8_t revealed;
    uint8_t flagged;
    uint8_t neighbor_count;
} Cell;

static os_api_t user_api;
static os_api_t* os = &user_api;
static Window mines_window;
static uint32_t mines_window_handle;
static uint8_t mines_close_requested;
static Cell grid[GRID_H][GRID_W];
static int game_state = STATE_PLAYING;
static uint32_t rng_seed = 0x5A5A1234;

static int user_register_window(Window* window) {
    user_window_desc_t description = {0};
    description.x = window->x;
    description.y = window->y;
    description.width = window->w;
    description.height = window->h;
    uint32_t i = 0;
    while (i < USER_WINDOW_TITLE_MAX - 1u && window->title[i]) {
        description.title[i] = window->title[i];
        i++;
    }
    description.title[i] = '\0';
    int32_t handle = sys_window_create(&description);
    if (handle < 0) return -1;
    mines_window_handle = (uint32_t)handle;
    return 0;
}

static void user_unregister_window(Window* window) {
    (void)window;
    if (mines_window_handle) {
        (void)sys_window_close(mines_window_handle);
        mines_window_handle = 0;
    }
}

static void user_fill_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                           uint32_t color) {
    user_window_draw_t drawing = {0};
    drawing.operation = USER_WINDOW_DRAW_FILL;
    drawing.x = x - mines_window.x;
    drawing.y = y - mines_window.y;
    drawing.width = width;
    drawing.height = height;
    drawing.color = color;
    (void)sys_window_draw(mines_window_handle, &drawing);
}

static void user_draw_string(int32_t x, int32_t y, const char* text,
                             uint32_t color) {
    user_window_draw_t drawing = {0};
    drawing.operation = USER_WINDOW_DRAW_TEXT;
    drawing.x = x - mines_window.x;
    drawing.y = y - mines_window.y;
    drawing.text = text;
    drawing.color = color;
    (void)sys_window_draw(mines_window_handle, &drawing);
}

static void user_blit_all(void) {
    if (mines_window_handle) (void)sys_window_present(mines_window_handle);
}

static uint32_t rand_simple(void) {
    rng_seed = rng_seed * 1103515245 + 12345;
    return (rng_seed >> 16) & 0x7FFF;
}

static void draw_sunken_panel(int32_t x, int32_t y, int32_t w, int32_t h) {
    os->graphics.fill_rect(x, y, w, h, COLOR_WIN_BG);
    os->graphics.fill_rect(x - 1, y - 1, w + 2, 1, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y - 1, 1, h + 2, COLOR_DARK_GRAY);
    os->graphics.fill_rect(x - 1, y + h, w + 2, 1, COLOR_WHITE);
    os->graphics.fill_rect(x + w, y - 1, 1, h + 2, COLOR_WHITE);
}

static void draw_tile(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t pressed) {
    os->graphics.fill_rect(x, y, w, h, COLOR_WIN_BG);
    uint32_t tl = pressed ? COLOR_DARK_GRAY : COLOR_WHITE;
    uint32_t br = pressed ? COLOR_WHITE : COLOR_DARK_GRAY;

    os->graphics.fill_rect(x, y, w, 1, tl);
    os->graphics.fill_rect(x, y, 1, h, tl);
    os->graphics.fill_rect(x, y + h - 1, w, 1, br);
    os->graphics.fill_rect(x + w - 1, y, 1, h, br);
}

static void reset_game(void) {
    game_state = STATE_PLAYING;

    for (int r = 0; r < GRID_H; r++) {
        for (int c = 0; c < GRID_W; c++) {
            grid[r][c].has_mine = 0;
            grid[r][c].revealed = 0;
            grid[r][c].flagged = 0;
            grid[r][c].neighbor_count = 0;
        }
    }

    int placed = 0;
    while (placed < NUM_MINES) {
        int r = rand_simple() % GRID_H;
        int c = rand_simple() % GRID_W;
        if (!grid[r][c].has_mine) {
            grid[r][c].has_mine = 1;
            placed++;
        }
    }

    for (int r = 0; r < GRID_H; r++) {
        for (int c = 0; c < GRID_W; c++) {
            if (grid[r][c].has_mine) continue;
            int count = 0;
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    int nr = r + dr;
                    int nc = c + dc;
                    if (nr >= 0 && nr < GRID_H && nc >= 0 && nc < GRID_W) {
                        if (grid[nr][nc].has_mine) count++;
                    }
                }
            }
            grid[r][c].neighbor_count = count;
        }
    }

}

static void reveal_empty_cells(int r, int c) {
    if (r < 0 || r >= GRID_H || c < 0 || c >= GRID_W) return;
    if (grid[r][c].revealed || grid[r][c].flagged) return;

    grid[r][c].revealed = 1;
    if (grid[r][c].neighbor_count > 0 || grid[r][c].has_mine) return;

    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr != 0 || dc != 0) {
                reveal_empty_cells(r + dr, c + dc);
            }
        }
    }

}

static void check_win(void) {
    for (int r = 0; r < GRID_H; r++) {
        for (int c = 0; c < GRID_W; c++) {
            if (!grid[r][c].has_mine && !grid[r][c].revealed) return;
        }
    }
    game_state = STATE_WON;
}

void mines_draw_content(Window* win) {
    int32_t panel_x = win->x + 12;
    int32_t panel_y = win->y + 24;
    int32_t panel_w = win->w - 24;
    int32_t board_x = win->x + 12;
    int32_t board_y = win->y + 60;

    draw_sunken_panel(panel_x, panel_y, panel_w, 28);

    int flags = 0;
    for (int r = 0; r < GRID_H; r++) {
        for (int c = 0; c < GRID_W; c++) {
            if (grid[r][c].flagged) flags++;
        }
    }
    int remain = NUM_MINES - flags;
    char rem_str[4];
    rem_str[0] = '0' + (remain < 0 ? 0 : (remain / 10));
    rem_str[1] = '0' + (remain < 0 ? 0 : (remain % 10));
    rem_str[2] = '\0';
    os->graphics.fill_rect(panel_x + 6, panel_y + 4, 30, 20, COLOR_BLACK);
    os->graphics.draw_string(panel_x + 10, panel_y + 8, rem_str, COLOR_RED);

    int32_t face_w = 26;
    int32_t face_h = 22;
    int32_t face_x = panel_x + (panel_w / 2) - 12;
    int32_t face_y = panel_y + 3;
    //draw_tile(face_x, face_y, 22, 22, 0);
    draw_tile(face_x, face_y, face_w, face_h, 0);
    const char* face = (game_state == STATE_PLAYING) ? ":-)" :
                       (game_state == STATE_WON)     ? "B-)" : "X-(";
    os->graphics.draw_string(face_x + 2, face_y + 5, face, COLOR_BLACK);
    //os->graphics.draw_string(face_x + 5, face_y + 4, face, COLOR_BLACK);

    draw_sunken_panel(board_x, board_y, GRID_W * CELL_SIZE, GRID_H * CELL_SIZE);

    for (int r = 0; r < GRID_H; r++) {
        for (int c = 0; c < GRID_W; c++) {
            int32_t tx = board_x + (c * CELL_SIZE);
            int32_t ty = board_y + (r * CELL_SIZE);
            Cell* cell = &grid[r][c];

            if (cell->revealed) {
                os->graphics.fill_rect(tx, ty, CELL_SIZE, CELL_SIZE, COLOR_LIGHT_GRAY);
                os->graphics.fill_rect(tx, ty, CELL_SIZE, 1, COLOR_DARK_GRAY);
                os->graphics.fill_rect(tx, ty, 1, CELL_SIZE, COLOR_DARK_GRAY);

                if (cell->has_mine) {
                    os->graphics.fill_rect(tx + 4, ty + 4, CELL_SIZE - 8, CELL_SIZE - 8, COLOR_BLACK);
                    os->graphics.draw_string(tx + 6, ty + 3, "*", COLOR_RED);
                } else if (cell->neighbor_count > 0) {
                    char digit[2] = { (char)('0' + cell->neighbor_count), '\0' };
                    uint32_t col = (cell->neighbor_count == 1) ? COLOR_BLUE :
                                   (cell->neighbor_count == 2) ? COLOR_GREEN :
                                   (cell->neighbor_count == 3) ? COLOR_RED : COLOR_NAVY;
                    os->graphics.draw_string(tx + 6, ty + 3, digit, col);
                }
            } else {
                draw_tile(tx, ty, CELL_SIZE, CELL_SIZE, 0);
                if (cell->flagged) {
                    os->graphics.draw_string(tx + 5, ty + 3, "P", COLOR_RED);
                }
            }
        }
    }

    if (game_state == STATE_LOST) {
        os->graphics.draw_string(win->x + 66, win->y + 225, "You lost!", COLOR_RED);
    } else if (game_state == STATE_WON) {
        os->graphics.draw_string(win->x + 66, win->y + 225, "You won!", COLOR_GREEN);
    }
}

void mines_handle_mouse(Window* win, int32_t lx, int32_t ly, uint8_t btn) {
    (void)win;
    (void)btn;

    int32_t face_lx = 12 + ((win->w - 24) / 2) - 12;
    int32_t face_ly = 24 + 3;

    if (lx >= face_lx && lx <= face_lx + 22 &&
        ly >= face_ly && ly <= face_ly + 22) 
    {
        reset_game();
        return;
    }

    int32_t board_lx = 12;
    int32_t board_ly = 60;

    if (lx >= board_lx && lx < board_lx + (GRID_W * CELL_SIZE) &&
        ly >= board_ly && ly < board_ly + (GRID_H * CELL_SIZE)) 
    {
        if (game_state != STATE_PLAYING) return;

        int col = (lx - board_lx) / CELL_SIZE;
        int row = (ly - board_ly) / CELL_SIZE;

        if (row < GRID_H && col < GRID_W && !grid[row][col].flagged) {
            if (grid[row][col].has_mine) {
                grid[row][col].revealed = 1;
                game_state = STATE_LOST;
                for (int r = 0; r < GRID_H; r++) {
                    for (int c = 0; c < GRID_W; c++) {
                        if (grid[r][c].has_mine) grid[r][c].revealed = 1;
                    }
                }
            } else {
                reveal_empty_cells(row, col);
                check_win();
            }
        }
    }
}

void mines_handle_key(Window* win, char c) {
    if ((uint8_t)c == 0x01) { // Esc key
        (void)win;
        mines_close_requested = 1;
        return;
    }
    if (c == 'r' || c == 'R') {
        reset_game();
        return;
    }
    if (c == 'f' || c == 'F') {
        for (int r = 0; r < GRID_H; r++) {
            for (int col = 0; col < GRID_W; col++) {
                if (!grid[r][col].revealed) {
                    grid[r][col].flagged = !grid[r][col].flagged;
                    return;
                }
            }
        }
    }
}

void user_main(const char* arguments) {
    (void)arguments;
    mines_window_handle = 0;
    mines_close_requested = 0;
    user_api.graphics.fill_rect = user_fill_rect;
    user_api.graphics.draw_string = user_draw_string;
    user_api.graphics.blit_all = user_blit_all;
    user_api.windows.register_window = user_register_window;
    user_api.windows.unregister_window = user_unregister_window;
    reset_game();

    mines_window.x = 260;
    mines_window.y = 70;
    mines_window.w = (GRID_W * CELL_SIZE) + 24;  // 186
    mines_window.h = (GRID_H * CELL_SIZE) + 76;  // 238
    mines_window.title = "Minesweeper";
    mines_window.visible = 1;
    mines_window.focused = 1;
    mines_window.draw_content = mines_draw_content;
    mines_window.handle_key = mines_handle_key;
    mines_window.handle_mouse = mines_handle_mouse;

    if (os->windows.register_window(&mines_window) < 0) {
        static const char error[] = "mines: unable to create window\n";
        sys_write(error, sizeof(error) - 1u);
        return;
    }
    static const char started[] = "Minesweeper is running.\n";
    sys_write(started, sizeof(started) - 1u);

    mines_draw_content(&mines_window);
    os->graphics.blit_all();
    while (!mines_close_requested) {
        user_window_event_t event;
        sys_yield();
        while (sys_window_poll(mines_window_handle, &event) == 1) {
            if (event.type == USER_WINDOW_EVENT_CLOSE) {
                mines_close_requested = 1;
                break;
            }
            if (event.type == USER_WINDOW_EVENT_REDRAW) {
                mines_window.x = event.x;
                mines_window.y = event.y;
                mines_draw_content(&mines_window);
            } else if (event.type == USER_WINDOW_EVENT_KEY) {
                mines_handle_key(&mines_window, (char)event.key);
            } else if (event.type == USER_WINDOW_EVENT_MOUSE) {
                mines_handle_mouse(&mines_window, event.x, event.y,
                                   (uint8_t)event.buttons);
            }
            if (mines_close_requested) break;
            mines_draw_content(&mines_window);
            os->graphics.blit_all();
        }
    }
    os->windows.unregister_window(&mines_window);
}
