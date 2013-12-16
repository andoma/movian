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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ptrvec.h"


void
ptrvec_insert_entry(ptrvec_t *pv, unsigned int position, void *ptr)
{
  int s, i;

  assert(position <= pv->size);

  if(pv->size + 1 > pv->capacity) {
    s = pv->size * 2 + 1;
    pv->vec = realloc(pv->vec, s * sizeof(void *));
    pv->capacity = s;
  }
  assert(pv->size + 1 <= pv->capacity);

  for(i = pv->size; i > position; i--) {
    pv->vec[i] = pv->vec[i - 1];
  }
  pv->vec[i] = ptr;
  pv->size++;
}


void *
ptrvec_remove_entry(ptrvec_t *pv, unsigned int position)
{
  int i;
  void *r;

  assert(position < pv->size);

  r = pv->vec[position];

  for(i = position; i < pv->size - 1; i++) 
    pv->vec[i] = pv->vec[i + 1];

  pv->size--;
  return r;
}


void *
ptrvec_get_entry(ptrvec_t *pv, unsigned int position)
{
  if(position >= pv->size) 
    return NULL;

  return pv->vec[position];
}


