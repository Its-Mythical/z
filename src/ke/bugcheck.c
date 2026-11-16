#include <zto2/ke.h>
#include <zto2/portio.h>

void KeBugCheck(u32 code, u64 p1, u64 p2, u64 p3, u64 p4)
{
    __asm__ volatile("cli");
    KdPrint("*** BUGCHECK 0x%x (0x%llx, 0x%llx, 0x%llx, 0x%llx)\n", code,
            (unsigned long long)p1, (unsigned long long)p2,
            (unsigned long long)p3, (unsigned long long)p4);
#ifdef ZTO2_FAULT_TEST
    /* Fault-test builds only: exit QEMU (isa-debug-exit, status 33) so make
     * test-df does not wait for the runner's timeout. The default build halts. */
    HalOutb(0xF4, 0x10);
#endif
    for (;;)
        __asm__ volatile("cli; hlt");
}
