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
static void
event_bubble(glw_t *w, event_t *e)
{
  if(e->e_nav == NULL)
    e->e_nav = prop_ref_inc(w->glw_root->gr_prop_nav);

  while(w != NULL) {
    if(glw_signal0(w, GLW_SIGNAL_EVENT_BUBBLE, e))
      return;
    w = w->glw_parent;
  }
}


/**
 *
 */
typedef struct glw_event_navOpen {
  glw_event_map_t map;

  char *url;
  char *view;
  prop_t *origin;
  prop_t *model;
  char *how;
} glw_event_navOpen_t;


/**
 *
 */
static void
glw_event_map_navOpen_dtor(glw_root_t *gr, glw_event_map_t *gem)
{
  glw_event_navOpen_t *no = (glw_event_navOpen_t *)gem;

  prop_ref_dec(no->origin);
  prop_ref_dec(no->model);

  free(no->url);
  free(no->view);
  free(no->how);
  free(no);
}

/**
 *
 */
static void
glw_event_map_navOpen_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_navOpen_t *no = (glw_event_navOpen_t *)gem;

  if(no->url == NULL)
    return; // Must have an URL to fire

  event_t *e = event_create_openurl(no->url, no->view, no->origin,
				    no->model, no->how);
  
  e->e_mapped = 1;
  event_bubble(w, e);
  event_release(e);
}


/**
 *
 */
glw_event_map_t *
glw_event_map_navOpen_create(const char *url, const char *view, prop_t *origin,
			     prop_t *model, const char *how)
{
  glw_event_navOpen_t *no = malloc(sizeof(glw_event_navOpen_t));
  
  no->url      = url    ? strdup(url)    : NULL;
  no->view     = view   ? strdup(view)   : NULL;
  no->origin   = prop_ref_inc(origin);
  no->model    = prop_ref_inc(model);
  no->how      = how    ? strdup(how)   : NULL;
  
  no->map.gem_dtor = glw_event_map_navOpen_dtor;
  no->map.gem_fire = glw_event_map_navOpen_fire;
  return &no->map;
}



/**
 *
 */
typedef struct glw_event_playTrack {
  glw_event_map_t map;

  prop_t *track;
  prop_t *source;
  int mode;

} glw_event_playTrack_t;


/**
 *
 */
static void
glw_event_map_playTrack_dtor(glw_root_t *gr, glw_event_map_t *gem)
{
  glw_event_playTrack_t *g = (glw_event_playTrack_t *)gem;

  prop_ref_dec(g->track);
  if(g->source != NULL)
    prop_ref_dec(g->source);
  free(g);
}

/**
 *
 */
static void
glw_event_map_playTrack_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_playTrack_t *g = (glw_event_playTrack_t *)gem;
  event_t *e = event_create_playtrack(g->track, g->source, g->mode);
  
  e->e_mapped = 1;
  event_bubble(w, e);
  event_release(e);
}


/**
 *
 */
glw_event_map_t *
glw_event_map_playTrack_create(prop_t *track, prop_t *source, int mode)
{
  glw_event_playTrack_t *g = malloc(sizeof(glw_event_playTrack_t));

  g->track  = prop_ref_inc(track);
  g->source = prop_ref_inc(source);
  g->mode   = mode;

  g->map.gem_dtor = glw_event_map_playTrack_dtor;
  g->map.gem_fire = glw_event_map_playTrack_fire;
  return &g->map;
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
    event_release(e);
  } else {
    e->e_mapped = 1;
    event_bubble(w, e);
  }
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
typedef struct glw_event_selectTrack {
  glw_event_map_t map;
  event_type_t type;
  char *id;
} glw_event_selectTrack_t;


/**
 *
 */
static void
glw_event_map_selectTrack_dtor(glw_root_t *gr, glw_event_map_t *gem)
{
  glw_event_selectTrack_t *g = (glw_event_selectTrack_t *)gem;

  free(g->id);
  free(g);
}

/**
 *
 */
static void
glw_event_map_selectTrack_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_selectTrack_t *st = (glw_event_selectTrack_t *)gem;

  if(st->id == NULL)
    return; // Must have an ID to fire

  event_t *e = event_create_select_track(st->id, st->type, 1);
  
  e->e_mapped = 1;
  event_bubble(w, e);
}


/**
 *
 */
glw_event_map_t *
glw_event_map_selectTrack_create(const char *id, event_type_t type)
{
  glw_event_selectTrack_t *st = malloc(sizeof(glw_event_selectTrack_t));
  
  st->id = id ? strdup(id) : NULL;
  st->type = type;

  st->map.gem_dtor = glw_event_map_selectTrack_dtor;
  st->map.gem_fire = glw_event_map_selectTrack_fire;
  return &st->map;
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

  if(w->glw_id != NULL && !strcmp(w->glw_id, id))
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
      glw_signal0(t, GLW_SIGNAL_EVENT, e);
    }
  } else {
    event_bubble(w, e);
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
