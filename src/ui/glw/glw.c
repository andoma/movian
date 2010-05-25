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

#include <arch/threads.h>

#include "glw.h"
#include "glw_text_bitmap.h"
#include "glw_texture.h"
#include "glw_view.h"
#include "glw_event.h"

#if CONFIG_GLW_BACKEND_OPENGL
#include "glw_video_opengl.h"
#endif

static void glw_focus_init_widget(glw_t *w, float weight);
static void glw_focus_leave(glw_t *w);
static void glw_root_set_hover(glw_root_t *gr, glw_t *w);

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
static int
top_event_handler(glw_t *w, void *opaque, glw_signal_t sig, void *extra)
{
  event_t *e = extra;

  if(sig != GLW_SIGNAL_EVENT_BUBBLE)
    return 0;

  event_dispatch(e);
  return 1;
}



/**
 * Save settings
 */
void 
glw_settings_save(void *opaque, htsmsg_t *msg)
{
  glw_root_t *gr = opaque;

  assert(msg == gr->gr_settings_store);
  htsmsg_store_save(msg, "displays/%s", gr->gr_settings_instance);
}

/**
 *
 */
static void
glw_init_settings(glw_root_t *gr, const char *instance,
		  const char *instance_title)
{
  char title[256];

  gr->gr_settings_instance = strdup(instance);

  gr->gr_settings_store = htsmsg_store_load("displays/%s", instance);
  
  if(gr->gr_settings_store == NULL)
    gr->gr_settings_store = htsmsg_create_map();

  if(instance_title) {
    snprintf(title, sizeof(title), "Display settings on screen %s",
	     instance_title);
  } else {
    snprintf(title, sizeof(title), "Display settings");
  }

  gr->gr_settings = settings_add_dir(NULL, "display", title, "display");

  gr->gr_setting_fontsize =
    settings_create_int(gr->gr_settings, "fontsize",
			"Font size", 20, gr->gr_settings_store, 14, 40, 1,
			glw_font_change_size, gr,
			SETTINGS_INITIAL_UPDATE, "px", gr->gr_courier,
			glw_settings_save, gr);

  prop_link(settings_get_value(gr->gr_setting_fontsize),
	    prop_create(gr->gr_uii.uii_prop, "fontsize"));

  gr->gr_pointer_visible = 
    prop_create(gr->gr_uii.uii_prop, "pointerVisible");
}

/**
 *
 */
int
glw_init(glw_root_t *gr, const char *theme, ui_t *ui, int primary,
	 const char *instance, const char *instance_title)
{

  hts_mutex_init(&gr->gr_mutex);
  gr->gr_courier = prop_courier_create_passive();
  gr->gr_theme = theme;

  gr->gr_uii.uii_ui = ui;
  gr->gr_uii.uii_prop = prop_create(NULL, "ui");

  if(glw_text_bitmap_init(gr))
    return -1;

  glw_init_settings(gr, instance, instance_title);

  TAILQ_INIT(&gr->gr_destroyer_queue);
  glw_tex_init(gr);

  gr->gr_frameduration = 1000000 / 60;
  uii_register(&gr->gr_uii, primary);

  return 0;
}


/**
 *
 */
void
glw_load_universe(glw_root_t *gr)
{
  glw_view_cache_flush(gr);

  if(gr->gr_universe != NULL)
    glw_destroy(gr->gr_universe);

  glw_flush(gr);

  gr->gr_universe = glw_view_create(gr,
				    "theme://universe.view", NULL, NULL,
				    NULL, 0);

  glw_set_i(gr->gr_universe,
	    GLW_ATTRIB_SIGNAL_HANDLER, top_event_handler, gr, 1000,
	    NULL);
}

/**
 *
 */
static void
update_in_path(glw_t *w)
{
  glw_root_t *gr = w->glw_root;
  int f = 0;
  glw_t *p;

  for(p = w->glw_parent; p != NULL; p = p->glw_parent) {
    if(p == gr->gr_current_focus) f |= GLW_IN_FOCUS_PATH;
    if(p == gr->gr_pointer_hover) f |= GLW_IN_HOVER_PATH;
    if(p == gr->gr_pointer_press) f |= GLW_IN_PRESSED_PATH;
  }
  w->glw_flags |= f;
}


/**
 *
 */
