/* =============================================================================
 * XIU Operating System — GUI Calculator Application
 * usr/bin/calc.c
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gui.h>

static char s_display[32] = "0";
static long long s_accum = 0;
static char s_op = 0;
static int  s_clear_on_next = 1;

static void handle_button(const char *label) {
    if (label[0] >= '0' && label[0] <= '9') {
        if (s_clear_on_next || strcmp(s_display, "0") == 0) {
            s_display[0] = label[0];
            s_display[1] = '\0';
            s_clear_on_next = 0;
        } else if (strlen(s_display) < 14) {
            strcat(s_display, label);
        }
    } else if (strcmp(label, "C") == 0) {
        strcpy(s_display, "0");
        s_accum = 0;
        s_op = 0;
        s_clear_on_next = 1;
    } else if (strcmp(label, "+") == 0 || strcmp(label, "-") == 0 ||
               strcmp(label, "*") == 0 || strcmp(label, "/") == 0) {
        s_accum = atoll(s_display);
        s_op = label[0];
        s_clear_on_next = 1;
    } else if (strcmp(label, "=") == 0) {
        long long current = atoll(s_display);
        long long res = current;
        if (s_op == '+') res = s_accum + current;
        else if (s_op == '-') res = s_accum - current;
        else if (s_op == '*') res = s_accum * current;
        else if (s_op == '/') res = (current != 0) ? (s_accum / current) : 0;

        snprintf(s_display, sizeof(s_display), "%lld", res);
        s_accum = res;
        s_op = 0;
        s_clear_on_next = 1;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    gui_window_t *win = gui_create_window(240, 310, "Calculator");
    if (!win) {
        printf("[-] Failed to connect to WindowServer\n");
        return 1;
    }

    const char *btn_labels[5][4] = {
        { "C", "+/-", "%", "/" },
        { "7", "8", "9", "*" },
        { "4", "5", "6", "-" },
        { "1", "2", "3", "+" },
        { "0", ".", "", "=" }
    };

    int active_btn_r = -1, active_btn_c = -1;
    int needs_redraw = 1;

    for (;;) {
        ws_event_t ev;
        while (gui_poll_event(win, &ev)) {
            if (ev.type == WS_EVENT_WINDOW_CLOSE) {
                gui_destroy_window(win);
                return 0;
            } else if (ev.type == WS_EVENT_MOUSE_DOWN) {
                // check button clicks in grid
                if (ev.y >= 70) {
                    int r = (ev.y - 70) / 46;
                    int c = ev.x / 60;
                    if (r >= 0 && r < 5 && c >= 0 && c < 4) {
                        active_btn_r = r;
                        active_btn_c = c;
                        handle_button(btn_labels[r][c]);
                        needs_redraw = 1;
                    }
                }
            }
        }

        if (needs_redraw) {
            // render Calculator UI
            gui_clear(win, 0x001E222A);

            gui_fill_rect(win, 10, 10, 220, 50, 0x00121418);
            int dlen = (int)strlen(s_display);
            int dx = 215 - (dlen * 12);
            if (dx < 20) dx = 20;
            gui_draw_text(win, dx, 25, s_display, GUI_COLOR_WHITE);

            // keypad Buttons
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 4; c++) {
                    if (r == 4 && c == 2) continue; // merged with 0
                    int bx = c * 60 + 5;
                    int by = 70 + r * 46 + 2;
                    int bw = (r == 4 && c == 0) ? 110 : 50;
                    int bh = 40;

                    int is_op = (c == 3);
                    int is_active = (r == active_btn_r && c == active_btn_c);
                    unsigned int col = is_op ? (is_active ? 0x00CC7A00 : 0x00FF9500)
                                             : (is_active ? 0x004A505C : 0x002D3037);

                    gui_fill_rect(win, bx, by, bw, bh, col);
                    const char *lbl = btn_labels[r][c];
                    int tx = bx + (bw - (int)strlen(lbl) * 8) / 2;
                    int ty = by + 12;
                    gui_draw_text(win, tx, ty, lbl, GUI_COLOR_WHITE);
                }
            }

            active_btn_r = -1;
            active_btn_c = -1;
            gui_update(win);
            needs_redraw = 0;
        }

        usleep(16000); // low latency 60Hz event check
    }

    return 0;
}
