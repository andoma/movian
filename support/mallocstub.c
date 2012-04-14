#include <stddef.h>
struct _reent;

void _free_r(struct _reent *r, void *ptr)
{
}

void *_malloc_r(struct _reent *r, size_t size)
{
  return NULL;
}

void *_calloc_r(struct _reent *r, size_t nmemb, size_t size)
{
  return NULL;
}

void *_realloc_r(struct _reent *r, void *ptr, size_t size)
{
  return NULL;
}

void free(void *ptr)
{
}

void *malloc(size_t size)
{
  return NULL;
}

void *calloc(size_t nmemb, size_t size)
{
  return NULL;
}

void *realloc(void *ptr, size_t size)
{
  return NULL;
}

void *memalign(size_t boundary, size_t size)
{
  return NULL;
}
