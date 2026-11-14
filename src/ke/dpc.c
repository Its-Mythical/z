#include <zto2/ke_kernel.h>

static KSPIN_LOCK g_DpcLock = KE_SPIN_LOCK_INIT;
static KDPC *g_DpcHead;
static KDPC *g_DpcTail;

bool KeInsertQueueDpc(KDPC *dpc)
{
    u64 f = KeAcquireSpinLock(&g_DpcLock);
    bool queued = !dpc->queued;

    if (queued) {
        dpc->queued = true;
        dpc->next = NULL;
        if (g_DpcTail != NULL)
            g_DpcTail->next = dpc;
        else
            g_DpcHead = dpc;
        g_DpcTail = dpc;
    }
    KeReleaseSpinLock(&g_DpcLock, f);
    return queued;
}

void KeDrainDpcs(void)
{
    for (;;) {
        u64 f = KeAcquireSpinLock(&g_DpcLock);
        KDPC *dpc = g_DpcHead;

        if (dpc != NULL) {
            g_DpcHead = dpc->next;
            if (g_DpcHead == NULL)
                g_DpcTail = NULL;
            dpc->next = NULL;
            dpc->queued = false; /* dequeued before it runs, so it may requeue */
        }
        KeReleaseSpinLock(&g_DpcLock, f);
        if (dpc == NULL)
            return;
        /* Lock dropped: the DPC may switch threads, and another thread's tick
         * may drain the rest of the queue while this one is switched out. */
        dpc->fn(dpc->ctx);
    }
}
