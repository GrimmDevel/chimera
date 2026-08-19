#ifndef XIU_SETJMP_H
#define XIU_SETJMP_H

#include <kernel/xiu_types.h>

typedef struct {
    u64 rbx;
    u64 rbp;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rsp;
    u64 rip;
} jmp_buf[1];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
