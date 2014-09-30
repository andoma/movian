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

#include <string.h>

#include "event.h"
#include "glw.h"
#include "glw_event.h"


/**
 *
 */
typedef struct glw_event_external {
  glw_event_map_t map;
  event_t *e;
} glw_event_external_t;


/**
 *
 */
static void
glw_event_map_external_dtor(glw_root_t *gr, glw_event_map_t *gem)
{
  glw_event_external_t *gee = (glw_event_external_t *)gem;
  event_release(gee->e);
  free(gee);
}

/**
 *
 */
static void
glw_event_map_external_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_external_t *gee = (glw_event_external_t *)gem;
  glw_event_to_widget(w, gee->e);
}


/**
 *
 */
glw_event_map_t *
glw_event_map_external_create(event_t *e)
{
  glw_event_external_t *gee = malloc(sizeof(glw_event_external_t));
  e->e_mapped = 1;
  gee->e = e;
  gee->map.gem_dtor = glw_event_map_external_dtor;
  gee->map.gem_fire = glw_event_map_external_fire;
  return &gee->map;
}


/**
 *
 */
typedef struct glw_event_propref {
  glw_event_map_t map;
  prop_t *prop;
  prop_t *target;
} glw_event_propref_t;


/**
 *
 */
static void
glw_event_map_propref_dtor(glw_root_t *gr, glw_event_map_t *gem)
{
  glw_event_propref_t *g = (glw_event_propref_t *)gem;

  prop_ref_dec(g->prop);
  prop_ref_dec(g->target);
  free(g);
}

/**
 *
 */
static void
glw_event_map_propref_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_propref_t *g = (glw_event_propref_t *)gem;

  event_t *e = event_create_prop(EVENT_PROPREF, g->prop);

  if(g->target != NULL) {
    e->e_nav = prop_ref_inc(w->glw_root->gr_prop_nav);
    prop_send_ext_event(g->target, e);
  } else {
    e->e_mapped = 1;
    glw_event_to_widget(w, e);
  }
  event_release(e);
}


/**
 *
 */
glw_event_map_t *
glw_event_map_propref_create(prop_t *prop, prop_t *target)
{
  glw_event_propref_t *g = malloc(sizeof(glw_event_propref_t));

  g->prop   = prop_ref_inc(prop);
  g->target = prop_ref_inc(target);
  g->map.gem_dtor = glw_event_map_propref_dtor;
  g->map.gem_fire = glw_event_map_propref_fire;
  return &g->map;
}



/**
 *
 */
typedef struct glw_event_deliverEvent {
  glw_event_map_t map;
  prop_t *target;
  rstr_t *action;
} glw_event_deliverEvent_t;


/**
 *
 */
static void
glw_event_map_deliverEvent_dtor(glw_root_t *gr, glw_event_map_t *gem)
{
  glw_event_deliverEvent_t *g = (glw_event_deliverEvent_t *)gem;

  rstr_release(g->action);
  prop_ref_dec(g->target);
  free(g);
}

/**
 *
 */
static void
glw_event_map_deliverEvent_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_deliverEvent_t *de = (glw_event_deliverEvent_t *)gem;

  if(de->action == NULL) {
    if(src != NULL)
      prop_send_ext_event(de->target, src);
    return;
  }

  event_t *e = event_create_action_str(rstr_get(de->action));
  e->e_nav = prop_ref_inc(w->glw_root->gr_prop_nav);
  prop_send_ext_event(de->target, e);
  event_release(e);
}


/**
 *
 */
glw_event_map_t *
glw_event_map_deliverEvent_create(prop_t *target, rstr_t *action)
{
  glw_event_deliverEvent_t *de = malloc(sizeof(glw_event_deliverEvent_t));
  
  de->target = prop_ref_inc(target);
  de->action = rstr_dup(action);

  de->map.gem_dtor = glw_event_map_deliverEvent_dtor;
  de->map.gem_fire = glw_event_map_deliverEvent_fire;
  return &de->map;
}



/**
 *
 */
void
glw_event_map_add(glw_t *w, glw_event_map_t *gem)
{
  glw_event_map_t *o;

  LIST_FOREACH(o, &w->glw_event_maps, gem_link) {
    if(o->gem_action == gem->gem_action) {
      LIST_REMOVE(o, gem_link);
      o->gem_dtor(w->glw_root, o);
      break;
    }
  }
  LIST_INSERT_HEAD(&w->glw_event_maps, gem, gem_link);
}


/**
 *
 */
void
glw_event_map_remove_by_action(glw_t *w, action_type_t action)
{
  glw_event_map_t *o;

  LIST_FOREACH(o, &w->glw_event_maps, gem_link) {
    if(o->gem_action == action) {
      LIST_REMOVE(o, gem_link);
      o->gem_dtor(w->glw_root, o);
      break;
    }
  }
}


/**
 *
 */
static glw_t *
glw_event_find_target2(glw_t *w, glw_t *forbidden, const char *id)
{
  glw_t *c, *r;

  if(w->glw_id_rstr != NULL && !strcmp(rstr_get(w->glw_id_rstr), id))
    return w;

  if(w->glw_class->gc_flags & GLW_NAVIGATION_SEARCH_BOUNDARY)
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
 *
 */
glw_t *
glw_find_neighbour(glw_t *w, const char *id)
{
  return glw_event_find_target(w, id);
}


typedef struct glw_event_internal {
  glw_event_map_t map;

  char *target;
  action_type_t event;
  int uc;

} glw_event_internal_t;




/**
 *
 */
static void
glw_event_map_internal_dtor(glw_root_t *gr, glw_event_map_t *gem)
{
  glw_event_internal_t *g = (glw_event_internal_t *)gem;

  free(g->target);
  free(g);
}


/**
 *
 */
static void
glw_event_map_internal_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_internal_t *g = (glw_event_internal_t *)gem;
  glw_t *t;
  event_t *e;

  if(g->uc)
    e = event_create_int(EVENT_UNICODE, g->uc);
  else
    e = event_create_action(g->event);
  e->e_mapped = 1;
  e->e_nav = prop_ref_inc(w->glw_root->gr_prop_nav);

  if(g->target != NULL) {

    if((t = glw_event_find_target(w, g->target)) == NULL) {
      TRACE(TRACE_ERROR, "GLW", "Targeted widget %s not found", g->target);
    } else {
      glw_send_event2(t, e);
    }
  } else {
    glw_event_to_widget(w, e);
  }
  event_release(e);
}



/**
 *
 */
glw_event_map_t *
glw_event_map_internal_create(const char *target, action_type_t event,
			      int uc)
{
  glw_event_internal_t *g = malloc(sizeof(glw_event_internal_t));
  
  g->target = target ? strdup(target) : NULL;
  g->event  = event;
  g->uc     = uc;

  g->map.gem_dtor = glw_event_map_internal_dtor;
  g->map.gem_fire = glw_event_map_internal_fire;
  return &g->map;
}


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

    if((gem->gem_action == ACTION_invalid && event_is_type(e, EVENT_KEYDESC)) ||
       event_is_action(e, gem->gem_action)) {
      gem->gem_fire(w, gem, e);
      return 1;
    }
  }
  return 0;
}
