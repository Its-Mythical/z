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

/* --- Threads and the ready queue (host-compilable; no hardware) ------------ */
#define KE_PRIORITY_LEVELS 32 /* priorities 0 (idle) to 31 */

#define KT_READY    0
#define KT_RUNNING  1
#define KT_FINISHED 2

typedef struct KTHREAD {
    u64 rsp;              /* saved stack pointer while switched out */
    struct KTHREAD *next; /* ready-queue link */
    u8 priority;
    u8 quantum_left;
    u8 state;
    void *stack;
} KTHREAD;

typedef struct {
    KTHREAD *head[KE_PRIORITY_LEVELS];
    KTHREAD *tail[KE_PRIORITY_LEVELS];
    u32 bitmap; /* bit p set when priority p has a ready thread */
} KREADY_QUEUE;

void KeRqInit(KREADY_QUEUE *rq);
/* Appends to the tail of its priority's FIFO. priority must be <= 31. */
void KeRqInsert(KREADY_QUEUE *rq, KTHREAD *t);
/* Removes and returns the oldest thread of the highest ready priority; NULL if empty. */
KTHREAD *KeRqPopHighest(KREADY_QUEUE *rq);
/* Highest ready priority, or -1 if empty. */
int KeRqHighestPriority(const KREADY_QUEUE *rq);

#endif