int
glw_attrib_set(glw_t *w, int init, va_list ap)
{
  glw_attribute_t attrib;
  glw_t *p, *b;
  void *v, *o;
  int pri, a, r = 0;
  float f;

  va_list apx;

  va_copy(apx, ap);

  do {
    attrib = va_arg(ap, int);
    
    assert(attrib >= 0 && attrib < GLW_ATTRIB_num);

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
    case GLW_ATTRIB_PARENT_BEFORE:
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
	
	update_in_path(w);

	if(attrib == GLW_ATTRIB_PARENT_BEFORE) {
	  b = va_arg(ap, void *);
	  if(b == NULL) {
	    TAILQ_INSERT_TAIL(&w->glw_parent->glw_childs, w, glw_parent_link);
	  } else {
	    TAILQ_INSERT_BEFORE(b, w, glw_parent_link);
	  }

	} else if(attrib == GLW_ATTRIB_PARENT_HEAD) {
	  TAILQ_INSERT_HEAD(&w->glw_parent->glw_childs, w, glw_parent_link);
	} else {
	  TAILQ_INSERT_TAIL(&w->glw_parent->glw_childs, w, glw_parent_link);
	}
	glw_signal0(p, GLW_SIGNAL_CHILD_CREATED, w);
      }
      break;

    case GLW_ATTRIB_WEIGHT:
      glw_set_constraints(w, 0, 0, 0, va_arg(ap, double), GLW_CONSTRAINT_W,
			  GLW_CONSTRAINT_CONF_WAF);
      break;

    case GLW_ATTRIB_ASPECT:
      glw_set_constraints(w, 0, 0, va_arg(ap, double), 0, GLW_CONSTRAINT_A,
			  GLW_CONSTRAINT_CONF_WAF);
      break;

    case GLW_ATTRIB_WIDTH:
      glw_set_constraints(w, 
			  va_arg(ap, double), 
			  w->glw_req_size_y, 
			  0, 0, 
			  GLW_CONSTRAINT_X | 
			  (w->glw_flags & GLW_CONSTRAINT_CONF_XY ?
			   w->glw_flags & GLW_CONSTRAINT_Y : 0),
			  GLW_CONSTRAINT_CONF_XY);
      break;

    case GLW_ATTRIB_HEIGHT:
      glw_set_constraints(w, 
			  w->glw_req_size_x, 
			  va_arg(ap, double),
			  0, 0, 
			  GLW_CONSTRAINT_Y | 
			  (w->glw_flags & GLW_CONSTRAINT_CONF_XY ?
			   w->glw_flags & GLW_CONSTRAINT_X : 0),
			  GLW_CONSTRAINT_CONF_XY);
      break;

    case GLW_ATTRIB_ID:
      v = va_arg(ap, char *);
      free((void *)w->glw_id);
      w->glw_id = v ? strdup(v) : NULL;
      break;

    case GLW_ATTRIB_ALPHA:
      w->glw_alpha = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ALIGNMENT:
      w->glw_alignment = va_arg(ap, int);
      break;

    case GLW_ATTRIB_SET_FLAGS:
      a = va_arg(ap, int);

      a &= ~w->glw_flags; // Mask out already set flags


      w->glw_flags |= a;

      if(a & GLW_HIDDEN)
	glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_HIDDEN, w);

      break;

    case GLW_ATTRIB_CLR_FLAGS:
      a = va_arg(ap, int);

      a &= w->glw_flags; // Mask out already cleared flags

      w->glw_flags &= ~a;

      if(a & GLW_HIDDEN)
	glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_UNHIDDEN, w);
      break;

    case GLW_ATTRIB_FOCUS_WEIGHT:
      f = va_arg(ap, double);

      if(f == w->glw_focus_weight)
	break;

      if(w->glw_focus_weight > 0 && w->glw_root->gr_current_focus == w)
	glw_focus_leave(w);

      if(f > 0)
	glw_focus_init_widget(w, f);
      else
	w->glw_focus_weight = 0;
      break;

    case GLW_ATTRIB_ORIGINATING_PROP:
      assert(w->glw_originating_prop == NULL);
      w->glw_originating_prop = va_arg(ap, void *);
      if(w->glw_originating_prop != NULL)
	prop_ref_inc(w->glw_originating_prop);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(w->glw_class->gc_signal_handler != NULL)
    glw_signal_handler_int(w, w->glw_class->gc_signal_handler);

  if(w->glw_class->gc_set != NULL)
    w->glw_class->gc_set(w, init, apx);

  va_end(apx);
  return r;
}

/**
 *
 */
glw_t *
glw_create(glw_root_t *gr, const glw_class_t *class, va_list ap)
{
  glw_t *w; 

  /* Common initializers */
  w = calloc(1, class->gc_instance_size);
  w->glw_root = gr;
  w->glw_class = class;
  w->glw_alpha = 1.0f;
  w->glw_refcnt = 1;
  w->glw_alignment = class->gc_default_alignment;

  LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);

  if(class->gc_newframe != NULL)
    LIST_INSERT_HEAD(&gr->gr_every_frame_list, w, glw_every_frame_link);

  TAILQ_INIT(&w->glw_childs);
  TAILQ_INIT(&w->glw_render_list);

  /* Parse arguments */
  
  if(glw_attrib_set(w, 1, ap) < 0) {
    glw_destroy(w);
    return NULL;
  }

  return w;
}

/*
 *
 */

