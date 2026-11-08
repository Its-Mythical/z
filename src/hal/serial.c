#include <zto2/hal.h>

#define COM1_BASE        0x3F8
#define UART_DATA        0 /* RBR/THR, or divisor low when DLAB=1 */
#define UART_IER         1 /* divisor high when DLAB=1 */
#define UART_FCR         2
#define UART_LCR         3
#define UART_MCR         4
#define UART_LSR         5
#define LSR_THR_EMPTY    0x20
#define SERIAL_MAX_POLLS 100000

static inline void Outb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline u8 Inb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void HalSerialInit(void)
{
    Outb(COM1_BASE + UART_IER, 0x00); /* no interrupts */
    Outb(COM1_BASE + UART_LCR, 0x80); /* DLAB on */
    Outb(COM1_BASE + UART_DATA, 0x01); /* divisor 1 = 115200 baud (low) */
    Outb(COM1_BASE + UART_IER, 0x00); /* divisor high */
    Outb(COM1_BASE + UART_LCR, 0x03); /* 8N1, DLAB off */
    Outb(COM1_BASE + UART_FCR, 0xC7); /* enable and clear FIFOs, 14-byte trigger */
    Outb(COM1_BASE + UART_MCR, 0x03); /* DTR, RTS */
}

void HalSerialWrite(const char *s, usize n)
{
    for (usize i = 0; i < n; i++) {
        for (unsigned polls = 0; polls < SERIAL_MAX_POLLS; polls++) {
            if (Inb(COM1_BASE + UART_LSR) & LSR_THR_EMPTY) {
                Outb(COM1_BASE + UART_DATA, (u8)s[i]);
                break;
            }
        }
        /* Timed out: drop the byte so a missing UART cannot hang boot. */
    }
}
