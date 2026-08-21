/* =============================================================================
 * XIU Operating System — User Space C Library
 * usr/libsystem/gen/getprogname.c
 * ============================================================================= */

#include <stdlib.h>
#include <string.h>

static const char *s_program_name = "xiu";

const char **_NSGetProgname(void) {
    return &s_program_name;
}

const char *getprogname(void) {
    return s_program_name;
}

const char *_getprogname(void) {
    return s_program_name;
}
