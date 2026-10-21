#include "../../kernel/os_api.h"
#include "../../kernel/window.h"

int app_main(os_api_t* api) {
    if (!api) return -1;

    int mouse_x = SCREEN_WIDTH / 2;
    int mouse_y = SCREEN_HEIGHT / 2;
    uint8_t prev_buttons = 0;
    int needs_full_draw = 1;

    
    const int card_x = 100;
    const int card_y = 120;
    const int card_w = 600;
    const int card_h = 360;

    
    const int btn_x = card_x + 430;
    const int btn_y = card_y + 290;
    const int btn_w = 140;
    const int btn_h = 36;

    
    while (1) {
        
        if (api->network.net_poll) {
            api->network.net_poll();
        }

        
        if (needs_full_draw) {
            
            api->graphics.fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0x00003366);

            
            api->graphics.fill_rect(0, 0, SCREEN_WIDTH, 56, 0x00002244);
            api->graphics.draw_string(24, 20, "NixelOS Out-of-Box Experience", COLOR_WHITE);

            
            api->graphics.fill_rect(card_x, card_y, card_w, card_h, COLOR_WIN_BG);

            
            api->graphics.fill_rect(card_x, card_y, card_w, 2, COLOR_WHITE);
            api->graphics.fill_rect(card_x, card_y, 2, card_h, COLOR_WHITE);
            api->graphics.fill_rect(card_x + card_w - 2, card_y, 2, card_h, COLOR_DARK_GRAY);
            api->graphics.fill_rect(card_x, card_y + card_h - 2, card_w, 2, COLOR_DARK_GRAY);

            
            api->graphics.draw_string(card_x + 30, card_y + 40, "Welcome to your new NixelOS installation!", COLOR_BLACK);
            api->graphics.draw_string(card_x + 30, card_y + 80, "[PLACEHOLDER]", 0x00333333);
            api->graphics.draw_string(card_x + 30, card_y + 110, "[PLACEHOLDER]", 0x00333333);
            api->graphics.draw_string(card_x + 30, card_y + 150, "[PLACEHOLDER]", 0x00333333);

            
            api->graphics.draw_button(btn_x, btn_y, btn_w, btn_h, "Finish Setup", 0);

            
            api->graphics.blit_all();
            needs_full_draw = 0;
        }

        
        os_input_event_t ev;
        while (api->input.poll_input && api->input.poll_input(&ev)) {
            if (ev.type == OS_INPUT_MOUSE) {
                mouse_x += ev.mouse_dx;
                mouse_y -= ev.mouse_dy; 

                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x >= SCREEN_WIDTH) mouse_x = SCREEN_WIDTH - 1;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y >= SCREEN_HEIGHT) mouse_y = SCREEN_HEIGHT - 1;

                if (api->input.move_cursor) {
                    api->input.move_cursor(mouse_x, mouse_y, 1);
                }

                uint8_t left_click = (ev.mouse_buttons & 1) && !(prev_buttons & 1);
                prev_buttons = ev.mouse_buttons;

                if (left_click) {
                    if (mouse_x >= btn_x && mouse_x <= (btn_x + btn_w) &&
                        mouse_y >= btn_y && mouse_y <= (btn_y + btn_h)) {
                        
                        const char marker = '1';
                        api->fs.write("OOBE.DON", (const uint8_t*)&marker, 1);
                        return 0;
                    }
                }
            } else if (ev.type == OS_INPUT_KEY) {
                char ch = api->input.translate_scancode ? api->input.translate_scancode(ev.scancode) : 0;
                if (ch == '\n' || ch == 27 || ch == ' ') {
                    const char marker = '1';
                    api->fs.write("OOBE.DON", (const uint8_t*)&marker, 1);
                    return 0;
                }
            }
        }

        __asm__ volatile ("pause");
    }

    return 0;
}