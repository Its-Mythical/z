#include <zto2/ke.h>

typedef struct {
    char  *buf;
    size_t cap;
    size_t len; /* length that would have been written */
} Out;

static void Put(Out *o, char c)
{
    /* Keep one slot for the terminating NUL. */
    if (o->cap != 0 && o->len < o->cap - 1)
        o->buf[o->len] = c;
    o->len++;
}

static void PutStr(Out *o, const char *s)
{
    while (*s)
        Put(o, *s++);
}

static void PutUnsigned(Out *o, u64 v, unsigned base, bool negative)
{
    char tmp[24]; /* 20 decimal digits max for u64, 16 for hex */
    unsigned n = 0;

    do {
        tmp[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v != 0);

    if (negative)
        Put(o, '-');
    while (n != 0)
        Put(o, tmp[--n]);
}

size_t KeFormat(char *buf, size_t cap, const char *fmt, va_list ap)
{
    Out o = { buf, cap, 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            Put(&o, *fmt);
            continue;
        }

        const char *spec = fmt; /* points at '%' for literal fallback */
        unsigned longs = 0;

        fmt++;
        while (*fmt == 'l' && longs < 2) {
            longs++;
            fmt++;
        }

        switch (*fmt) {
        case 'c':
            if (longs == 0) {
                Put(&o, (char)va_arg(ap, int));
                continue;
            }
            break;
        case 's':
            if (longs == 0) {
                const char *s = va_arg(ap, const char *);
                PutStr(&o, s ? s : "(null)");
                continue;
            }
            break;
        case 'd':
            if (longs == 0) {
                i32 v = va_arg(ap, int);
                /* Negate via unsigned arithmetic so INT_MIN is well-defined. */
                PutUnsigned(&o, v < 0 ? 0 - (u64)(i64)v : (u64)v, 10, v < 0);
                continue;
            }
            if (longs == 2) {
                i64 v = va_arg(ap, long long);
                PutUnsigned(&o, v < 0 ? 0 - (u64)v : (u64)v, 10, v < 0);
                continue;
            }
            break;
        case 'u':
            if (longs == 0) {
                PutUnsigned(&o, va_arg(ap, unsigned), 10, false);
                continue;
            }
            if (longs == 2) {
                PutUnsigned(&o, va_arg(ap, unsigned long long), 10, false);
                continue;
            }
            break;
        case 'x':
            if (longs == 0) {
                PutUnsigned(&o, va_arg(ap, unsigned), 16, false);
                continue;
            }
            if (longs == 2) {
                PutUnsigned(&o, va_arg(ap, unsigned long long), 16, false);
                continue;
            }
            break;
        case 'p':
            if (longs == 0) {
                PutStr(&o, "0x");
                PutUnsigned(&o, (u64)(usize)va_arg(ap, void *), 16, false);
                continue;
            }
            break;
        case '%':
            if (longs == 0) {
                Put(&o, '%');
                continue;
            }
            break;
        default:
            break;
        }

        /* Unknown specifier: copy what we consumed literally. */
        for (; spec < fmt; spec++)
            Put(&o, *spec);
        if (*fmt)
            Put(&o, *fmt);
        else
            break; /* format ended inside a specifier */
    }

    if (o.cap != 0)
        o.buf[o.len < o.cap ? o.len : o.cap - 1] = '\0';
    return o.len;
}
