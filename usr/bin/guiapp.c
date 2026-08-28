// simple gui monitor test app
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gui.h>

typedef struct {
    unsigned long long total_memory;
    unsigned long long free_memory;
    unsigned int       cpu_count;
    unsigned int       uptime_seconds;
    char               os_name[32];
    char               os_version[32];
    char               kernel_name[32];
    char               architecture[16];
    char               hostname[64];
} chimera_sysinfo_t;

extern int sysinfo(chimera_sysinfo_t *info);

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[GUI App] Connecting to WindowServer via Mach IPC...\n");

    gui_window_t *win = gui_create_window(360, 240, "Activity Monitor - XIU OS");
    if (!win) {
        printf("[-] Failed to create window. Is WindowServer running?\n");
        return 1;
    }

    printf("[GUI App] Window created! ID=%u (surface=%p). Entering event loop...\n", win->window_id, win->pixels);

    int btn_pressed = 0;
    int counter = 0;
    int tick = 0;
    int needs_redraw = 1;

    for (;;) {
        ws_event_t ev;
        while (gui_poll_event(win, &ev)) {
            if (ev.type == WS_EVENT_WINDOW_CLOSE) {
                printf("[GUI App] Window close event received.\n");
                gui_destroy_window(win);
                return 0;
            } else if (ev.type == WS_EVENT_MOUSE_DOWN) {
                // clicked refresh button
                if (ev.x >= 20 && ev.x <= 160 && ev.y >= 180 && ev.y <= 215) {
                    btn_pressed = 1;
                    counter++;
                    needs_redraw = 1;
                }
            }
        }

        tick++;
        if (tick >= 25) {
            tick = 0;
            needs_redraw = 1;
        }

        if (needs_redraw) {
            gui_clear(win, GUI_COLOR_BG_GRAY);

            gui_fill_rect(win, 10, 10, 340, 45, GUI_COLOR_DARK_GRAY);
            gui_draw_text(win, 20, 16, "XIU Darwin Activity Monitor", GUI_COLOR_WHITE);
            gui_draw_text(win, 20, 32, "Hybrid Mach/BSD Microkernel", GUI_COLOR_LIGHT_GRAY);

            chimera_sysinfo_t info;
            memset(&info, 0, sizeof(info));
            if (sysinfo(&info) == 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "SMP Cores: %u Active Cores", info.cpu_count);
                gui_draw_text(win, 20, 68, buf, GUI_COLOR_BLACK);

                snprintf(buf, sizeof(buf), "RAM: %llu MB Total / %llu MB Free",
                         info.total_memory / (1024 * 1024), info.free_memory / (1024 * 1024));
                gui_draw_text(win, 20, 88, buf, GUI_COLOR_BLACK);

                for (unsigned int c = 0; c < 4; c++) {
                    int bx = 20 + (c * 80);
                    gui_fill_rect(win, bx, 115, 70, 18, GUI_COLOR_LIGHT_GRAY);
                    gui_fill_rect(win, bx, 115, 35 + ((counter * 7 + c * 13) % 35), 18, GUI_COLOR_GREEN);
                    snprintf(buf, sizeof(buf), "CPU %u", c);
                    gui_draw_text(win, bx + 12, 117, buf, GUI_COLOR_WHITE);
                }
            }

            char cnt_buf[64];
            snprintf(cnt_buf, sizeof(cnt_buf), "Clicks: %d", counter);
            gui_draw_text(win, 180, 192, cnt_buf, GUI_COLOR_DARK_GRAY);

            gui_draw_button(win, 20, 180, 140, 35, "Refresh Stats", btn_pressed);
            btn_pressed = 0;

            gui_update(win);
            needs_redraw = 0;
        }

        usleep(20000);
    }

    return 0;
}
