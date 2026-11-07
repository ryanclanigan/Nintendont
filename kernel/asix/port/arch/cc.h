/* Nintendont's freestanding, big-endian Starlet port. */
#ifndef ASIX_ARCH_CC_H
#define ASIX_ARCH_CC_H
#include "global.h"
#include "debug.h"
#include <stdint.h>
void *memmove(void *dest, const void *source, size_t size);
#ifdef ASIX_HOST_TEST
#define BYTE_ORDER LITTLE_ENDIAN
#ifdef __TINYC__
#define SSIZE_MAX PTRDIFF_MAX
#endif
#else
#define BYTE_ORDER BIG_ENDIAN
#endif
#define LWIP_NO_UNISTD_H 1
#define LWIP_PLATFORM_DIAG(x) dbgprintf x
#define LWIP_PLATFORM_ASSERT(x) asix_assert(x)
#define PACK_STRUCT_STRUCT __attribute__((packed))
extern void *asix_lwip_heap;
void asix_assert(const char *message) __attribute__((noreturn));
u32 asix_random(void);
#define LWIP_RAND() asix_random()
#endif
