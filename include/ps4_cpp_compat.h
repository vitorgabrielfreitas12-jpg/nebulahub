#pragma once
/*
 * OpenOrbis already defines max_align_t in bits/alltypes.h.
 * Avoid the duplicate definition from Clang's builtin stddef header.
 */
#ifndef __CLANG_MAX_ALIGN_T_DEFINED
#define __CLANG_MAX_ALIGN_T_DEFINED
#endif

#include <time.h>
#include <stdlib.h>

/*
 * libc++'s FreeBSD threading shim expects this POSIX declaration.
 * This is a compile-time compatibility declaration, not an implementation.
 * Linking must still confirm the symbol is available in the PS4 SDK.
 */
#ifdef __cplusplus
extern "C" {
#endif
int nanosleep(const struct timespec *requested, struct timespec *remaining);
/* libc++ uses this POSIX allocation interface for over-aligned new. */
int posix_memalign(void **memptr, size_t alignment, size_t size);
#ifdef __cplusplus
}
#endif
