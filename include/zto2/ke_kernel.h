#ifndef ZTO2_KE_KERNEL_H
#define ZTO2_KE_KERNEL_H

/* Kernel-only executive pieces: spinlocks, DPCs and the scheduler. Needs inline
 * asm and the hardware, so unlike ke.h it is not host-compilable. */

#include <zto2/ke.h>

/* --- Spinlocks -------------------------------------------------------------- */

typedef struct KSPIN_LOCK {
    volatile u32 v;
} KSPIN_LOCK;

#define KE_SPIN_LOCK_INIT {0}

/* Saves RFLAGS, disables interrupts, then takes the lock. Returns the saved
 * flags, to be passed to KeReleaseSpinLock. Interrupts stay off while held, so
 * a tick can never preempt a lock holder on this CPU. Not recursive. */
u64 KeAcquireSpinLock(KSPIN_LOCK *lock);
void KeReleaseSpinLock(KSPIN_LOCK *lock, u64 saved);

/* Saves RFLAGS and clears IF / restores a value from KiSaveFlagsAndCli. */
static inline u64 KiSaveFlagsAndCli(void)
{
    u64 f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) : : "memory");
    return f;
}

static inline void KiRestoreFlags(u64 f)
{
    __asm__ volatile("pushq %0; popfq" : : "r"(f) : "memory", "cc");
}

/* --- DPCs -------------------------------------------------------------------- */

typedef struct KDPC {
    void (*fn)(void *);
    void *ctx;
    struct KDPC *next;
    bool queued;
} KDPC;

/* Queues dpc for KeDrainDpcs; returns false (and does nothing) if it is
 * already queued. Safe from interrupt context. */
bool KeInsertQueueDpc(KDPC *dpc);

/* Runs every queued DPC in FIFO order, with interrupts as the caller had them
 * (off in the tick ISR). A DPC is dequeued before it runs, so it may requeue
 * itself, and may switch threads. */
void KeDrainDpcs(void);

/* --- Threads and the scheduler ------------------------------------------------ */

#define KE_QUANTUM_TICKS 3 /* ticks per quantum at 100 Hz (30 ms) */
#define KE_MAX_THREADS   16 /* static pool; slots of finished threads are not reused */
#define KE_STACK_SIZE    16384

/* Adopts the running boot context as a priority 4 thread and creates the idle
 * thread (priority 0). Call once, before interrupts can preempt. */
void KeSchedulerInit(void);

/* Creates a ready thread running entry(arg); returns NULL if priority > 31 or
 * the thread pool is exhausted. If the new thread outranks the caller it runs
 * immediately. When entry returns the thread finishes and never runs again. */
KTHREAD *KeCreateThread(void (*entry)(void *), void *arg, u8 priority);

KTHREAD *KeGetCurrentThread(void);

/* Gives up the processor to the next ready thread of equal or higher priority.
 * Thread context only (not from an ISR or DPC). Interrupt state is preserved. */
void KeYield(void);

/* Called by the timer ISR after the tick is counted and EOI'd. Queues and runs
 * the reschedule DPC. No-op until KeSchedulerInit has run. */
void KeOnTimerTick(void);

/* Times the idle thread has entered its sti; hlt loop (observability for self-tests). */
u64 KeIdleRuns(void);

/* switch.asm: saves rbx rbp r12-r15 and RSP into from->rsp, loads to->rsp and
 * the same registers from it. Interrupts must be off. */
void KiSwitchContext(KTHREAD *from, KTHREAD *to);

#endif