glw_t *
glw_create_i(glw_root_t *gr, const glw_class_t *class, ...)
{
  glw_t *w; 
  va_list ap;

  va_start(ap, class);
  w = glw_create(gr, class, ap);
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
  glw_attrib_set(w, 0, ap);
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
glw_prepare_frame(glw_root_t *gr)
{
  glw_t *w;

  gr->gr_frame_start = showtime_get_ts();
  gr->gr_frames++;

  prop_courier_poll(gr->gr_courier);

  //  glw_cursor_layout_frame(gr);

  LIST_FOREACH(w, &gr->gr_every_frame_list, glw_every_frame_link)
    w->glw_class->gc_newframe(w);

  while((w = LIST_FIRST(&gr->gr_active_flush_list)) != NULL) {
    LIST_REMOVE(w, glw_active_link);
    LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);
    w->glw_flags &= ~GLW_ACTIVE;
    glw_signal0(w, GLW_SIGNAL_INACTIVE, NULL);
  }

  LIST_MOVE(&gr->gr_active_flush_list, &gr->gr_active_list, glw_active_link);
  LIST_INIT(&gr->gr_active_list);

  glw_tex_purge(gr);

  glw_tex_autoflush(gr);

  while((w = TAILQ_FIRST(&gr->gr_destroyer_queue)) != NULL) {
    TAILQ_REMOVE(&gr->gr_destroyer_queue, w, glw_parent_link);

    if(w->glw_class->gc_dtor != NULL)
      w->glw_class->gc_dtor(w);

    glw_signal_handler_clean(w);

    glw_unref(w);
  }

  if(gr->gr_mouse_valid) {
    glw_pointer_event_t gpe;

    gpe.x = gr->gr_mouse_x;
    gpe.y = gr->gr_mouse_y;
    gpe.type = GLW_POINTER_MOTION_REFRESH;
    glw_pointer_event(gr, &gpe);
  }

}

/*
 *
 */
void
glw_unref(glw_t *w)
{
  if(w->glw_refcnt == 1)
    free(w);
  else
    w->glw_refcnt--;
}


/**
 *
 */
void
glw_remove_from_parent(glw_t *w, glw_t *p)
{
  assert(w->glw_parent == p);
  glw_focus_leave(w);

  if(p->glw_focused == w)
    p->glw_focused = NULL;

  assert(w->glw_root->gr_current_focus != w);

  if(p->glw_selected == w)
    p->glw_selected = TAILQ_NEXT(w, glw_parent_link);
  
  TAILQ_REMOVE(&p->glw_childs, w, glw_parent_link);
  w->glw_parent = NULL;
}


/**
 *
 */
void
glw_destroy_subscriptions(glw_t *w)
{
  glw_t *c;
  glw_prop_subscription_destroy_list(&w->glw_prop_subscriptions);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_destroy_subscriptions(c);
}


/**
 *
 */
void
glw_destroy(glw_t *w)
{
  glw_t *c, *p;
  glw_root_t *gr = w->glw_root;
  glw_event_map_t *gem;

  w->glw_flags |= GLW_DESTROYING;

  if(w->glw_originating_prop != NULL)
    prop_ref_dec(w->glw_originating_prop);

  if(gr->gr_pointer_grab == w)
    gr->gr_pointer_grab = NULL;

  if(gr->gr_pointer_hover == w)
    glw_root_set_hover(gr, NULL);

  if(gr->gr_pointer_press == w)
    gr->gr_pointer_press = NULL;

  glw_prop_subscription_destroy_list(&w->glw_prop_subscriptions);

  while((gem = LIST_FIRST(&w->glw_event_maps)) != NULL) {
    LIST_REMOVE(gem, gem_link);
    gem->gem_dtor(gem);
  }

  free(w->glw_matrix);
  w->glw_matrix = NULL;
  
  if(w->glw_class->gc_newframe != NULL)
    LIST_REMOVE(w, glw_every_frame_link);

  if(w->glw_flags & GLW_RENDER_LINKED)
    TAILQ_REMOVE(&w->glw_parent->glw_childs, w, glw_render_link);

  LIST_REMOVE(w, glw_active_link);

  while((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_destroy(c);

  glw_signal0(w, GLW_SIGNAL_DESTROY, NULL);

  if((p = w->glw_parent) != NULL) {
    /* Some classes needs to do some stuff is a child is destroyed */

    if(!(p->glw_flags & GLW_DESTROYING))
      glw_signal0(p, GLW_SIGNAL_CHILD_DESTROYED, w);

    glw_remove_from_parent(w, p);
  }

  free((void *)w->glw_id);

  TAILQ_INSERT_TAIL(&gr->gr_destroyer_queue, w, glw_parent_link);

  glw_view_free_chain(w->glw_dynamic_expressions);
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
    if(gsh->gsh_func == func && gsh->gsh_opaque == opaque)
      return;

    if(gsh->gsh_pri < pri)
      p = gsh;
  } 

  gsh = malloc(sizeof(glw_signal_handler_t));
  gsh->gsh_func   = func;
  gsh->gsh_opaque = opaque;
  gsh->gsh_pri    = pri;
  gsh->gsh_defer_remove = 0;

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
    if(gsh->gsh_defer_remove) {
      gsh->gsh_func = NULL;
      gsh->gsh_opaque = NULL;

    } else {
      LIST_REMOVE(gsh, gsh_link);
      free(gsh);
    }
  }
}


/**
 *
 */
