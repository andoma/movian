#include <malloc/malloc.h>

#include "arch/arch.h"

size_t
arch_malloc_size(void *ptr)
{
  return malloc_size(ptr);
}
