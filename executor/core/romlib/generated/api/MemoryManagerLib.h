/*****************************************************************************
 *                                                                           *
 *                             MemoryManagerLib.h                            *
 *                             ==================                            *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Size, Ptr, OSErr                                        *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME MemoryManagerLib
#include <base/api-module.h>


namespace Executor {

// A heap; opaque.
struct MMHeap;

typedef MMHeap *MMHeapRef;

MMHeapRef C_MMNewHeap(uint32_t kind, Size initialSize, Size growSize, const char *name);LIBRARY_FUNCTION_NAMED(Memory_Manager, "Memory Manager", MMNewHeap, C_MMNewHeap);


MMHeapRef C_MMGetDefaultHeap();LIBRARY_FUNCTION_NAMED(Memory_Manager, "Memory Manager", MMGetDefaultHeap, C_MMGetDefaultHeap);


Ptr C_MMAllocateIn(Size size, MMHeapRef heap);LIBRARY_FUNCTION_NAMED(Memory_Manager, "Memory Manager", MMAllocateIn, C_MMAllocateIn);


Ptr C_MMAllocateClearIn(Size size, MMHeapRef heap);LIBRARY_FUNCTION_NAMED(Memory_Manager, "Memory Manager", MMAllocateClearIn, C_MMAllocateClearIn);


OSErr C_MMFree(Ptr block);LIBRARY_FUNCTION_NAMED(Memory_Manager, "Memory Manager", MMFree, C_MMFree);


OSErr C_MMGetFreeSpace(MMHeapRef heap, GUEST<Size> *total, GUEST<Size> *contiguous);LIBRARY_FUNCTION_NAMED(Memory_Manager, "Memory Manager", MMGetFreeSpace, C_MMGetFreeSpace);


} /* namespace Executor */