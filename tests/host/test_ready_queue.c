#include <stdio.h>
#include <zto2/ke.h>

static int g_Failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_Failures++; \
        } \
    } while (0)

static void test_empty(void)
{
    KREADY_QUEUE rq;
    KeRqInit(&rq);
    CHECK(KeRqHighestPriority(&rq) == -1);
    CHECK(KeRqPopHighest(&rq) == NULL);
}

static void test_fifo_same_priority(void)
{
    KREADY_QUEUE rq;
    KTHREAD a = {.priority = 5}, b = {.priority = 5};
    KeRqInit(&rq);
    KeRqInsert(&rq, &a);
    KeRqInsert(&rq, &b);
    CHECK(KeRqPopHighest(&rq) == &a);
    CHECK(KeRqPopHighest(&rq) == &b);
    CHECK(KeRqPopHighest(&rq) == NULL);
}

static void test_highest_first(void)
{
    KREADY_QUEUE rq;
    KTHREAD lo = {.priority = 3}, hi = {.priority = 9};
    KeRqInit(&rq);
    KeRqInsert(&rq, &lo);
    KeRqInsert(&rq, &hi);
    CHECK(KeRqHighestPriority(&rq) == 9);
    CHECK(KeRqPopHighest(&rq) == &hi);
    CHECK(KeRqPopHighest(&rq) == &lo);
}

static void test_bitmap_clears(void)
{
    KREADY_QUEUE rq;
    KTHREAD lo = {.priority = 3}, hi = {.priority = 9};
    KeRqInit(&rq);
    KeRqInsert(&rq, &lo);
    KeRqInsert(&rq, &hi);
    CHECK(KeRqPopHighest(&rq) == &hi);
    CHECK(KeRqHighestPriority(&rq) == 3);
}

static void test_reinsert_after_drain(void)
{
    KREADY_QUEUE rq;
    KTHREAD a = {.priority = 0}, b = {.priority = 31};
    KeRqInit(&rq);
    KeRqInsert(&rq, &a);
    KeRqInsert(&rq, &b);
    CHECK(KeRqHighestPriority(&rq) == 31);
    CHECK(KeRqPopHighest(&rq) == &b);
    CHECK(KeRqPopHighest(&rq) == &a);
    KeRqInsert(&rq, &a); /* queue reused after emptying: tail must have been reset */
    CHECK(KeRqPopHighest(&rq) == &a);
    CHECK(KeRqPopHighest(&rq) == NULL);
}

int main(void)
{
    test_empty();
    test_fifo_same_priority();
    test_highest_first();
    test_bitmap_clears();
    test_reinsert_after_drain();
    if (g_Failures != 0) {
        printf("%d failure(s)\n", g_Failures);
        return 1;
    }
    printf("test_ready_queue: ok\n");
    return 0;
}
