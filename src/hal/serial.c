#include <zto2/hal.h>
#include <zto2/portio.h>

#define COM1_BASE        0x3F8
#define UART_DATA        0 /* RBR/THR, or divisor low when DLAB=1 */
#define UART_IER         1 /* divisor high when DLAB=1 */
#define UART_FCR         2
#define UART_LCR         3
#define UART_MCR         4
#define UART_LSR         5
#define LSR_THR_EMPTY    0x20
#define SERIAL_MAX_POLLS 100000

/* Set when a byte times out: a dead or absent UART would otherwise cost
 * SERIAL_MAX_POLLS port reads for every later byte. */
static bool g_SerialDead;

void HalSerialInit(void)
{
    g_SerialDead = false;
    HalOutb(COM1_BASE + UART_IER, 0x00); /* no interrupts */
    HalOutb(COM1_BASE + UART_LCR, 0x80); /* DLAB on */
    HalOutb(COM1_BASE + UART_DATA, 0x01); /* divisor 1 = 115200 baud (low) */
    HalOutb(COM1_BASE + UART_IER, 0x00); /* divisor high */
    HalOutb(COM1_BASE + UART_LCR, 0x03); /* 8N1, DLAB off */
    HalOutb(COM1_BASE + UART_FCR, 0xC7); /* enable and clear FIFOs, 14-byte trigger */
    HalOutb(COM1_BASE + UART_MCR, 0x03); /* DTR, RTS */
}

void HalSerialWrite(const char *s, usize n)
{
    for (usize i = 0; i < n && !g_SerialDead; i++) {
        unsigned polls = 0;
        while (!(HalInb(COM1_BASE + UART_LSR) & LSR_THR_EMPTY)) {
            if (++polls >= SERIAL_MAX_POLLS) {
                g_SerialDead = true; /* drop this byte and all later ones */
                return;
            }
        }
        HalOutb(COM1_BASE + UART_DATA, (u8)s[i]);
    }
}
