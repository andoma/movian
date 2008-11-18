/*
 *  GL Widgets, event handling
 *  Copyright (C) 2008 Andreas Öman
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

#include "glw.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/**
 *
 */
void
glw_event_map_destroy(glw_event_map_t *gem)
{
  LIST_REMOVE(gem, gem_link);
  free(gem->gem_target);
  free(gem->gem_method);
  free(gem->gem_argument);
  free(gem);
}

/**
 *
 */
static glw_t *
glw_event_find_target2(glw_t *w, glw_t *forbidden, const char *id)
{
  glw_t *c, *r;

  if(w->glw_id != NULL && !strcmp(w->glw_id, id))
    return w;

  if(w->glw_class == GLW_LIST || w->glw_class == GLW_ARRAY)
    return NULL;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c == forbidden)
      continue;
    if((r = glw_event_find_target2(c, NULL, id)) != NULL)
      return r;
  }
  return NULL;
}

/**
 *
 */
static glw_t *
glw_event_find_target(glw_t *w, const char *id)
{
  glw_t *r;

  if((r = glw_event_find_target2(w, NULL, id)) != NULL)
    return r;

  while(w->glw_parent != NULL) {
    if((r = glw_event_find_target2(w->glw_parent, w, id)) != NULL)
      return r;
    w = w->glw_parent;
  }
  return NULL;
}

/**
 * Destroy a sys signal
 */
static void
generic_dtor(event_t *e)
{
  event_generic_t *g = (void *)e;
  free(g->target);
  free(g->method);
  free(g->argument);
  free(g);
}


/**
 *
 */
int
glw_event_map_intercept(glw_t *w, event_t *e)
{
  glw_event_map_t *gem;
  glw_t *t;
  event_t *n;
  event_generic_t *g;
  int r = 0;

  if(e->e_mapped)
    return 0; /* Avoid recursion */

  LIST_FOREACH(gem, &w->glw_event_maps, gem_link) {
    if(gem->gem_inevent == e->e_type)
      break;
  }
  if(gem == NULL)
    return 0;


  switch(gem->gem_outevent) {
  case EVENT_GENERIC:
    g = event_create(EVENT_GENERIC, sizeof(event_generic_t));
    g->h.e_dtor = generic_dtor;
    
    g->target   = strdup(gem->gem_target);
    g->method   = strdup(gem->gem_method);
    g->argument = strdup(gem->gem_argument);
    
    n = &g->h;
    n->e_mapped = 1;

    while(w != NULL) {
      if((r = glw_signal0(w, GLW_SIGNAL_EVENT_BUBBLE, n)) != 0)
	return 1; /* Taker gets our refcount */
      w = w->glw_parent;
    }
    break;


  default:
    n = event_create(gem->gem_outevent, sizeof(event_t));
    n->e_mapped = 1;
    
    if((t = glw_event_find_target(w, gem->gem_target)) != NULL)
      r = glw_signal0(t, GLW_SIGNAL_EVENT, n);
    break;
  }

  event_unref(n);
  return r;
}


#if 0
/**
 *
 */
int
glw_event_enqueuer(glw_t *w, void *opaque, glw_signal_t sig, void *extra)
{
  glw_event_queue_t *geq = opaque;
  glw_event_t *ge = extra;

  if(sig != GLW_SIGNAL_EVENT && sig != GLW_SIGNAL_EVENT_BUBBLE)
    return 0;

  glw_event_enqueue(geq, ge);
  return 1;
}


/**
 *
 */
void
glw_event_signal_simple(glw_t *w, event_type_t type)
{
  event_t *e = event_create(type, sizeof(event_t));
  glw_signal(w, GLW_SIGNAL_EVENT, e);
  event_unref(e);
}

#endif
