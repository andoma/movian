/*
 *  GL Widgets, common stuff
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
#include <assert.h>

#include <libhts/htsthreads.h>

#include "glw.h"
#include "glw_container.h"
#include "glw_text.h"
#include "glw_text_bitmap.h"
#include "glw_bitmap.h"
#include "glw_array.h"
#include "glw_cursor.h"
#include "glw_rotator.h"
#include "glw_model.h"
#include "glw_list.h"
#include "glw_deck.h"
#include "glw_expander.h"
#include "glw_slideshow.h"
#include "glw_mirror.h"
#include "glw_animator.h"
#include "glw_fx_texrot.h"
#include "glw_event.h"
#include "glw_video.h"

static const size_t glw_class_to_size[] = {
  [GLW_DUMMY] = sizeof(glw_t),
  [GLW_MODEL] = sizeof(glw_t),
  [GLW_CONTAINER] = sizeof(glw_t),
  [GLW_CONTAINER_X] = sizeof(glw_t),
  [GLW_CONTAINER_Y] = sizeof(glw_t),
  [GLW_CONTAINER_Z] = sizeof(glw_t),
  [GLW_BITMAP]  = sizeof(glw_bitmap_t),
  [GLW_LABEL]  = sizeof(glw_text_bitmap_t),
  [GLW_TEXT]  = sizeof(glw_text_bitmap_t),
  [GLW_INTEGER]  = sizeof(glw_text_bitmap_t),
  [GLW_ROTATOR] = sizeof(glw_t),
  //  [GLW_ARRAY] = sizeof(glw_array_t),
  [GLW_LIST] = sizeof(glw_list_t),
  [GLW_DECK] = sizeof(glw_deck_t),
  [GLW_EXPANDER] = sizeof(glw_t),
  //  [GLW_SLIDESHOW] = sizeof(glw_slideshow_t),
  [GLW_CURSOR] = sizeof(glw_cursor_t),
  [GLW_MIRROR] = sizeof(glw_t),
  [GLW_ANIMATOR] = sizeof(glw_animator_t),
  [GLW_FX_TEXROT] = sizeof(glw_fx_texrot_t),
  [GLW_VIDEO] = sizeof(glw_video_t),
};

/*
 *
 */
void
glw_lock(glw_root_t *gr)
{
  hts_mutex_lock(&gr->gr_mutex);
}

/*
 *
 */
void
glw_unlock(glw_root_t *gr)
{
  hts_mutex_unlock(&gr->gr_mutex);
}


/*
 *
 */
void
glw_cond_wait(glw_root_t *gr, hts_cond_t *c)
{
  hts_cond_wait(c, &gr->gr_mutex);
}


/**
 *
 */
int
glw_init(glw_root_t *gr)
{
  hts_mutex_init(&gr->gr_mutex);
  gr->gr_courier = prop_courier_create(&gr->gr_mutex);

  if(glw_text_init(gr)) {
    free(gr);
    return -1;
  }

  TAILQ_INIT(&gr->gr_focus_childs);

  TAILQ_INIT(&gr->gr_destroyer_queue);
  glw_image_init(gr);

  gr->gr_framerate = 60; /* default until we know better */

  glw_check_system_features(gr);
  return 0;
}
  

/*
 *
 */
