/* Preemptive priority scheduler for one CPU.
 *
 * Preemption: the tick ISR (HalApic TimerIsr) counts the tick, sends EOI and
 * calls KeOnTimerTick, which queues the reschedule DPC and drains DPCs. The
 * DPC runs on the interrupted thread's stack with interrupts off, burns one
 * tick of quantum and may call KiReschedule, which switches stacks right
 * there inside the ISR. The next thread resumes wherever it was switched out:
 * a thread switched out by the tick unwinds through its own ISR frames and
 * iretq (restoring its own RFLAGS, IF included); one switched out by KeYield
 * returns from KeYield and restores its flags there; a new thread starts in
 * KiThreadStart/KiThreadMain, which executes sti.
 *
 * Locking: g_SchedLock guards the ready queue and is never held across a
 * switch. Single CPU only for now: g_current is global and the switch runs
 * with interrupts off. */
#include <zto2/hal.h>
#include <zto2/ke_kernel.h>

typedef struct {
    KTHREAD t;
    u8 stack[KE_STACK_SIZE] __attribute__((aligned(16)));
} ThreadSlot;

static ThreadSlot g_Pool[KE_MAX_THREADS]; /* in .bss; finished slots are not reclaimed */
static u32 g_PoolUsed;

static KTHREAD g_BootThread;
static KTHREAD *g_Idle;
static KTHREAD *volatile g_Current;
static KREADY_QUEUE g_Ready;
static KSPIN_LOCK g_SchedLock = KE_SPIN_LOCK_INIT;
static volatile bool g_SchedReady;
static volatile bool g_InScheduler; /* per-CPU: set from picking a thread until the next one is running */
static KDPC g_RescheduleDpc;
static volatile u64 g_IdleRuns;

#define BOOT_PRIORITY 4

extern void KiThreadStart(void);

KTHREAD *KeGetCurrentThread(void)
{
    return g_Current;
}

u64 KeIdleRuns(void)
{
    return g_IdleRuns;
}

/* Picks the next thread and switches to it. Interrupts must be off. The
 * current thread goes back on the ready queue unless it has finished, so a
 * finished thread is never re-queued. The idle thread is always either
 * current or queued, so the pop cannot come back empty. */
static void KiReschedule(void)
{
    u64 f = KeAcquireSpinLock(&g_SchedLock);
    KTHREAD *prev = g_Current;

    g_InScheduler = true;
    if (prev->state == KT_RUNNING) {
        prev->state = KT_READY;
        KeRqInsert(&g_Ready, prev);
    }
    KTHREAD *next = KeRqPopHighest(&g_Ready);
    if (next == NULL)
        KeBugCheck(KE_BUGCHECK_KMODE_EXCEPTION_NOT_HANDLED, 0xDEAD0001, 0, 0, 0);
    next->state = KT_RUNNING;
    next->quantum_left = KE_QUANTUM_TICKS; /* fresh quantum every time a thread is picked */
    g_Current = next;
    KeReleaseSpinLock(&g_SchedLock, f); /* flags restored are the caller's (IF=0) */

    if (next != prev)
        KiSwitchContext(prev, next);
    g_InScheduler = false; /* now running as whichever thread was switched in */
}

/* Reschedule DPC: one tick of the current quantum has elapsed. Switches when a
 * higher priority thread is ready, or the quantum is used up and an equal or
 * higher one is waiting (round robin). Otherwise the current thread continues. */
static void RescheduleDpc(void *ctx)
{
    (void)ctx;
    KTHREAD *cur = g_Current;
    bool expired = cur->quantum_left <= 1;

    if (!expired)
        cur->quantum_left--;

    u64 f = KeAcquireSpinLock(&g_SchedLock);
    int top = KeRqHighestPriority(&g_Ready);
    KeReleaseSpinLock(&g_SchedLock, f);

    if (top > (int)cur->priority || (expired && top >= (int)cur->priority))
        KiReschedule();
    else if (expired)
        cur->quantum_left = KE_QUANTUM_TICKS;
}

