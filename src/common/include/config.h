/*
 *  config.h - Minimal configuration for mac-phoenix
 *
 *  This is a simplified config.h for meson builds.
 *  Platform detection and feature configuration.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Platform detection */
#if defined(__linux__)
#define HAVE_PTHREADS 1
#define HAVE_POSIX_MEMALIGN 1
#endif

/* UAE CPU emulation */
#define EMULATED_68K 1
#define DIRECT_ADDRESSING 1

/* Disable features we don't need yet */
/* Don't define these at all - use #ifdef checks */
#undef ENABLE_MON
#undef ENABLE_XF86_DGA
#undef ENABLE_VOSF
#undef ENABLE_GTK

/* Standard features */
#define HAVE_FCNTL_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_UNISTD_H 1

/* Integer types */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 16
#define SIZEOF_VOID_P 8

#endif /* CONFIG_H */
