/*
 *  arch/cc.h - lwIP architecture-specific definitions
 */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Define byte order */
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* Compiler/platform hints */
#define LWIP_NO_STDDEF_H 0
#define LWIP_NO_STDINT_H 0
#define LWIP_NO_INTTYPES_H 0
#define LWIP_NO_LIMITS_H 0

/* Diagnostic output */
#define LWIP_PLATFORM_DIAG(x) do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { \
	fprintf(stderr, "lwIP assertion \"%s\" failed at %s:%d\n", x, __FILE__, __LINE__); \
	abort(); \
} while(0)

/* Packing macros */
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#endif /* LWIP_ARCH_CC_H */
