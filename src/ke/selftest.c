#include <zto2/hal.h>
#include <zto2/ke.h>
#include <zto2/ke_kernel.h>

static volatile bool g_BreakpointRan;

static void BreakpointHandler(TrapFrame *tf)
{
    (void)tf;
    g_BreakpointRan = true;
}

static void SelfTestBreakpoint(void)
{
    if (!HalRegisterInterrupt(3, BreakpointHandler))
        KeSelfTestFail("breakpoint");
    __asm__ volatile("int3");
    if (!g_BreakpointRan)
        KeSelfTestFail("breakpoint");
    KdPrint("ZTO2-SELFTEST: ok breakpoint\n");
}

#define UNHANDLED_VECTOR    0x41
#define UNHANDLED_WAIT_SPIN 20000000ull

static void WaitUnhandled(bool (*busy)(u8))
{
    for (u64 spin = 0; busy(UNHANDLED_VECTOR); spin++) {
        if (spin >= UNHANDLED_WAIT_SPIN)
            KeSelfTestFail("unhandled-vector");
        __asm__ volatile("pause");
    }
}

/* Unregistered hardware vectors must be EOI'd, and the spurious vector must
 * not hang or bugcheck. A software int does not set the in-service bit, so
 * the EOI is checked with a real (self-IPI) interrupt: while interrupts are
 * masked the vector latches in the APIC's IRR; after sti it is taken, and
 * without the dispatcher's EOI its ISR bit would stay set. The dispatcher
 * logs vector 0x41 only once; 0xFF is silent and gets no EOI. */
static void SelfTestUnhandledVector(void)
{
    __asm__ volatile("int $0x41");
    __asm__ volatile("int $0x41");
    __asm__ volatile("int $0xFF");

    __asm__ volatile("cli");
    HalApicSelfIpi(UNHANDLED_VECTOR);
    u64 spin = 0;
    while (!HalApicVectorRequested(UNHANDLED_VECTOR)) {
        if (++spin >= UNHANDLED_WAIT_SPIN)
            KeSelfTestFail("unhandled-vector");
        __asm__ volatile("pause");
    }
    __asm__ volatile("sti");
    WaitUnhandled(HalApicVectorRequested); /* taken */
    WaitUnhandled(HalApicVectorInService); /* EOI'd */
    __asm__ volatile("cli");
    KdPrint("ZTO2-SELFTEST: ok unhandled-vector\n");
}

#define TIMER_HZ        100
#define TIMER_WAIT_SPIN 500000000ull

/* Leaves interrupts enabled and the timer running at TIMER_HZ for later tests. */
static void SelfTestTimerTicks(void)
{
    HalApicTimerStart(TIMER_HZ);
    __asm__ volatile("sti");

    u64 spin = 0;
    while (HalTickCount() < 10) {
        if (++spin >= TIMER_WAIT_SPIN)
            KeSelfTestFail("timer");
        __asm__ volatile("pause");
    }

    /* Rate check against an independent clock: ticks taken in 1 s of PIT time. */
    u64 before = HalTickCount();
    if (!HalPitDelayMs(1000))
        KeSelfTestFail("timer");
    u64 ticks = HalTickCount() - before;
    KdPrint("timer: %llu ticks in 1 s at %u Hz\n", ticks, (unsigned)TIMER_HZ);
    if (ticks < TIMER_HZ * 80 / 100 || ticks > TIMER_HZ * 120 / 100)
        KeSelfTestFail("timer");
    KdPrint("ZTO2-SELFTEST: ok timer\n");
}

/* --- Scheduler self-tests ----------------------------------------------------- */

#define BOOT_PRIORITY    4 /* the adopted boot context's priority */
#define SCHED_WAIT_SPIN  500000000ull

static u8 ThreadState(KTHREAD *t)
{
    return __atomic_load_n(&t->state, __ATOMIC_ACQUIRE);
}

static void NopThread(void *arg)
{
    (void)arg;
}

static void SelfTestCreateBadPriority(void)
{
    if (KeCreateThread(NopThread, 0, 32) != NULL ||
        KeCreateThread(NopThread, 0, 255) != NULL)
        KeSelfTestFail("create-bad-priority");
    KdPrint("ZTO2-SELFTEST: ok create-bad-priority\n");
}

static volatile bool g_PreemptStop;
static volatile u64 g_PreemptCount[2];

/* Spins without ever yielding; only the timer can take the CPU away. */
static void PreemptWorker(void *arg)
{
    u64 i = (u64)(usize)arg;
    while (!g_PreemptStop)
        g_PreemptCount[i]++;
}

#define PREEMPT_TICKS 50