int
glw_signal0(glw_t *w, glw_signal_t sig, void *extra)
{
  glw_signal_handler_t *x, *gsh = LIST_FIRST(&w->glw_signal_handlers);
  int r;

  while(gsh != NULL) {
    if(gsh->gsh_func != NULL) {
      gsh->gsh_defer_remove = 1;

      r = gsh->gsh_func(w, gsh->gsh_opaque, sig, extra);

      if(gsh->gsh_func == NULL) {
	/* Was inteded to be removed during call */
	
	x = gsh;
	gsh = LIST_NEXT(gsh, gsh_link);

	LIST_REMOVE(x, gsh_link);
	free(x);
	continue;
      }

      if(r)
	return 1;
    }
    gsh = LIST_NEXT(gsh, gsh_link);
  }
  return 0;
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
glw_flush(glw_root_t *gr)
{
  glw_gf_do();
  glw_tex_flush_all(gr);
  glw_text_flush(gr);
}


/**
 *
 */
void
glw_detach(glw_t *w)
{
  glw_t *p = w->glw_parent;
  if(p != NULL && p->glw_class->gc_detach != NULL) {
    p->glw_class->gc_detach(p, w);
    return;
  }
  glw_destroy(w);
}


/**
 *
 */
void
glw_move(glw_t *w, glw_t *b)
{
  TAILQ_REMOVE(&w->glw_parent->glw_childs, w, glw_parent_link);

  if(b == NULL) {
    TAILQ_INSERT_TAIL(&w->glw_parent->glw_childs, w, glw_parent_link);
  } else {
    TAILQ_INSERT_BEFORE(b, w, glw_parent_link);
  }
}


/**
 *
 */
static void
glw_path_flood(glw_t *w, int or, int and)
{
  glw_t *c;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    glw_path_flood(c, or, and);
    c->glw_flags = (c->glw_flags | or) & and;
    if(!(c->glw_flags & GLW_DESTROYING))
      glw_signal0(c, GLW_SIGNAL_FHP_PATH_CHANGED, NULL);
  }
}


/**
 *
 */
static void
glw_path_modify(glw_t *w, int set, int clr, glw_t *stop)
{
  clr = ~clr; // Invert so we can just AND it

  glw_path_flood(w, set, clr);

  for(; w != NULL && w != stop; w = w->glw_parent) {
    w->glw_flags = (w->glw_flags | set) & clr;
    if(!(w->glw_flags & GLW_DESTROYING))
      glw_signal0(w, GLW_SIGNAL_FHP_PATH_CHANGED, NULL);
  }
}


/**
 *
 */
static glw_t *
find_common_ancestor(glw_t *a, glw_t *b)
{
  glw_t *c;

  if(a == NULL)
    return NULL;

  for(; b != NULL; b = b->glw_parent)
    for(c = a; c != NULL; c = c->glw_parent)
      if(c == b)
	return b;
  return NULL;
}


/**
 *
 */
static void
glw_root_set_hover(glw_root_t *gr, glw_t *w)
{
  glw_t *com;

  if(gr->gr_pointer_hover == w)
    return;

  com = find_common_ancestor(gr->gr_pointer_hover, w);

  if(gr->gr_pointer_hover != NULL)
    glw_path_modify(gr->gr_pointer_hover, 0, GLW_IN_HOVER_PATH, com);

  gr->gr_pointer_hover = w;
  if(w != NULL)
    glw_path_modify(w, GLW_IN_HOVER_PATH, 0, com);
}


/**
 *
 */
static int
glw_path_in_focus(glw_t *w)
{
  return !!(w->glw_flags & GLW_IN_FOCUS_PATH);
}


/**
 *
 */
glw_t *
glw_focus_by_path(glw_t *w)
{
  while(w->glw_focused != NULL) {
    if(w->glw_focused->glw_flags & (GLW_FOCUS_BLOCKED | GLW_DESTROYING))
      return NULL;
    w = w->glw_focused;
  }
  return w;
}


/**
 *
 */
static prop_t *
get_originating_prop(glw_t *w)
{
  for(; w != NULL; w = w->glw_parent)
    if(w->glw_originating_prop != NULL)
      return w->glw_originating_prop;
  return NULL;
}


/**
 *
 */
void
glw_focus_set(glw_root_t *gr, glw_t *w, int interactive)
{
  glw_t *x, *y, *com;
  glw_signal_t sig;
  float weight = w ? w->glw_focus_weight : 0;

  if(interactive) {
    sig = GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE;
  } else {
    sig = GLW_SIGNAL_FOCUS_CHILD_AUTOMATIC;
  }

  if(w != NULL) {

    for(x = w; x->glw_parent != NULL; x = x->glw_parent) {

      if(!interactive && x->glw_flags & GLW_FOCUS_BLOCKED)
	return;

      if(x->glw_parent->glw_focused != x) {
	/* Path switches */
      
	y = glw_focus_by_path(x->glw_parent);
      
	if(y == NULL || interactive || weight > y->glw_focus_weight) {
	  x->glw_parent->glw_focused = x;
	  glw_signal0(x->glw_parent, sig, x);
	} else {
	  /* Other path outranks our weight, stop now */
	  return;
	}
      }
    }
  }

  if(gr->gr_current_focus == w)
    return;

  com = find_common_ancestor(gr->gr_current_focus, w);

  if(gr->gr_current_focus != NULL)
    glw_path_modify(gr->gr_current_focus, 0, GLW_IN_FOCUS_PATH, com);

  gr->gr_current_focus = w;
  if(w == NULL)
    return;

  glw_path_modify(w, GLW_IN_FOCUS_PATH, 0, com);

  
  if(interactive) {
    prop_t *p = get_originating_prop(w);

    if(p != NULL) {
    
      if(gr->gr_last_focused_interactive != NULL)
	prop_ref_dec(gr->gr_last_focused_interactive);

      gr->gr_last_focused_interactive = p;
      prop_ref_inc(p);
    }
  }
}

