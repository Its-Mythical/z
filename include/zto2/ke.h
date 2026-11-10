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

/* Bugcheck codes. */
#define KE_BUGCHECK_KMODE_EXCEPTION_NOT_HANDLED 0x0000001Eu
#define KE_BUGCHECK_UNEXPECTED_KERNEL_MODE_TRAP 0x0000007Fu

/* Prints "*** BUGCHECK 0x<code> (<p1>, <p2>, <p3>, <p4>)" to COM1, then halts
 * with interrupts off. Fault-safe: no allocation, no locks. */
__attribute__((noreturn)) void KeBugCheck(u32 code, u64 p1, u64 p2, u64 p3, u64 p4);

/* Boot self-tests: each prints "ZTO2-SELFTEST: ok <name>" on success. Add new
 * tests to the ordered list in KeSelfTestRun (src/ke/selftest.c). */
void KeSelfTestRun(void);

/* Prints "ZTO2-SELFTEST: FAIL <name>" and halts. */
__attribute__((noreturn)) void KeSelfTestFail(const char *name);

#endif
