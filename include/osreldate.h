#pragma once
/*
 * FreeBSD release version compatibility for the PS4 cross-compilation target.
 * The workflow uses -target x86_64-pc-freebsd12-elf.
 * This header is used by libc++ for feature detection only.
 */
#ifndef __FreeBSD_version
#define __FreeBSD_version 1200000
#endif
