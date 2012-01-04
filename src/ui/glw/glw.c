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

#include "keymapper.h"

#include "arch/threads.h"

#include "glw.h"
#include "glw_text_bitmap.h"
#include "glw_texture.h"
#include "glw_view.h"
#include "glw_event.h"

static void glw_focus_init_widget(glw_t *w, float weight);
static void glw_focus_leave(glw_t *w);
static void glw_root_set_hover(glw_root_t *gr, glw_t *w);

const float glw_identitymtx[16] = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0,
  0,0,0,1};

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
  glw_root_t *gr = opaque;

  if(sig != GLW_SIGNAL_EVENT_BUBBLE)
    return 0;

  if(e->e_type_x == EVENT_KEYDESC)
    return 0;

  if(event_is_action(e, ACTION_ENABLE_SCREENSAVER)) {
    gr->gr_screensaver_force_enable = 1;
  } else {
    event_addref(e);
    event_dispatch(e);
  }

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
glw_set_screensaver_delay(void *opaque, int v)
{
  glw_root_t *gr = opaque;

  gr->gr_screensaver_delay = v;
}


/**
 *
 */
static void
glw_change_size(void *opaque, int v)
{
  glw_root_t *gr = opaque;

  v += gr->gr_base_size;
  v = GLW_CLAMP(v, 14, 40);
  prop_set_int(gr->gr_prop_size, v);
  TRACE(TRACE_DEBUG, "GLW", "UI size scale changed to %d", v);
  glw_font_change_size(gr, v);
}


/**
 *
 */
static void
glw_change_underscan_h(void *opaque, int v)
{
  glw_root_t *gr = opaque;

  v += gr->gr_base_underscan_h;
  v = GLW_CLAMP(v, 0, 100);
  prop_set_int(gr->gr_prop_underscan_h, v);
  gr->gr_underscan_h = v;
}


/**
 *
 */
static void
glw_change_underscan_v(void *opaque, int v)
{
  glw_root_t *gr = opaque;

  v += gr->gr_base_underscan_v;
  v = GLW_CLAMP(v, 0, 100);
  prop_set_int(gr->gr_prop_underscan_v, v);
  gr->gr_underscan_v = v;
}




/**
 *
 */
static void
glw_init_settings(glw_root_t *gr, const char *instance,
		  const char *instance_title)
{
  prop_t *r = gr->gr_uii.uii_prop;

  if(gr->gr_base_size == 0)
    gr->gr_base_size = 20;

  gr->gr_settings_instance = strdup(instance);

  gr->gr_settings_store = htsmsg_store_load("displays/%s", instance);
  
  if(gr->gr_settings_store == NULL)
    gr->gr_settings_store = htsmsg_create_map();

  if(instance_title) {
    char title[256];
    snprintf(title, sizeof(title), "Display and user interface on screen %s",
	     instance_title);

    gr->gr_settings = settings_add_dir_cstr(NULL, title,
					    "display", NULL, NULL);

  } else {
    gr->gr_settings = settings_add_dir(NULL, 
				       _p("Display and user interface"),
				       "display", NULL, NULL);

  }

  gr->gr_prop_size = prop_create(r, "size");
  gr->gr_prop_underscan_h = prop_create(r, "underscan_h");
  gr->gr_prop_underscan_v = prop_create(r, "underscan_v");


  gr->gr_setting_size =
    settings_create_int(gr->gr_settings, "size",
			_p("Userinterface size"), 0,
			gr->gr_settings_store, -10, 30, 1,
			glw_change_size, gr,
			SETTINGS_INITIAL_UPDATE, "px", gr->gr_courier,
			glw_settings_save, gr);

  gr->gr_setting_underscan_h =
    settings_create_int(gr->gr_settings, "underscan_h",
			_p("Horizontal underscan"), 0,
			gr->gr_settings_store, -100, +100, 1,
			glw_change_underscan_h, gr,
			SETTINGS_INITIAL_UPDATE, "px", gr->gr_courier,
			glw_settings_save, gr);

  gr->gr_setting_underscan_v =
    settings_create_int(gr->gr_settings, "underscan_v",
			_p("Vertical underscan"), 0,
			gr->gr_settings_store, -100, +100, 1,
			glw_change_underscan_v, gr,
			SETTINGS_INITIAL_UPDATE, "px", gr->gr_courier,
			glw_settings_save, gr);


  gr->gr_setting_screensaver =
    settings_create_int(gr->gr_settings, "screensaver",
			_p("Screensaver delay"),
			10, gr->gr_settings_store, 1, 60, 1,
			glw_set_screensaver_delay, gr,
			SETTINGS_INITIAL_UPDATE, " min", gr->gr_courier,
			glw_settings_save, gr);


  gr->gr_pointer_visible    = prop_create(r, "pointerVisible");
  gr->gr_is_fullscreen      = prop_create(r, "fullscreen");
  gr->gr_screensaver_active = prop_create(r, "screensaverActive");
  gr->gr_prop_width         = prop_create(r, "width");
  gr->gr_prop_height        = prop_create(r, "height");

  prop_set_int(gr->gr_screensaver_active, 0);
}