int
glw_attrib_set0(glw_t *w, int init, va_list ap)
{
  glw_attribute_t attrib;
  glw_t *p;
  void *v, *o;
  int pri, a, r = 0;
  glw_root_t *gr = w->glw_root;
  glw_focus_mode_t gfm;

  va_list apx;

  va_copy(apx, ap);

  do {
    attrib = va_arg(ap, int);

    switch(attrib) {

    case GLW_ATTRIB_SIGNAL_HANDLER:
      v   = va_arg(ap, void *);
      o   = va_arg(ap, void *);
      pri = va_arg(ap, int);

      if(pri == -1)
	glw_signal_handler_unregister(w, v, o);
      else
	glw_signal_handler_register(w, v, o, pri);
      break;

    case GLW_ATTRIB_PARENT:
    case GLW_ATTRIB_PARENT_HEAD:
      if(w->glw_parent != NULL) {


	glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_DESTROYED, w);

	if(w->glw_parent->glw_selected == w)
	  w->glw_parent->glw_selected = TAILQ_NEXT(w, glw_parent_link);

	if(w->glw_flags & GLW_RENDER_LINKED) {
	  w->glw_flags &= ~GLW_RENDER_LINKED;
	  TAILQ_REMOVE(&w->glw_parent->glw_render_list, w, glw_render_link);
	}

	TAILQ_REMOVE(&w->glw_parent->glw_childs, w, glw_parent_link);

      }
      p = va_arg(ap, void *);

      w->glw_parent = p;
      if(p != NULL) {
	if(attrib == GLW_ATTRIB_PARENT_HEAD) {
	  TAILQ_INSERT_HEAD(&w->glw_parent->glw_childs, w, glw_parent_link);
	} else {
	  TAILQ_INSERT_TAIL(&w->glw_parent->glw_childs, w, glw_parent_link);
	}
	glw_signal0(p, GLW_SIGNAL_CHILD_CREATED, w);
      }
      break;

    case GLW_ATTRIB_WEIGHT:
      w->glw_weight = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ASPECT:
      w->glw_aspect = va_arg(ap, double);
      break;

    case GLW_ATTRIB_CAPTION:
      v = va_arg(ap, char *);
      if(w->glw_caption != NULL) {
	if(!strcmp(w->glw_caption, v ?: ""))
	  break; /* no change */
	free((void *)w->glw_caption);
      }
      w->glw_caption = strdup(v ?: "");
      break;

    case GLW_ATTRIB_ID:
      v = va_arg(ap, char *);
      free((void *)w->glw_id);
      w->glw_id = v ? strdup(v) : NULL;
      break;

    case GLW_ATTRIB_ALPHA:
      w->glw_alpha = va_arg(ap, double);
      break;

    case GLW_ATTRIB_EXTRA:
      w->glw_extra = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ALIGNMENT:
      w->glw_alignment = va_arg(ap, int);
      break;

    case GLW_ATTRIB_SET_FLAGS:
      a = va_arg(ap, int);

      if(a & GLW_EVERY_FRAME && !(w->glw_flags & GLW_EVERY_FRAME)) 
	LIST_INSERT_HEAD(&gr->gr_every_frame_list, w, glw_every_frame_link);

      w->glw_flags |= a;
      break;

    case GLW_ATTRIB_CLR_FLAGS:
      a = va_arg(ap, int);

      if(a & GLW_EVERY_FRAME && (w->glw_flags & GLW_EVERY_FRAME)) 
	LIST_REMOVE(w, glw_every_frame_link);

      w->glw_flags &= ~a;
      break;

    case GLW_ATTRIB_ANGLE:
      w->glw_extra = va_arg(ap, double);
      break;

    case GLW_ATTRIB_DISPLACEMENT:
      w->glw_displacement.x = va_arg(ap, double);
      w->glw_displacement.y = va_arg(ap, double);
      w->glw_displacement.z = va_arg(ap, double);
      break;

    case GLW_ATTRIB_RGB:
      w->glw_col.r = va_arg(ap, double);
      w->glw_col.g = va_arg(ap, double);
      w->glw_col.b = va_arg(ap, double);
      break;

    case GLW_ATTRIB_TIME:
      w->glw_time = va_arg(ap, double);
      break;

    case GLW_ATTRIB_FOCUSABLE:
      gfm = va_arg(ap, int);

      if(w->glw_focus_mode != GLW_FOCUS_NONE) {
	p = w->glw_focus_parent;
	if(p != NULL)
	  TAILQ_REMOVE(&p->glw_focus_childs, w, glw_focus_parent_link);
	else
	  TAILQ_REMOVE(&gr->gr_focus_childs, w, glw_focus_parent_link);
      }

      w->glw_focus_mode = gfm;

      if(gfm != GLW_FOCUS_NONE) {
	/* Find first parent which is a focus leader */
	for(p = w->glw_parent; p != NULL; p = p->glw_parent)
	  if(p->glw_focus_mode == GLW_FOCUS_LEADER_ENABLED ||
	     p->glw_focus_mode == GLW_FOCUS_LEADER_DISABLED)
	    break;

	w->glw_focus_parent = p;

	if(p != NULL)
	  TAILQ_INSERT_TAIL(&p->glw_focus_childs, w, glw_focus_parent_link);
	else
	  TAILQ_INSERT_TAIL(&gr->gr_focus_childs, w, glw_focus_parent_link);
      }
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  /* Per-class attributes are parsed by class-specific parser,
     this function also servers as a per-class constructor */

  switch(w->glw_class) {

  case GLW_CONTAINER_X:
  case GLW_CONTAINER_Y:
  case GLW_CONTAINER_Z:
  case GLW_CONTAINER:
    glw_container_ctor(w, init, apx);
    break;

  case GLW_BITMAP:
    glw_bitmap_ctor(w, init, apx);
    break;

  case GLW_LABEL:
  case GLW_TEXT:
  case GLW_INTEGER:
    glw_text_bitmap_ctor(w, init, apx);
    break;
#if 0
  case GLW_ARRAY:
    glw_array_ctor(w, init, apx);
    break;
#endif
  case GLW_ROTATOR:
    glw_rotator_ctor(w, init, apx);
    break;

  case GLW_LIST:
    glw_list_ctor(w, init, apx);
    break;

  case GLW_DECK:
    glw_deck_ctor(w, init, apx);
    break;

  case GLW_EXPANDER:
    glw_expander_ctor(w, init, apx);
    break;
#if 0
  case GLW_SLIDESHOW:
    glw_slideshow_ctor(w, init, apx);
    break;
#endif
  case GLW_DUMMY:
    break;

  case GLW_CURSOR:
    glw_cursor_ctor(w, init, apx);
    break;

  case GLW_MIRROR:
    glw_mirror_ctor(w, init, apx);
    break;

  case GLW_MODEL:
    glw_model_ctor(w, init, apx);
    break;

  case GLW_ANIMATOR:
    glw_animator_ctor(w, init, apx);
    break;

  case GLW_FX_TEXROT:
    glw_fx_texrot_ctor(w, init, apx);
    break;

  case GLW_VIDEO:
    glw_video_ctor(w, init, apx);
    break;
  }

  va_end(apx);
  return r;
}

/**
 *
 */
glw_t *
glw_create0(glw_root_t *gr, glw_class_t class, va_list ap)
{
  size_t size; 
  glw_t *w; 

  /* Common initializers */

  size = glw_class_to_size[class];
  w = calloc(1, size);
  w->glw_root = gr;
  w->glw_class = class;
  w->glw_alpha = 1.0f;
  w->glw_weight = 1.0f;
  w->glw_col.r = 1.0f;
  w->glw_col.g = 1.0f;
  w->glw_col.b = 1.0f;
  w->glw_time = 1.0f;
  w->glw_alignment = GLW_ALIGN_DEFAULT;
  LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);

  TAILQ_INIT(&w->glw_childs);
  TAILQ_INIT(&w->glw_render_list);
  TAILQ_INIT(&w->glw_focus_childs);

  /* Parse arguments */
  
  if(glw_attrib_set0(w, 1, ap) < 0) {
    glw_destroy0(w);
    return NULL;
  }

  return w;
}

