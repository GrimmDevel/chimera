#ifndef XIU_STRING_H
#define XIU_STRING_H

#include <kernel/xiu_types.h>

usize strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, usize n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, usize n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, usize n);
int memcmp(const void *s1, const void *s2, usize n);
void *memset(void *s, int c, usize n);
void *memcpy(void *dest, const void *src, usize n);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strerror(int errnum);
usize strcspn(const char *s, const char *reject);
char *strtok(char *str, const char *delim);
char *stpncpy(char *dest, const char *src, usize n);
char *stpcpy(char *dest, const char *src);

char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *s, const char *accept);
usize strspn(const char *s, const char *accept);
void *memmove(void *dest, const void *src, usize n);
char *strdup(const char *s);
int strcoll(const char *s1, const char *s2);
int strcasecmp(const char *s1, const char *s2);
char *strsignal(int sig);

#endif
