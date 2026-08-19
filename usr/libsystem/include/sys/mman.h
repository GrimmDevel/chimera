#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED  1
#define MAP_PRIVATE 2
#define MAP_FIXED   16
#define MAP_ANONYMOUS 32
#define MAP_ANON    MAP_ANONYMOUS

#define MAP_FAILED ((void *)-1)

void *mmap(void *addr, usize length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, usize length);
int mprotect(void *addr, usize len, int prot);
int memfd_create(const char *name, unsigned int flags);

#endif
