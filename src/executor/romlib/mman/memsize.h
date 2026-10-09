#if !defined(_RSYS_MEMSIZE_H_)
#define _RSYS_MEMSIZE_H_

#define MIN_APPLZONE_SIZE (512 * 1024)
#define MAX_APPLZONE_SIZE (2046 * 1024 * 1024)

#ifdef TWENTYFOUR_BIT_ADDRESSING
#define DEFAULT_APPLZONE_SIZE (3 * 1024 * 1024)
#else
#define DEFAULT_APPLZONE_SIZE (64 * 1024 * 1024)
#endif

#define MIN_SYSZONE_SIZE (128 * 1024)
#define MAX_SYSZONE_SIZE (2047 * 1024 * 1024)

/* MacPhoenix: was 512K. A real 7.5.5 boot's System heap: 951K with no
   extensions, grown to $22D880 - $2000 with them (extension 'sysz'
   requests, AppleScript's 300K component code). Executor's doesn't grow
   yet, so it starts at the larger size. */
#define DEFAULT_SYSZONE_SIZE (0x22D880 - 0x2000)

#define MIN_STACK_SIZE (64 * 1024)
#define MAX_STACK_SIZE (2047 * 1024 * 1024)
#define DEFAULT_STACK_SIZE (256 * 1024)

#endif /* !_RSYS_MEMSIZE_H_ */
