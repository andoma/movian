#include <psl1ght/lv2.h>
#include "showtime.h"

#define	JEMALLOC_CHUNK_MMAP_C_
#include "jemalloc/internal/jemalloc_internal.h"

/******************************************************************************/
/* Data. */

#define ROUND_UP(p, round) ((p + (round) - 1) & ~((round) - 1))

static void *
chunk_alloc_mmap_internal(size_t size, bool noreserve)
{
	size_t allocsize = ROUND_UP(size, 1024*1024);
	u32 taddr;

	if(Lv2Syscall3(348, size, 0x400, (u64)&taddr))
		return NULL;
	return (void *)(uint64_t)taddr;
}

void *
chunk_alloc_mmap(size_t size)
{

	return chunk_alloc_mmap_internal(size, false);
}

void *
chunk_alloc_mmap_noreserve(size_t size)
{

	return chunk_alloc_mmap_internal(size, true);
}

void
chunk_dealloc_mmap(void *chunk, size_t size)
{
	Lv2Syscall1(349, (uint64_t)chunk);
}

bool
chunk_mmap_boot(void)
{
	return false;
}
