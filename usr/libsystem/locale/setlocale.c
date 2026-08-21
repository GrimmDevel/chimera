/* =============================================================================
 * XIU Operating System — User Space C Library
 * usr/libsystem/locale/setlocale.c
 * ============================================================================= */

#include <locale.h>
#include <string.h>

char *setlocale(int category, const char *locale) {
    (void)category;
    static char current_locale[32] = "C";
    if (!locale) return current_locale;
    if (strcmp(locale, "") == 0 || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0 ||
        strcmp(locale, "C.UTF-8") == 0 || strcmp(locale, "en_US.UTF-8") == 0 || strcmp(locale, "UTF-8") == 0) {
        strncpy(current_locale, locale[0] ? locale : "UTF-8", sizeof(current_locale) - 1);
        current_locale[sizeof(current_locale) - 1] = '\0';
        return current_locale;
    }
    return NULL;
}
