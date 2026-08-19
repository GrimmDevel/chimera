#ifndef XIU_STDIO_H
#define XIU_STDIO_H

#include <kernel/xiu_types.h>

// minimal FILE stub - dash needs the type but we stub actual I/O
typedef struct { int fd; } FILE;

#define stdin  ((FILE *)0)
#define stdout ((FILE *)1)
#define stderr ((FILE *)2)

#define BUFSIZ 1024

int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, usize n, const char *fmt, ...);
int vsnprintf(char *buf, usize n, const char *fmt, __builtin_va_list args);
int fprintf(FILE *f, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, __builtin_va_list args);
int puts(const char *s);
int putchar(int c);
int fflush(FILE *f);
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *f);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
usize fread(void *ptr, usize size, usize nmemb, FILE *f);
usize fwrite(const void *ptr, usize size, usize nmemb, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int fgetc(FILE *f);
int getc(FILE *f);
char *fgets(char *s, int size, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int sscanf(const char *str, const char *fmt, ...);
int remove(const char *pathname);
void perror(const char *s);
i64 getline(char **lineptr, usize *n, FILE *stream);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)

#endif
