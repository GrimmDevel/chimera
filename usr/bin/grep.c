// grep - search for patterns in files
#include <kernel/chimera_types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool case_insensitive = false;
static bool line_numbers = false;
static bool invert_match = false;
static bool count_only = false;
static bool list_files = false;

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static bool strstr_nocase(const char *haystack, const char *needle) {
    if (!*needle) return true;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && to_lower(*h) == to_lower(*n)) {
            h++;
            n++;
        }
        if (!*n) return true;
    }
    return false;
}

static int grep_fd(int fd, const char *filename, const char *pattern, bool print_name) {
    char buf[1024];
    char line[1024];
    int line_pos = 0;
    int line_num = 0;
    int match_count = 0;
    i64 n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (i64 i = 0; i < n; i++) {
            char c = buf[i];
            if (line_pos < (int)sizeof(line) - 1) {
                line[line_pos++] = c;
            }
            if (c == '\n') {
                line[line_pos] = '\0';
                line_num++;

                bool matched;
                if (case_insensitive) {
                    matched = strstr_nocase(line, pattern);
                } else {
                    matched = (strstr(line, pattern) != NULL);
                }

                if (invert_match) matched = !matched;

                if (matched) {
                    match_count++;
                    if (list_files) {
                        printf("%s\n", filename);
                        return 1;
                    }
                    if (!count_only) {
                        if (print_name && filename) printf("%s:", filename);
                        if (line_numbers) printf("%d:", line_num);
                        printf("%s", line);
                    }
                }
                line_pos = 0;
            }
        }
    }

    if (line_pos > 0) {
        line[line_pos] = '\0';
        line_num++;
        bool matched = case_insensitive ? strstr_nocase(line, pattern) : (strstr(line, pattern) != NULL);
        if (invert_match) matched = !matched;
        if (matched) {
            match_count++;
            if (!count_only && !list_files) {
                if (print_name && filename) printf("%s:", filename);
                if (line_numbers) printf("%d:", line_num);
                printf("%s\n", line);
            }
        }
    }

    if (count_only) {
        if (print_name && filename) printf("%s:", filename);
        printf("%d\n", match_count);
    }

    return (match_count > 0) ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: grep [-i] [-n] [-v] [-c] [-l] <pattern> [files...]\n");
        return 2;
    }

    int arg = 1;
    while (arg < argc && argv[arg][0] == '-' && argv[arg][1] != '\0') {
        const char *opt = &argv[arg][1];
        while (*opt) {
            if (*opt == 'i') case_insensitive = true;
            else if (*opt == 'n') line_numbers = true;
            else if (*opt == 'v') invert_match = true;
            else if (*opt == 'c') count_only = true;
            else if (*opt == 'l') list_files = true;
            opt++;
        }
        arg++;
    }

    if (arg >= argc) {
        printf("grep: missing pattern\n");
        return 2;
    }

    const char *pattern = argv[arg++];
    if (arg >= argc) {
        return grep_fd(0, NULL, pattern, false);
    }

    bool multi_file = (argc - arg > 1);
    int status = 1;
    for (int i = arg; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            printf("grep: %s: No such file or directory\n", argv[i]);
            continue;
        }
        if (grep_fd(fd, argv[i], pattern, multi_file) == 0) {
            status = 0;
        }
        close(fd);
    }
    return status;
}
