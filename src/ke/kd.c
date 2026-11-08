#include <zto2/hal.h>
#include <zto2/ke.h>

#define KD_BUF_SIZE 512

void KdPrint(const char *fmt, ...)
{
    char buf[KD_BUF_SIZE];
    va_list ap;

    va_start(ap, fmt);
    usize n = KeFormat(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (n >= sizeof buf)
        n = sizeof buf - 1; /* truncated; the NUL slot is not output */
    HalSerialWrite(buf, n);
}