static void WaitFinished(KTHREAD *t, const char *name)
{
    for (u64 spin = 0; ThreadState(t) != KT_FINISHED; spin++) {
        if (spin >= SCHED_WAIT_SPIN)
            KeSelfTestFail(name);
        __asm__ volatile("pause");
    }
}

/* Two equal-priority threads that never yield must both make progress. The
 * main thread shares their priority, so it is preempted too. */
static void SelfTestPreempt(void)
{
    KTHREAD *a = KeCreateThread(PreemptWorker, (void *)0, BOOT_PRIORITY);
    KTHREAD *b = KeCreateThread(PreemptWorker, (void *)1, BOOT_PRIORITY);
    if (a == NULL || b == NULL)
        KeSelfTestFail("preempt");

    u64 start = HalTickCount();
    for (u64 spin = 0; HalTickCount() < start + PREEMPT_TICKS; spin++) {
        if (spin >= SCHED_WAIT_SPIN)
            KeSelfTestFail("preempt");
        __asm__ volatile("pause");
    }
    u64 c0 = g_PreemptCount[0], c1 = g_PreemptCount[1];
    g_PreemptStop = true;
    KdPrint("preempt: counters %llu %llu\n", c0, c1);
    if (c0 == 0 || c1 == 0)
        KeSelfTestFail("preempt");
    WaitFinished(a, "preempt");
    WaitFinished(b, "preempt");
    KdPrint("ZTO2-SELFTEST: ok preempt\n");
}

static volatile bool g_PriorityRan;

static void PriorityThread(void *arg)
{
    (void)arg;
    g_PriorityRan = true;
}

/* A higher-priority thread must run before KeCreateThread returns to us. */
static void SelfTestPriority(void)
{
    KTHREAD *t = KeCreateThread(PriorityThread, 0, 10);
    if (t == NULL || !g_PriorityRan || ThreadState(t) != KT_FINISHED)
        KeSelfTestFail("priority");
    KdPrint("ZTO2-SELFTEST: ok priority\n");
}

static volatile bool g_IdleFlag;

/* Burns IDLE_HELPER_TICKS, sets the flag and returns (finishes). */
#define IDLE_HELPER_TICKS 10
static void IdleHelper(void *arg)
{
    (void)arg;
    u64 start = HalTickCount();
    for (u64 spin = 0; HalTickCount() < start + IDLE_HELPER_TICKS && spin < SCHED_WAIT_SPIN; spin++)
        __asm__ volatile("pause");
    g_IdleFlag = true;
}

/* The main thread drops to priority 0 (the idle thread's) and halts waiting
 * for a flag set by a thread that then finishes. While the helper runs, main
 * waits behind the idle thread in the priority 0 queue, so once the finished
 * helper is gone the scheduler must pick the idle thread, then hand back. */
static void SelfTestIdle(void)
{
    KTHREAD *me = KeGetCurrentThread();
    u64 idleBefore = KeIdleRuns();

    me->priority = 0;
    KTHREAD *t = KeCreateThread(IdleHelper, 0, BOOT_PRIORITY);
    if (t == NULL)
        KeSelfTestFail("idle");

    u64 start = HalTickCount();
    while (!g_IdleFlag) {
        if (HalTickCount() > start + 300)
            KeSelfTestFail("idle");
        __asm__ volatile("sti; hlt");
    }
    me->priority = BOOT_PRIORITY;
    WaitFinished(t, "idle");
    if (KeIdleRuns() == idleBefore)
        KeSelfTestFail("idle");
    KdPrint("ZTO2-SELFTEST: ok idle\n");
}

#ifdef ZTO2_FAULT_TEST_df
/* Not part of the default build. Points rsp at unmapped memory and takes a
 * breakpoint: delivering #BP faults (#PF), delivering that #PF faults again,
 * which escalates to #DF. Only the IST1 stack lets #DF reach KeBugCheck;
 * expect "*** BUGCHECK 0x7f (0x8, ..." on serial. */
static void SelfTestDoubleFault(void)
{
    __asm__ volatile("movq %0, %%rsp\n\tint3" : : "r"(0xDEAD0000ull));
    __builtin_unreachable();
}
#endif

void KeSelfTestRun(void)
{
    SelfTestBreakpoint();
    SelfTestUnhandledVector();
    SelfTestTimerTicks();
    SelfTestCreateBadPriority();
    SelfTestPreempt();
    SelfTestPriority();
    SelfTestIdle();
#ifdef ZTO2_FAULT_TEST_df
    SelfTestDoubleFault();
#endif
}

void KeSelfTestFail(const char *name)
{
    KdPrint("ZTO2-SELFTEST: FAIL %s\n", name);
    for (;;)
        __asm__ volatile("cli; hlt");
}
