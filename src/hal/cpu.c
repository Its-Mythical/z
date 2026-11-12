#include <zto2/hal.h>
#include <zto2/ke.h>

/* GDT layout is fixed for the M1b syscall/sysret path (STAR needs user data
 * immediately before user code). */
#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UDATA 0x18
#define SEL_UCODE 0x20
#define SEL_TSS   0x28

#define GDT_NULL    0x0000000000000000ull
#define GDT_KCODE64 0x00AF9A000000FFFFull /* P, DPL0, code, L=1, G */
#define GDT_KDATA   0x00CF92000000FFFFull /* P, DPL0, data RW, G, D/B */
#define GDT_UDATA   0x00CFF2000000FFFFull /* P, DPL3, data RW */
#define GDT_UCODE64 0x00AFFA000000FFFFull /* P, DPL3, code, L=1, G */
#define GDT_TSS_ACCESS 0x89 /* P, DPL0, type 9: available 64-bit TSS */

#define GDT_ENTRIES 7 /* null + 4 segments + 16-byte TSS descriptor */

#define IDT_VECTORS         256
#define IDT_INTERRUPT_GATE  0x8E /* P, DPL0, type 0xE: 64-bit interrupt gate */
#define VECTOR_DOUBLE_FAULT 8
#define VECTOR_FIRST_HW     32

#define DF_STACK_SIZE 16384

typedef struct __attribute__((packed)) {
    u32 reserved0;
    u64 rsp[3];
    u64 reserved1;
    u64 ist[7]; /* ist[0] is IST1 */
    u64 reserved2;
    u16 reserved3;
    u16 iomapBase;
} Tss;

_Static_assert(sizeof(Tss) == 104, "64-bit TSS is 104 bytes");

typedef struct __attribute__((packed)) {
    u16 offsetLow;
    u16 selector;
    u8 ist;      /* bits 0-2: IST index, 0 = none */
    u8 typeAttr;
    u16 offsetMid;
    u32 offsetHigh;
    u32 reserved;
} IdtGate;

_Static_assert(sizeof(IdtGate) == 16, "IDT gate is 16 bytes");

typedef struct __attribute__((packed)) {
    u16 limit;
    u64 base;
} DescTablePtr;

extern const u64 HalIsrStubTable[IDT_VECTORS]; /* isr.asm */

static u64 g_Gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static Tss g_Tss __attribute__((aligned(16)));
static IdtGate g_Idt[IDT_VECTORS] __attribute__((aligned(16)));
static u8 g_DoubleFaultStack[DF_STACK_SIZE] __attribute__((aligned(16)));
static HalIsr g_Handlers[IDT_VECTORS];
static bool g_UnhandledLogged[IDT_VECTORS];

static void SetTssDescriptor(u64 *slot, u64 base, u32 limit)
{
    slot[0] = (u64)(limit & 0xFFFF)
            | ((base & 0xFFFFFF) << 16)
            | ((u64)GDT_TSS_ACCESS << 40)
            | ((u64)((limit >> 16) & 0xF) << 48)
            | (((base >> 24) & 0xFF) << 56);
    slot[1] = base >> 32;
}

static void SetIdtGate(u8 vector, u64 handler, u8 ist)
{
    IdtGate *g = &g_Idt[vector];

    g->offsetLow = (u16)(handler & 0xFFFF);
    g->selector = SEL_KCODE;
    g->ist = ist;
    g->typeAttr = IDT_INTERRUPT_GATE;
    g->offsetMid = (u16)((handler >> 16) & 0xFFFF);
    g->offsetHigh = (u32)(handler >> 32);
    g->reserved = 0;
}

void HalInitCpu(void)
{
    g_Gdt[0] = GDT_NULL;
    g_Gdt[SEL_KCODE / 8] = GDT_KCODE64;
    g_Gdt[SEL_KDATA / 8] = GDT_KDATA;
    g_Gdt[SEL_UDATA / 8] = GDT_UDATA;
    g_Gdt[SEL_UCODE / 8] = GDT_UCODE64;

    g_Tss.ist[0] = (u64)(usize)&g_DoubleFaultStack[DF_STACK_SIZE];
    g_Tss.iomapBase = sizeof g_Tss; /* no I/O permission bitmap */
    SetTssDescriptor(&g_Gdt[SEL_TSS / 8], (u64)(usize)&g_Tss, sizeof g_Tss - 1);

    DescTablePtr gdtr = { sizeof g_Gdt - 1, (u64)(usize)g_Gdt };
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw %3, %%ax\n\t"
        "ltr %%ax"
        : /* no outputs */
        : "m"(gdtr), "i"((u64)SEL_KCODE), "i"((u16)SEL_KDATA), "i"((u16)SEL_TSS)
        : "rax", "memory");

    for (unsigned v = 0; v < IDT_VECTORS; v++)
        SetIdtGate((u8)v, HalIsrStubTable[v], v == VECTOR_DOUBLE_FAULT ? 1 : 0);

    DescTablePtr idtr = { sizeof g_Idt - 1, (u64)(usize)g_Idt };
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

bool HalRegisterInterrupt(u8 vector, HalIsr handler)
{
    if (handler == 0 || g_Handlers[vector] != 0)
        return false;
    g_Handlers[vector] = handler;
    return true;
}

void HalTrapDispatch(TrapFrame *tf)
{
    u8 vector = (u8)tf->vector;
    HalIsr handler = g_Handlers[vector];

    if (handler != 0) {
        handler(tf);
        return;
    }
    if (vector < VECTOR_FIRST_HW)
        KeBugCheck(KE_BUGCHECK_UNEXPECTED_KERNEL_MODE_TRAP, tf->vector, tf->error, tf->rip, 0);

    if (vector == HAL_VECTOR_SPURIOUS)
        return; /* spurious: no EOI is owed, and nothing is worth logging */

    /* Log once per vector so a stuck source cannot flood the serial port. */
    if (!g_UnhandledLogged[vector]) {
        g_UnhandledLogged[vector] = true;
        KdPrint("hal: unhandled interrupt vector %u ignored\n", (unsigned)vector);
    }
    HalApicEoi();
}