/*
 *
 */

glw_t *
glw_create_i(glw_root_t *gr, glw_class_t class, ...)
{
  glw_t *w; 
  va_list ap;

  va_start(ap, class);
  w = glw_create0(gr, class, ap);
  va_end(ap);
  return w;
}


/**
 *
 */
void
glw_set_i(glw_t *w, ...)
{
  va_list ap;

  va_start(ap, w);
  glw_attrib_set0(w, 0, ap);
  va_end(ap);
}


/**
 *
 */
static void
glw_signal_handler_clean(glw_t *w)
{
  glw_signal_handler_t *gsh;
  while((gsh = LIST_FIRST(&w->glw_signal_handlers)) != NULL) {
    LIST_REMOVE(gsh, gsh_link);
    free(gsh);
  }
}



/**
 *
 */
void
glw_reaper0(glw_root_t *gr)
{
  glw_t *w;

  glw_cursor_layout_frame(gr);

  LIST_FOREACH(w, &gr->gr_every_frame_list, glw_every_frame_link)
    glw_signal0(w, GLW_SIGNAL_NEW_FRAME, NULL);

  while((w = LIST_FIRST(&gr->gr_active_flush_list)) != NULL) {
    LIST_REMOVE(w, glw_active_link);
    LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);
    glw_signal0(w, GLW_SIGNAL_INACTIVE, NULL);
  }

  LIST_MOVE(&gr->gr_active_flush_list, &gr->gr_active_list, glw_active_link);
  LIST_INIT(&gr->gr_active_list);

  glw_texture_purge(gr);

  glw_tex_autoflush(gr);

  while((w = TAILQ_FIRST(&gr->gr_destroyer_queue)) != NULL) {
    TAILQ_REMOVE(&gr->gr_destroyer_queue, w, glw_parent_link);

    glw_signal0(w, GLW_SIGNAL_DTOR, NULL);

    w->glw_flags |= GLW_DESTROYED;
    glw_signal_handler_clean(w);

    if(w->glw_refcnt == 0)
      free(w);
  }
}

