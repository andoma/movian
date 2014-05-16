/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "showtime.h"
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
  if(b != NULL)
    memcpy(b->b_ptr, data, b->b_size);
  return b;
}


buf_t *
buf_create(size_t size)
{
  buf_t *b = mymalloc(sizeof(buf_t) + size + 1);
  if(b == NULL)
    return NULL;
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


buf_t *
buf_create_from_malloced(size_t size, void *data)
{
  return buf_create_and_adopt(size, data, &free);
}
