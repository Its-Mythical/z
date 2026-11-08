#ifndef ZTO2_STATUS_H
#define ZTO2_STATUS_H

#include "types.h"

typedef i32 ZSTATUS;

#define Z_SUCCESS                ((ZSTATUS)0x00000000)
#define Z_INVALID_PARAMETER      ((ZSTATUS)0xC000000D)
#define Z_BUFFER_TOO_SMALL       ((ZSTATUS)0xC0000023)
#define Z_INSUFFICIENT_RESOURCES ((ZSTATUS)0xC000009A)

#endif
