#pragma once

#include <assert.h>
#include "arch/atomic.h"

typedef struct buf {
  int b_refcount;
  size_t b_size;
  void *b_ptr;
  void (*b_free)(void *);
  uint8_t b_content[0];
} buf_t;

#define buf_cstr(buf) ((const char *)(buf)->b_ptr)

static inline char *
buf_str(buf_t *b)
{
  assert(b->b_refcount == 1);
  return (char *)b->b_ptr;
}

#define buf_c8(buf) ((const uint8_t *)(buf)->b_ptr)

void buf_release(buf_t *b);

buf_t *buf_make_writable(buf_t *b);

buf_t *buf_create(size_t size);

buf_t *buf_create_and_copy(size_t size, const void *data);

buf_t *buf_create_and_adopt(size_t size, void *data, void (*freefn)(void *));

static inline buf_t *  __attribute__ ((warn_unused_result))
buf_retain(buf_t *b)
{
  atomic_add(&b->b_refcount, 1);
  return b;
}
