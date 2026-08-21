#ifndef SUDO_USAGE_H
#define SUDO_USAGE_H

static const char *sudo_usage1[] = {
    "-h | -K | -k | -V",
    NULL
};
static const char *sudo_usage2[] = {
    "-v [-ABkNnS] [-g group] [-h host] [-p prompt] [-u user]",
    NULL
};
static const char *sudo_usage3[] = {
    "-l [-ABkNnS] [-g group] [-h host] [-p prompt] [-U user]",
    "[-u user] [command [arg ...]]",
    NULL
};
static const char *sudo_usage4[] = {
    "[-ABbEHkNnPS] [-C num] [-D directory]",
    "[-g group] [-h host] [-p prompt] [-R directory] [-T timeout]",
    "[-u user] [VAR=value] [-i | -s] [command [arg ...]]",
    NULL
};
static const char *sudo_usage5[] = {
    "-e [-ABkNnS] [-C num] [-D directory]",
    "[-g group] [-h host] [-p prompt] [-R directory] [-T timeout]",
    "[-u user] file ...",
    NULL
};
static const char * const *sudo_usage[] = {
    sudo_usage1,
    sudo_usage2,
    sudo_usage3,
    sudo_usage4,
    sudo_usage5,
    NULL
};

static const char *sudoedit_usage1[] = {
    "-h | -V",
    NULL
};
static const char *sudoedit_usage2[] = {
    "[-ABkNnS] [-C num] [-D directory]",
    "[-g group] [-h host] [-p prompt] [-R directory] [-T timeout]",
    "[-u user] file ...",
    NULL
};
static const char * const *sudoedit_usage[] = {
    sudoedit_usage1,
    sudoedit_usage2,
    NULL
};

#endif /* SUDO_USAGE_H */
