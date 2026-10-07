#if !defined(_tempalloc_h_)
#define _tempalloc_h_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

/* This header contains macros which are useful if you want to allocate
 * a potentially large amount of temporary storage, such as screen-sized
 * temporary pixel buffers.
 * Since we don't have much stack space under some environments,
 * this code will guarantee you don't blow your stack.  It is safe to
 * TEMP_ALLOC_FREE this memory even if you never allocated it
 * (in that case, nothing will happen).
 * This is handy when you may conditionally allocate new memory, and
 * you want to make sure any allocated memory gets freed at the end.
 *
 * Here's an example:
 *
 * void foo (void *x)
 * {
 *   TEMP_ALLOC_DECL (my_temp_storage);
 *   if (make_x_bigger)
 *     TEMP_ALLOC_ALLOCATE (x, my_temp_storage, 100000);
 *   do_stuff (x);
 *   TEMP_ALLOC_FREE (my_temp_storage);
 * }
 */

//#ifdef _WIN32
// Windows has a smaller default stack size.
// But a large screen set to 32-bit depth is more than the
// 8MB stack size default that's currently popular on Linux, as well.
#define TEMP_ALLOC_ON_MAC_HEAP_INSTEAD_OF_ALLOCA
//#endif

#ifdef TEMP_ALLOC_ON_MAC_HEAP_INSTEAD_OF_ALLOCA

#include <MemoryMgr.h>
#include <mman/mman.h>

namespace Executor
{
typedef enum
{
    TEMP_ALLOC_NO_FREE,
    TEMP_ALLOC_FREE,
    TEMP_ALLOC_DISPOSHANDLE
} temp_alloc_status_t;

typedef struct
{
    temp_alloc_status_t status;
    union
    {
        Executor::Handle handle;
        void *ptr;
    } u;
} temp_alloc_data_t;

#define TEMP_ALLOC_DECL(name) \
    temp_alloc_data_t name = {TEMP_ALLOC_NO_FREE, {0}}

#define TEMP_ALLOC_ALLOCATE(ptr_var, name, size)                                        \
    do                                                                                  \
    {                                                                                   \
        if((size) <= 8192) /* Satisfy small requests with alloca. */                    \
        {                                                                               \
            (name).status = TEMP_ALLOC_NO_FREE;                                         \
            ptr_var = (decltype(ptr_var))alloca(size);                                  \
        }                                                                               \
        else                                                                            \
        {                                                                               \
            {                                                                           \
                Executor::TheZoneGuard guard(LM(SysZone)); /* Try LM(SysZone) first. */ \
                (name).u.handle = Executor::NewHandle(size);                            \
            }                                                                           \
            if(!(name).u.handle)                                                        \
            {                                                                           \
                TheZoneGuard guard(LM(ApplZone)); /* Then LM(ApplZone). */              \
                (name).u.handle = NewHandle(size);                                      \
            }                                                                           \
            if((name).u.handle)                                                         \
            {                                                                           \
                (name).status = TEMP_ALLOC_DISPOSHANDLE;                                \
                HLock((name).u.handle);                                                 \
                ptr_var = (decltype(ptr_var))*(name).u.handle;                          \
            }                                                                           \
            else                                                                        \
            {                                                                           \
                /* Use malloc. */                                                       \
                (name).status = TEMP_ALLOC_FREE;                                        \
                (name).u.ptr = (void *)malloc(size);                                    \
                ptr_var = (decltype(ptr_var))(name.u.ptr);                              \
            }                                                                           \
        }                                                                               \
    } while(0)

#define TEMP_ALLOC_FREE(name)                             \
    do                                                    \
    {                                                     \
        if((name).status == TEMP_ALLOC_FREE)              \
            free((name).u.ptr);                           \
        else if((name).status == TEMP_ALLOC_DISPOSHANDLE) \
        {                                                 \
            HUnlock((name).u.handle);                     \
            DisposeHandle((name).u.handle);               \
        }                                                 \
        (name).u.ptr = 0;                                 \
    } while(0)

} // namespace Executor

#else /* !MSDOS */

/* On other platforms, we can alloca extremely large amounts,
 * so there's no need for this complexity.
 */
#define TEMP_ALLOC_DECL(name)
#define TEMP_ALLOC_ALLOCATE(ptr_var, name, size) (ptr_var = (decltype(ptr_var))alloca(size))
#define TEMP_ALLOC_FREE(name)

#endif /* !MSDOS */

#endif /* !_tempalloc_h_ */
