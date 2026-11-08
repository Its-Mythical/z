#ifndef ZTO2_KE_H
#define ZTO2_KE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <zto2/types.h>

/* snprintf-style formatter: returns the length that would have been written;
 * NUL-terminates when cap > 0. Supports %c %s %d %u %x %p %lld %llu %llx %%. */
size_t KeFormat(char *buf, size_t cap, const char *fmt, va_list ap);

/* Formats into a 512-byte buffer (truncating) and writes it to COM1. */
void KdPrint(const char *fmt, ...);

#endif
