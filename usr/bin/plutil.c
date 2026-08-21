/* =============================================================================
 * XIU Operating System — Apple plutil (Property List Utility)
 * usr/bin/plutil.c
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <plist.h>

static void print_usage(const char *prog) {
    printf("Usage: %s [-p | -lint | -key <name>] <file.plist>\n", prog);
    printf("Options:\n");
    printf("  -p            Print plist content in ASCII/property-list format\n");
    printf("  -lint         Validate syntax of plist file\n");
    printf("  -key <name>   Extract and print value of specified dictionary key\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int mode_print = 1;
    int mode_lint = 0;
    const char *key_query = NULL;
    const char *filepath = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            mode_print = 1;
            mode_lint = 0;
        } else if (strcmp(argv[i], "-lint") == 0) {
            mode_lint = 1;
            mode_print = 0;
        } else if (strcmp(argv[i], "-key") == 0 && i + 1 < argc) {
            key_query = argv[++i];
            mode_print = 0;
            mode_lint = 0;
        } else if (argv[i][0] == '-') {
            printf("plutil: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            filepath = argv[i];
        }
    }

    if (!filepath) {
        printf("plutil: missing input file\n");
        return 1;
    }

    plist_t *plist = plist_read_file(filepath);
    if (!plist) {
        if (mode_lint) {
            printf("%s: parse error or file not found\n", filepath);
        } else {
            printf("plutil: error: unable to parse plist at '%s'\n", filepath);
        }
        return 1;
    }

    if (mode_lint) {
        printf("%s: OK\n", filepath);
    } else if (key_query) {
        plist_t *val = plist_dict_get(plist, key_query);
        if (!val) {
            printf("plutil: key '%s' not found in dictionary\n", key_query);
            plist_free(plist);
            return 1;
        }
        if (plist_get_type(val) == PLIST_TYPE_STRING) {
            printf("%s\n", plist_get_string_val(val));
        } else if (plist_get_type(val) == PLIST_TYPE_INTEGER) {
            printf("%lld\n", (long long)plist_get_int_val(val));
        } else if (plist_get_type(val) == PLIST_TYPE_BOOLEAN) {
            printf("%s\n", plist_get_bool_val(val) ? "YES" : "NO");
        } else {
            plist_dump(val, 0);
        }
    } else if (mode_print) {
        plist_dump(plist, 0);
    }

    plist_free(plist);
    return 0;
}
