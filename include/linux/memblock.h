#ifndef _LINUX_MEMBLOCK_H
#define _LINUX_MEMBLOCK_H

#include <stdlib.h>

#define SMP_CACHE_BYTES 16

static inline void *memblock_alloc(size_t size, size_t align)
{
	/* dummy alignment */
	size = ((size / align) + 1) * align;
	return malloc(size);
}

static inline void memblock_free(void *blk)
{
	free(blk);
}

#endif
