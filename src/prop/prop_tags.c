/*
 *  Property trees
 *  Copyright (C) 2010 Andreas Öman
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
 */

#include "showtime.h"
#include "prop_i.h"

/**
 *
 */
typedef struct prop_tag {
  struct prop_tag *next;
  void *key;
  void *value;
} prop_tag_t;


/**
 *
 */
void *
prop_tag_get(prop_t *p, void *key)
{
  prop_tag_t *pt;
  void *v = NULL;
  hts_mutex_lock(&prop_tag_mutex);
  for(pt = p->hp_tags; pt != NULL; pt = pt->next)
    if(pt->key == key) {
      v = pt->value;
      break;
    }
  hts_mutex_unlock(&prop_tag_mutex);
  return v;
}


/**
 *
 */
void
prop_tag_set(prop_t *p, void *key, void *value)
{
  prop_tag_t *pt = malloc(sizeof(prop_tag_t));
  pt->key = key;
  pt->value = value;

  hts_mutex_lock(&prop_tag_mutex);
  pt->next = p->hp_tags;
  p->hp_tags = pt;
  hts_mutex_unlock(&prop_tag_mutex);
}


/**
 *
 */
void *
prop_tag_clear(prop_t *p, void *key)
{
  prop_tag_t *pt, **q = &p->hp_tags;

  hts_mutex_lock(&prop_tag_mutex);

  while((pt = *q) != NULL) {
    if(pt->key == key) {
      void *v = pt->value;
      *q = pt->next;
      free(pt);
      hts_mutex_unlock(&prop_tag_mutex);
      return v;
    } else {
      q = &pt->next;
    }
  }
  hts_mutex_unlock(&prop_tag_mutex);
  return NULL;
}
