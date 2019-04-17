/*
 * Use local definitions of C library macros and functions
 * NOTE: The function implementations may not be as efficient
 * as an inline or assembly code implementation provided by a
 * native C library.
 */

#pragma once

#include <types.h>

typedef __builtin_va_list	va_list;

#define va_start(v, l)		__builtin_va_start(v, l)
#define va_arg(v, l)		__builtin_va_arg(v, l)
#define va_end(v)			__builtin_va_end(v)
#define va_copy(d, s)		__builtin_va_copy(d, s)

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char * buf, size_t size, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list args);
int sprintf(char * buf, const char *fmt, ...);
int vsscanf(const char * buf, const char * fmt, va_list args);
int sscanf(const char * buf, const char * fmt, ...);
