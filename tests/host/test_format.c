#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zto2/ke.h>

static int g_Failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_Failures++; \
        } \
    } while (0)

static size_t Fmt(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t n = KeFormat(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

static void test_basic(void)
{
    char b[64];
    size_t n = Fmt(b, sizeof b, "%d %u %x", -5, 7u, 255u);
    CHECK(strcmp(b, "-5 7 ff") == 0);
    CHECK(n == 7);
}

static void test_llx(void)
{
    char b[64];
    Fmt(b, sizeof b, "%llx", 0xDEADBEEFCAFEull);
    CHECK(strcmp(b, "deadbeefcafe") == 0);
}

static void test_int64_min(void)
{
    char b[64];
    Fmt(b, sizeof b, "%lld", (long long)INT64_MIN);
    CHECK(strcmp(b, "-9223372036854775808") == 0);
}

static void test_null_string(void)
{
    char b[64];
    Fmt(b, sizeof b, "%s", (const char *)NULL);
    CHECK(strcmp(b, "(null)") == 0);
}

static void test_truncation(void)
{
    char b[16];
    memset(b, 'X', sizeof b);
    size_t n = Fmt(b, 4, "%s", "abcdefgh");
    CHECK(n == 8);
    CHECK(strcmp(b, "abc") == 0);
    CHECK(b[4] == 'X');
}

static void test_cap_zero(void)
{
    char b[4] = { 'X', 'X', 'X', 'X' };
    size_t n = Fmt(b, 0, "%s", "abcdefgh");
    CHECK(n == 8);
    CHECK(b[0] == 'X');
}

static void test_unknown_spec(void)
{
    char b[64];
    Fmt(b, sizeof b, "%q");
    CHECK(strcmp(b, "%q") == 0);
}

int main(void)
{
    test_basic();
    test_llx();
    test_int64_min();
    test_null_string();
    test_truncation();
    test_cap_zero();
    test_unknown_spec();
    if (g_Failures) {
        printf("test_format: %d failure(s)\n", g_Failures);
        return 1;
    }
    printf("test_format: all passed\n");
    return 0;
}