void KeOnTimerTick(void)
{
    if (!g_SchedReady || g_InScheduler)
        return; /* the tick is already counted; never recurse into a switch */
    KeInsertQueueDpc(&g_RescheduleDpc);
    KeDrainDpcs();
}

void KeYield(void)
{
    u64 f = KiSaveFlagsAndCli();
    KiReschedule();
    KiRestoreFlags(f);
}

/* Runs on a new thread's own stack, entered from KiThreadStart. The thread was
 * switched to from an ISR (IF=0) or KeYield, so interrupts are enabled here. */
__attribute__((noreturn, used)) void KiThreadMain(void (*entry)(void *), void *arg)
{
    g_InScheduler = false;
    __asm__ volatile("sti" : : : "memory");
    entry(arg);

    __asm__ volatile("cli" : : : "memory");
    g_Current->state = KT_FINISHED;
    KeYield(); /* picks someone else, never re-queues us, never returns */
    KeBugCheck(KE_BUGCHECK_KMODE_EXCEPTION_NOT_HANDLED, 0xDEAD0002, 0, 0, 0);
}

static void IdleThread(void *arg)
{
    (void)arg;
    for (;;) {
        g_IdleRuns++;
        __asm__ volatile("sti; hlt" : : : "memory");
    }
}

static KTHREAD *NewThread(void (*entry)(void *), void *arg, u8 priority)
{
    u64 f = KeAcquireSpinLock(&g_SchedLock);
    ThreadSlot *slot = g_PoolUsed < KE_MAX_THREADS ? &g_Pool[g_PoolUsed++] : NULL;
    KeReleaseSpinLock(&g_SchedLock, f);
    if (slot == NULL)
        return NULL;

    KTHREAD *t = &slot->t;
    t->priority = priority;
    t->state = KT_READY;
    t->quantum_left = KE_QUANTUM_TICKS;
    t->stack = slot->stack;

    /* Build the frame KiSwitchContext expects (see switch.asm). The return
     * address slot sits at top-24, which is 8 mod 16, so after the ret pops it
     * rsp is top-16: 16-aligned, ready for KiThreadStart's call. */
    u64 *top = (u64 *)(slot->stack + KE_STACK_SIZE);
    u64 *sp = top - 3;
    *sp = (u64)(usize)KiThreadStart;
    *--sp = 0;                   /* rbp */
    *--sp = 0;                   /* rbx */
    *--sp = (u64)(usize)entry;   /* r12 */
    *--sp = (u64)(usize)arg;     /* r13 */
    *--sp = 0;                   /* r14 */
    *--sp = 0;                   /* r15 */
    t->rsp = (u64)(usize)sp;
    return t;
}

KTHREAD *KeCreateThread(void (*entry)(void *), void *arg, u8 priority)
{
    if (priority >= KE_PRIORITY_LEVELS || entry == NULL)
        return NULL;
    KTHREAD *t = NewThread(entry, arg, priority);
    if (t == NULL)
        return NULL;

    u64 f = KeAcquireSpinLock(&g_SchedLock);
    KeRqInsert(&g_Ready, t);
    bool preempt = g_SchedReady && priority > g_Current->priority;
    KeReleaseSpinLock(&g_SchedLock, f);
    if (preempt)
        KeYield(); /* the new thread outranks us: it runs now */
    return t;
}

void KeSchedulerInit(void)
{
    KeRqInit(&g_Ready);
    g_RescheduleDpc.fn = RescheduleDpc;

    /* Adopt the boot context. Its stack is the bootloader's, so no stack field. */
    g_BootThread.priority = BOOT_PRIORITY;
    g_BootThread.state = KT_RUNNING;
    g_BootThread.quantum_left = KE_QUANTUM_TICKS;
    g_Current = &g_BootThread;

    g_Idle = KeCreateThread(IdleThread, 0, 0);
    if (g_Idle == NULL)
        KeBugCheck(KE_BUGCHECK_KMODE_EXCEPTION_NOT_HANDLED, 0xDEAD0003, 0, 0, 0);
    g_SchedReady = true; /* enables KeOnTimerTick, and preemption in KeCreateThread */
}
