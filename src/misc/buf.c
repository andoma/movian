#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "buf.h"
#include "arch/atomic.h"

void
buf_release(buf_t *b)
{
  if(b != NULL && atomic_add(&b->b_refcount, -1) == 1) {
    if(b->b_free != NULL)
      b->b_free(b->b_ptr);
    rstr_release(b->b_content_type);
    free(b);
  }
}

buf_t *
buf_make_writable(buf_t *b)
{
  if(b->b_refcount == 1)
    return b;

  buf_t *b2 = buf_create_and_copy(b->b_size, b->b_ptr);
  
  b2->b_content_type = rstr_dup(b->b_content_type);
  buf_release(b);
  return b2;
}


buf_t *
buf_create_and_copy(size_t size, const void *data)
{
  buf_t *b = buf_create(size);
  memcpy(b->b_ptr, data, b->b_size);
  return b;
}


buf_t *
buf_create(size_t size)
{
  buf_t *b = malloc(sizeof(buf_t) + size + 1);
  b->b_refcount = 1;
  b->b_size = size;
  b->b_ptr = b->b_content;
  b->b_free = NULL;
  b->b_content_type = NULL;
  b->b_content[size] = 0;
  return b;
}


buf_t *
buf_create_and_adopt(size_t size, void *data, void (*freefn)(void *))
{
  buf_t *b = malloc(sizeof(buf_t));
  b->b_refcount = 1;
  b->b_size = size;
  b->b_ptr = data;
  b->b_free = freefn;
  b->b_content_type = NULL;
  return b;
}
