/*
 *  vm_alloc.h - Minimal shim for KPX CPU build
 *
 *  Wraps mmap/munmap for the decode cache allocator.
 */

#ifndef VM_ALLOC_H
#define VM_ALLOC_H

#include <sys/mman.h>
#include <cstddef>

#define VM_MAP_FAILED ((void *)-1)

static inline void *vm_acquire(size_t size)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return (p == MAP_FAILED) ? VM_MAP_FAILED : p;
}

static inline int vm_release(void *addr, size_t size)
{
	return munmap(addr, size);
}

#endif /* VM_ALLOC_H */
