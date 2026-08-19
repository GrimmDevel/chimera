#ifndef _DLFCN_H
#define _DLFCN_H

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_GLOBAL 4
#define RTLD_LOCAL  8
#define RTLD_DEFAULT ((void *)0)

static inline void *dlopen(const char *filename, int flag) { (void)filename; (void)flag; return (void *)0; }
static inline char *dlerror(void) { return (char *)"Dynamic loading not supported"; }
static inline void *dlsym(void *handle, const char *symbol) { (void)handle; (void)symbol; return (void *)0; }
static inline int dlclose(void *handle) { (void)handle; return 0; }

#endif