/**
 *
 */
static int
was_interactive(glw_t *w)
{
  prop_t *last = w->glw_root->gr_last_focused_interactive;

  if(last == NULL)
    return 0;

  if(w->glw_originating_prop == last)
    return 1;

  while((w = w->glw_parent) != NULL) {
    if(w->glw_focus_weight)
      return 0;
    if(w->glw_originating_prop == last)
      return 1;
  }
  return 0;
}


/**
 *
 */
static void
glw_focus_init_widget(glw_t *w, float weight)
{
  w->glw_focus_weight = weight;
  int v = w->glw_flags & GLW_AUTOREFOCUSABLE && was_interactive(w);
  glw_focus_set(w->glw_root, w, v);
}


/**
 *
 */
static glw_t *
glw_focus_leave0(glw_t *w, glw_t *cur)
{
  glw_t *c, *r;

  c = cur ? TAILQ_NEXT(cur, glw_parent_link) : TAILQ_FIRST(&w->glw_childs);

  for(;;c = TAILQ_NEXT(c, glw_parent_link)) {
    if(c == NULL) {
      if(cur == NULL)
	return NULL;
      c = TAILQ_FIRST(&w->glw_childs);
    }

    if(c == cur)
      return NULL;
    if(c->glw_flags & (GLW_DESTROYING | GLW_FOCUS_BLOCKED))
      continue;
    if(glw_is_focusable(c))
      return c;
    if(TAILQ_FIRST(&c->glw_childs)) {
      if((r = glw_focus_leave0(c, NULL)) != NULL)
	return r;
    }
  }
}


/**
 *
 */
static void
glw_focus_leave(glw_t *w)
{
  glw_t *r = NULL;

  if(w->glw_root->gr_current_focus != w)
    return;

  while(w->glw_parent != NULL) {

    assert(w->glw_parent->glw_focused == w);

    if(!(w->glw_parent->glw_flags & GLW_DESTROYING)) {
      r = glw_focus_leave0(w->glw_parent, w);
      if(r != NULL)
	break;
    }
    w = w->glw_parent;
  }
  glw_focus_set(w->glw_root, r, 1);
}


/**
 *
 */
static glw_t *
glw_focus_crawl0(glw_t *w, glw_t *cur, int forward)
{
  glw_t *c, *r;

  if(forward) {
    c = cur ? TAILQ_NEXT(cur, glw_parent_link) : TAILQ_FIRST(&w->glw_childs);
  } else {
    c = cur ? TAILQ_PREV(cur, glw_queue, glw_parent_link) : 
      TAILQ_LAST(&w->glw_childs, glw_queue);
  }

  for(; c != NULL; c = forward ? TAILQ_NEXT(c, glw_parent_link) : 
	TAILQ_PREV(c, glw_queue, glw_parent_link)) {

    if(c->glw_flags & (GLW_FOCUS_BLOCKED | GLW_HIDDEN))
      continue;
    if(glw_is_focusable(c))
      return c;
    if(TAILQ_FIRST(&c->glw_childs))
      if((r = glw_focus_crawl0(c, NULL, forward)) != NULL)
	return r;
  }
  return NULL;
}


/**
 *
 */
static glw_t *
glw_focus_crawl1(glw_t *w, int forward)
{
  glw_t *c, *r;

  c = forward ? TAILQ_FIRST(&w->glw_childs) : 
    TAILQ_LAST(&w->glw_childs, glw_queue);

  for(; c != NULL; c = forward ? TAILQ_NEXT(c, glw_parent_link) : 
	TAILQ_PREV(c, glw_queue, glw_parent_link)) {

    if(!(c->glw_flags & GLW_FOCUS_BLOCKED)) {
      if(glw_is_focusable(c))
	return c;
      if(TAILQ_FIRST(&c->glw_childs))
	if((r = glw_focus_crawl1(c, forward)) != NULL)
	return r;
    }
  }
  return NULL;
}


/**
 * Used to focus next (or previous) focusable widget.
 */
void
glw_focus_crawl(glw_t *w, int forward)
{
  glw_t *r = NULL;

  while(w->glw_parent != NULL) {
    if((r = glw_focus_crawl0(w->glw_parent, w, forward)) != NULL)
      break;
    w = w->glw_parent;
  }

  if(r == NULL)
    r = glw_focus_crawl1(w, forward);

  if(r != NULL)
    glw_focus_set(w->glw_root, r, 1);
}



/**
 *
 */
