#ifndef __STRING_H__
#define __STRING_H__

#include "types.h"

extern char * ___strtok;
extern char * strpbrk(const char *,const char *);
extern char * strtok(char *,const char *);
extern char * strsep(char **,const char *);
extern size_t strspn(const char *,const char *);
extern unsigned char getc(void);
extern void putc(unsigned char c);
extern int puts(const char *s);
extern int putchar(int c);

/*
 * Include machine specific inline routines
 */
extern char * strcpy(char *,const char *);
extern char * strncpy(char *,const char *, size_t);
extern char * strcat(char *, const char *);
extern char * strncat(char *, const char *, size_t);
extern int strcmp(const char *,const char *);
extern int strncmp(const char *,const char *,size_t);
extern int strnicmp(const char *, const char *, size_t);
extern char * strchr(const char *,int);
extern char * strrchr(const char *,int);
extern char * strstr(const char *,const char *);
extern size_t strlen(const char *);
extern size_t strnlen(const char *,size_t);
extern char * strdup(const char *);

extern void * memset(void *,int,size_t);
extern void * memcpy(void *,const void *,size_t);
extern void * memmove(void *,const void *,size_t);
extern void * memscan(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
extern void * memchr(const void *,int,size_t);

/*
 * ffs - find first (least-significant) bit set
 */
static inline __attribute__((always_inline)) int ffs(int x)
{
	return __builtin_ffs(x);
}

/*
 * fls - find last (most-significant) bit set
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */
static inline __attribute__((always_inline)) int fls(int x)
{
	return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}

/*
 * __ffs - find first bit in word.
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline __attribute__((always_inline)) unsigned long __ffs(unsigned long word)
{
	return __builtin_ctzl(word);
}

/*
 * __fls - find last (most-significant) set bit in a long word
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static inline __attribute__((always_inline)) unsigned long __fls(unsigned long word)
{
	return (sizeof(word) * 8) - 1 - __builtin_clzl(word);
}

#endif
