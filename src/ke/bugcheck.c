#include <zto2/ke.h>

void KeBugCheck(u32 code, u64 p1, u64 p2, u64 p3, u64 p4)
{
    __asm__ volatile("cli");
    KdPrint("*** BUGCHECK 0x%x (0x%llx, 0x%llx, 0x%llx, 0x%llx)\n", code,
            (unsigned long long)p1, (unsigned long long)p2,
            (unsigned long long)p3, (unsigned long long)p4);
    for (;;)
        __asm__ volatile("cli; hlt");
}