void
glw_focus_open_path_close_all_other(glw_t *w)
{
  glw_t *c;

  TAILQ_FOREACH(c, &w->glw_parent->glw_childs, glw_parent_link)
    c->glw_flags |= GLW_FOCUS_BLOCKED;
  
  w->glw_flags &= ~GLW_FOCUS_BLOCKED;
  c = glw_focus_by_path(w);

  if(c != NULL)
    glw_focus_set(w->glw_root, c, 0);
  else
    TRACE(TRACE_DEBUG, "GLW", "Nothing can be (re-)focused");
}



/**
 *
 */
void
glw_focus_open_path(glw_t *w)
{
  glw_t *c;

  if(!(w->glw_flags & GLW_FOCUS_BLOCKED))
    return;

  w->glw_flags &= ~GLW_FOCUS_BLOCKED;

  c = glw_focus_by_path(w);

  assert(c != NULL);

  glw_focus_set(w->glw_root, c, 0);
}


/**
 *
 */
void
glw_focus_close_path(glw_t *w)
{
  if(w->glw_flags & GLW_FOCUS_BLOCKED)
    return;
  
  w->glw_flags |= GLW_FOCUS_BLOCKED;

  if(w->glw_parent->glw_focused != w)
    return;

  glw_focus_leave(w);
}


/**
 *
 */
int
glw_focus_step(glw_t *w, int forward)
{
  event_t *e;

  if(!glw_path_in_focus(w))
    return 0;

  e = event_create_action(forward ? ACTION_DOWN : ACTION_UP);

  while(w->glw_focused != NULL) {
    w = w->glw_focused;
    if(glw_is_focusable(w)) {
      if(glw_event_to_widget(w, e, 1))
	break;
    }
  }

  event_unref(e);
  return 1;
}


/**
 *
 */
void
glw_focus_suggest(glw_t *w)
{
  for(; w->glw_parent != NULL; w = w->glw_parent) {
    if(w->glw_parent->glw_class->gc_suggest_focus != NULL) {
      w->glw_parent->glw_class->gc_suggest_focus(w->glw_parent, w);
      break;
    }
  }
}




/**
 *
 */
int
glw_event_to_widget(glw_t *w, event_t *e, int local)
{
  if(glw_event_map_intercept(w, e))
    return 1;

  if(glw_signal0(w, GLW_SIGNAL_EVENT, e))
    return 1;

  return glw_navigate(w, e, local);
}

/**
 *
 */
int
glw_event(glw_root_t *gr, event_t *e)
{
  glw_t *w;

  if(glw_event_map_intercept(gr->gr_universe, e))
    return 1;

  if((w = gr->gr_current_focus) == NULL)
    return 0;

  return glw_event_to_widget(w, e, 0);
}


/**
 *
 */
int
glw_pointer_event0(glw_root_t *gr, glw_t *w, glw_pointer_event_t *gpe, 
		   glw_t **hp, float *p, float *dir)
{
  glw_t *c;
  event_t *e;
  float x, y;
  glw_pointer_event_t gpe0;

  if(w->glw_flags & GLW_FOCUS_BLOCKED)
    return 0;

  if(w->glw_matrix != NULL) {

    if(glw_widget_unproject(w->glw_matrix, &x, &y, p, dir) &&
       x <= 1 && y <= 1 && x >= -1 && y >= -1) {
      gpe0.type = gpe->type;
      gpe0.x = x;
      gpe0.y = y;
      gpe0.delta_y = gpe->delta_y;

      if(glw_is_focusable(w) && *hp == NULL)
	*hp = w;

      if(glw_signal0(w, GLW_SIGNAL_POINTER_EVENT, &gpe0))
	return 1;

      if(glw_is_focusable(w)) {
	switch(gpe->type) {

	case GLW_POINTER_RIGHT_PRESS:
	  glw_focus_set(gr, w, 1);
	  return 1;

	case GLW_POINTER_LEFT_PRESS:
	  gr->gr_pointer_press = w;
	  glw_path_modify(w, GLW_IN_PRESSED_PATH, 0, NULL);
	  return 1;

	case GLW_POINTER_LEFT_RELEASE:
	  if(gr->gr_pointer_press == w) {
	    if(w->glw_flags & GLW_FOCUS_ON_CLICK)
	      glw_focus_set(gr, w, 1); 

	    glw_path_modify(w, 0, GLW_IN_PRESSED_PATH, NULL);
	    e = event_create_action(ACTION_ENTER);
	    glw_event_to_widget(w, e, 0);
	    event_unref(e);

	  }
	  return 1;
	default:
	  break;
	}
      }
    } else {
      // Don't decend
      return 0;
    }
  }


  if(w->glw_class->gc_gpe_iterator != NULL ) {
    return w->glw_class->gc_gpe_iterator(gr, w, gpe, hp, p, dir);
  } else {
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_pointer_event0(gr, c, gpe, hp, p, dir))
	return 1;
    return 0;
  }

}


/**
 *
 */
