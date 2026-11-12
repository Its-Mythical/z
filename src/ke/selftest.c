#include <zto2/hal.h>
#include <zto2/ke.h>

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
