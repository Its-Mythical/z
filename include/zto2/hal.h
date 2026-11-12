#ifndef ZTO2_HAL_H
#define ZTO2_HAL_H

#include <stdbool.h>
#include <zto2/types.h>

/* COM1 (0x3F8), 115200 8N1. Safe to call with no UART present. */
void HalSerialInit(void);

/* Writes n bytes; a byte is dropped if the UART does not become ready. */
void HalSerialWrite(const char *s, usize n);

/* Register image pushed by the ISR stubs (src/hal/isr.asm), lowest address
 * first. vector and error are pushed by the stub (error is 0 for vectors the
 * CPU does not give an error code); rip..ss are the CPU's interrupt frame. */
typedef struct TrapFrame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector, error;
    u64 rip, cs, rflags, rsp, ss;
} TrapFrame;

_Static_assert(sizeof(TrapFrame) == 22 * 8, "TrapFrame must match isr.asm");

typedef void (*HalIsr)(TrapFrame *tf);

/* Installs the GDT (0x08 kernel code, 0x10 kernel data, 0x18 user data,
 * 0x20 user code, 0x28 TSS), a TSS whose IST1 is a dedicated 16 KiB stack
 * used by the double-fault gate, and an IDT for vectors 0-255. Interrupts
 * stay disabled (IF is not touched). */
void HalInitCpu(void);

/* Binds handler to vector; returns false if the vector is already taken or
 * handler is NULL. Not locked: call from a single CPU. */
bool HalRegisterInterrupt(u8 vector, HalIsr handler);

/* Common C entry for every vector; called only from isr.asm. Runs the
 * registered handler. An unregistered exception (vector < 32) bugchecks. An
 * unregistered hardware vector other than the spurious vector 0xFF is logged
 * once per vector and EOI'd; the spurious vector is ignored (no EOI). */
void HalTrapDispatch(TrapFrame *tf);

/* Higher-half direct map offset from the bootloader; physical address p is
 * reachable at p + g_HhdmOffset. Set once in kmain before anything else. */
extern u64 g_HhdmOffset;

/* Vectors used by the local APIC. */
#define HAL_VECTOR_TIMER    0x40
#define HAL_VECTOR_SPURIOUS 0xFF

/* Masks and disables both legacy 8259 PICs, then enables the boot CPU's
 * local APIC (xAPIC MMIO via the HHDM) with spurious vector 0xFF. Requires
 * HalInitCpu. Interrupts stay disabled (IF is not touched). */
void HalApicInit(void);

/* Calibrates the APIC timer against PIT channel 2 over 10 ms, then runs it
 * periodically at about hz (1..10000) on vector 0x40. Does not touch IF. If
 * calibration fails, logs and leaves the timer stopped. */
void HalApicTimerStart(u32 hz);

/* Timer interrupts taken so far on the boot CPU. */
u64 HalTickCount(void);

/* Signals end-of-interrupt to the local APIC. No-op before HalApicInit. */
void HalApicEoi(void);

/* Busy-waits ms milliseconds using PIT channel 2 (independent of the APIC
 * timer). Returns false if the PIT did not respond. */
bool HalPitDelayMs(u32 ms);

#endif
