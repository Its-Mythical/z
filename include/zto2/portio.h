#ifndef ZTO2_PORTIO_H
#define ZTO2_PORTIO_H

#include <zto2/types.h>

/* x86 port I/O. The "memory" clobber keeps the compiler from reordering
 * ordinary accesses across a device access. */
static inline void HalOutb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port) : "memory");
}

static inline u8 HalInb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

#endif