/*
 *
 */
void
glw_deref0(glw_t *w)
{
  if(w->glw_refcnt == 1)
    free(w);
  else
    w->glw_refcnt--;
}

/*
 *
 */
void
glw_destroy0(glw_t *w)
{
  glw_t *c, *p;
  glw_root_t *gr = w->glw_root;
  glw_event_map_t *gem;

  glw_prop_subscription_destroy_list(&w->glw_prop_subscriptions);

  while((gem = LIST_FIRST(&w->glw_event_maps)) != NULL) {
    LIST_REMOVE(gem, gem_link);
    gem->gem_dtor(gem);
  }

  free(w->glw_matrix);
  w->glw_matrix = NULL;
  
  if(w->glw_focus_mode != GLW_FOCUS_NONE) {
    p = w->glw_focus_parent;
    if(p != NULL)
      TAILQ_REMOVE(&p->glw_focus_childs, w, glw_focus_parent_link);
    else
      TAILQ_REMOVE(&gr->gr_focus_childs, w, glw_focus_parent_link);
  }

  if(w->glw_flags & GLW_EVERY_FRAME)
    LIST_REMOVE(w, glw_every_frame_link);

  if(w->glw_flags & GLW_RENDER_LINKED)
    TAILQ_REMOVE(&w->glw_parent->glw_childs, w, glw_render_link);

  LIST_REMOVE(w, glw_active_link);

  while((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_destroy0(c);

  glw_signal0(w, GLW_SIGNAL_DESTROY, NULL);

  if((p = w->glw_parent) != NULL) {
    /* Some classes needs to do some stuff is a child is destroyed */
    glw_signal0(p, GLW_SIGNAL_CHILD_DESTROYED, w);

    if(p->glw_selected == w)
      p->glw_selected = TAILQ_NEXT(w, glw_parent_link);

    TAILQ_REMOVE(&p->glw_childs, w, glw_parent_link);
  }

  if(w->glw_caption != NULL)
    free((void *)w->glw_caption);

  free((void *)w->glw_id);

  TAILQ_INSERT_TAIL(&gr->gr_destroyer_queue, w, glw_parent_link);

  glw_model_free_chain(w->glw_dynamic_expressions);
}


/*
 *
 */
void
glw_set_active0(glw_t *w)
{
  glw_root_t *gr = w->glw_root;
  LIST_REMOVE(w, glw_active_link);
  LIST_INSERT_HEAD(&gr->gr_active_list, w, glw_active_link);
}

/*
 *
 */
void
glw_signal_handler_register(glw_t *w, glw_callback_t *func, void *opaque, 
			    int pri)
{
  glw_signal_handler_t *gsh, *p = NULL;

  LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link) {
    if(gsh->gsh_func == func) {
      gsh->gsh_opaque = opaque;
      return;
    }
    if(gsh->gsh_pri < pri)
      p = gsh;
  } 

  gsh = malloc(sizeof(glw_signal_handler_t));
  gsh->gsh_func   = func;
  gsh->gsh_opaque = opaque;
  gsh->gsh_pri    = pri;

  if(p == NULL) {
    LIST_INSERT_HEAD(&w->glw_signal_handlers, gsh, gsh_link);
  } else {
    LIST_INSERT_AFTER(p, gsh, gsh_link);
  }
}

/*
 *
 */
void
glw_signal_handler_unregister(glw_t *w, glw_callback_t *func, void *opaque)
{
  glw_signal_handler_t *gsh;

  LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link)
    if(gsh->gsh_func == func && gsh->gsh_opaque == opaque)
      break;
  
  if(gsh != NULL) {
    LIST_REMOVE(gsh, gsh_link);
    free(gsh);
  }
}

