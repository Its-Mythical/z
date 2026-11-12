/* Priority ready queue: FIFO per priority plus a bitmap of non-empty levels.
 * No hardware or locking here; the scheduler serializes access. Compiled for
 * both the kernel and the host tests. */
#include <zto2/ke.h>

void KeRqInit(KREADY_QUEUE *rq)
{
    for (int p = 0; p < KE_PRIORITY_LEVELS; p++) {
        rq->head[p] = NULL;
        rq->tail[p] = NULL;
    }
    rq->bitmap = 0;
}

void KeRqInsert(KREADY_QUEUE *rq, KTHREAD *t)
{
    u8 p = t->priority;

    t->next = NULL;
    if (rq->tail[p] != NULL)
        rq->tail[p]->next = t;
    else
        rq->head[p] = t;
    rq->tail[p] = t;
    rq->bitmap |= 1u << p;
}

int KeRqHighestPriority(const KREADY_QUEUE *rq)
{
    if (rq->bitmap == 0)
        return -1;
    return 31 - __builtin_clz(rq->bitmap);
}

KTHREAD *KeRqPopHighest(KREADY_QUEUE *rq)
{
    int p = KeRqHighestPriority(rq);
    if (p < 0)
        return NULL;

    KTHREAD *t = rq->head[p];
    rq->head[p] = t->next;
    if (rq->head[p] == NULL) {
        rq->tail[p] = NULL;
        rq->bitmap &= ~(1u << p);
    }
    t->next = NULL;
    return t;
}