void
glw_pointer_event(glw_root_t *gr, glw_pointer_event_t *gpe)
{
  glw_t *c, *w, *top;
  glw_pointer_event_t gpe0;
  float x, y;
  glw_t *hover = NULL;


  float p[3];
  float dir[3];

  p[0] = gpe->x;
  p[1] = gpe->y;
  p[2] = -2.41;

  dir[0] = p[0] - gpe->x * 42.38; // 42.38 comes from unprojecting
  dir[1] = p[1] - gpe->y * 42.38; // this camera and projection matrix
  dir[2] = p[2] - -100;
 
  if(gpe->type != GLW_POINTER_MOTION_REFRESH)
    runcontrol_activity();

  /* If a widget has grabbed to pointer (such as when holding the button
     on a slider), dispatch events there */

  gr->gr_mouse_x = gpe->x;
  gr->gr_mouse_y = gpe->y;
  gr->gr_mouse_valid = 1;

  if(gpe->type == GLW_POINTER_MOTION_UPDATE ||
     gpe->type == GLW_POINTER_MOTION_REFRESH) {
    
    prop_set_int(gr->gr_pointer_visible, 1);

    if((w = gr->gr_pointer_grab) != NULL && w->glw_matrix != NULL) {
      glw_widget_unproject(w->glw_matrix, &x, &y, p, dir);
      gpe0.type = GLW_POINTER_FOCUS_MOTION;
      gpe0.x = x;
      gpe0.y = y;
      
      glw_signal0(w, GLW_SIGNAL_POINTER_EVENT, &gpe0);
    }

    if((w = gr->gr_pointer_press) != NULL && w->glw_matrix != NULL) {
      if(!glw_widget_unproject(w->glw_matrix, &x, &y, p, dir) ||
	 x < -1 || y < -1 || x > 1 || y > 1) {
	// Moved outside button, release 

	glw_path_modify(w, 0, GLW_IN_PRESSED_PATH, NULL);
	gr->gr_pointer_press = NULL;
      }
    }
  }

  if(gpe->type == GLW_POINTER_LEFT_RELEASE && gr->gr_pointer_grab != NULL) {
    gr->gr_pointer_grab = NULL;
    return;
  }

  if(gpe->type == GLW_POINTER_GONE) {
    // Mouse pointer left our screen
    glw_root_set_hover(gr, NULL);
    gr->gr_mouse_valid = 0;
    prop_set_int(gr->gr_pointer_visible, 0);
    return;
  }

  top = gr->gr_universe;

  TAILQ_FOREACH(c, &top->glw_childs, glw_parent_link)
    if(glw_pointer_event0(gr, c, gpe, &hover, p, dir))
      break;

  glw_root_set_hover(gr, hover);
}

/**
 *
 */
void
glw_select(glw_t *p, glw_t *c)
{
  if(c->glw_originating_prop) {
    prop_select(c->glw_originating_prop, 0);
  } else {
    p->glw_selected = c;
    glw_signal0(c, GLW_SIGNAL_SELECTED_UPDATE, NULL);
  }
}


/**
 * Render a widget with prior translation and scaling
 */
void
glw_render_TS(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc)
{
  rc->rc_size_x = prevrc->rc_size_x * c->glw_parent_scale.x;
  rc->rc_size_y = prevrc->rc_size_y * c->glw_parent_scale.y;

  glw_PushMatrix(rc, prevrc);

  glw_Translatef(rc, 
		 c->glw_parent_pos.x,
		 c->glw_parent_pos.y,
		 c->glw_parent_pos.z);

  glw_Scalef(rc, 
	     c->glw_parent_scale.x,
	     c->glw_parent_scale.y,
	     c->glw_parent_scale.z);

  c->glw_class->gc_render(c, rc);
  glw_PopMatrix();
}


/**
 * Render a widget with prior translation
 */
void
glw_render_T(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc)
{
  glw_PushMatrix(rc, prevrc);

  glw_Translatef(rc, 
		 c->glw_parent_pos.x,
		 c->glw_parent_pos.y,
		 c->glw_parent_pos.z);

  c->glw_class->gc_render(c, rc);
  glw_PopMatrix();
}



/**
 *
 */
void
glw_scale_to_aspect(glw_rctx_t *rc, float t_aspect)
{
  float s_aspect = rc->rc_size_x / rc->rc_size_y;
  float a = s_aspect / t_aspect;

  if(a > 1.0f) {
    a = 1.0 / a;
    glw_Scalef(rc, a, 1.0f, 1.0f);
    rc->rc_size_x *= a;
  } else {
    glw_Scalef(rc, 1.0f, a, 1.0f);
    rc->rc_size_y *= a;
  }
}



/**
 *
 */
void
glw_dispatch_event(uii_t *uii, event_t *e)
{
  glw_root_t *gr = (glw_root_t *)uii;
  int r;

  runcontrol_activity();

  glw_lock(gr);

  if(event_is_action(e, ACTION_RELOAD_UI)) {
    glw_load_universe(gr);
    event_unref(e);
    glw_unlock(gr);
    return;

  } else if(event_is_action(e, ACTION_ZOOM_UI_INCR)) {

    settings_add_int(gr->gr_setting_fontsize, 1);
    event_unref(e);
    glw_unlock(gr);
    return;

  } else if(event_is_action(e, ACTION_ZOOM_UI_DECR)) {

    settings_add_int(gr->gr_setting_fontsize, -1);
    event_unref(e);
    glw_unlock(gr);
    return;

  }

  r = glw_event(gr, e);
  glw_unlock(gr);

  if(!r)
    event_dispatch(e);
}