/**
 *
 */
glw_t *
glw_get_prev_n(glw_t *c, int count)
{
  glw_t *t = c;
  int i;
  c = NULL;
  for(i = 0; i < count; i++) {
    if((t = TAILQ_PREV(t, glw_queue, glw_parent_link)) == NULL)
      break;
    c = t;
  }
  return c;
}


/**
 *
 */
glw_t *
glw_get_next_n(glw_t *c, int count)
{
  glw_t *t = c;
  int i;

  c = NULL;

  for(i = 0; i < count; i++) {
    if((t = TAILQ_NEXT(t, glw_parent_link)) == NULL)
      break;
    c = t;
  }
  return c;
}


/**
 *
 */
glw_t *
glw_get_prev_n_all(glw_t *c, int count)
{
  int i;
  for(i = 0; i < count; i++) {
    if((c = TAILQ_PREV(c, glw_queue, glw_parent_link)) == NULL)
      break;
  }
  return c;
}


/**
 *
 */
glw_t *
glw_get_next_n_all(glw_t *c, int count)
{
  int i;
  for(i = 0; i < count; i++) {
    if((c = TAILQ_NEXT(c, glw_parent_link)) == NULL)
      break;
  }
  return c;
}


int
glw_signal0(glw_t *w, glw_signal_t sig, void *extra)
{
  glw_signal_handler_t *gsh, *next;
  for(gsh = LIST_FIRST(&w->glw_signal_handlers); gsh != NULL; gsh = next) {
    next = LIST_NEXT(gsh, gsh_link);
    if(gsh->gsh_func(w, gsh->gsh_opaque, sig, extra))
      return 1;
  }
  return 0;
}

/**
 *
 */

static LIST_HEAD(, glw_gf_ctrl) ggcs;

void
glw_gf_register(glw_gf_ctrl_t *ggc)
{
  LIST_INSERT_HEAD(&ggcs, ggc, link);
}

void
glw_gf_unregister(glw_gf_ctrl_t *ggc)
{
  LIST_REMOVE(ggc, link);
}

void
glw_gf_do(void)
{
  glw_gf_ctrl_t *ggc;
  LIST_FOREACH(ggc, &ggcs, link)
    ggc->flush(ggc->opaque);
}




/**
 *
 */
void
glw_flush0(glw_root_t *gr)
{
  glw_gf_do();
  glw_tex_flush_all(gr);
  glw_text_flush(gr);
}


/**
 *
 */
void
glw_detach0(glw_t *w)
{
  glw_t *p;
  glw_signal_handler_t *gsh;

  p = w->glw_parent;
  if(p != NULL) {

    LIST_FOREACH(gsh, &p->glw_signal_handlers, gsh_link)
      if(gsh->gsh_func(p, gsh->gsh_opaque, GLW_SIGNAL_DETACH_CHILD, w))
	break;

    if(gsh == NULL)
      /* Parent does not support detach, destroy child instead */
      glw_destroy0(w);
  }
}


/**
 *
 */
void
glw_store_matrix(glw_t *w, glw_rctx_t *rc)
{
  glw_cursor_painter_t *gcp = rc->rc_cursor_painter;
  if(w->glw_matrix == NULL)
    w->glw_matrix = malloc(sizeof(float) * 16);
  
  glGetFloatv(GL_MODELVIEW_MATRIX, w->glw_matrix);
  
  if(glw_is_focused(w) && gcp != NULL) {
    gcp->gcp_alpha  = rc->rc_alpha;
    gcp->gcp_aspect = rc->rc_aspect;
    memcpy(gcp->gcp_m, w->glw_matrix, 16 * sizeof(float));
  }
}



/**
 *
 */
