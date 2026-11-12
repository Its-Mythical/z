#include <zto2/hal.h>
#include <zto2/ke.h>
#include <zto2/portio.h>

/* --- Local APIC (xAPIC, MMIO) --------------------------------------------- */
#define MSR_APIC_BASE        0x1B
#define APIC_BASE_EXTD       (1ull << 10) /* x2APIC mode */
#define APIC_BASE_ENABLE     (1ull << 11) /* APIC global enable */
#define APIC_BASE_ADDR_MASK  0x000FFFFFFFFFF000ull

#define LAPIC_REG_EOI        0x0B0
#define LAPIC_REG_ISR        0x100 /* 8 x 32-bit, 16-byte stride */
#define LAPIC_REG_IRR        0x200 /* 8 x 32-bit, 16-byte stride */
#define LAPIC_REG_ICR_LOW    0x300
#define LAPIC_REG_SVR        0x0F0 /* spurious-interrupt vector register */
#define LAPIC_REG_LVT_TIMER  0x320
#define LAPIC_REG_TIMER_INIT 0x380 /* initial count */
#define LAPIC_REG_TIMER_CUR  0x390 /* current count */
#define LAPIC_REG_TIMER_DIV  0x3E0 /* divide configuration */

#define LAPIC_ICR_ASSERT     (1u << 14) /* level = assert */
#define LAPIC_ICR_SELF       (1u << 18) /* destination shorthand 01 = self */
#define LAPIC_SVR_ENABLE     (1u << 8) /* APIC software enable */
#define LAPIC_LVT_MASKED     (1u << 16)
#define LAPIC_LVT_PERIODIC   (1u << 17) /* bits 18:17 = 01 periodic; 00 one-shot */
#define LAPIC_DIV_BY_16      0x3 /* divide config encoding for /16 */

/* --- Legacy 8259 PICs ----------------------------------------------------- */
#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1
#define PIC_ICW1   0x11 /* init, ICW4 needed, cascade */
#define PIC_ICW4   0x01 /* 8086 mode */
#define PIC1_VEC   0x20 /* remap off the CPU exception vectors, then mask all */
#define PIC2_VEC   0x28

/* --- PIT channel 2 -------------------------------------------------------- */
#define PIT_CH2_DATA 0x42
#define PIT_CMD      0x43
#define PIT_CMD_CH2_LOHI_MODE0 0xB0 /* channel 2, lobyte/hibyte, mode 0, binary */
#define PORT_SYSCTL  0x61
#define SYSCTL_GATE2 0x01 /* bit 0: channel 2 gate */
#define SYSCTL_SPKR  0x02 /* bit 1: speaker data enable (kept off) */
#define SYSCTL_OUT2  0x20 /* bit 5 (read): channel 2 OUT */
#define PIT_HZ       1193182u
#define PIT_CHUNK_MS 50u /* 50 ms * 1193182 Hz = 59659 ticks, fits the 16-bit count */
#define PIT_MAX_POLLS 50000000u

#define CALIBRATE_MS 10u

/* --- Page tables ---------------------------------------------------------- */
#define PTE_P        (1ull << 0)
#define PTE_RW       (1ull << 1)
#define PTE_PWT      (1ull << 3)
#define PTE_PCD      (1ull << 4)
#define PTE_PS       (1ull << 7)
#define PTE_NX       (1ull << 63)
#define PTE_ADDR     0x000FFFFFFFFFF000ull
#define PT_ENTRIES   512
#define PAGE_SIZE    4096ull

/* Tables needed to reach one 4 KiB leaf: PDPT, PD, PT (the PML4 exists). */
#define MMIO_TABLES  3

static volatile u32 *g_Lapic; /* HHDM mapping of the LAPIC MMIO page */
static u64 g_Ticks;

static inline u64 ReadMsr(u32 msr)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void WriteMsr(u32 msr, u64 v)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((u32)v), "d"((u32)(v >> 32)) : "memory");
}

static inline u64 ReadCr3(void)
{
    u64 v;
    __asm__ volatile("movq %%cr3, %0" : "=r"(v));
    return v;
}

/* Page-table memory is reachable through the HHDM. */
static u64 *TableAt(u64 phys)
{
    return (u64 *)(usize)(phys + g_HhdmOffset);
}

