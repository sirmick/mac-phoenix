/**
 * Minimal config.h for UAE CPU in mac-phoenix
 */

#ifndef CONFIG_H
#define CONFIG_H

/* System features */
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define TIME_WITH_SYS_TIME 1

/* Pthread support */
#define HAVE_PTHREADS 1

/* Direct addressing for simplicity */
#ifndef DIRECT_ADDRESSING
#define DIRECT_ADDRESSING 1
#endif

/* No real addressing */
#ifndef REAL_ADDRESSING
#define REAL_ADDRESSING 0
#endif

/* Using emulated 68k */
#define EMULATED_68K 1

/* Use optimized flags (required for correct flag handling) */
#define OPTIMIZED_FLAGS 1

/* Platform-specific flag handling */
#if defined(__x86_64__) || defined(__amd64__)
#define X86_64_ASSEMBLY 1
#elif defined(__i386__) || defined(__i686__)
#define X86_ASSEMBLY 1
#endif

/* No JIT */
#define USE_JIT 0

/* No prefetch buffer */
#define USE_PREFETCH_BUFFER 0

/* ROM is write protected */
#define ROM_IS_WRITE_PROTECTED 1

/* Support features */
#define SUPPORTS_EXTFS 1
#define SUPPORTS_UDP_TUNNEL 1

/* Use pthread services */
#define USE_PTHREADS_SERVICES 1

/* Use CPU emulation services (enables int32 tick counter for precise timing) */
#define USE_CPU_EMUL_SERVICES 1

/* Byte order - little endian x86 */
/* Don't define WORDS_BIGENDIAN on little-endian systems - leave it undefined */
/*
#ifndef WORDS_BIGENDIAN
#define WORDS_BIGENDIAN 0
#endif
*/

/* Sizes */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_VOID_P 8

/* Float format */
#define IEEE_FLOAT_FORMAT 1
#define HOST_FLOAT_FORMAT IEEE_FLOAT_FORMAT

/* FPU emulation - use IEEE implementation (most portable) */
#define FPU_IEEE 1

/* Long double size */
#define SIZEOF_LONG_DOUBLE 16

/* Function attributes */
#define REGPARAM
#define REGPARAM2

/* Force byte-swapped memory access even on x86
 * This is needed because we store M68K memory in big-endian format,
 * not native (little-endian) format */
#ifdef HAVE_GET_WORD_UNSWAPPED
#undef HAVE_GET_WORD_UNSWAPPED
#endif

#endif /* CONFIG_H */
