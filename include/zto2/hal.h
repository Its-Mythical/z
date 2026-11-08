#ifndef ZTO2_HAL_H
#define ZTO2_HAL_H

#include <zto2/types.h>

/* COM1 (0x3F8), 115200 8N1. Safe to call with no UART present. */
void HalSerialInit(void);

/* Writes n bytes; a byte is dropped if the UART does not become ready. */
void HalSerialWrite(const char *s, usize n);

#endif
