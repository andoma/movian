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

#include "showtime.h"
#include "prop_i.h"


/**
 *
 */
prop_vec_t *
prop_vec_create(int capacity)
{
  prop_vec_t *pv;

  pv = malloc(sizeof(prop_vec_t) + sizeof(prop_t *) * capacity);
  pv->pv_capacity = capacity;
  pv->pv_length = 0;
  pv->pv_refcount = 1;
  return pv;
}


/**
 *
 */
prop_vec_t *prop_vec_append(prop_vec_t *pv, prop_t *p)
{
  assert(pv->pv_refcount == 1);

  if(pv->pv_length == pv->pv_capacity) {
    pv->pv_capacity++;
    pv = realloc(pv, sizeof(prop_vec_t) + sizeof(prop_t *) * pv->pv_capacity);
  }
  assert(pv->pv_length < pv->pv_capacity);
  pv->pv_vec[pv->pv_length] = prop_ref_inc(p);
  pv->pv_length++;
  return pv;
}


/**
 *
 */
prop_vec_t *
prop_vec_addref(prop_vec_t *pv)
{
  atomic_add(&pv->pv_refcount, 1);
  return pv;
}


/**
 *
 */
void
prop_vec_release(prop_vec_t *pv)
{
  int i;

  if(atomic_add(&pv->pv_refcount, -1) > 1)
    return;

  for(i = 0; i < pv->pv_length; i++)
    prop_ref_dec(pv->pv_vec[i]);
  
  free(pv);
}


/**
 *
 */
void
prop_vec_destroy_entries(prop_vec_t *pv)
{
  int i;

  for(i = 0; i < pv->pv_length; i++)
    prop_destroy(pv->pv_vec[i]);
}

