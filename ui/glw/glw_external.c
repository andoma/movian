/*
 *  GL Widgets, External interface
 *  Copyright (C) 2007 Andreas Öman
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
#include <math.h>
#include <errno.h>

#include "glw.h"
#include "glw_i.h"
#include "glw_texture.h"
#include "glw_text_bitmap.h"


/**
 *
 */
void
glw_flush(void)
{
  glw_lock();
  glw_gf_do();
  glw_tex_flush_all();
  glw_text_flush();
  glw_unlock();
}


/*
 *
 */

void
glw_deref(glw_t *w)
{
  glw_lock();
  glw_deref0(w);
  glw_unlock();
}

/*
 *
 */

void
glw_ref(glw_t *w)
{
  glw_lock();
  w->glw_refcnt++;
  glw_unlock();
}


/*
 *
 */
void
glw_set_active(glw_t *w)
{
  glw_lock();
  glw_set_active0(w);
  glw_unlock();
}


/*
 *
 */

void
glw_render(glw_t *w, glw_rctx_t *rc)
{
  glw_lock();
  glw_render0(w, rc);
  glw_unlock();
}


/*
 *
 */

void
glw_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_lock();
  glw_layout0(w, rc);
  glw_unlock();
}


/*
 *
 */

glw_t *
glw_create(glw_class_t class, ...)
{
  glw_t *w; 
  va_list ap;

  va_start(ap, class);

  glw_lock();
  w = glw_create0(class, ap);
  glw_unlock();

  va_end(ap);

  return w;
}

/*
 *
 */

void
glw_set(glw_t *w, ...)
{
  va_list ap;

  va_start(ap, w);

  glw_lock();
  glw_attrib_set0(w, 0, ap);
  glw_unlock();

  va_end(ap);
}

/*
 *
 */

void 
glw_destroy(glw_t *w)
{
  glw_lock();
  glw_destroy0(w);
  glw_unlock();
}

/*
 *
 */

void
glw_destroy_childs(glw_t *w)
{
  glw_t *c;

  glw_lock();

  while((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_destroy0(c);

  glw_unlock();
}


/*
 *
 */

 
static glw_t *
glw_find_by_class_r(glw_t *p, glw_class_t class)
{
  glw_t *c, *x;

  if(p->glw_class == class)
    return p;

  TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link) {
    x = glw_find_by_class_r(c, class);
    if(x != NULL)
      return x;
  }

  return NULL;
}

 
glw_t *
glw_find_by_class(glw_t *p, glw_class_t class)
{
  glw_t *r;

  glw_lock();
  r = glw_find_by_class_r(p, class);
  glw_unlock();
  return r;
}


/*
 *
 */
void
glw_hide(glw_t *w)
{
  glw_lock();
  glw_hide0(w);
  glw_unlock();
}


/*
 *
 */
void
glw_unhide(glw_t *w)
{
  glw_lock();
  glw_unhide0(w);
  glw_unlock();
}



/*
 *
 */

int
glw_nav_signal(glw_t *w, glw_signal_t sig)
{
  int r;

  glw_lock();
  r = glw_signal0(w, sig, NULL);
  glw_unlock();
  return r;
}


/*
 *
 */
int
glw_signal(glw_t *w, glw_signal_t sig, void *extra)
{
  int r;

  glw_lock();
  r = glw_signal0(w, sig, extra);
  glw_unlock();
  return r;
}

/*
 *
 */
void *
glw_get_opaque(glw_t *w, glw_callback_t *func)
{
  glw_signal_handler_t *gsh;

  glw_lock();
  
  LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link)
    if(gsh->gsh_func == func)
      break;
  glw_unlock();

  return gsh ? gsh->gsh_opaque : NULL;
}


/**
 *
 */
glw_t *
glw_find_by_id(glw_t *w, const char *id, int deepsearch)
{
  glw_t *r;

  glw_lock();
  r = glw_find_by_id0(w, id, deepsearch);
  glw_unlock();
  return r;
}


/**
 *
 */
void
glw_detach(glw_t *w)
{
  glw_t *p;
  glw_signal_handler_t *gsh;

  glw_lock();

  p = w->glw_parent;
  if(p != NULL) {

    LIST_FOREACH(gsh, &p->glw_signal_handlers, gsh_link)
      if(gsh->gsh_func(p, gsh->gsh_opaque, GLW_SIGNAL_DETACH_CHILD, w))
	break;

    if(gsh == NULL)
      /* Parent does not support detach, destroy child instead */
      glw_destroy0(w);
  }
  glw_unlock();
}

/*
 *
 */

void
glw_vertex_anim_fwd(glw_vertex_anim_t *gva, float v)
{
  gva->gva_i = GLW_MIN(1.0f, gva->gva_i + v);
}

float
glw_vertex_anim_read_i(glw_vertex_anim_t *gva)
{
  return gva->gva_flags & GLW_VERTEX_ANIM_SIN_LERP ? 
    GLW_S(gva->gva_i) : gva->gva_i;
}

void
glw_vertex_anim_read(glw_vertex_anim_t *gva, glw_vertex_t *t)
{
  float i = glw_vertex_anim_read_i(gva);
  
  t->x = GLW_LERP(i, gva->gva_prev.x, gva->gva_next.x);
  t->y = GLW_LERP(i, gva->gva_prev.y, gva->gva_next.y);
  t->z = GLW_LERP(i, gva->gva_prev.z, gva->gva_next.z);
}


void
glw_vertex_anim_set(glw_vertex_anim_t *gva, glw_vertex_t *t)
{
  if(!memcmp(&gva->gva_next, t, sizeof(glw_vertex_t)))
    return;

  glw_vertex_anim_read(gva, &gva->gva_prev);
  memcpy(&gva->gva_next, t, sizeof(glw_vertex_t));
  gva->gva_i = 0;
}

void
glw_vertex_anim_set3f(glw_vertex_anim_t *gva, float x, float y, float z)
{
  glw_vertex_t t = {.x = x, .y = y, .z = z};
  glw_vertex_anim_set(gva, &t);
}

void
glw_vertex_anim_init(glw_vertex_anim_t *gva, float x, float y, float z,
		     int flags)
{
  gva->gva_prev.x = gva->gva_next.x = x;
  gva->gva_prev.y = gva->gva_next.y = y;
  gva->gva_prev.z = gva->gva_next.z = z;
  gva->gva_flags = flags;
}



/**
 *
 */
int
glw_is_selected(glw_t *w)
{
  while(w->glw_parent != NULL) {
    if(w->glw_parent->glw_selected != w)
      return 0;
    w = w->glw_parent;
  }
  return 1;
}


/**
 *
 */
void
glw_set_framerate(float r)
{
  extern float glw_framerate;

  glw_framerate = r;
}
