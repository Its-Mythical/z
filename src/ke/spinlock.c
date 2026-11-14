#include <zto2/ke_kernel.h>

u64 KeAcquireSpinLock(KSPIN_LOCK *lock)
{
    /* Interrupts off first: an ISR on this CPU must never spin on a lock its
     * own interrupted code holds. */
    u64 saved = KiSaveFlagsAndCli();

    while (__atomic_exchange_n(&lock->v, 1, __ATOMIC_ACQUIRE) != 0) {
        while (__atomic_load_n(&lock->v, __ATOMIC_RELAXED) != 0)
            __asm__ volatile("pause");
    }
    return saved;
}

void KeReleaseSpinLock(KSPIN_LOCK *lock, u64 saved)
{
    __atomic_store_n(&lock->v, 0, __ATOMIC_RELEASE);
    KiRestoreFlags(saved);
}
