/* =============================================================================
 * XIU Operating System — Apple defaults command (Property List Editor)
 * usr/bin/defaults.c
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <plist.h>

static void print_usage(const char *prog) {
    printf("Command line interface to a user's defaults.\n");
    printf("Usage:\n");
    printf("  %s read [<domain | file.plist>] [<key>]\n", prog);
    printf("  %s write <domain | file.plist> <key> [-string | -int | -bool] <value>\n", prog);
    printf("  %s delete <domain | file.plist> [<key>]\n", prog);
    printf("\nExamples:\n");
    printf("  %s read /System/Library/CoreServices/SystemVersion.plist\n", prog);
    printf("  %s read /System/Library/CoreServices/SystemVersion.plist ProductVersion\n", prog);
    printf("  %s write /System/Library/CoreServices/SystemVersion.plist ProductVersion 1.2.0\n", prog);
    printf("  %s write /System/Library/CoreServices/SystemVersion.plist ProductBuildVersion 24B51\n", prog);
}

static void resolve_domain_path(const char *domain, char *out, size_t maxlen) {
    if (domain[0] == '/' || domain[0] == '.') {
        strncpy(out, domain, maxlen - 1);
    } else {
        snprintf(out, maxlen, "/Library/Preferences/%s.plist", domain);
    }
    out[maxlen - 1] = '\0';
}

static int is_all_digits(const char *str) {
    if (!str || !*str) return 0;
    if (*str == '-' || *str == '+') str++;
    if (!*str) return 0;
    while (*str) {
        if (*str < '0' || *str > '9') return 0;
        str++;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "read") == 0) {
        const char *domain = (argc >= 3) ? argv[2] : "/System/Library/CoreServices/SystemVersion.plist";
        const char *key = (argc >= 4) ? argv[3] : NULL;

        char resolved_path[256];
        resolve_domain_path(domain, resolved_path, sizeof(resolved_path));

        plist_t *plist = plist_read_file(resolved_path);
        if (!plist) {
            printf("The domain/default pair of (%s, %s) does not exist\n", domain, key ? key : "all");
            return 1;
        }

        if (key) {
            plist_t *val = plist_dict_get(plist, key);
            if (!val) {
                printf("The domain/default pair of (%s, %s) does not exist\n", domain, key);
                plist_free(plist);
                return 1;
            }
            if (plist_get_type(val) == PLIST_TYPE_STRING) {
                printf("%s\n", plist_get_string_val(val));
            } else if (plist_get_type(val) == PLIST_TYPE_INTEGER) {
                printf("%lld\n", (long long)plist_get_int_val(val));
            } else if (plist_get_type(val) == PLIST_TYPE_BOOLEAN) {
                printf("%d\n", plist_get_bool_val(val));
            } else {
                plist_dump(val, 0);
            }
        } else {
            plist_dump(plist, 0);
        }

        plist_free(plist);
        return 0;
    }

    if (strcmp(cmd, "write") == 0) {
        if (argc < 5) {
            printf("defaults write: missing domain, key, or value\n");
            print_usage(argv[0]);
            return 1;
        }

        const char *domain = argv[2];
        const char *key = argv[3];
        const char *type_flag = NULL;
        const char *raw_val = NULL;

        if (argc == 5) {
            raw_val = argv[4];
        } else if (argc >= 6) {
            type_flag = argv[4];
            raw_val = argv[5];
        }

        char resolved_path[256];
        resolve_domain_path(domain, resolved_path, sizeof(resolved_path));

        plist_t *plist = plist_read_file(resolved_path);
        if (!plist) {
            plist = plist_create_dict();
        }
        if (plist_get_type(plist) != PLIST_TYPE_DICT) {
            printf("defaults write: root of '%s' is not a dictionary\n", resolved_path);
            plist_free(plist);
            return 1;
        }

        if (type_flag) {
            if (strcmp(type_flag, "-string") == 0) {
                plist_dict_set_string(plist, key, raw_val);
            } else if (strcmp(type_flag, "-int") == 0 || strcmp(type_flag, "-integer") == 0) {
                plist_dict_set_int(plist, key, strtoll(raw_val, NULL, 10));
            } else if (strcmp(type_flag, "-bool") == 0 || strcmp(type_flag, "-boolean") == 0) {
                int bval = (strcmp(raw_val, "true") == 0 || strcmp(raw_val, "YES") == 0 || strcmp(raw_val, "1") == 0);
                plist_dict_set_bool(plist, key, bval);
            } else {
                printf("defaults write: unknown type flag '%s'\n", type_flag);
                plist_free(plist);
                return 1;
            }
        } else {
            // Auto-detect type
            if (strcmp(raw_val, "true") == 0 || strcmp(raw_val, "YES") == 0) {
                plist_dict_set_bool(plist, key, 1);
            } else if (strcmp(raw_val, "false") == 0 || strcmp(raw_val, "NO") == 0) {
                plist_dict_set_bool(plist, key, 0);
            } else if (is_all_digits(raw_val)) {
                plist_dict_set_int(plist, key, strtoll(raw_val, NULL, 10));
            } else {
                plist_dict_set_string(plist, key, raw_val);
            }
        }

        if (plist_write_file(plist, resolved_path) != 0) {
            printf("defaults write: failed to write to '%s'\n", resolved_path);
            plist_free(plist);
            return 1;
        }

        printf("Updated '%s' = '%s' in %s\n", key, raw_val, resolved_path);
        plist_free(plist);
        return 0;
    }

    if (strcmp(cmd, "delete") == 0) {
        if (argc < 3) {
            printf("defaults delete: missing domain\n");
            return 1;
        }
        const char *domain = argv[2];
        const char *key = (argc >= 4) ? argv[3] : NULL;

        char resolved_path[256];
        resolve_domain_path(domain, resolved_path, sizeof(resolved_path));

        if (!key) {
            unlink(resolved_path);
            return 0;
        }

        plist_t *plist = plist_read_file(resolved_path);
        if (!plist) return 1;

        plist_dict_remove(plist, key);
        plist_write_file(plist, resolved_path);
        plist_free(plist);
        return 0;
    }

    printf("defaults: unknown command '%s'\n", cmd);
    print_usage(argv[0]);
    return 1;
}
