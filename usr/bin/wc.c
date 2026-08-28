// wc - word, line, character, and byte count
#include <kernel/chimera_types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool opt_l = false;
static bool opt_w = false;
static bool opt_c = false;

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static void count_fd(int fd, i64 *out_lines, i64 *out_words, i64 *out_bytes) {
    char buf[2048];
    i64 lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    i64 n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        bytes += n;
        for (i64 i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') lines++;
            if (is_space(c)) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                words++;
            }
        }
    }
    *out_lines = lines;
    *out_words = words;
    *out_bytes = bytes;
}

static void print_counts(i64 lines, i64 words, i64 bytes, const char *name) {
    bool all_default = (!opt_l && !opt_w && !opt_c);

    if (all_default || opt_l) printf(" %7lld", (long long)lines);
    if (all_default || opt_w) printf(" %7lld", (long long)words);
    if (all_default || opt_c) printf(" %7lld", (long long)bytes);
    if (name) printf(" %s", name);
    printf("\n");
}

int main(int argc, char *argv[]) {
    int arg = 1;
    while (arg < argc && argv[arg][0] == '-' && argv[arg][1] != '\0') {
        const char *p = &argv[arg][1];
        while (*p) {
            if (*p == 'l') opt_l = true;
            else if (*p == 'w') opt_w = true;
            else if (*p == 'c' || *p == 'm') opt_c = true;
            p++;
        }
        arg++;
    }

    if (arg >= argc) {
        i64 lines, words, bytes;
        count_fd(0, &lines, &words, &bytes);
        print_counts(lines, words, bytes, NULL);
        return 0;
    }

    i64 total_l = 0, total_w = 0, total_c = 0;
    int files = 0;

    for (int i = arg; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            printf("wc: %s: open failed\n", argv[i]);
            continue;
        }
        i64 l, w, c;
        count_fd(fd, &l, &w, &c);
        close(fd);

        print_counts(l, w, c, argv[i]);
        total_l += l;
        total_w += w;
        total_c += c;
        files++;
    }

    if (files > 1) {
        print_counts(total_l, total_w, total_c, "total");
    }

    return 0;
}
