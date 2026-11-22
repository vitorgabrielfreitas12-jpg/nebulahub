#pragma once
/*
 * Compatibility header for FreeBSD-targeted libc++ headers.
 * The OpenOrbis SDK used by this project does not provide sys/endian.h.
 * PS4 is little-endian x86-64; use compiler-provided endian constants.
 */
#include <stdint.h>

#ifndef _LITTLE_ENDIAN
#define _LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif
#ifndef _BIG_ENDIAN
#define _BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif
#ifndef _BYTE_ORDER
#define _BYTE_ORDER __BYTE_ORDER__
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN _LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN _BIG_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER _BYTE_ORDER
#endif
#ifndef __bswap16
#define __bswap16(x) __builtin_bswap16((uint16_t)(x))
#endif
#ifndef __bswap32
#define __bswap32(x) __builtin_bswap32((uint32_t)(x))
#endif
#ifndef __bswap64
#define __bswap64(x) __builtin_bswap64((uint64_t)(x))
#endif
