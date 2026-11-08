#include <limine.h>
#include <zto2/types.h>

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request g_HhdmRequest = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request g_MemmapRequest = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request g_FramebufferRequest = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER

u64 g_HhdmOffset;

static void Debugcon_Write(const char *s)
{
    for (; *s; s++)
        __asm__ volatile("outb %0, %1" : : "a"(*s), "Nd"((u16)0xE9));
}

static void Halt_Forever(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

void kmain(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED ||
        g_HhdmRequest.response == 0 ||
        g_MemmapRequest.response == 0) {
        Debugcon_Write("ZTO2: missing boot info\n");
        Halt_Forever();
    }

    g_HhdmOffset = g_HhdmRequest.response->offset;
    Halt_Forever();
}
