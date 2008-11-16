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

/**
 *
 */
int
glw_get_text(glw_t *w, char *buf, size_t buflen)
{
  int r;
  glw_lock();
  r = glw_get_text0(w, buf, buflen);
  glw_unlock();
  return r;
}
/**
 *
 */
int
glw_get_int(glw_t *w, int *result)
{
  int r;
  glw_lock();
  r = glw_get_int0(w, result);
  glw_unlock();
  return r;
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
void
glw_get_caption(glw_t *w, const char *id, char *buf, size_t buflen)
{
  glw_lock();

  if((w = glw_find_by_id0(w, id, 0)) != NULL)
    glw_get_text0(w, buf, buflen);
  else
    buf[0] = 0;
  glw_unlock();
}

/**
 *
 */
int
glw_get_model(glw_t *w, const char *id, char *buf, size_t buflen)
{
  glw_t *c;
  buf[0] = 0;

  glw_lock();

  if((w = glw_find_by_id0(w, id, 0)) != NULL) {
    c = w->glw_selected;
    if(c != NULL && c->glw_class == GLW_MODEL)
      snprintf(buf, buflen, "%s", c->glw_caption);
  }
  glw_unlock();
  return 0;
}




/**
 *
 */
glw_event_t *
glw_wait_form(glw_t *root)
{
  glw_event_t *ge;
  glw_event_queue_t geq;

  glw_event_initqueue(&geq);
  
  glw_set(root,
	  GLW_ATTRIB_SIGNAL_HANDLER, glw_event_enqueuer, &geq, 1000, 
	  NULL);

  while(1) {
    ge = glw_event_get(-1, &geq);
    if(ge->ge_type == GEV_OK ||
       ge->ge_type == GEV_CANCEL)
      break;
    glw_event_unref(ge);
  }

  glw_event_flushqueue(&geq);

  glw_set(root,
	  GLW_ATTRIB_SIGNAL_HANDLER, glw_event_enqueuer, &geq, -1,
	  NULL);
  return ge;
}



/**
 *
 */
int
glw_wait_form_ok_cancel(glw_t *root)
{
  glw_event_t *ge = glw_wait_form(root);
  int r = ge->ge_type == GEV_OK ? 0 : -1;
  glw_event_unref(ge);
  return r;
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
typedef struct glw_option_data {
  void (*cb)(void *opaque, void *opaque2, int value);
  void *opaque;
  void *opaque2;
  int value;
} glw_option_data_t;


/**
 *
 */
static int
glw_selection_option_cb(glw_t *w, void *opaque, glw_signal_t sig, void *ex)
{
  glw_option_data_t *d = opaque;
  glw_event_t *ge;

  switch(sig) {
  default:
    break;

  case GLW_SIGNAL_DTOR:
    free(d);
    return 0;

  case GLW_SIGNAL_EVENT:
    ge = ex;
    if(ge->ge_type == GEV_ENTER) {
      if(d->cb != NULL) 
	d->cb(d->opaque, d->opaque2, d->value);

      return 1;
    }
    break;
  }
  return 0;
}
/**
 *
 */
glw_t *
glw_selection_add_text_option(glw_t *opt, const char *caption,
			      void (*cb)(void *opaque, void *opaque2, 
					 int value),
			      void *opaque, void *opaque2, 
			      int value, int selected)
{
  glw_option_data_t *d = malloc(sizeof(glw_option_data_t));
  glw_t *w;
  char *x, *y;

  d->opaque = opaque;
  d->opaque2 = opaque2;
  d->value = value;
  d->cb = cb;

  if(strchr(caption, '\n')) {
    x = strdup(caption);

    w =
      glw_create(GLW_CONTAINER_Y, 
		 GLW_ATTRIB_PARENT, opt,
		 GLW_ATTRIB_FLAGS, GLW_FOCUS_ADJ_ALPHA | GLW_FOCUS_DRAW_CURSOR,
		 GLW_ATTRIB_SIGNAL_HANDLER, glw_selection_option_cb, d, 1000,
		 NULL);
    y = strchr(x, '\n');
    *y = 0;
    y++;
    
    glw_create(GLW_LABEL,
	       GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_CAPTION, x,
	       NULL);

    glw_create(GLW_LABEL,
	       GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_CAPTION, y,
	       NULL);
    free(x);
  } else {
    w = 
      glw_create(GLW_LABEL, 
		 GLW_ATTRIB_PARENT, opt,
		 GLW_ATTRIB_FLAGS, GLW_FOCUS_ADJ_ALPHA | GLW_FOCUS_DRAW_CURSOR,
		 GLW_ATTRIB_CAPTION, caption,
		 GLW_ATTRIB_SIGNAL_HANDLER, glw_selection_option_cb, d, 1000,
		 NULL);
  }

  if(selected)
    opt->glw_selected = w;

  return w;
}

/**
 *
 */
glw_t *
glw_selection_get_widget_by_opaque(glw_t *parent, void *opaque)
{
  glw_t *c;
  glw_option_data_t *d;
  glw_lock();

  TAILQ_FOREACH(c, &parent->glw_childs, glw_parent_link) {
    if((d = glw_get_opaque(c, glw_selection_option_cb)) != NULL)
      if(opaque == d->opaque)
	break;
  }

  glw_unlock();
  return c;
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