/* Software walk of the live tables for a kernel virtual address. */
static bool VirtToPhys(u64 virt, u64 *phys)
{
    u64 *tbl = TableAt(ReadCr3() & PTE_ADDR);

    for (int shift = 39; shift >= 12; shift -= 9) {
        u64 e = tbl[(virt >> shift) & (PT_ENTRIES - 1)];
        if (!(e & PTE_P))
            return false;
        if (shift == 12 || ((e & PTE_PS) && shift != 39)) {
            u64 span = 1ull << shift;
            *phys = (e & PTE_ADDR & ~(span - 1)) | (virt & (span - 1));
            return true;
        }
        tbl = TableAt(e & PTE_ADDR);
    }
    return false;
}

/* The bootloader's HHDM covers RAM only, not device memory, so the LAPIC page
 * is mapped here, uncached, at its HHDM address. Stopgap until the VMM owns
 * the page tables. Returns false if no table memory is left or on failure. */
static u64 g_MmioTables[MMIO_TABLES][PT_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static unsigned g_MmioTablesUsed;

static bool MapMmioPage(u64 phys)
{
    u64 virt = phys + g_HhdmOffset;
    u64 *tbl = TableAt(ReadCr3() & PTE_ADDR);

    for (int shift = 39; shift > 12; shift -= 9) {
        u64 *e = &tbl[(virt >> shift) & (PT_ENTRIES - 1)];
        if (!(*e & PTE_P)) {
            u64 tphys;
            if (g_MmioTablesUsed == MMIO_TABLES ||
                !VirtToPhys((u64)(usize)g_MmioTables[g_MmioTablesUsed], &tphys))
                return false;
            g_MmioTablesUsed++;
            *e = tphys | PTE_P | PTE_RW;
        } else if ((*e & PTE_PS) && shift != 39) {
            return false; /* a large page already maps it; cacheability unknown */
        }
        tbl = TableAt(*e & PTE_ADDR);
    }
    tbl[(virt >> 12) & (PT_ENTRIES - 1)] = (phys & PTE_ADDR) | PTE_P | PTE_RW | PTE_PWT | PTE_PCD | PTE_NX;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return true;
}

static u32 LapicRead(u32 reg)
{
    return g_Lapic[reg / 4];
}

static void LapicWrite(u32 reg, u32 v)
{
    g_Lapic[reg / 4] = v;
}

static void PicDisable(void)
{
    HalOutb(PIC1_CMD, PIC_ICW1);
    HalOutb(PIC2_CMD, PIC_ICW1);
    HalOutb(PIC1_DATA, PIC1_VEC);
    HalOutb(PIC2_DATA, PIC2_VEC);
    HalOutb(PIC1_DATA, 4); /* slave on IRQ2 */
    HalOutb(PIC2_DATA, 2); /* cascade identity */
    HalOutb(PIC1_DATA, PIC_ICW4);
    HalOutb(PIC2_DATA, PIC_ICW4);
    HalOutb(PIC1_DATA, 0xFF); /* mask every line */
    HalOutb(PIC2_DATA, 0xFF);
}

/* Timer interrupt: counts the tick and acknowledges it. The scheduler hooks in here. */
static void TimerIsr(TrapFrame *tf)
{
    (void)tf;
    __atomic_fetch_add(&g_Ticks, 1, __ATOMIC_RELAXED);
    HalApicEoi();
}

void HalApicInit(void)
{
    PicDisable();

    u64 base = ReadMsr(MSR_APIC_BASE);
    if (base & APIC_BASE_EXTD) /* leave x2APIC: disable, then re-enable as xAPIC */
        WriteMsr(MSR_APIC_BASE, base & ~(APIC_BASE_EXTD | APIC_BASE_ENABLE));
    WriteMsr(MSR_APIC_BASE, (base & ~APIC_BASE_EXTD) | APIC_BASE_ENABLE);

    u64 lapicPhys = base & APIC_BASE_ADDR_MASK;
    if (!MapMmioPage(lapicPhys)) {
        KdPrint("apic: cannot map local APIC at %llx\n", lapicPhys);
        return;
    }
    g_Lapic = (volatile u32 *)(usize)(lapicPhys + g_HhdmOffset);
    LapicWrite(LAPIC_REG_SVR, LAPIC_SVR_ENABLE | HAL_VECTOR_SPURIOUS);

    if (!HalRegisterInterrupt(HAL_VECTOR_TIMER, TimerIsr))
        KdPrint("apic: timer vector already registered\n");
}

void HalApicEoi(void)
{
    if (g_Lapic != 0)
        LapicWrite(LAPIC_REG_EOI, 0);
}

void HalApicSelfIpi(u8 vector)
{
    if (g_Lapic != 0)
        LapicWrite(LAPIC_REG_ICR_LOW, LAPIC_ICR_SELF | LAPIC_ICR_ASSERT | vector);
}

static bool LapicBitSet(u32 base, u8 vector)
{
    if (g_Lapic == 0)
        return false;
    return (LapicRead(base + 0x10u * (vector >> 5)) >> (vector & 31)) & 1;
}

bool HalApicVectorRequested(u8 vector)
{
    return LapicBitSet(LAPIC_REG_IRR, vector);
}

bool HalApicVectorInService(u8 vector)
{
    return LapicBitSet(LAPIC_REG_ISR, vector);
}

u64 HalTickCount(void)
{
    return __atomic_load_n(&g_Ticks, __ATOMIC_RELAXED);
}

/* One PIT channel 2 mode-0 countdown of at most PIT_CHUNK_MS. */
static bool PitWaitChunk(u32 ms)
{
    u32 count = (u32)((u64)PIT_HZ * ms / 1000);
    u8 sysctl = HalInb(PORT_SYSCTL) & ~(SYSCTL_GATE2 | SYSCTL_SPKR);

    HalOutb(PORT_SYSCTL, sysctl); /* gate low: hold the counter */
    HalOutb(PIT_CMD, PIT_CMD_CH2_LOHI_MODE0); /* OUT goes low */
    HalOutb(PIT_CH2_DATA, (u8)count);
    HalOutb(PIT_CH2_DATA, (u8)(count >> 8));
    HalOutb(PORT_SYSCTL, sysctl | SYSCTL_GATE2); /* gate high: start counting */

    bool done = false;
    for (u32 polls = 0; polls < PIT_MAX_POLLS; polls++) {
        if (HalInb(PORT_SYSCTL) & SYSCTL_OUT2) { /* OUT high at terminal count */
            done = true;
            break;
        }
    }
    HalOutb(PORT_SYSCTL, sysctl); /* gate low again */
    return done;
}

bool HalPitDelayMs(u32 ms)
{
    while (ms > 0) {
        u32 chunk = ms < PIT_CHUNK_MS ? ms : PIT_CHUNK_MS;
        if (!PitWaitChunk(chunk))
            return false;
        ms -= chunk;
    }
    return true;
}

void HalApicTimerStart(u32 hz)
{
    if (g_Lapic == 0 || hz == 0 || hz > 10000) {
        KdPrint("apic: timer start refused (hz=%u)\n", (unsigned)hz);
        return;
    }

    /* Calibrate: count down from the maximum, masked, for 10 ms of PIT time. */
    LapicWrite(LAPIC_REG_TIMER_DIV, LAPIC_DIV_BY_16);
    LapicWrite(LAPIC_REG_LVT_TIMER, LAPIC_LVT_MASKED | HAL_VECTOR_TIMER); /* one-shot */
    LapicWrite(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);
    bool ok = HalPitDelayMs(CALIBRATE_MS);
    u32 elapsed = 0xFFFFFFFFu - LapicRead(LAPIC_REG_TIMER_CUR);
    LapicWrite(LAPIC_REG_TIMER_INIT, 0); /* stop */
    if (!ok || elapsed == 0) {
        KdPrint("apic: timer calibration failed\n");
        return;
    }

    u64 count = (u64)elapsed * (1000 / CALIBRATE_MS) / hz;
    if (count == 0)
        count = 1;
    if (count > 0xFFFFFFFFull)
        count = 0xFFFFFFFFull;
    KdPrint("apic: %u timer counts per %u ms (div 16), %u Hz -> initial count %llu\n",
            (unsigned)elapsed, (unsigned)CALIBRATE_MS, (unsigned)hz, count);

    LapicWrite(LAPIC_REG_LVT_TIMER, LAPIC_LVT_PERIODIC | HAL_VECTOR_TIMER);
    LapicWrite(LAPIC_REG_TIMER_INIT, (u32)count); /* writing starts the timer */
}
