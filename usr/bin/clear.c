// clear - clear terminal screen
#include <kernel/chimera_types.h>
#include <unistd.h>

int main(void) {
    const char esc[] = "\033[2J\033[H";
    write(1, esc, sizeof(esc) - 1);
    return 0;
}
