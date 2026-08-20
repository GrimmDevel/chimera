// tail - output last part of files
#include <kernel/xiu_types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_TAIL_LINES 1024
#define MAX_LINE_LEN 512

static void tail_fd(int fd, int max_lines) {
    if (max_lines > MAX_TAIL_LINES) max_lines = MAX_TAIL_LINES;
    if (max_lines <= 0) max_lines = 10;

    char ring[MAX_TAIL_LINES][MAX_LINE_LEN];
    int line_lens[MAX_TAIL_LINES];
    int total_lines = 0;

    char cur_line[MAX_LINE_LEN];
    int cur_pos = 0;

    char buf[1024];
    i64 n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (i64 i = 0; i < n; i++) {
            char c = buf[i];
            if (cur_pos < MAX_LINE_LEN - 1) {
                cur_line[cur_pos++] = c;
            }
            if (c == '\n') {
                cur_line[cur_pos] = '\0';
                int slot = total_lines % max_lines;
                memcpy(ring[slot], cur_line, cur_pos + 1);
                line_lens[slot] = cur_pos;
                total_lines++;
                cur_pos = 0;
            }
        }
    }

    if (cur_pos > 0) {
        cur_line[cur_pos] = '\0';
        int slot = total_lines % max_lines;
        memcpy(ring[slot], cur_line, cur_pos + 1);
        line_lens[slot] = cur_pos;
        total_lines++;
    }

    int count = total_lines < max_lines ? total_lines : max_lines;
    int start = (total_lines >= max_lines) ? (total_lines % max_lines) : 0;

    for (int i = 0; i < count; i++) {
        int idx = (start + i) % max_lines;
        write(1, ring[idx], line_lens[idx]);
    }
}

int main(int argc, char *argv[]) {
    int max_lines = 10;
    int arg_start = 1;

    if (argc > 1 && argv[1][0] == '-') {
        if (strcmp(argv[1], "-n") == 0 && argc > 2) {
            max_lines = atoi(argv[2]);
            arg_start = 3;
        } else if (argv[1][1] >= '0' && argv[1][1] <= '9') {
            max_lines = atoi(&argv[1][1]);
            arg_start = 2;
        }
    }

    if (arg_start >= argc) {
        tail_fd(0, max_lines);
        return 0;
    }

    for (int i = arg_start; i < argc; i++) {
        if (argc - arg_start > 1) {
            printf("==> %s <==\n", argv[i]);
        }
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            printf("tail: cannot open '%s'\n", argv[i]);
            continue;
        }
        tail_fd(fd, max_lines);
        close(fd);
    }
    return 0;
}
