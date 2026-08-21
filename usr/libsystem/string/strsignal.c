/* =============================================================================
 * XIU Operating System — User Space C Library
 * usr/libsystem/string/strsignal.c
 * ============================================================================= */

#include <signal.h>
#include <stdio.h>
#include <string.h>

const char *const sys_siglist[NSIG] = {
    "Signal 0",
    "Hangup",
    "Interrupt",
    "Quit",
    "Illegal instruction",
    "Trace/BPT trap",
    "Abort trap",
    "EMT trap",
    "Floating point exception",
    "Killed",
    "Bus error",
    "Segmentation fault",
    "Bad system call",
    "Broken pipe",
    "Alarm clock",
    "Terminated",
    "Urgent I/O condition",
    "Suspended (signal)",
    "Suspended",
    "Continued",
    "Child exited",
    "Stopped (tty input)",
    "Stopped (tty output)",
    "I/O possible",
    "Cputime limit exceeded",
    "Filesize limit exceeded",
    "Virtual timer expired",
    "Profiling timer expired",
    "Window size changes",
    "Information request",
    "User defined signal 1",
    "User defined signal 2"
};

char *strsignal(int sig) {
    static char buf[64];
    if (sig >= 0 && sig < NSIG && sys_siglist[sig]) {
        return (char *)sys_siglist[sig];
    }
    snprintf(buf, sizeof(buf), "Unknown signal: %d", sig);
    return buf;
}