int
glw_is_focused(glw_t *w)
{
  glw_t *p;
  if(!glw_is_focusable(w))
    return 0;

  while(1) {
    p = w->glw_focus_parent;

    if(p == NULL)
      return TAILQ_FIRST(&w->glw_root->gr_focus_childs) == w;

    if(TAILQ_FIRST(&p->glw_focus_childs) != w)
      return 0;
    w = p;
  }
}

/**
 * Return the child of 'w' which in turn has one of its childs focused
 */
glw_t *
glw_get_indirectly_focused_child(glw_t *w)
{
  glw_t *l, *c;

  /* Find closest focus leader */
  for(l = w; l != NULL; l = l->glw_parent) {
    if(l->glw_focus_mode == GLW_FOCUS_LEADER_ENABLED ||
       l->glw_focus_mode == GLW_FOCUS_LEADER_DISABLED)
      break;
  }

  if(l == NULL)
    return l;

  /* Find widgets currently focused by the leader */
  while(1) {
    c = TAILQ_FIRST(&l->glw_focus_childs);
    if(c == NULL || c->glw_focus_mode == GLW_FOCUS_TARGET)
      break;
    l = c;
  }

  /* Trace back and se if we hit the list */
  while(c != NULL) {
    if(c->glw_parent == w)
      return c;
    c = c->glw_parent;
  }
  return NULL;
}



/**
 *
 */
void
glw_focus_set(glw_t *w)
{
  glw_t *l;
  struct glw_queue *q;

  while(1) {

    assert(w->glw_focus_mode != GLW_FOCUS_NONE);

    l = w->glw_focus_parent;
    
    q = l != NULL ? &l->glw_focus_childs : &w->glw_root->gr_focus_childs;

    TAILQ_REMOVE(q, w, glw_focus_parent_link);
    TAILQ_INSERT_HEAD(q, w, glw_focus_parent_link);

    if(l == NULL)
      break;

    w = l;
  }
}

/**
 *
 */
int
glw_event_to_widget(glw_t *w, event_t *e)
{
  if(glw_event_map_intercept(w, e))
    return 1;

  if(glw_signal0(w, GLW_SIGNAL_EVENT, e))
    return 1;

  return glw_navigate(w, e);
}


/**
 *
 */
int
glw_event(glw_root_t *gr, event_t *e)
{
  glw_t *w;

  if((w = TAILQ_FIRST(&gr->gr_focus_childs)) == NULL)
    return 0;

  while(1) {
    if(w->glw_focus_mode == GLW_FOCUS_TARGET)
      break;
    if(w->glw_focus_mode == GLW_FOCUS_LEADER_ENABLED) {
      w = TAILQ_FIRST(&w->glw_focus_childs);
      if(w == NULL)
	return 0;
    }
  }
  return glw_event_to_widget(w, e);
}



static glw_t *
pointer_scan(glw_t *w, float x, float y)
{
  glw_t *c, *r;
  float *m;
  float x1, x2, y1, y2;

  switch(w->glw_focus_mode) {
  case GLW_FOCUS_NONE:
  case GLW_FOCUS_LEADER_DISABLED:
    return NULL;

  case GLW_FOCUS_LEADER_ENABLED:
    TAILQ_FOREACH(c, &w->glw_focus_childs, glw_focus_parent_link)
      if((r = pointer_scan(c, x, y)) != NULL)
	return r;
    return NULL;

  case GLW_FOCUS_TARGET:

    if(w->glw_focus_mode == GLW_FOCUS_TARGET && (m = w->glw_matrix) != NULL) {
      
      x1 = m[12] - m[0];
      x2 = m[12] + m[0];

      y1 = m[13] - m[5];
      y2 = m[13] + m[5];
      
      if(x >= x1 && x <= x2 && y >= y1 && y <= y2) {
	glw_focus_set(w);
	return w;
      }
    }
    break;
  }
  return NULL;
}


/**
 *
 */
glw_t *
glw_pointer_motion(glw_root_t *gr, float x, float y)
{
  glw_t *c, *r;

  TAILQ_FOREACH(c, &gr->gr_focus_childs, glw_focus_parent_link)
    if((r = pointer_scan(c, x, y)) != NULL)
      return r;
  return NULL;
}
