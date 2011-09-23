#pragma once

// #define POOL_DEBUG

typedef struct pool pool_t;

#define POOL_REENTRANT 0x1
#define POOL_ZERO_MEM  0x2

pool_t *pool_create(const char *name, size_t item_size, int flags);

#ifdef POOL_DEBUG

void *pool_get_ex(pool_t *p, const char *file, int line)
  __attribute__ ((malloc));

#define pool_get(p) pool_get_ex(p, __FILE__, __LINE__)

#else

void *pool_get(pool_t *p) __attribute__ ((malloc));

#endif

void pool_put(pool_t *p, void *ptr);

void pool_destroy(pool_t *p);
