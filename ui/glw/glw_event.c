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
#include "glw_i.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>


static hts_mutex_t gev_ref_mutex;

/**
 *
 */
void
glw_event_init(void)
{
  hts_mutex_init(&gev_ref_mutex);
}


/**
 *
 */
static void
glw_event_default_dtor(glw_event_t *ge)
{
  free(ge);
}

/**
 *
 */
void *
glw_event_create(glw_event_type_t type, size_t size)
{
  glw_event_t *ge = malloc(size);
  ge->ge_dtor = glw_event_default_dtor;
  ge->ge_refcnt = 1;
  ge->ge_mapped = 0;
  ge->ge_type = type;
  return ge;
}

/**
 *
 */
void *
glw_event_create_unicode(int sym)
{
  glw_event_unicode_t *ge = malloc(sizeof(glw_event_unicode_t));
  ge->h.ge_dtor = glw_event_default_dtor;
  ge->h.ge_refcnt = 1;
  ge->h.ge_type = GEV_UNICODE;
  ge->sym = sym;
  return ge;
}


/**
 *
 */
void
glw_event_enqueue(glw_event_queue_t *geq, glw_event_t *ge)
{
  hts_mutex_lock(&gev_ref_mutex);
  ge->ge_refcnt++;
  hts_mutex_unlock(&gev_ref_mutex);

  hts_mutex_lock(&geq->geq_mutex);
  TAILQ_INSERT_TAIL(&geq->geq_q, ge, ge_link);
  hts_cond_signal(&geq->geq_cond);
  hts_mutex_unlock(&geq->geq_mutex);
}


/**
 *
 * @param timeout Timeout in milliseconds
 */
glw_event_t *
glw_event_get(int timeout, glw_event_queue_t *geq)
{
  glw_event_t *ge;

  hts_mutex_lock(&geq->geq_mutex);

  if(timeout == 0) {
    ge = TAILQ_FIRST(&geq->geq_q);
  } else if(timeout == -1) {
    while((ge = TAILQ_FIRST(&geq->geq_q)) == NULL)
      hts_cond_wait(&geq->geq_cond, &geq->geq_mutex);
  } else {
    while((ge = TAILQ_FIRST(&geq->geq_q)) == NULL) {
      if(hts_cond_wait_timeout(&geq->geq_cond, &geq->geq_mutex, timeout))
	break;
    }
  }

  if(ge != NULL)
    TAILQ_REMOVE(&geq->geq_q, ge, ge_link);
  hts_mutex_unlock(&geq->geq_mutex);
  return ge;
}


/**
 *
 */
void
glw_event_unref(glw_event_t *ge)
{
  if(ge->ge_refcnt == 1) {
    ge->ge_dtor(ge);
    return;
  }

  hts_mutex_lock(&gev_ref_mutex);
  ge->ge_refcnt--;
  hts_mutex_unlock(&gev_ref_mutex);
}


/**
 *
 */
void
glw_event_initqueue(glw_event_queue_t *geq)
{
  TAILQ_INIT(&geq->geq_q);
  hts_cond_init(&geq->geq_cond);
  hts_mutex_init(&geq->geq_mutex);
}


/**
 *
 */
void
glw_event_flushqueue(glw_event_queue_t *geq)
{
  glw_event_t *ge;
  hts_mutex_lock(&geq->geq_mutex);

  while((ge = TAILQ_FIRST(&geq->geq_q)) != NULL) {
    TAILQ_REMOVE(&geq->geq_q, ge, ge_link);
    glw_event_unref(ge);
  }
  hts_mutex_unlock(&geq->geq_mutex);
}


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
sys_dtor(glw_event_t *ge)
{
  glw_event_sys_t *sys = (void *)ge;
  free(sys->target);
  free(sys->method);
  free(sys->argument);
  free(sys);
}


/**
 *
 */
int
glw_event_map_intercept(glw_t *w, glw_event_t *ge)
{
  glw_event_map_t *gem;
  glw_t *t;
  glw_event_t *n;
  glw_event_sys_t *sys;
  int r = 0;

  if(ge->ge_mapped)
    return 0; /* Avoid recursion */

  LIST_FOREACH(gem, &w->glw_event_maps, gem_link) {
    if(gem->gem_inevent == ge->ge_type)
      break;
  }
  if(gem == NULL)
    return 0;


  switch(gem->gem_outevent) {
  case GEV_SYS:
    sys = glw_event_create(GEV_SYS, sizeof(glw_event_sys_t));
    sys->h.ge_dtor = sys_dtor;
    
    sys->target   = strdup(gem->gem_target);
    sys->method   = strdup(gem->gem_method);
    sys->argument = strdup(gem->gem_argument);
    
    n = &sys->h;
    n->ge_mapped = 1;

    while(w != NULL) {
      if((r = glw_signal(w, GLW_SIGNAL_EVENT_BUBBLE, n)) != 0)
	return 1; /* Taker gets our refcount */
      w = w->glw_parent;
    }
    break;


  default:
    n = glw_event_create(gem->gem_outevent, sizeof(glw_event_t));
    n->ge_mapped = 1;
    
    if((t = glw_event_find_target(w, gem->gem_target)) != NULL)
      r = glw_signal(t, GLW_SIGNAL_EVENT, n);
    break;
  }

  glw_event_unref(n);
  return r;
}



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
glw_event_signal_simple(glw_t *w, glw_event_type_t type)
{
  glw_event_t *ge = glw_event_create(type, sizeof(glw_event_t));
  glw_signal(w, GLW_SIGNAL_EVENT, ge);
  glw_event_unref(ge);
}
