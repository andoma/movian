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

#include <stdlib.h>
#include <string.h>

#include "glw.h"
#include "glw_event.h"



typedef struct glw_event_generic {
  glw_event_map_t map;

  char *target;
  char *method;
  char *argument;

} glw_event_generic_t;



/**
 *
 */
static void
glw_event_map_generic_dtor(glw_event_map_t *gem)
{
  glw_event_generic_t *g = (glw_event_generic_t *)gem;

  free(g->target);
  free(g->method);
  free(g->argument);

  free(g);
}

/**
 *
 */
static void
glw_event_map_generic_fire(glw_t *w, glw_event_map_t *gem)
{
  glw_event_generic_t *g = (glw_event_generic_t *)gem;
  event_generic_t *e;

  e = event_create(EVENT_GENERIC, sizeof(event_generic_t));
  e->h.e_dtor = event_generic_dtor;
    
  e->target   = strdup(g->target);
  e->method   = strdup(g->method);
  e->argument = strdup(g->argument);
  
  e->h.e_mapped = 1;

  while(w != NULL) {
    if(glw_signal0(w, GLW_SIGNAL_EVENT_BUBBLE, e))
      return; /* Taker gets our refcount */
    w = w->glw_parent;
  }

  event_unref(&e->h); /* Nobody took it */
}


/**
 *
 */
glw_event_map_t *
glw_event_map_generic_create(const char *target, 
			     const char *method,
			     const char *argument)
{
  glw_event_generic_t *g = malloc(sizeof(glw_event_generic_t));
  
  g->target   = strdup(target);
  g->method   = strdup(method);
  g->argument = strdup(argument);

  g->map.gem_dtor = glw_event_map_generic_dtor;
  g->map.gem_fire = glw_event_map_generic_fire;
  return &g->map;
}




/**
 *
 */
void
glw_event_map_add(glw_t *w, glw_event_map_t *gem)
{
  glw_event_map_t *o;

  LIST_FOREACH(o, &w->glw_event_maps, gem_link) {
    if(o->gem_srcevent == gem->gem_srcevent) {
      LIST_REMOVE(o, gem_link);
      o->gem_dtor(o);
      break;
    }
  }
  LIST_INSERT_HEAD(&w->glw_event_maps, gem, gem_link);
}


#if 0

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

#endif



/**
 *
 */
int
glw_event_map_intercept(glw_t *w, event_t *e)
{
  glw_event_map_t *gem;

  if(e->e_mapped)
    return 0; /* Avoid recursion */

  LIST_FOREACH(gem, &w->glw_event_maps, gem_link) {
    if(gem->gem_srcevent == e->e_type)
      break;
  }
  if(gem == NULL)
    return 0;

  gem->gem_fire(w, gem);
  return 1;
}
