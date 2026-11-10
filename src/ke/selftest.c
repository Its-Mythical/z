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