const glw_vertex_t align_vertices[GLW_ALIGN_num] = 
  {
    [GLW_ALIGN_CENTER] = {  0.0,  0.0, 0.0 },
    [GLW_ALIGN_LEFT]   = { -1.0,  0.0, 0.0 },
    [GLW_ALIGN_RIGHT]  = {  1.0,  0.0, 0.0 },
    [GLW_ALIGN_BOTTOM] = {  0.0, -1.0, 0.0 },
    [GLW_ALIGN_TOP]    = {  0.0,  1.0, 0.0 },
  };


/**
 *
 */
void
glw_set_constraints(glw_t *w, int x, int y, float a, float weight, 
		    int flags, int conf)
{
  int ch = 0;

  if((w->glw_flags | flags) & GLW_CONSTRAINT_FLAGS_XY) {

    int f = flags & GLW_CONSTRAINT_FLAGS_XY;

    if(!(w->glw_flags & GLW_CONSTRAINT_CONF_XY) ||
       conf == GLW_CONSTRAINT_CONF_XY) {

      if(!(w->glw_req_size_x == x &&
	   w->glw_req_size_y == y && 
	   (w->glw_flags & GLW_CONSTRAINT_FLAGS_XY) == f)) {

	ch = 1;

	w->glw_req_size_x = x;
	w->glw_req_size_y = y;
	
	w->glw_flags &= ~GLW_CONSTRAINT_FLAGS_XY;
	w->glw_flags |= f | conf;
      }
    }
  }

  if((w->glw_flags | flags) & GLW_CONSTRAINT_FLAGS_WAF) {

    int f = flags & GLW_CONSTRAINT_FLAGS_WAF;

   if(!(w->glw_flags & GLW_CONSTRAINT_CONF_WAF) ||
       conf == GLW_CONSTRAINT_CONF_WAF) {

      if(!(w->glw_req_aspect == a &&
	   w->glw_req_weight == weight && 
	   (w->glw_flags & GLW_CONSTRAINT_FLAGS_WAF) == f)) {

	ch = 1;

	w->glw_req_aspect = a;
	w->glw_req_weight = weight;
	
	w->glw_flags &= ~GLW_CONSTRAINT_FLAGS_WAF;
	w->glw_flags |= f | conf;
      }
    }
  }

  if(!ch)
    return;

  if(w->glw_parent != NULL)
    glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED, w);
}


/**
 *
 */
void
glw_clear_constraints(glw_t *w)
{
  int ch = 0;

  if(!(w->glw_flags & GLW_CONSTRAINT_CONF_XY)) {
    if(w->glw_flags & GLW_CONSTRAINT_FLAGS_XY) {
      w->glw_flags &= ~GLW_CONSTRAINT_FLAGS_XY;
      w->glw_req_size_x = 0;
      w->glw_req_size_y = 0;
      ch = 1;
    }
  }

  if(!(w->glw_flags & GLW_CONSTRAINT_CONF_WAF)) {
    if(w->glw_flags & GLW_CONSTRAINT_FLAGS_WAF) {
      w->glw_flags &= ~GLW_CONSTRAINT_FLAGS_WAF;
      w->glw_req_aspect = 0;
      w->glw_req_weight = 0;
      ch = 1;
    }
  }
  
  if(!ch)
    return;

  if(w->glw_parent != NULL)
    glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED, w);
}


/**
 *
 */
void
glw_copy_constraints(glw_t *w, glw_t *src)
{
  glw_set_constraints(w, 
		      src->glw_req_size_x,
		      src->glw_req_size_y,
		      src->glw_req_aspect,
		      src->glw_req_weight,
		      src->glw_flags & GLW_CONSTRAINT_FLAGS, 0);
}


static LIST_HEAD(, glw_class) glw_classes;

/**
 *
 */
const glw_class_t *
glw_class_find_by_name(const char *name)
{
  glw_class_t *gc;

  LIST_FOREACH(gc, &glw_classes, gc_link)
    if(!strcmp(gc->gc_name, name))
      break;
  return gc;
}

/**
 *
 */
void
glw_register_class(glw_class_t *gc)
{
  LIST_INSERT_HEAD(&glw_classes, gc, gc_link);
}


/**
 *
 */
const char *
glw_get_a_name(glw_t *w)
{
  glw_t *c;
  const char *r;

  if(w->glw_class->gc_get_text != NULL)
    return w->glw_class->gc_get_text(w);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if((r = glw_get_a_name(c)) != NULL)
      return r;
  }
  return NULL;
}


/**
 *
 */
void
glw_rctx_init(glw_rctx_t *rc, int width, int height)
{
  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_size_x = width;
  rc->rc_size_y = height;
  rc->rc_alpha = 1.0f;
}
