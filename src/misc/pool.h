#pragma once
#include "arch/threads.h"

// #define POOL_DEBUG

LIST_HEAD(pool_segment_list, pool_segment);


/**
 *
 */
typedef struct pool {
  struct pool_segment_list p_segments;
  
  size_t p_item_size;

  int p_flags;

  hts_mutex_t p_mutex;
  struct pool_item *p_item;

  int p_num_out;
  const char *p_name;
} pool_t;


#define POOL_REENTRANT 0x1
#define POOL_ZERO_MEM  0x2

pool_t *pool_create(const char *name, size_t item_size, int flags);

void pool_init(pool_t *pool, const char *name, size_t item_size, int flags);

#ifdef POOL_DEBUG

void *pool_get_ex(pool_t *p, const char *file, int line)
  __attribute__ ((malloc));

#define pool_get(p) pool_get_ex(p, __FILE__, __LINE__)

#else

void *pool_get(pool_t *p) __attribute__ ((malloc));

#endif

void pool_put(pool_t *p, void *ptr);

void pool_destroy(pool_t *p);

int pool_num(pool_t *p);