/**
 *
 */
int
glw_init(glw_root_t *gr, const char *theme,
	 ui_t *ui, int primary, 
	 const char *instance, const char *instance_title)
{
  hts_mutex_init(&gr->gr_mutex);
  gr->gr_courier = prop_courier_create_passive();

  gr->gr_vpaths[0] = "theme";
  gr->gr_vpaths[1] = theme;
  gr->gr_vpaths[2] = NULL;

  gr->gr_uii.uii_ui = ui;

  glw_text_bitmap_init(gr);
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
glw_unload_universe(glw_root_t *gr)
{
  glw_view_cache_flush(gr);

  if(gr->gr_universe != NULL)
    glw_destroy(gr->gr_universe);

  glw_flush(gr);
}

/**
 *
 */
void
glw_load_universe(glw_root_t *gr)
{
  prop_t *page = prop_create(gr->gr_uii.uii_prop, "root");
  glw_unload_universe(gr);

  rstr_t *universe = rstr_alloc("theme://universe.view");

  gr->gr_universe = glw_view_create(gr,
				    universe, NULL, page,
				    NULL, NULL, NULL, 0);

  rstr_release(universe);

  glw_signal_handler_register(gr->gr_universe, top_event_handler, gr, 1000);
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
void
glw_layout0(glw_t *w, glw_rctx_t *rc)
{
  glw_root_t *gr = w->glw_root;
  LIST_REMOVE(w, glw_active_link);
  LIST_INSERT_HEAD(&gr->gr_active_list, w, glw_active_link);
  if(!(w->glw_flags & GLW_ACTIVE)) {
    w->glw_flags |= GLW_ACTIVE;
    glw_signal0(w, GLW_SIGNAL_ACTIVE, NULL);
  }
  glw_signal0(w, GLW_SIGNAL_LAYOUT, rc);
}


/**
 *
 */
glw_t *
glw_create(glw_root_t *gr, const glw_class_t *class,
	   glw_t *parent, glw_t *before, prop_t *originator)
{
  glw_t *w; 
 
   /* Common initializers */
  w = calloc(1, class->gc_instance_size);
  w->glw_root = gr;
  w->glw_class = class;
  w->glw_alpha = 1.0f;
  w->glw_blur  = 1.0f;
  w->glw_refcnt = 1;
  w->glw_alignment = class->gc_default_alignment;
  w->glw_flags = GLW_NAV_FOCUSABLE;
  w->glw_flags2 = GLW2_ENABLED;

  LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);

  if(class->gc_newframe != NULL)
    LIST_INSERT_HEAD(&gr->gr_every_frame_list, w, glw_every_frame_link);

  TAILQ_INIT(&w->glw_childs);

  w->glw_originating_prop = prop_ref_inc(originator);

  w->glw_parent = parent;
  if(parent != NULL) {
    update_in_path(w);
    
    if(before != NULL)
      TAILQ_INSERT_BEFORE(before, w, glw_parent_link);
    else
      TAILQ_INSERT_TAIL(&parent->glw_childs, w, glw_parent_link);

    glw_signal0(parent, GLW_SIGNAL_CHILD_CREATED, w);
  }

  if(class->gc_ctor != NULL)
    class->gc_ctor(w);

  if(class->gc_signal_handler != NULL)
    glw_signal_handler_int(w, class->gc_signal_handler);

   return w;
}


/**
 *
 */
void
glw_set(glw_t *w, ...)
{
  va_list ap;

  va_start(ap, w);

  if(w->glw_class->gc_set != NULL)
    w->glw_class->gc_set(w, ap);
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
static int
glw_screensaver_is_active(glw_root_t *gr)
{
  return gr->gr_screensaver_force_enable ||
    (gr->gr_is_fullscreen && gr->gr_framerate && gr->gr_screensaver_delay &&
     (gr->gr_screensaver_counter > 
      gr->gr_screensaver_delay * gr->gr_framerate * 60));
}


/**
 *
 */
void
glw_reap(glw_root_t *gr)
{
  glw_t *w;

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
}

/**
 *
 */
void
glw_prepare_frame(glw_root_t *gr, int flags)
{
  glw_t *w;

  gr->gr_frame_start = showtime_get_ts();

  if((gr->gr_frames & 0x7f) == 0) {

    if(gr->gr_hz_sample) {
      int64_t d = gr->gr_frame_start - gr->gr_hz_sample;

      double hz = 128000000.0 / d;

      prop_set_float(prop_create(gr->gr_uii.uii_prop, "framerate"), hz);
      gr->gr_framerate = hz;
    }
    gr->gr_hz_sample = gr->gr_frame_start;
  }

  gr->gr_frames++;

  gr->gr_screensaver_counter++;

  prop_set_int(gr->gr_screensaver_active, glw_screensaver_is_active(gr));
  prop_set_int(gr->gr_prop_width, gr->gr_width);
  prop_set_int(gr->gr_prop_height, gr->gr_height);

  prop_courier_poll(gr->gr_courier);

  //  glw_cursor_layout_frame(gr);

  LIST_FOREACH(w, &gr->gr_every_frame_list, glw_every_frame_link)
    w->glw_class->gc_newframe(w, flags);

  while((w = LIST_FIRST(&gr->gr_active_flush_list)) != NULL) {
    LIST_REMOVE(w, glw_active_link);
    LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);
    w->glw_flags &= ~GLW_ACTIVE;
    glw_signal0(w, GLW_SIGNAL_INACTIVE, NULL);
  }

  glw_reap(gr);

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
  if(w->glw_refcnt == 1) {
    assert(w->glw_clone == NULL);
    free(w);
  }
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
glw_suspend_subscriptions(glw_t *w)
{
  glw_t *c;
  glw_prop_subscription_suspend_list(&w->glw_prop_subscriptions);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_suspend_subscriptions(c);
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
    if(t->glw_flags & GLW_HIDDEN)
      i--;
    else
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
    if(t->glw_flags & GLW_HIDDEN)
      i--;
    else
      c = t;
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
glw_retire_child(glw_t *w)
{
  glw_t *p = w->glw_parent;
  if(p != NULL && p->glw_class->gc_retire_child != NULL) {
    p->glw_class->gc_retire_child(p, w);
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
void
glw_set_focus_weight(glw_t *w, float f)
{
  if(f == w->glw_focus_weight)
    return;

  if(w->glw_focus_weight > 0 && w->glw_root->gr_current_focus == w)
    glw_focus_leave(w);
  
  if(f > 0)
    glw_focus_init_widget(w, f);
  else
    w->glw_focus_weight = 0;
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
glw_focus_set(glw_root_t *gr, glw_t *w, int how)
{
  glw_t *x, *y, *com;
  glw_signal_t sig;
  float weight = w ? w->glw_focus_weight : 0;

  if(gr->gr_focus_work)
    return;

  gr->gr_focus_work = 1;

  if(how == GLW_FOCUS_SET_AUTOMATIC) {
    sig = GLW_SIGNAL_FOCUS_CHILD_AUTOMATIC;
  } else {
    sig = GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE;
  }

  if(w != NULL) {

    for(x = w; x->glw_parent != NULL; x = x->glw_parent) {

      if(how != GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE &&
	 x->glw_flags & GLW_FOCUS_BLOCKED) {
	gr->gr_focus_work = 0;
	return;
      }

      if(x->glw_parent->glw_focused != x) {
	/* Path switches */
	glw_t *p = x->glw_parent;
	y = glw_focus_by_path(p);
      
	/* Handle floating focus
	 *
	 * Floating focus is when the first widget of a child currently
	 * has focus and we insert an entry with equal focus weight before
	 * it. 
	 *
	 * This allows the focus to "stay" at the first entry even if we
	 * insert entries in random order
	 */
	int ff = p->glw_flags2 & GLW2_FLOATING_FOCUS && 
	  x == TAILQ_FIRST(&p->glw_childs) && 
	  TAILQ_NEXT(x, glw_parent_link) == p->glw_focused;

	if(y == NULL || how != GLW_FOCUS_SET_AUTOMATIC ||
	   weight > y->glw_focus_weight || 
	   (ff && weight == y->glw_focus_weight)) {
	  x->glw_parent->glw_focused = x;
	  glw_signal0(x->glw_parent, sig, x);
	} else {
	  /* Other path outranks our weight, stop now */
	  gr->gr_focus_work = 0;
	  return;
	}
      }
    }
  }

  if(gr->gr_current_focus == w) {
    gr->gr_focus_work = 0;
    return;
  }
  com = find_common_ancestor(gr->gr_current_focus, w);

  glw_t *ww = gr->gr_current_focus;

  if(ww != NULL)
    glw_path_modify(ww, 0, GLW_IN_FOCUS_PATH, com);

  gr->gr_current_focus = w;

#if 0
  glw_t *h = w;
  while(h->glw_parent != NULL) {
    printf("Verifying %p %p %p\n", h, h->glw_parent, h->glw_parent->glw_focused);
    if(h->glw_parent->glw_focused != h) {
      glw_t *f = h->glw_parent->glw_focused;
      printf("Parent %p %s points to %p %s <%s> instead of %p %s <%s>\n",
	     h->glw_parent, h->glw_parent->glw_class->gc_name,
	     f, f->glw_class->gc_name, glw_get_a_name(f),
	     h, h->glw_class->gc_name, glw_get_a_name(h));
    }
    h = h->glw_parent;
  }
#endif

  if(w != NULL) {

    glw_path_modify(w, GLW_IN_FOCUS_PATH, 0, com);

  
    if(how) {
      prop_t *p = get_originating_prop(w);

      if(p != NULL) {
    
	if(gr->gr_last_focused_interactive != NULL)
	  prop_ref_dec(gr->gr_last_focused_interactive);

	gr->gr_last_focused_interactive = prop_ref_inc(p);
      }
    }
  }
  gr->gr_focus_work = 0;
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
  glw_focus_set(w->glw_root, r, GLW_FOCUS_SET_INTERACTIVE);
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
    glw_focus_set(w->glw_root, r, GLW_FOCUS_SET_INTERACTIVE);
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
    glw_focus_set(w->glw_root, c, GLW_FOCUS_SET_AUTOMATIC);
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

  glw_focus_set(w->glw_root, c, GLW_FOCUS_SET_AUTOMATIC);
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
  event_release(e);
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

  if((w = gr->gr_current_focus) != NULL) {
    if(glw_event_to_widget(w, e, 0))
      return 1;

    while((w = w->glw_parent) != NULL) {
      if(glw_event_map_intercept(w, e))
	return 1;
      if(glw_signal0(w, GLW_SIGNAL_EVENT_BUBBLE, e))
	return 1;
    }
  }
  if(glw_event_map_intercept(gr->gr_universe, e))
    return 1;
  return 0;
}


/**
 *
 */
int
glw_pointer_event0(glw_root_t *gr, glw_t *w, glw_pointer_event_t *gpe, 
		   glw_t **hp, Vec3 p, Vec3 dir)
{
  glw_t *c;
  event_t *e;
  float x, y;
  glw_pointer_event_t gpe0;

  if(w->glw_flags & (GLW_FOCUS_BLOCKED | GLW_CLIPPED))
    return 0;

  if(w->glw_matrix != NULL) {

    if(glw_widget_unproject(*w->glw_matrix, &x, &y, p, dir) &&
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
	  glw_focus_set(gr, w, GLW_FOCUS_SET_INTERACTIVE);
	  return 1;

	case GLW_POINTER_LEFT_PRESS:
	  gr->gr_pointer_press = w;
	  glw_path_modify(w, GLW_IN_PRESSED_PATH, 0, NULL);
	  return 1;

	case GLW_POINTER_LEFT_RELEASE:
	  if(gr->gr_pointer_press == w) {
	    if(w->glw_flags & GLW_FOCUS_ON_CLICK)
	      glw_focus_set(gr, w, GLW_FOCUS_SET_INTERACTIVE); 

	    glw_path_modify(w, 0, GLW_IN_PRESSED_PATH, NULL);
	    e = event_create_action(ACTION_ACTIVATE);
	    glw_event_to_widget(w, e, 0);
	    event_release(e);

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

  Vec3 p, dir;

  glw_vec3_copy(p, glw_vec3_make(gpe->x, gpe->y, -2.41));
  glw_vec3_sub(dir, p, glw_vec3_make(gpe->x * 42.38,
				     gpe->y * 42.38,
				     -100));


  if(gpe->type != GLW_POINTER_MOTION_REFRESH) {
    runcontrol_activity();
    gr->gr_screensaver_counter = 0;
    gr->gr_screensaver_force_enable = 0;
  }

  /* If a widget has grabbed to pointer (such as when holding the button
     on a slider), dispatch events there */

  if(gpe->x != gr->gr_mouse_x || gpe->y != gr->gr_mouse_y) {
    gr->gr_mouse_x = gpe->x;
    gr->gr_mouse_y = gpe->y;
    gr->gr_mouse_valid = 1;

    if(gpe->type == GLW_POINTER_MOTION_UPDATE ||
       gpe->type == GLW_POINTER_MOTION_REFRESH) {
    
      prop_set_int(gr->gr_pointer_visible, 1);

      if((w = gr->gr_pointer_grab) != NULL && w->glw_matrix != NULL) {
	glw_widget_unproject(*w->glw_matrix, &x, &y, p, dir);
	gpe0.type = GLW_POINTER_FOCUS_MOTION;
	gpe0.x = x;
	gpe0.y = y;
      
	glw_signal0(w, GLW_SIGNAL_POINTER_EVENT, &gpe0);
      }

      if((w = gr->gr_pointer_press) != NULL && w->glw_matrix != NULL) {
	if(!glw_widget_unproject(*w->glw_matrix, &x, &y, p, dir) ||
	   x < -1 || y < -1 || x > 1 || y > 1) {
	  // Moved outside button, release 

	  glw_path_modify(w, 0, GLW_IN_PRESSED_PATH, NULL);
	  gr->gr_pointer_press = NULL;
	}
      }
    }
  }
  if(gpe->type == GLW_POINTER_LEFT_RELEASE && gr->gr_pointer_grab != NULL) {
    w = gr->gr_pointer_grab;
    glw_widget_unproject(*w->glw_matrix, &x, &y, p, dir);
    gpe0.type = GLW_POINTER_LEFT_RELEASE;
    gpe0.x = x;
    gpe0.y = y;

    glw_signal0(w, GLW_SIGNAL_POINTER_EVENT, &gpe0);
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
glw_scale_to_aspect(glw_rctx_t *rc, float t_aspect)
{
  if(t_aspect * rc->rc_height < rc->rc_width) {
    // Shrink X
    int border = rc->rc_width - t_aspect * rc->rc_height;
    int left  = (border + 1) / 2;
    int right = rc->rc_width - (border / 2);

    float s = (right - left) / (float)rc->rc_width;
    float t = -1.0f + (right + left) / (float)rc->rc_width;

    glw_Translatef(rc, t, 0, 0);
    glw_Scalef(rc, s, 1.0f, 1.0f);

    rc->rc_width = right - left;

  } else {
    // Shrink Y
    int border = rc->rc_height - rc->rc_width / t_aspect;
    int bottom  = (border + 1) / 2;
    int top     = rc->rc_height - (border / 2);

    float s = (top - bottom) / (float)rc->rc_height;
    float t = -1.0f + (top + bottom) / (float)rc->rc_height;

    glw_Translatef(rc, 0, t, 0);
    glw_Scalef(rc, 1.0f, s, 1.0f);
    rc->rc_height = top - bottom;
  }
}

/**
 *
 */
void
glw_reposition(glw_rctx_t *rc, int left, int top, int right, int bottom)
{
  float sx =         (right - left) / (float)rc->rc_width;
  float tx = -1.0f + (right + left) / (float)rc->rc_width;
  float sy =         (top - bottom) / (float)rc->rc_height;
  float ty = -1.0f + (top + bottom) / (float)rc->rc_height;
  
  glw_Translatef(rc, tx, ty, 0);
  glw_Scalef(rc, sx, sy, GLW_MIN(sx, sy));

  rc->rc_width  = right - left;
  rc->rc_height = top - bottom;
}


/**
 *
 */
void
glw_repositionf(glw_rctx_t *rc, float left, float top,
		float right, float bottom)
{
  float sx =         (right - left) / (float)rc->rc_width;
  float tx = -1.0f + (right + left) / (float)rc->rc_width;
  float sy =         (top - bottom) / (float)rc->rc_height;
  float ty = -1.0f + (top + bottom) / (float)rc->rc_height;
  
  glw_Translatef(rc, tx, ty, 0);
  glw_Scalef(rc, sx, sy, GLW_MIN(sx, sy));

  rc->rc_width  = right - left;
  rc->rc_height = top - bottom;
}


/**
 *
 */
int
glw_kill_screensaver(glw_root_t *gr)
{
  int r = glw_screensaver_is_active(gr);
  gr->gr_screensaver_counter = 0;
  gr->gr_screensaver_force_enable = 0;
  return r;
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
 
  if(e->e_type_x == EVENT_KEYDESC) {
    event_t *e2;
    
    if(glw_event(gr, e)) {
      glw_unlock(gr);
      return; // Was consumed
    }

    e2 = keymapper_resolve(e->e_payload);

    glw_unlock(gr);

    if(e2 != NULL)
      uii->uii_ui->ui_dispatch_event(uii, e2);
    return;
  }

  if(!(event_is_action(e, ACTION_SEEK_FAST_BACKWARD) ||
       event_is_action(e, ACTION_SEEK_BACKWARD) ||
       event_is_action(e, ACTION_SEEK_FAST_FORWARD) ||
       event_is_action(e, ACTION_SEEK_FORWARD) ||
       event_is_action(e, ACTION_PLAYPAUSE) ||
       event_is_action(e, ACTION_PLAY) ||
       event_is_action(e, ACTION_PAUSE) ||
       event_is_action(e, ACTION_STOP) ||
       event_is_action(e, ACTION_EJECT) ||
       event_is_action(e, ACTION_PREV_TRACK) ||
       event_is_action(e, ACTION_NEXT_TRACK) ||
       event_is_action(e, ACTION_SHOW_MEDIA_STATS) ||
       event_is_action(e, ACTION_SHUFFLE) ||
       event_is_action(e, ACTION_REPEAT) ||
       event_is_action(e, ACTION_NEXT_CHANNEL) ||
       event_is_action(e, ACTION_PREV_CHANNEL) ||
       event_is_action(e, ACTION_VOLUME_UP) ||
       event_is_action(e, ACTION_VOLUME_DOWN) ||
       event_is_action(e, ACTION_VOLUME_MUTE_TOGGLE) ||
       event_is_action(e, ACTION_POWER_OFF) ||
       event_is_action(e, ACTION_STANDBY) ||
       event_is_type(e, EVENT_SELECT_AUDIO_TRACK) ||
       event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)

     )) {
    
    if(glw_kill_screensaver(gr)) {
      glw_unlock(gr);
      return;
    }
  }

  if(event_is_action(e, ACTION_RELOAD_UI)) {
    glw_load_universe(gr);
    glw_unlock(gr);
    return;

  } else if(event_is_action(e, ACTION_ZOOM_UI_INCR)) {

    settings_add_int(gr->gr_setting_size, 1);
    glw_unlock(gr);
    return;

  } else if(event_is_action(e, ACTION_ZOOM_UI_DECR)) {

    settings_add_int(gr->gr_setting_size, -1);
    glw_unlock(gr);
    return;

  }

  r = glw_event(gr, e);
  glw_unlock(gr);

  if(!r) {
    event_addref(e);
    event_dispatch(e);
  }
}

const glw_vertex_t align_vertices[] = 
  {
    [0] = {  0.0,  0.0, 0.0 },
    [LAYOUT_ALIGN_CENTER] = {  0.0,  0.0, 0.0 },
    [LAYOUT_ALIGN_LEFT]   = { -1.0,  0.0, 0.0 },
    [LAYOUT_ALIGN_RIGHT]  = {  1.0,  0.0, 0.0 },
    [LAYOUT_ALIGN_BOTTOM] = {  0.0, -1.0, 0.0 },
    [LAYOUT_ALIGN_TOP]    = {  0.0,  1.0, 0.0 },

    [LAYOUT_ALIGN_TOP_LEFT] = { -1.0,  1.0, 0.0 },
    [LAYOUT_ALIGN_TOP_RIGHT] = { 1.0,  1.0, 0.0 },
    [LAYOUT_ALIGN_BOTTOM_LEFT] = { -1.0, -1.0, 0.0 },
    [LAYOUT_ALIGN_BOTTOM_RIGHT] = { 1.0, -1.0, 0.0 },
  };

void
glw_align_1(glw_rctx_t *rc, int a)
{
  if(a && a != LAYOUT_ALIGN_CENTER)
    glw_Translatef(rc, 
		   align_vertices[a].x, 
		   align_vertices[a].y, 
		   align_vertices[a].z);
}

void
glw_align_2(glw_rctx_t *rc, int a)
{
  if(a && a != LAYOUT_ALIGN_CENTER)
    glw_Translatef(rc, 
		   -align_vertices[a].x, 
		   -align_vertices[a].y, 
		   -align_vertices[a].z);
}



/**
 *
 */
void
glw_set_constraints(glw_t *w, int x, int y, float weight, 
		    int flags, int conf)
{
  int ch = 0;

  if((w->glw_flags | flags) & GLW_CONSTRAINT_X) {

    int f = flags & GLW_CONSTRAINT_X;

    if(!(w->glw_flags & GLW_CONSTRAINT_CONF_X) ||
       conf == GLW_CONSTRAINT_CONF_X) {

      if(!(w->glw_req_size_x == x &&
	   (w->glw_flags & GLW_CONSTRAINT_X) == f)) {

	ch = 1;

	w->glw_req_size_x = x;
	
	w->glw_flags &= ~GLW_CONSTRAINT_X;
	w->glw_flags |= f | conf;
      }
    }
  }

  if((w->glw_flags | flags) & GLW_CONSTRAINT_Y) {

    int f = flags & GLW_CONSTRAINT_Y;

    if(!(w->glw_flags & GLW_CONSTRAINT_CONF_Y) ||
       conf == GLW_CONSTRAINT_CONF_Y) {

      if(!(w->glw_req_size_y == y && 
	   (w->glw_flags & GLW_CONSTRAINT_Y) == f)) {

	ch = 1;

	w->glw_req_size_y = y;
	
	w->glw_flags &= ~GLW_CONSTRAINT_Y;
	w->glw_flags |= f | conf;
      }
    }
  }

  if((w->glw_flags | flags) & GLW_CONSTRAINT_W) {

    int f = flags & GLW_CONSTRAINT_W;

   if(!(w->glw_flags & GLW_CONSTRAINT_W) ||
       conf == GLW_CONSTRAINT_CONF_W) {

      if(!(w->glw_req_weight == weight && 
	   (w->glw_flags & GLW_CONSTRAINT_W) == f)) {

	ch = 1;

	w->glw_req_weight = weight;
	
	w->glw_flags &= ~GLW_CONSTRAINT_W;
	w->glw_flags |= f | conf;
      }
    }
  }


  if((w->glw_flags ^ flags) & GLW_CONSTRAINT_F) {
    ch = 1;
    w->glw_flags &= ~GLW_CONSTRAINT_F;
    w->glw_flags |= (flags & GLW_CONSTRAINT_F);
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

  if(!(w->glw_flags & GLW_CONSTRAINT_CONF_X)) {
    if(w->glw_flags & GLW_CONSTRAINT_X) {
      w->glw_flags &= ~GLW_CONSTRAINT_X;
      w->glw_req_size_x = 0;
      ch = 1;
    }
  }

  if(!(w->glw_flags & GLW_CONSTRAINT_CONF_Y)) {
    if(w->glw_flags & GLW_CONSTRAINT_Y) {
      w->glw_flags &= ~GLW_CONSTRAINT_Y;
      w->glw_req_size_y = 0;
      ch = 1;
    }
  }

  if(!(w->glw_flags & GLW_CONSTRAINT_CONF_W)) {
    if(w->glw_flags & GLW_CONSTRAINT_W) {
      w->glw_flags &= ~GLW_CONSTRAINT_W;
      w->glw_req_weight = 0;
      ch = 1;
    }
  }

  if(w->glw_flags & GLW_CONSTRAINT_F) {
    w->glw_flags &= ~GLW_CONSTRAINT_F;
    ch = 1;
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
static const char *
glw_get_a_name_r(glw_t *w)
{
  glw_t *c;
  const char *r;

  if(w->glw_class->gc_get_text != NULL)
    return w->glw_class->gc_get_text(w);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if((r = glw_get_a_name_r(c)) != NULL)
      return r;
  }
  return NULL;
}


/**
 *
 */
const char *
glw_get_a_name(glw_t *w)
{
  if(w->glw_id != NULL)
    return w->glw_id;

  return glw_get_a_name_r(w);
}


/**
 *
 */
void
glw_set_fullscreen(glw_root_t *gr, int fullscreen)
{
  prop_set_int(gr->gr_is_fullscreen, !!fullscreen);
}


/**
 *
 */
static void
glw_print_tree0(glw_t *w, int indent)
{
  glw_t *c;

  fprintf(stderr, "%*.s%p %s: %s [%08x] %s\n", 
	  indent, "",
	  w,
	  w->glw_class->gc_name,
	  w->glw_class->gc_get_text ? w->glw_class->gc_get_text(w) : "",
	  w->glw_flags,
	  w->glw_flags & GLW_HIDDEN ? " <hidden>" : "");
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    glw_print_tree0(c, indent + 2);
  }
}


/**
 *
 */
void
glw_print_tree(glw_t *w)
{
  glw_print_tree0(w, 0);

}


/**
 *
 */
glw_t *
glw_next_widget(glw_t *w)
{
  do {
    w = TAILQ_NEXT(w, glw_parent_link);
  } while (w != NULL && w->glw_flags & GLW_HIDDEN);
  return w;
}


/**
 *
 */
glw_t *
glw_prev_widget(glw_t *w)
{
  do {
    w = TAILQ_PREV(w, glw_queue, glw_parent_link);
  } while (w != NULL && w->glw_flags & GLW_HIDDEN);
  return w;
}


/**
 *
 */
glw_t *
glw_first_widget(glw_t *w)
{
  w = TAILQ_FIRST(&w->glw_childs);

  while(w != NULL && w->glw_flags & GLW_HIDDEN)
    w = TAILQ_NEXT(w, glw_parent_link);

  return w;
}


/**
 *
 */
glw_t *
glw_last_widget(glw_t *w)
{
  w = TAILQ_LAST(&w->glw_childs, glw_queue);

  while(w != NULL && w->glw_flags & GLW_HIDDEN)
    w = TAILQ_PREV(w, glw_queue, glw_parent_link);

  return w;
}


void
glw_hide(glw_t *w)
{
  if(w->glw_flags & GLW_HIDDEN)
    return;
  w->glw_flags |= GLW_HIDDEN;
  glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_HIDDEN, w);
}


void
glw_unhide(glw_t *w)
{
  if(!(w->glw_flags & GLW_HIDDEN))
    return;
  w->glw_flags &= ~GLW_HIDDEN;
  glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_UNHIDDEN, w);
}



/**
 *
 */
void
glw_store_matrix(glw_t *w, glw_rctx_t *rc)
{
  if(rc->rc_inhibit_matrix_store)
    return;

  if(w->glw_matrix == NULL)
    w->glw_matrix = malloc(sizeof(Mtx));
  
  memcpy(w->glw_matrix, rc->rc_mtx, sizeof(Mtx));
}



/**
 *
 */
void
glw_rctx_init(glw_rctx_t *rc, int width, int height, int overscan)
{
  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_width  = width;
  rc->rc_height = height;
  rc->rc_alpha = 1.0f;
  rc->rc_blur  = 1.0f;
  rc->rc_overscanning = overscan;

  glw_LoadIdentity(rc);
  glw_Translatef(rc, 0, 0, -1 / tan(45 * M_PI / 360));
}


/**
 * m   Model matrix
 * x   Return x in model space
 * y   Return y in model space
 * p   Mouse pointer at camera z plane
 * dir Mouse pointer direction vector
 */
int
glw_widget_unproject(Mtx m, float *xp, float *yp, const Vec3 p, const Vec3 dir)
{
  Vec3 u, v, n, w0, T0, T1, T2, out, I;
  Mtx inv;
  float b;

  PMtx tm;
  glw_pmtx_mul_prepare(tm, m);

  glw_pmtx_mul_vec3(T0, tm, glw_vec3_make(-1, -1, 0));
  glw_pmtx_mul_vec3(T1, tm, glw_vec3_make( 1, -1, 0));
  glw_pmtx_mul_vec3(T2, tm, glw_vec3_make( 1,  1, 0));

  glw_vec3_sub(u, T1, T0);
  glw_vec3_sub(v, T2, T0);
  glw_vec3_cross(n, u, v);
  
  glw_vec3_sub(w0, p, T0);
  b = glw_vec3_dot(n, dir);
  if(fabs(b) < 0.000001)
    return 0;

  glw_vec3_addmul(I, p, dir, -glw_vec3_dot(n, w0) / b);

  if(!glw_mtx_invert(inv, m))
    return 0;

  glw_pmtx_mul_prepare(tm, inv);
  glw_pmtx_mul_vec3(out, tm, I);

  *xp = glw_vec3_extract(out, 0);
  *yp = glw_vec3_extract(out, 1);
  return 1;
}
