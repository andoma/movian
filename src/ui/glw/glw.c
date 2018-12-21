/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include "arch/threads.h"
#include "text/text.h"

#include "glw.h"
#include "glw_settings.h"
#include "glw_text_bitmap.h"
#include "glw_texture.h"
#include "glw_view.h"
#include "glw_event.h"
#include "glw_style.h"
#include "glw_navigation.h"
#include "glw_rec.h"

#include "api/screenshot.h"

#include "fileaccess/fileaccess.h"

static void glw_focus_init_widget(glw_t *w, float weight);
static void glw_focus_leave(glw_t *w);
static void glw_root_set_hover(glw_root_t *gr, glw_t *w);
static void glw_eventsink(void *opaque, prop_event_t event, ...);
static void glw_update_em(glw_root_t *gr);
static int  glw_set_keyboard_mode(glw_root_t *gr, int on);
static void glw_register_activity(glw_root_t *gr);
static void glw_touch_longpress(glw_root_t *gr);

glw_settings_t glw_settings;


const float glw_identitymtx[16] = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0,
  0,0,0,1};


static void glw_osk_open_default(glw_root_t *gr, const char *title, const char *input,
                                 glw_t *w, int password);


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
static void
glw_update_underscan(glw_root_t *gr)
{
  int val;

  if(gr->gr_init_flags & GLW_INIT_OVERSCAN) {
    if(gr->gr_height >= 1080) {
      gr->gr_base_underscan_h = 66;
      gr->gr_base_underscan_v = 34;
    } else if(gr->gr_height >= 720) {
      gr->gr_base_underscan_h = 43;
      gr->gr_base_underscan_v = 22;
    } else {
      gr->gr_base_underscan_h = 36;
      gr->gr_base_underscan_v = 20;
    }
  }

  if(glw_settings.gs_underscan_h != gr->gr_user_underscan_h ||
     glw_settings.gs_underscan_v != gr->gr_user_underscan_v) {
    gr->gr_user_underscan_h = glw_settings.gs_underscan_h;
    gr->gr_user_underscan_v = glw_settings.gs_underscan_v;

    if(gr->gr_user_underscan_changed > 0) {
      // Don't send change first time
      prop_set(gr->gr_prop_ui, "underscan_changes", PROP_SET_INT,
               gr->gr_user_underscan_changed);
    }
    gr->gr_user_underscan_changed++;
  }

  val = GLW_CLAMP(gr->gr_base_underscan_h + glw_settings.gs_underscan_h,
                  0, 100);

  if(gr->gr_underscan_h != val) {
    prop_set(gr->gr_prop_ui, "underscan_h", PROP_SET_INT, val);
    gr->gr_underscan_h = val;
    gr->gr_need_refresh = GLW_REFRESH_FLAG_LAYOUT | GLW_REFRESH_FLAG_RENDER;
  }


  val = GLW_CLAMP(gr->gr_base_underscan_v + glw_settings.gs_underscan_v,
                  0, 100);

  if(gr->gr_underscan_v != val) {
    prop_set(gr->gr_prop_ui, "underscan_v", PROP_SET_INT, val);
    gr->gr_underscan_v = val;
    gr->gr_need_refresh = GLW_REFRESH_FLAG_LAYOUT | GLW_REFRESH_FLAG_RENDER;
  }
}


/**
 *
 */
static void
glw_update_size(glw_root_t *gr)
{
  int val;
  int bs1 = gr->gr_height / 35; // 35 is just something
  //  int bs2 = gr->gr_width  / 65; // 65 is another value

  int base_size = bs1; // MIN(bs1, bs2);

  val = GLW_CLAMP(base_size + glw_settings.gs_size +
                  gr->gr_skin_scale_adjustment, 8, 80);

  if(gr->gr_current_size != val) {
    gr->gr_current_size = val;
    prop_set(gr->gr_prop_ui, "size", PROP_SET_INT, val);
    glw_text_flush(gr);
    glw_icon_flush(gr);
    glw_update_em(gr);
    TRACE(TRACE_DEBUG, "GLW",
          "UI size scale changed to %d (user adj: %d  skin adj: %d) ",
          val, glw_settings.gs_size, gr->gr_skin_scale_adjustment);
  }
  glw_update_underscan(gr);
}


/**
 *
 */
static void
glw_sizeoffset_callback(void *opaque, int value)
{
  glw_root_t *gr = opaque;
  gr->gr_skin_scale_adjustment = value;
}


/**
 *
 */
static void
glw_dis_screensaver_callback(void *opaque, int value)
{
  glw_root_t *gr = opaque;
  gr->gr_inhibit_screensaver = value;
  TRACE(TRACE_DEBUG, "GLW", "Screensaver %s",
        value ? "inhibited" : "allowed");
}

/**
 *
 */
int
glw_init(glw_root_t *gr)
{
  return glw_init2(gr, 0);
}

/**
 *
 */
int
glw_init2(glw_root_t *gr, int flags)
{
  return glw_init4(gr, &prop_courier_poll_timed, prop_courier_create_passive(),
                   flags);
}



/**
 *
 */
int
glw_init4(glw_root_t *gr,
          void (*dispatcher)(prop_courier_t *pc, int timeout),
          prop_courier_t *courier,
          int flags)
{
  char skinbuf[PATH_MAX];
  const char *skin = gconf.skin;
  prop_t *p;

  atomic_set(&gr->gr_refcount, 1);

  if(gr->gr_prop_core == NULL)
    gr->gr_prop_core = prop_get_global();

  gr->gr_prop_dispatcher = dispatcher;
  gr->gr_courier = courier;
  gr->gr_init_flags = flags;
  gr->gr_prop_maxtime = -1;

  assert(glw_settings.gs_settings != NULL);

  p = prop_create(prop_get_global(), "userinterfaces");

  if(prop_set_parent(gr->gr_prop_ui, p))
    abort();

  if(skin == NULL) {
    snprintf(skinbuf, sizeof(skinbuf),
             "%s/glwskins/"SHOWTIME_GLW_DEFAULT_SKIN, app_dataroot());
    skin = skinbuf;
  }
  hts_mutex_init(&gr->gr_mutex);
  gr->gr_token_pool = pool_create("glwtokens", sizeof(token_t), POOL_ZERO_MEM);
  gr->gr_clone_pool = pool_create("glwclone", sizeof(glw_clone_t),
                                  POOL_ZERO_MEM);
  gr->gr_style_binding_pool = pool_create("glwstylebindings",
                                          sizeof(glw_style_binding_t), 0);

  gr->gr_user_underscan_h = INT32_MIN;
  gr->gr_user_underscan_v = INT32_MIN;

  gr->gr_skin = strdup(skin);

  gr->gr_font_domain = freetype_get_context();

  glw_text_bitmap_init(gr);

  prop_setv(gr->gr_prop_ui, "skin", "path", NULL,
            PROP_SET_STRING, skin);

  gr->gr_pointer_visible    = prop_create(gr->gr_prop_ui, "pointerVisible");
  gr->gr_screensaver_active = prop_create(gr->gr_prop_ui, "screensaverActive");
  gr->gr_prop_width         = prop_create(gr->gr_prop_ui, "width");
  gr->gr_prop_height        = prop_create(gr->gr_prop_ui, "height");
  gr->gr_prop_aspect        = prop_create(gr->gr_prop_ui, "aspect");

  prop_set_int(gr->gr_screensaver_active, 0);

  if(flags & GLW_INIT_KEYBOARD_MODE)
    glw_set_keyboard_mode(gr, 1);

  if(flags & GLW_INIT_IN_FULLSCREEN)
    gr->gr_is_fullscreen = 1;

  gr->gr_evsub =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK, glw_eventsink, gr,
                   PROP_TAG_NAME("ui", "eventSink"),
                   PROP_TAG_ROOT, gr->gr_prop_ui,
                   PROP_TAG_COURIER, gr->gr_courier,
                   NULL);

  gr->gr_scalesub =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK_INT, glw_sizeoffset_callback, gr,
                   PROP_TAG_NAME("ui", "sizeOffset"),
                   PROP_TAG_ROOT, gr->gr_prop_ui,
                   PROP_TAG_COURIER, gr->gr_courier,
                   NULL);

  gr->gr_disable_screensaver_sub =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK_INT, glw_dis_screensaver_callback, gr,
                   PROP_TAG_NAME("ui", "disableScreensaver"),
                   PROP_TAG_ROOT, gr->gr_prop_ui,
                   PROP_TAG_COURIER, gr->gr_courier,
                   NULL);

  TAILQ_INIT(&gr->gr_destroyer_queue);

  TAILQ_INIT(&gr->gr_view_load_requests);
  TAILQ_INIT(&gr->gr_view_eval_requests);
  hts_cond_init(&gr->gr_view_loader_cond, &gr->gr_mutex);

  glw_tex_init(gr);

  gr->gr_frontface = GLW_CCW;


  gr->gr_framerate = 60;
  gr->gr_frameduration = 1000000 / gr->gr_framerate;
  gr->gr_ui_start = arch_get_ts();
  gr->gr_frame_start = gr->gr_ui_start;
  glw_register_activity(gr);
  gr->gr_open_osk = glw_osk_open_default;
  return 0;
}


/**
 *
 */
void
glw_fini(glw_root_t *gr)
{
  if(gr->gr_osk_widget != NULL) {
    glw_unref(gr->gr_osk_widget);
    prop_unsubscribe(gr->gr_osk_text_sub);
    prop_unsubscribe(gr->gr_osk_ev_sub);
  }

  gr->gr_view_loader_run = 0;
  hts_cond_signal(&gr->gr_view_loader_cond);

  glw_text_bitmap_fini(gr);
  rstr_release(gr->gr_default_font);
  glw_tex_fini(gr);
  prop_unsubscribe(gr->gr_evsub);
  prop_unsubscribe(gr->gr_scalesub);
  prop_unsubscribe(gr->gr_disable_screensaver_sub);
  prop_courier_destroy(gr->gr_courier);

  /*
   * The view loader thread sometimes run with gr_mutex unlocked
   * and when doing so it expects certain variables in glw_root to
   * be available.
   *
   * gr_vpaths (indirectly gr_skin) must be intact
   *
   * It may also allocate items from gr_token_pool (although not while
   * locked but after we've asked it to exit), thus we must not
   * destroy the pool until after the thread has joined.
   *
   */

  if(gr->gr_view_loader_thread)
    hts_thread_join(&gr->gr_view_loader_thread);

  glw_view_loader_flush(gr);

  glw_style_cleanup(gr);

  pool_destroy(gr->gr_token_pool);
  pool_destroy(gr->gr_clone_pool);
  pool_destroy(gr->gr_style_binding_pool);

  free(gr->gr_vtmp_buffer);
  free(gr->gr_render_jobs);
  free(gr->gr_render_order);
  free(gr->gr_vertex_buffer);
  free(gr->gr_index_buffer);
  rstr_release(gr->gr_pending_focus);
}

/**
 *
 */
void
glw_release_root(glw_root_t *gr)
{
  if(atomic_dec(&gr->gr_refcount))
    return;

  hts_mutex_destroy(&gr->gr_mutex);
  free(gr->gr_skin);
  free(gr);
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
  char buf[PATH_MAX];
  glw_unload_universe(gr);

  fa_pathjoin(buf, sizeof(buf), gr->gr_skin, "universe.view");

  rstr_t *universe = rstr_alloc(buf);

  glw_scope_t *scope = glw_scope_create();

  scope->gs_roots[GLW_ROOT_CORE].p = prop_ref_inc(gr->gr_prop_core);

  gr->gr_universe = glw_view_create(gr, universe, NULL, NULL, scope,
                                    NULL, 0);

  rstr_release(universe);
  glw_scope_release(scope);
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
glw_layout0(glw_t *w, const glw_rctx_t *rc)
{
  glw_root_t *gr = w->glw_root;
  int mask = GLW_VIEW_EVAL_LAYOUT;

  if(likely(!rc->rc_invisible)) {
    LIST_REMOVE(w, glw_active_link);
    LIST_INSERT_HEAD(&gr->gr_active_list, w, glw_active_link);

    if(unlikely(!(w->glw_flags & GLW_ACTIVE))) {
      w->glw_flags |= GLW_ACTIVE;
      mask |= GLW_VIEW_EVAL_ACTIVE;
      glw_signal0(w, GLW_SIGNAL_ACTIVE, NULL);
    }
  }

  if(unlikely(rc->rc_preloaded != !!(w->glw_flags & GLW_PRELOADED))) {

    if(rc->rc_preloaded)
      w->glw_flags |= GLW_PRELOADED;
    else
      w->glw_flags &= ~GLW_PRELOADED;

    mask |= GLW_VIEW_EVAL_ACTIVE;
  }

  if(unlikely(w->glw_dynamic_eval & mask))
    glw_view_eval_layout(w, rc, mask);

  if(unlikely(w->glw_flags & GLW_HAVE_MARGINS)) {
    glw_rctx_t rc0 = *rc;
    glw_reposition(&rc0,
                   w->glw_margin[0],
                   rc->rc_height - w->glw_margin[1],
                   rc->rc_width - w->glw_margin[2],
                   w->glw_margin[3]);

    if(rc0.rc_width < 1 || rc0.rc_height < 1)
      return;

    w->glw_class->gc_layout(w, &rc0);
  } else {
    w->glw_class->gc_layout(w, rc);
  }
}


void
glw_render0(glw_t *w, const glw_rctx_t *rc)
{
  if(unlikely(w->glw_zoffset)) {
    glw_rctx_t rc0 = *rc;
    int zmax = 0;
    rc0.rc_zmax = &zmax;
    rc0.rc_zindex = w->glw_zoffset;


    if(unlikely(w->glw_flags & GLW_HAVE_MARGINS)) {
      glw_reposition(&rc0,
                     w->glw_margin[0],
                     rc->rc_height - w->glw_margin[1],
                     rc->rc_width - w->glw_margin[2],
                     w->glw_margin[3]);
      if(rc0.rc_width < 1 || rc0.rc_height < 1)
        return;
    }
    w->glw_class->gc_render(w, &rc0);

  } else if(unlikely(w->glw_flags & GLW_HAVE_MARGINS)) {
    glw_rctx_t rc0 = *rc;
    glw_reposition(&rc0,
                   w->glw_margin[0],
                   rc->rc_height - w->glw_margin[1],
                   rc->rc_width - w->glw_margin[2],
                   w->glw_margin[3]);
    if(rc0.rc_width < 1 || rc0.rc_height < 1)
      return;

    w->glw_class->gc_render(w, &rc0);
  } else {
    w->glw_class->gc_render(w, rc);
  }

  if(unlikely(w->glw_flags2 & GLW2_DEBUG))
    glw_wirebox(w->glw_root, rc);
}



/**
 *
 */
glw_t *
glw_create(glw_root_t *gr, const glw_class_t *class,
           glw_t *parent, glw_t *before, prop_t *originator,
           glw_scope_t *scope, rstr_t *file, int line)
{
  glw_t *w;

  /* Common initializers */
  w = calloc(1, class->gc_instance_size +
             (parent ? parent->glw_class->gc_parent_data_size : 0));
  w->glw_root = gr;
  w->glw_class = class;
  w->glw_alpha = 1.0f;
  w->glw_sharpness = 1.0f;
  w->glw_refcnt = 1;
  w->glw_alignment = class->gc_default_alignment;
  w->glw_flags2 = GLW2_ENABLED | GLW2_NAV_FOCUSABLE | GLW2_CURSOR;
  w->glw_file = rstr_dup(file);
  w->glw_line = line;

  if(likely(parent != NULL))
    w->glw_styles = glw_styleset_retain(parent->glw_styles);

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

  w->glw_scope = glw_scope_retain(scope);

  if(class->gc_ctor != NULL)
    class->gc_ctor(w);

  return w;
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
  if(gr->gr_screensaver_force_enable)
    return 1;

  if(gr->gr_inhibit_screensaver) {
    gr->gr_screensaver_reset_at = gr->gr_frame_start;
    return 0;
  }

  if(!gr->gr_is_fullscreen) {
    return 0;
  }

  int d = glw_settings.gs_screensaver_delay;

  if(!d)
    return 0;

  return gr->gr_frame_start > gr->gr_screensaver_reset_at + d * 60000000LL;
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
glw_idle(glw_root_t *gr)
{
  if(gr->gr_prop_dispatcher != NULL)
    gr->gr_prop_dispatcher(gr->gr_courier, gr->gr_prop_maxtime);
  glw_reap(gr);
}

/**
 *
 */
void
glw_prepare_frame(glw_root_t *gr, int flags)
{
  glw_t *w;

  glw_update_size(gr);

  gr->gr_frame_start        = arch_get_ts();
  gr->gr_frame_start_avtime = arch_get_avtime();
  gr->gr_time_usec          = gr->gr_frame_start - gr->gr_ui_start;
  gr->gr_time_sec           = gr->gr_time_usec / 1000000.0f;

  if(!(flags & GLW_NO_FRAMERATE_UPDATE)) {

    if(likely(gr->gr_frames > 16)) {
      int64_t d = gr->gr_frame_start - gr->gr_framerate_avg[gr->gr_frames & 0xf];
      double hz = 16000000.0 / d;
      prop_set(gr->gr_prop_ui, "framerate", PROP_SET_FLOAT, hz);
      gr->gr_framerate = hz;
    }

    gr->gr_framerate_avg[gr->gr_frames & 0xf] = gr->gr_frame_start;
  }
  gr->gr_frames++;

  gr->gr_num_render_jobs = 0;
  gr->gr_vertex_offset = 0;
  gr->gr_index_offset = 0;

  prop_set_int(gr->gr_screensaver_active, glw_screensaver_is_active(gr));
  prop_set_int(gr->gr_prop_width, gr->gr_width);
  prop_set_int(gr->gr_prop_height, gr->gr_height);
  prop_set_float(gr->gr_prop_aspect, (float)gr->gr_width / gr->gr_height);

  if(gr->gr_prop_dispatcher != NULL)
    gr->gr_prop_dispatcher(gr->gr_courier, gr->gr_prop_maxtime);

  LIST_FOREACH(w, &gr->gr_every_frame_list, glw_every_frame_link)
    w->glw_class->gc_newframe(w, flags);

  if(gr->gr_need_refresh) {

    while((w = LIST_FIRST(&gr->gr_active_flush_list)) != NULL) {
      LIST_REMOVE(w, glw_active_link);
      LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);
      w->glw_flags &= ~GLW_ACTIVE;
      glw_signal0(w, GLW_SIGNAL_INACTIVE, NULL);
    }

    glw_reap(gr);
  }

  if(gr->gr_mouse_valid) {
    glw_pointer_event_t gpe;
    gpe.ts = 0;
    gpe.screen_x = gr->gr_mouse_x;
    gpe.screen_y = gr->gr_mouse_y;
    gpe.type = GLW_POINTER_MOTION_REFRESH;
    glw_pointer_event(gr, &gpe);
  }

  if(unlikely(gr->gr_pointer_press != NULL) &&
     gr->gr_pointer_press_time &&
     gr->gr_frame_start > gr->gr_pointer_press_time + 500000) {
    // touch longpress
    glw_touch_longpress(gr);
  }


  if(gr->gr_delayed_focus_leave) {
    if(--gr->gr_delayed_focus_leave == 0 && gr->gr_current_focus) {
      glw_focus_leave(gr->gr_current_focus);
    }
  }

  if(gr->gr_scheduled_refresh <= gr->gr_frame_start) {
    gr->gr_need_refresh = GLW_REFRESH_FLAG_LAYOUT | GLW_REFRESH_FLAG_RENDER;
    gr->gr_scheduled_refresh = INT64_MAX;
  }

  if(gr->gr_rec)
    gr->gr_need_refresh = GLW_REFRESH_FLAG_LAYOUT | GLW_REFRESH_FLAG_RENDER;

  glw_view_loader_eval(gr);
}


/**
 *
 */
void
glw_post_scene(glw_root_t *gr)
{
  glw_renderer_render(gr);
#if CONFIG_GLW_REC
  if(gr->gr_rec != NULL) {
    pixmap_t *pm = gr->gr_br_read_pixels(gr);
    glw_rec_deliver_vframe(gr->gr_rec, pm);
    pixmap_release(pm);
  }
#endif
}

/*
 *
 */
void
glw_unref(glw_t *w)
{
  if(w->glw_refcnt > 1) {
    w->glw_refcnt--;
    return;
  }
  assert(w->glw_clone == NULL);
  rstr_release(w->glw_file);
  glw_scope_release(w->glw_scope);
  free(w);
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

  if(gr->gr_last_focus == w)
    gr->gr_last_focus = NULL;

  w->glw_flags |= GLW_DESTROYING;

  if(w->glw_originating_prop != NULL)
    prop_ref_dec(w->glw_originating_prop);

  if(gr->gr_pointer_grab == w)
    gr->gr_pointer_grab = NULL;

  if(gr->gr_pointer_hover == w)
    glw_root_set_hover(gr, NULL);

  if(gr->gr_pointer_press == w)
    gr->gr_pointer_press = NULL;

  glw_prop_subscription_destroy_list(gr, &w->glw_prop_subscriptions);

  while((gem = LIST_FIRST(&w->glw_event_maps)) != NULL) {
    LIST_REMOVE(gem, gem_link);
    glw_event_map_destroy(gr, gem);
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

  glw_style_unbind_all(w);

  glw_styleset_release(w->glw_styles);

  rstr_release(w->glw_id_rstr);

  TAILQ_INSERT_TAIL(&gr->gr_destroyer_queue, w, glw_parent_link);

  glw_view_free_chain(gr, w->glw_dynamic_expressions);
}


/**
 *
 */
void
glw_destroy_childs(glw_t *w)
{
  glw_t *c;
  while((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_destroy(c);
}


/*
 *
 */
void
glw_signal_handler_register(glw_t *w, glw_callback_t *func, void *opaque)
{
  glw_signal_handler_t *gsh;

  LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link) {
    if(gsh->gsh_func == func && gsh->gsh_opaque == opaque)
      return;

  }

  gsh = malloc(sizeof(glw_signal_handler_t));
  gsh->gsh_func   = func;
  gsh->gsh_opaque = opaque;
  gsh->gsh_defer_remove = 0;

  LIST_INSERT_HEAD(&w->glw_signal_handlers, gsh, gsh_link);
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
void
glw_signal0(glw_t *w, glw_signal_t sig, void *extra)
{
  glw_signal_handler_t *x, *gsh = LIST_FIRST(&w->glw_signal_handlers);
  int r;

  if(w->glw_class->gc_signal_handler != NULL)
    w->glw_class->gc_signal_handler(w, NULL, sig, extra);

  glw_view_eval_signal(w, sig);

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
        return;
    }
    gsh = LIST_NEXT(gsh, gsh_link);
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
  glw_t *p = w->glw_parent;
  glw_root_t *gr = w->glw_root;
  int was_first = TAILQ_FIRST(&p->glw_childs) == w && w == p->glw_focused;

  TAILQ_REMOVE(&p->glw_childs, w, glw_parent_link);

  if(b == NULL) {
    TAILQ_INSERT_TAIL(&p->glw_childs, w, glw_parent_link);
  } else {
    TAILQ_INSERT_BEFORE(b, w, glw_parent_link);
  }
  if(p->glw_flags & GLW_FLOATING_FOCUS) {
    if(w == TAILQ_FIRST(&p->glw_childs)) {
      glw_t *w2 = TAILQ_NEXT(w, glw_parent_link);
      if(w2 != NULL && p->glw_focused == w2) {
        glw_t *c = glw_focus_by_path(w);
        glw_focus_set(gr, c, GLW_FOCUS_SET_AUTOMATIC_FF, "Move");
      }
    } else if(was_first) {
      glw_t *w2 = TAILQ_FIRST(&p->glw_childs);
      glw_t *c = glw_focus_by_path(w2);
      glw_focus_set(gr, c, GLW_FOCUS_SET_AUTOMATIC_FF, "Move");
    }
  }
  glw_signal0(p, GLW_SIGNAL_CHILD_MOVED, w);
  glw_need_refresh(gr, 0);
}

/**
 *
 */
static void
glw_fhp_update(glw_t *w, int or, int and)
{
  if(w->glw_flags & GLW_DESTROYING)
    return;

  if(w->glw_flags2 & GLW2_SELECT_ON_FOCUS && w->glw_originating_prop &&
     !(w->glw_flags & GLW_IN_FOCUS_PATH) && (or & GLW_IN_FOCUS_PATH)) {
    prop_select(w->glw_originating_prop);
  }

  if(w->glw_flags2 & GLW2_SELECT_ON_HOVER && w->glw_originating_prop &&
     !(w->glw_flags & GLW_IN_HOVER_PATH) && (or & GLW_IN_HOVER_PATH)) {
    prop_select(w->glw_originating_prop);
  }

  w->glw_flags = (w->glw_flags | or) & and;
  glw_signal0(w, GLW_SIGNAL_FHP_PATH_CHANGED, NULL);
}


/**
 *
 */
static void
glw_path_flood(glw_t *w, int or, int and)
{
  glw_t *c;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags2 & GLW2_CLICKABLE ||
       c->glw_focus_weight > 0)
      continue;
    glw_path_flood(c, or, and);
    glw_fhp_update(c, or, and);
  }
}


/**
 *
 */
void
glw_path_modify(glw_t *w, int set, int clr, glw_t *stop)
{
  clr = ~clr; // Invert so we can just AND it

  glw_path_flood(w, set, clr);

  for(; w != NULL; w = w->glw_parent) {

    int old_flags = w->glw_flags;
    glw_fhp_update(w, set, clr);

    if((old_flags ^ w->glw_flags) & GLW_IN_FOCUS_PATH) {
      glw_event_glw_action(w, w->glw_flags & GLW_IN_FOCUS_PATH ?
                           "GainedFocus" : "LostFocus");
    }

    if(w->glw_flags & GLW_FHP_SPILL_TO_CHILDS) {
      glw_t *c;
      TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
        if(c->glw_flags2 & GLW2_FHP_SPILL) {
          glw_fhp_update(c, set, clr);
          glw_path_flood(c, set, clr);
        }
      }
    }
    if(w == stop)
      break;
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

  glw_need_refresh(gr, 0);
}


/**
 *
 */
void
glw_set_focus_weight(glw_t *w, float f, glw_style_t *gs)
{
  if(w->glw_class->gc_set_focus_weight != NULL) {
    w->glw_class->gc_set_focus_weight(w, f, gs);
    return;
  }

  if(f == w->glw_focus_weight)
    return;

  if(w->glw_focus_weight > 0 && w->glw_root->gr_current_focus == w) {
    w->glw_root->gr_delayed_focus_leave = 2;
  }

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
    if(w->glw_focused->glw_flags &
       (GLW_FOCUS_BLOCKED | GLW_DESTROYING | GLW_HIDDEN))
      return NULL;
    w = w->glw_focused;
  }

  if(w->glw_focus_weight == 0)
    return NULL;
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
static int
check_autofocus_limit(glw_t *n, glw_t *o)
{
  int limit = 0;
  glw_t *w, *x;

  // Mark new tree
  for(w = n; w != NULL; w = w->glw_parent)
    w->glw_flags |= GLW_MARK;

  // Scan old tree and try to find fork point
  for(x = o; x != NULL; x = x->glw_parent) {
    if(x->glw_flags & GLW_MARK)
      break;
  }

  // Scan new tree up to intersection point
  for(w = n; w != NULL && w != x; w = w->glw_parent) {
    if(w->glw_flags2 & GLW2_AUTO_FOCUS_LIMIT) {
      limit = 1;
      break;
    }
  }

  // Unmark
  for(w = n; w != NULL; w = w->glw_parent)
    w->glw_flags &= ~GLW_MARK;

  return limit;
}


/**
 *
 */
int
glw_focus_set(glw_root_t *gr, glw_t *w, int how, const char *whom)
{
  glw_t *x, *y, *com;
  glw_signal_t sig;
  float weight = w ? w->glw_focus_weight : 0;

  if(gr->gr_focus_work)
    return 0;

  gr->gr_focus_work = 1;

  if(how == GLW_FOCUS_SET_AUTOMATIC ||
     how == GLW_FOCUS_SET_AUTOMATIC_FF) {
    sig = GLW_SIGNAL_FOCUS_CHILD_AUTOMATIC;
  } else {
    sig = GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE;
  }

  if(w != NULL) {


    if(how != GLW_FOCUS_SET_INTERACTIVE) {
      if(check_autofocus_limit(w, gr->gr_last_focus)) {
        gr->gr_focus_work = 0;
        return 0;
      }
    }

    for(x = w; x->glw_parent != NULL; x = x->glw_parent) {

      if(sig != GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE &&
         (x->glw_flags & GLW_FOCUS_BLOCKED ||
          x->glw_flags & GLW_HIDDEN)) {
        gr->gr_focus_work = 0;
        return 0;
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
        int ff = p->glw_flags & GLW_FLOATING_FOCUS &&
          (x == TAILQ_FIRST(&p->glw_childs) ||
           how == GLW_FOCUS_SET_AUTOMATIC_FF);

        if(y == NULL || how == GLW_FOCUS_SET_INTERACTIVE ||
           weight > y->glw_focus_weight ||
           (ff && weight == y->glw_focus_weight)) {
          x->glw_parent->glw_focused = x;
#if 0
          printf("Signal %s child %p focused %d %f %f %d\n",
                 x->glw_parent->glw_class->gc_name,
                 x, how, weight, w->glw_focus_weight, ff);
#endif
          glw_signal0(x->glw_parent, sig, x);
        } else {
          /* Other path outranks our weight, stop now */
          gr->gr_focus_work = 0;
          return 0;
        }
      }
    }
  }

  if(gr->gr_current_focus == w) {
    gr->gr_focus_work = 0;
    return 1;
  }
  com = find_common_ancestor(gr->gr_current_focus, w);

  glw_t *ww = gr->gr_current_focus;

  if(ww != NULL)
    glw_path_modify(ww, 0, GLW_IN_FOCUS_PATH, com);

  gr->gr_current_focus = w;
  gr->gr_delayed_focus_leave = 0;

  if(w != NULL) {
    glw_need_refresh(gr, 0);

    GLW_TRACE("Focus set to %s:%d by %s",
              rstr_get(w->glw_file), w->glw_line, whom);

#if 0
    glw_t *t = w->glw_parent;
    while(t != NULL) {
      printf("Parent %s\n", glw_get_name(t));
      t = t->glw_parent;
    }
#endif

    gr->gr_last_focus = w;

    glw_path_modify(w, GLW_IN_FOCUS_PATH, 0, NULL);


    if(how) {
      prop_t *p = get_originating_prop(w);

      if(p != NULL) {

        if(gr->gr_last_focused_interactive != NULL)
          prop_ref_dec(gr->gr_last_focused_interactive);

        gr->gr_last_focused_interactive = prop_ref_inc(p);
      }
    }
  } else {
    GLW_TRACE("Focus set to none by %s", whom);
  }
  gr->gr_focus_work = 0;
  if(how == GLW_FOCUS_SET_INTERACTIVE)
    rstr_set(&gr->gr_pending_focus, NULL);
  return 1;
}



/**
 *
 */
void
glw_focus_check_pending(glw_t *w)
{
  glw_root_t *gr = w->glw_root;
  if(rstr_eq(gr->gr_pending_focus, w->glw_id_rstr)) {
    w = glw_get_focusable_child(w);
    if(w != NULL)
      glw_focus_set(gr, w, GLW_FOCUS_SET_INTERACTIVE, "FocusMethodDelayed");
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
  int how = GLW_FOCUS_SET_AUTOMATIC;

  if(w->glw_flags2 & GLW2_AUTOREFOCUSABLE && was_interactive(w))
    how = GLW_FOCUS_SET_INTERACTIVE;

  glw_focus_set(w->glw_root, w, how, "Init");
}


/**
 *
 */
static glw_t *
glw_focus_find_focusable(glw_t *w, glw_t *cur)
{
  glw_t *c, *r;

  if(w->glw_focused != NULL) {
    c = w->glw_focused;
    if(!(c->glw_flags & (GLW_DESTROYING | GLW_FOCUS_BLOCKED))) {
      if(glw_is_focusable(c))
        return c;
      if(TAILQ_FIRST(&c->glw_childs)) {
        if((r = glw_focus_find_focusable(c, NULL)) != NULL)
          return r;
      }
    }
  }

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
      if((r = glw_focus_find_focusable(c, NULL)) != NULL)
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
      r = glw_focus_find_focusable(w->glw_parent, w);
      if(r != NULL)
        break;
    }
    w = w->glw_parent;
  }
  glw_focus_set(w->glw_root, r, GLW_FOCUS_SET_INTERACTIVE, "FocusLeave");
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

    if(!(c->glw_flags & (GLW_FOCUS_BLOCKED | GLW_HIDDEN))) {
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
glw_focus_crawl(glw_t *w, int forward, int interactive)
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
    glw_focus_set(w->glw_root, r,
                  interactive ? GLW_FOCUS_SET_INTERACTIVE :
                  GLW_FOCUS_SET_AUTOMATIC, "FocusCrawl");
}



/**
 *
 */
void
glw_focus_open_path_close_all_other(glw_t *w)
{
  glw_t *c;
  glw_t *p = w->glw_parent;
  int do_clear = 0;
  TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link) {
    if(c == w)
      continue;
    c->glw_flags |= GLW_FOCUS_BLOCKED;

    if(c->glw_flags & GLW_IN_FOCUS_PATH && p->glw_focused == c) {
      do_clear = 1;
    }
  }

  w->glw_flags &= ~GLW_FOCUS_BLOCKED;
  c = glw_focus_by_path(w);

  if(c != NULL) {
    glw_focus_set(w->glw_root, c, GLW_FOCUS_SET_AUTOMATIC,
                  "OpenCloseFound");
    return;
  } else if(p->glw_parent->glw_focused == p && do_clear) {
    glw_t *r = glw_focus_crawl1(w, 1);
    if(r != NULL) {
      glw_focus_set(w->glw_root, r, GLW_FOCUS_SET_AUTOMATIC,
                    "OpenCloseCrawlDown");
      return;
    }

    while(w->glw_parent != NULL) {
      if((r = glw_focus_find_focusable(w->glw_parent, w)) != NULL) {
        glw_focus_set(w->glw_root, r, GLW_FOCUS_SET_AUTOMATIC,
                      "OpenCloseCrawlUp");
        return;
      }
      w = w->glw_parent;
    }
  }

  if(do_clear)
    glw_focus_set(w->glw_root, NULL, GLW_FOCUS_SET_AUTOMATIC,
                  "OpenCloseNone");

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
  if(c != NULL)
    glw_focus_set(w->glw_root, c, GLW_FOCUS_SET_AUTOMATIC,
                  "OpenPath");
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
  e->e_nav = prop_ref_inc(w->glw_root->gr_prop_nav);

  while(w->glw_focused != NULL) {
    w = w->glw_focused;
    if(glw_is_focusable(w)) {
      if(glw_event_to_widget(w, e))
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
glw_is_child_focusable(glw_t *w)
{
  glw_t *c;
  if(glw_is_focusable(w))
    return 1;
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(glw_is_child_focusable(c))
      return 1;
  }
  return 0;
}


/**
 * Find a focusable child given a widget
 *
 * Initially we try to follow the current focus path otherwise
 * we try to locate any focusable widget that's a child
 *
 */
glw_t *
glw_get_focusable_child(glw_t *w)
{
  if(w == NULL)
    return NULL;

  glw_t *c = glw_focus_by_path(w);

  if(c == NULL)
    c = glw_focus_crawl1(w, 1);

  return c;
}


/**
 *
 */
int
glw_focus_child(glw_t *w)
{
  glw_t *c = glw_get_focusable_child(w);
  if(c == NULL)
    return 0;

  glw_focus_set(w->glw_root, c, GLW_FOCUS_SET_INTERACTIVE, "FocusChild");
  return 1;
}


/**
 *
 */
static int
glw_root_event_handler(glw_root_t *gr, event_t *e)
{
  if(e->e_type == EVENT_KEYDESC)
    return 0;

  if(event_is_action(e, ACTION_ENABLE_SCREENSAVER)) {
    gr->gr_screensaver_force_enable = 1;

  } else if(event_is_action(e, ACTION_NAV_BACK) ||
            event_is_action(e, ACTION_NAV_FWD) ||
            event_is_action(e, ACTION_HOME) ||
            event_is_action(e, ACTION_PLAYQUEUE) ||
            event_is_action(e, ACTION_RELOAD_DATA) ||
            event_is_type(e, EVENT_OPENURL)) {

    prop_t *p = prop_get_by_name(PNVEC("nav", "eventSink"), 0,
                                 PROP_TAG_NAMED_ROOT, gr->gr_prop_nav, "nav",
                                 NULL);
    prop_send_ext_event(p, e);
    prop_ref_dec(p);
  } else {
    event_addref(e);
    event_dispatch(e);
  }
  return 0;
}



/**
 *
 */
int
glw_event_to_widget(glw_t *w, event_t *e)
{
  glw_root_t *gr = w->glw_root;

  // First, descend in the view hierarchy

  GLW_TRACE("Event '%s' route start at widget '%s'%s",
            event_sprint(e), glw_get_name(w),
            gr->gr_current_focus == NULL ? ", Nothing is focused" : "");

  if(glw_event_map_intercept(w, e, 1)) {
    // glw_event_map_intercept() does GLW_TRACE() by itself
    return 1;
  }

  while(1) {

    if(!glw_path_in_focus(w))
      break;

    if(w->glw_flags2 & GLW2_POSITIONAL_NAVIGATION &&
       glw_navigate_matrix(w, e)) {
      GLW_TRACE("Event '%s' intercepted by matrix-nav at '%s' (descending)",
                event_sprint(e), glw_get_name(w));
      return 1;
    }

    if(glw_send_event2(w, e)) {
      GLW_TRACE("Event '%s' intercepted by widget '%s' (descending)",
                event_sprint(e), glw_get_name(w));
      return 1;
    }

    if(w->glw_focused == NULL)
      break;

    w = w->glw_focused;
    if(glw_event_map_intercept(w, e, 1)) {
      // glw_event_map_intercept() does GLW_TRACE() by itself
      return 1;
    }

  }

  // Then ascend all the way up to root

  GLW_TRACE("Event '%s' bounced at widget '%s'",
            event_sprint(e), glw_get_name(w));

  while(w != NULL) {
    w->glw_flags &= ~GLW_FLOATING_FOCUS; // Correct ??

    if(glw_event_map_intercept(w, e, 0))
      return 1;

    if(glw_bubble_event2(w, e)) {
      GLW_TRACE("Event '%s' intercepted by widget '%s' (ascending)",
                event_sprint(e), glw_get_name(w));
      return 1;
    }
    w = w->glw_parent;
  }

  GLW_TRACE("Event '%s' relayed to root handler",
            event_sprint(e));

      // Nothing grabbed the event, default it

  return glw_root_event_handler(gr, e);
}


/**
 *
 */
static int
glw_event(glw_root_t *gr, event_t *e)
{
  if(gr->gr_current_focus != NULL) {
    if(event_is_action(e, ACTION_FOCUS_NEXT)) {
      glw_focus_crawl(gr->gr_current_focus, 1, 1);
      return 1;
    }
    if(event_is_action(e, ACTION_FOCUS_PREV)) {
      glw_focus_crawl(gr->gr_current_focus, 0, 1);
      return 1;
    }
  }
  return glw_event_to_widget(gr->gr_universe, e);
}


/**
 *
 */
int
glw_pointer_event_deliver(glw_t *w, glw_pointer_event_t *gpe)
{
  glw_root_t *gr = w->glw_root;
  event_t *e;

  if(glw_send_pointer_event(w, gpe))
    return 1;

  if(!glw_is_focusable_or_clickable(w))
    return 0;

  int r;
  int flags = 0;
  switch(gpe->type) {

  case GLW_POINTER_RIGHT_PRESS:
    e = event_create_action(ACTION_ITEMMENU);
    e->e_nav = prop_ref_inc(gr->gr_prop_nav);
    e->e_flags |= EVENT_MOUSE | EVENT_SCREEN_POSITION;
    e->e_screen_x = gpe->screen_x;
    e->e_screen_y = gpe->screen_y;
    r = glw_event_to_widget(w, e);
    event_release(e);
    return r;

  case GLW_POINTER_TOUCH_START:
    gr->gr_pointer_press_time = gr->gr_frame_start;
    // FALLTHRU
  case GLW_POINTER_LEFT_PRESS:
    gr->gr_pointer_press = w;
    glw_path_modify(w, GLW_IN_PRESSED_PATH, 0, NULL);
    return 1;

  case GLW_POINTER_LEFT_RELEASE:
    flags = EVENT_MOUSE;
    // FALLTHRU
  case GLW_POINTER_TOUCH_END:
    if(gr->gr_pointer_press == w) {
      if(w->glw_flags2 & GLW2_FOCUS_ON_CLICK)
        glw_focus_set(gr, w, GLW_FOCUS_SET_INTERACTIVE, "LeftPress");

      glw_path_modify(w, 0, GLW_IN_PRESSED_PATH, NULL);
      e = event_create_action_multi((const action_type_t[]){
          ACTION_CLICK, ACTION_ACTIVATE}, 2);
      e->e_nav = prop_ref_inc(gr->gr_prop_nav);
      e->e_flags |= flags | EVENT_SCREEN_POSITION;
      e->e_screen_x = gpe->screen_x;
      e->e_screen_y = gpe->screen_y;
      glw_event_to_widget(w, e);
      event_release(e);
      gr->gr_pointer_press = NULL;
    }
    return 1;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_touch_longpress(glw_root_t *gr)
{
  gr->gr_pointer_press_time = 0;
  glw_t *w = gr->gr_pointer_press;
  event_t *e = event_create_action(ACTION_ITEMMENU);
  e->e_nav = prop_ref_inc(gr->gr_prop_nav);
  int r = glw_event_to_widget(w, e);
  event_release(e);
  if(r) {
    glw_path_modify(w, 0, GLW_IN_PRESSED_PATH, NULL);
    gr->gr_pointer_press = NULL;
  }
}

/**
 *
 */
int
glw_pointer_event0(glw_root_t *gr, glw_t *w, glw_pointer_event_t *gpe,
                   glw_t **hoverp, Vec3 p, Vec3 dir)
{
  glw_t *c;
  float x, y;
  glw_pointer_event_t *gpe0 = NULL;
  int r = 0;

  if(w->glw_flags & (GLW_FOCUS_BLOCKED | GLW_CLIPPED | GLW_HIDDEN))
    return 0;

  if(w->glw_matrix != NULL) {

    if(glw_widget_unproject(w->glw_matrix, &x, &y, p, dir) &&
       x <= 1 && y <= 1 && x >= -1 && y >= -1) {
      gpe0 = alloca(sizeof(glw_pointer_event_t));

      const glw_class_t *gc = w->glw_class;
      *gpe0 = *gpe;
      gpe0->local_x = x;
      gpe0->local_y = y;

      if(gc->gc_pointer_event_filter != NULL)
        if(gc->gc_pointer_event_filter(w, gpe0))
          return 1;

      if(gpe->type < GLW_POINTER_MOTION_UPDATE)
        r = 1;

      if(glw_is_focusable_or_clickable(w))
	*hoverp = w;

    } else {
      return 0;
    }
  }

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {
    if(glw_pointer_event0(gr, c, gpe, hoverp, p, dir))
      return 1;
  }

  if(gpe0 == NULL)
    return 0;

  return glw_pointer_event_deliver(w, gpe0) | r;
}


/**
 *
 */
void
glw_pointer_event(glw_root_t *gr, glw_pointer_event_t *gpe)
{
  glw_t *c, *w;
  glw_pointer_event_t gpe0;
  float x, y;
  glw_t *hover = gr->gr_pointer_grab;

  Vec3 p, dir;

  if(gconf.convert_pointer_to_touch) {
    switch(gpe->type) {
    case GLW_POINTER_LEFT_PRESS:
      gr->gr_left_pressed = 1;
      gpe->type = GLW_POINTER_TOUCH_START;
      break;

    case GLW_POINTER_LEFT_RELEASE:
      gpe->type = GLW_POINTER_TOUCH_END;
      gr->gr_left_pressed = 0;
      break;

    case GLW_POINTER_MOTION_UPDATE:
      if(gr->gr_left_pressed) {
        gpe->type = GLW_POINTER_TOUCH_MOVE;
        break;
      }
      return;

    default:
      break;
    }
  }

  glw_vec3_copy(p, glw_vec3_make(gpe->screen_x, gpe->screen_y, -2.41));
  glw_vec3_sub(dir, p, glw_vec3_make(gpe->screen_x * 42.38,
				     gpe->screen_y * 42.38,
				     -100));


  if(gpe->type != GLW_POINTER_MOTION_REFRESH &&
     gpe->type != GLW_POINTER_GONE) {
    runcontrol_activity();
    glw_register_activity(gr);
    gr->gr_screensaver_force_enable = 0;
    glw_set_keyboard_mode(gr, 0);
  }

  if(gpe->type == GLW_POINTER_TOUCH_START) {
    gr->gr_touch_start_x = gpe->screen_x;
    gr->gr_touch_start_y = gpe->screen_y;
    if(gr->gr_touch_mode == 0) {
      TRACE(TRACE_DEBUG, "GLW", "Operating in touch mode");
      gr->gr_touch_mode = 1;
      prop_set(gr->gr_prop_ui, "touch", PROP_SET_INT, 1);
    }
  }

  if(gpe->type == GLW_POINTER_TOUCH_MOVE) {
    gr->gr_touch_move_x = gpe->screen_x;
    gr->gr_touch_move_y = gpe->screen_y;
  }

  if(gpe->type == GLW_POINTER_TOUCH_END) {
    gr->gr_touch_end_x = gpe->screen_x;
    gr->gr_touch_end_y = gpe->screen_y;
  }

  /* If a widget has grabbed to pointer (such as when holding the button
     on a slider), dispatch events there */

  if(gpe->screen_x != gr->gr_mouse_x || gpe->screen_y != gr->gr_mouse_y) {
    gr->gr_mouse_x = gpe->screen_x;
    gr->gr_mouse_y = gpe->screen_y;
    gr->gr_mouse_valid = 1;

    if(gpe->type == GLW_POINTER_MOTION_UPDATE ||
       gpe->type == GLW_POINTER_TOUCH_MOVE ||
       gpe->type == GLW_POINTER_MOTION_REFRESH) {

      if(gpe->type == GLW_POINTER_MOTION_UPDATE)
        prop_set_int(gr->gr_pointer_visible, 1);

      if((w = gr->gr_pointer_grab) != NULL && w->glw_matrix != NULL) {
        glw_widget_unproject(w->glw_matrix, &x, &y, p, dir);
        gpe0 = *gpe;
        gpe0.type = GLW_POINTER_FOCUS_MOTION;
        gpe0.local_x = x;
        gpe0.local_y = y;
        glw_send_pointer_event(w, &gpe0);
      } else if((w = gr->gr_pointer_grab_scroll) != NULL && w->glw_matrix != NULL) {
        glw_widget_unproject(w->glw_matrix, &x, &y, p, dir);
        gpe0 = *gpe;
        gpe0.type = GLW_POINTER_FOCUS_MOTION;
        gpe0.local_x = x;
        gpe0.local_y = y;
        glw_send_pointer_event(w, &gpe0);
      } else if((w = gr->gr_pointer_press) != NULL && w->glw_matrix != NULL) {

        int loss_press = 0;

        if(!glw_widget_unproject(w->glw_matrix, &x, &y, p, dir) ||
           x < -1 || y < -1 || x > 1 || y > 1)
          loss_press = 1;

        if(loss_press) {
          // Moved outside button, release
          glw_path_modify(w, 0, GLW_IN_PRESSED_PATH, NULL);
          gr->gr_pointer_press = NULL;
        }
      }
    }
  }

  if((gpe->type == GLW_POINTER_LEFT_RELEASE ||
      gpe->type == GLW_POINTER_TOUCH_END) && gr->gr_pointer_grab != NULL) {
    w = gr->gr_pointer_grab;
    glw_widget_unproject(w->glw_matrix, &x, &y, p, dir);
    gpe0 = *gpe;
    gpe0.local_x = x;
    gpe0.local_y = y;

    glw_send_pointer_event(w, &gpe0);
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

  TAILQ_FOREACH(c, &gr->gr_universe->glw_childs, glw_parent_link)
    if(glw_pointer_event0(gr, c, gpe, &hover, p, dir))
      break;

  if(gr->gr_touch_mode)
    return;

  if(gpe->type == GLW_POINTER_MOTION_UPDATE ||
     gpe->type == GLW_POINTER_MOTION_REFRESH)
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
  glw_register_activity(gr);
  gr->gr_screensaver_force_enable = 0;
  return r;
}


/**
 *
 */
static void
glw_screenshot(glw_root_t *gr)
{
  if(gr->gr_br_read_pixels == NULL) {
    screenshot_deliver(NULL);
    return;
  }

  pixmap_t *pm = gr->gr_br_read_pixels(gr);
  screenshot_deliver(pm);
  pixmap_release(pm);
}


/**
 *
 */
#if CONFIG_GLW_REC
static void
glw_rec_toggle(glw_root_t *gr)
{
  if(gr->gr_rec != NULL) {
    // Stop
    TRACE(TRACE_DEBUG, "GLW", "Recording stopped");
    glw_rec_stop(gr->gr_rec);
    gr->gr_rec = NULL;
  } else {
    gr->gr_rec = glw_rec_init("capture.mkv", gr->gr_width, gr->gr_height, 60);
    if(gr->gr_rec != NULL) {
      TRACE(TRACE_DEBUG, "GLW", "Recording started");
    }
  }
}


#endif

/**
 *
 */
static void
glw_dispatch_event(glw_root_t *gr, event_t *e)
{
  runcontrol_activity();

  if(gr->gr_osk_widget != NULL) {
    if(event_is_type(e, EVENT_INSERT_STRING) ||
       event_is_action(e, ACTION_ENTER) ||
       event_is_action(e, ACTION_BS)) {
      glw_event_to_widget(gr->gr_osk_widget, e);
      return;
    }
  }
  
  if(e->e_type == EVENT_REPAINT_UI) {
    glw_text_flush(gr);
    return;
  }

  if(e->e_type == EVENT_MAKE_SCREENSHOT) {
    glw_screenshot(gr);
    return;
  }

#if CONFIG_GLW_REC
  if(event_is_action(e, ACTION_RECORD_UI)) {
    glw_rec_toggle(gr);
    return;
  }
#endif

  if(e->e_type == EVENT_KEYDESC) {

    if(glw_event(gr, e))
      return; // Was consumed

    return;
  }

  if(!(event_is_action(e, ACTION_SEEK_BACKWARD) ||
       event_is_action(e, ACTION_SEEK_FORWARD) ||
       event_is_action(e, ACTION_PLAYPAUSE) ||
       event_is_action(e, ACTION_PLAY) ||
       event_is_action(e, ACTION_PAUSE) ||
       event_is_action(e, ACTION_STOP) ||
       event_is_action(e, ACTION_EJECT) ||
       event_is_action(e, ACTION_SKIP_BACKWARD) ||
       event_is_action(e, ACTION_SKIP_FORWARD) ||
       event_is_action(e, ACTION_SHOW_MEDIA_STATS) ||
       event_is_action(e, ACTION_SHUFFLE) ||
       event_is_action(e, ACTION_REPEAT) ||
       event_is_action(e, ACTION_NEXT_CHANNEL) ||
       event_is_action(e, ACTION_PREV_CHANNEL) ||
       event_is_action(e, ACTION_VOLUME_UP) ||
       event_is_action(e, ACTION_VOLUME_DOWN) ||
       event_is_action(e, ACTION_VOLUME_MUTE_TOGGLE) ||
       event_is_action(e, ACTION_POWER_OFF) ||
       event_is_action(e, ACTION_RESTART) ||
       event_is_action(e, ACTION_STANDBY) ||
       event_is_type(e, EVENT_SELECT_AUDIO_TRACK) ||
       event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK))) {

    if(e->e_flags & EVENT_KEYPRESS) {
      if(glw_set_keyboard_mode(gr, 1)) {
        /*
         * Ok, we switched form mouse to keyboard mode.
         * For some events we don't want to actually execute on the
         * action but rather "use" the event to do the switch
         */

        if(event_is_action(e, ACTION_UP) ||
           event_is_action(e, ACTION_DOWN) ||
           event_is_action(e, ACTION_LEFT) ||
           event_is_action(e, ACTION_RIGHT))
          return;

      }
    }
    if(glw_kill_screensaver(gr)) {
      if(e->e_flags & EVENT_KEYPRESS)
        return;
    }
  }

  if(event_is_action(e, ACTION_RELOAD_UI)) {
    glw_load_universe(gr);
    return;

  } else if(event_is_action(e, ACTION_ZOOM_UI_INCR)) {

    glw_settings_adj_size(1);
    return;

  } else if(event_is_action(e, ACTION_ZOOM_UI_DECR)) {

    glw_settings_adj_size(-1);
    return;

  } else if(event_is_action(e, ACTION_ZOOM_UI_RESET)) {

    glw_settings_adj_size(0);
    return;

  }

  glw_event(gr, e);
}

/**
 *
 */
static void
glw_eventsink(void *opaque, prop_event_t event, ...)
{
  glw_root_t *gr = opaque;
  va_list ap;
  event_t *e;
  va_start(ap, event);

  switch(event) {
  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    glw_dispatch_event(gr, e);
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
void
glw_inject_event(glw_root_t *gr, event_t *e)
{
  prop_t *p;

  if(gr->gr_current_focus == NULL &&
     (event_is_action(e, ACTION_NAV_BACK) ||
      event_is_action(e, ACTION_NAV_FWD) ||
      event_is_action(e, ACTION_HOME) ||
      event_is_action(e, ACTION_PLAYQUEUE) ||
      event_is_action(e, ACTION_RELOAD_DATA) ||
      event_is_type(e, EVENT_OPENURL))) {
    p = prop_get_by_name(PNVEC("nav", "eventSink"), 0,
			 PROP_TAG_NAMED_ROOT, gr->gr_prop_nav, "nav",
			 NULL);
  } else {
    p = prop_get_by_name(PNVEC("ui", "eventSink"), 0,
			 PROP_TAG_NAMED_ROOT, gr->gr_prop_ui, "nav",
			 NULL);
  }
  prop_send_ext_event(p, e);
  event_release(e);
  prop_ref_dec(p);
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
glw_conf_constraints(glw_t *w, int x, int y, float weight, int conf)
{
  switch(conf) {
  case GLW_CONSTRAINT_CONF_X:
    w->glw_req_size_x = x;
    w->glw_flags |= GLW_CONSTRAINT_X;
    break;

  case GLW_CONSTRAINT_CONF_Y:
    w->glw_req_size_y = y;
    w->glw_flags |= GLW_CONSTRAINT_Y;
    break;

  case GLW_CONSTRAINT_CONF_W:
    w->glw_req_weight = weight;
    w->glw_flags |= GLW_CONSTRAINT_W;
    break;

  case GLW_CONSTRAINT_CONF_D:
    w->glw_flags |= GLW_CONSTRAINT_D;
    break;

  default:
    abort();
  }
  w->glw_flags |= conf;

  if(w->glw_parent != NULL)
    glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED, w);
}

/**
 *
 */
void
glw_mod_constraints(glw_t *w, int x, int y, float weight, int flags,
                    int modflags)
{
  int ch = 0;
  const int fc = w->glw_flags ^ flags;
  if(modflags & GLW_CONSTRAINT_X) {
    if(!(w->glw_flags & GLW_CONSTRAINT_CONF_X)) {
      if(fc & GLW_CONSTRAINT_X) {
        ch = 1;
        w->glw_flags =
          (w->glw_flags & ~GLW_CONSTRAINT_X) | (flags & GLW_CONSTRAINT_X);
      }
      if(w->glw_flags & GLW_CONSTRAINT_X && w->glw_req_size_x != x) {
        w->glw_req_size_x = x;
        ch = 1;
      }
    }
  }

  if(modflags & GLW_CONSTRAINT_Y) {
    if(!(w->glw_flags & GLW_CONSTRAINT_CONF_Y)) {
      if(fc & GLW_CONSTRAINT_Y) {
        ch = 1;
        w->glw_flags =
          (w->glw_flags & ~GLW_CONSTRAINT_Y) | (flags & GLW_CONSTRAINT_Y);
      }
      if(w->glw_flags & GLW_CONSTRAINT_Y && w->glw_req_size_y != y) {
        w->glw_req_size_y = y;
        ch = 1;
      }
    }
  }

  if(modflags & GLW_CONSTRAINT_W) {
    if(!(w->glw_flags & GLW_CONSTRAINT_CONF_W)) {
      if(fc & GLW_CONSTRAINT_W) {
        ch = 1;
        w->glw_flags =
          (w->glw_flags & ~GLW_CONSTRAINT_W) | (flags & GLW_CONSTRAINT_W);
      }
      if(w->glw_flags & GLW_CONSTRAINT_W && w->glw_req_weight != weight) {
        w->glw_req_weight = weight;
        ch = 1;
      }
    }
  }

  if(modflags & GLW_CONSTRAINT_D) {
    if(!(w->glw_flags & GLW_CONSTRAINT_CONF_D)) {
      if(fc & GLW_CONSTRAINT_D) {
        ch = 1;
        w->glw_flags =
          (w->glw_flags & ~GLW_CONSTRAINT_D) | (flags & GLW_CONSTRAINT_D);
      }
    }
  }

  if(ch && w->glw_parent != NULL)
    glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED, w);
}


void
glw_set_constraints(glw_t *w, int x, int y, float weight,
                    int flags)
{
  glw_mod_constraints(w, x, y, weight, flags, -1);
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


  if(!(w->glw_flags & GLW_CONSTRAINT_CONF_D)) {
    if(w->glw_flags & GLW_CONSTRAINT_D) {
      w->glw_flags &= ~GLW_CONSTRAINT_D;
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
		      glw_req_width(src),
		      glw_req_height(src),
		      src->glw_req_weight,
		      glw_filter_constraints(src));
}


static LIST_HEAD(, glw_class) glw_classes;

/**
 *
 */
const glw_class_t *
glw_class_find_by_name(const char *name)
{
  glw_class_t *gc;

  LIST_FOREACH(gc, &glw_classes, gc_link) {
    if(!strcmp(gc->gc_name, name))
      break;
    if(gc->gc_name2 != NULL && !strcmp(gc->gc_name2, name))
      break;
  }
  return gc;
}

/**
 *
 */
void
glw_register_class(glw_class_t *gc)
{
  assert(gc->gc_layout != NULL);
  LIST_INSERT_HEAD(&glw_classes, gc, gc_link);
}


/**
 *
 */
const char *
glw_get_name(glw_t *w)
{
  static char buf[1024];
  const char *extra = NULL;
  char tmp[512];
  const glw_class_t *gc = w->glw_class;
  if(w == w->glw_root->gr_universe)
    return "Universe";

  if(gc->gc_get_identity != NULL)
    extra = gc->gc_get_identity(w, tmp, sizeof(tmp));

  if(extra == NULL && gc->gc_get_text != NULL)
    extra = gc->gc_get_text(w);

  snprintf(buf, sizeof(buf), "%s @ %s:%d%s%s",
           gc->gc_name,
           rstr_get(w->glw_file),
           w->glw_line,
           extra ? " " : "",
           extra ? extra : "");
  return buf;
}


/**
 *
 */
static void
glw_get_path_r(char *buf, size_t buflen, glw_t *w)
{
  char tmp[32];
  if(w->glw_parent)
    glw_get_path_r(buf, buflen, w->glw_parent);
  const char *ident = w->glw_class->gc_get_identity ?
    w->glw_class->gc_get_identity(w, tmp, sizeof(tmp)) : NULL;

  if(ident == NULL)
    ident = rstr_get(w->glw_id_rstr);

  snprintf(buf + strlen(buf), buflen - strlen(buf), "%s[%s%s%s%s%s%s%s@%s:%d]",
	   strlen(buf) ? "." : "", w->glw_class->gc_name,
	   w->glw_flags & GLW_FOCUS_BLOCKED ? "<B>" : "",
	   w->glw_flags & GLW_DESTROYING    ? "<D>" : "",
	   w->glw_flags & GLW_HIDDEN        ? "<H>" : "",
	   ident ? "(" : "",
	   ident ?: "",
	   ident ? ")" : "",
           rstr_get(w->glw_file),
           w->glw_line
           );
}

/**
 *
 */
const char *
glw_get_path(glw_t *w)
{
  if(w == NULL)
    return "<null>";

  static char buf[1024];
  buf[0] = 0;
  glw_get_path_r(buf, sizeof(buf), w);
  return buf;
}


/**
 *
 */
void
glw_set_fullscreen(glw_root_t *gr, int fullscreen)
{
  gr->gr_is_fullscreen = fullscreen;
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

  if(unlikely(w->glw_parent == NULL))
    return; // For style widgets

  glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_HIDDEN, w);

  if(glw_is_focused(w))
    glw_focus_crawl(w, 1, 0);
}


void
glw_unhide(glw_t *w)
{
  if(!(w->glw_flags & GLW_HIDDEN))
    return;
  w->glw_flags &= ~GLW_HIDDEN;

  if(unlikely(w->glw_parent == NULL))
    return; // For style widgets

  glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_UNHIDDEN, w);
}


/**
 *
 */
void
glw_mod_flags2(glw_t *w, int set, int clr)
{
  const glw_class_t *gc = w->glw_class;

  set &= ~w->glw_flags2;
  w->glw_flags2 |= set;

  clr &= w->glw_flags2;
  w->glw_flags2 &= ~clr;

  if(w->glw_parent != NULL) {
    glw_t *p = w->glw_parent;
    if(set & GLW2_FHP_SPILL) {
      p->glw_flags |= GLW_FHP_SPILL_TO_CHILDS;
    }

    if(clr & GLW2_FHP_SPILL) {
      glw_t *c;

      p->glw_flags &= ~GLW_FHP_SPILL_TO_CHILDS;
      TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link) {
        if(c->glw_flags2 & GLW2_FHP_SPILL) {
          p->glw_flags |= GLW_FHP_SPILL_TO_CHILDS;
          break;
        }
      }
    }
  }

  if((set | clr) && gc->gc_mod_flags2 != NULL)
    gc->gc_mod_flags2(w, set, clr);
}


/**
 *
 */
void
glw_store_matrix(glw_t *w, const glw_rctx_t *rc)
{
  if(rc->rc_inhibit_matrix_store)
    return;

  if(w->glw_matrix == NULL)
    w->glw_matrix = malloc(sizeof(Mtx));

  *w->glw_matrix = rc->rc_mtx;

  if(likely(!(w->glw_flags & (GLW_IN_FOCUS_PATH | GLW_IN_HOVER_PATH))))
    return;

  if(w->glw_root->gr_cursor_focus_tracker != NULL)
    w->glw_root->gr_cursor_focus_tracker(w, rc,
                                         w->glw_root->gr_current_cursor);
}



/**
 *
 */
void
glw_rctx_init(glw_rctx_t *rc, int width, int height, int overscan, int *zmax)
{
  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_width  = width;
  rc->rc_height = height;
  rc->rc_alpha = 1.0f;
  rc->rc_sharpness  = 1.0f;
  rc->rc_overscanning = overscan;
  rc->rc_zmax = zmax;
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
glw_widget_unproject(const Mtx *m, float *xp, float *yp,
                     const Vec3 p, const Vec3 dir)
{
  Vec3 u, v, n, w0, T0, T1, T2, out, I;
  Mtx inv;
  float b;

  PMtx tm;
  glw_pmtx_mul_prepare(&tm, m);

  glw_pmtx_mul_vec3(T0, &tm, glw_vec3_make(-1, -1, 0));
  glw_pmtx_mul_vec3(T1, &tm, glw_vec3_make( 1, -1, 0));
  glw_pmtx_mul_vec3(T2, &tm, glw_vec3_make( 1,  1, 0));

  glw_vec3_sub(u, T1, T0);
  glw_vec3_sub(v, T2, T0);
  glw_vec3_cross(n, u, v);

  glw_vec3_sub(w0, p, T0);
  b = glw_vec3_dot(n, dir);
  if(fabs(b) < 0.000001)
    return 0;

  glw_vec3_addmul(I, p, dir, -glw_vec3_dot(n, w0) / b);

  if(!glw_mtx_invert(&inv, m))
    return 0;

  glw_pmtx_mul_prepare(&tm, &inv);
  glw_pmtx_mul_vec3(out, &tm, I);

  *xp = glw_vec3_extract(out, 0);
  *yp = glw_vec3_extract(out, 1);
  return 1;
}


/**
 *
 */
static void
glw_osk_text(void *opaque, const char *str)
{
  glw_root_t *gr = opaque;
  glw_t *w = gr->gr_osk_widget;
  if(w != NULL) {
    w->glw_class->gc_update_text(w, str);
  }
}


/**
 *
 */
void
glw_osk_close(glw_root_t *gr)
{
  glw_unref(gr->gr_osk_widget);
  gr->gr_osk_widget = NULL;
}



/**
 *
 */
static void
glw_osk_done(glw_root_t *gr, int submit)
{
  glw_t *w = gr->gr_osk_widget;
  if(w != NULL) {
    if(submit) {
      event_t *e = event_create_action(ACTION_SUBMIT);
      e->e_nav = prop_ref_inc(gr->gr_prop_nav);
      glw_event_to_widget(w, e);
      event_release(e);
    } else {
      glw_t *w = gr->gr_osk_widget;
      w->glw_class->gc_update_text(w, gr->gr_osk_revert);
    }

    prop_unsubscribe(gr->gr_osk_text_sub);
    prop_unsubscribe(gr->gr_osk_ev_sub);

    gr->gr_osk_text_sub = NULL;
    gr->gr_osk_ev_sub = NULL;
    glw_osk_close(gr);
  }

  prop_t *osk = prop_create(gr->gr_prop_ui, "osk");
  prop_set(osk, "show", PROP_SET_INT, 0);
}


/**
 *
 */
static void
glw_osk_event(void *opaque, prop_event_t event, ...)
{
  va_list ap;

  if(event != PROP_EXT_EVENT)
    return;

  va_start(ap, event);
  event_t *e = va_arg(ap, event_t *);
  va_end(ap);

  if(event_is_action(e, ACTION_OK)) {
    glw_osk_done(opaque, 1);
  } else if(event_is_action(e, ACTION_CANCEL)) {
    glw_osk_done(opaque, 0);
  }
}


/**
 *
 */
void
glw_osk_open(glw_root_t *gr, const char *title, const char *input,
             glw_t *w, int password)
{
  mystrset(&gr->gr_osk_revert, input);
  
  if(gr->gr_osk_widget != NULL)
    glw_unref(gr->gr_osk_widget);
  
  gr->gr_osk_widget = w;
  glw_ref(w);
  
  gr->gr_open_osk(gr, title, input, w, password);
}


/**
 *
 */
static void
glw_osk_open_default(glw_root_t *gr, const char *title, const char *input,
                     glw_t *w, int password)
{
  prop_t *osk = prop_create(gr->gr_prop_ui, "osk");
  
  prop_unsubscribe(gr->gr_osk_text_sub);
  prop_unsubscribe(gr->gr_osk_ev_sub);
  
  prop_set(osk, "title", PROP_SET_STRING, title);
  prop_set(osk, "text",  PROP_SET_STRING, input);
  prop_set(osk, "password", PROP_SET_INT, password);
  prop_set(osk, "show", PROP_SET_INT, 1);
  
  
  gr->gr_osk_text_sub =
  prop_subscribe(0,
                 PROP_TAG_CALLBACK_STRING, glw_osk_text, gr,
                 PROP_TAG_NAME("ui", "osk", "text"),
                 PROP_TAG_ROOT, gr->gr_prop_ui,
                 PROP_TAG_COURIER, gr->gr_courier,
                 NULL);
  
  gr->gr_osk_ev_sub =
  prop_subscribe(0,
                 PROP_TAG_CALLBACK, glw_osk_event, gr,
                 PROP_TAG_NAME("ui", "osk", "eventSink"),
                 PROP_TAG_ROOT, gr->gr_prop_ui,
                 PROP_TAG_COURIER, gr->gr_courier,
                 NULL);
}


/**
 *
 */
static void
glw_register_activity(glw_root_t *gr)
{
  gr->gr_screensaver_reset_at = gr->gr_frame_start;
  gr->gr_last_activity_at = gr->gr_frame_start;
}


const static float projection[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};


/**
 *
 */
void
glw_project_matrix(glw_rect_t *r, const Mtx *m, const glw_root_t *gr)
{
  Mtx tmp;

  PMtx tm, tp;
  Vec4 T0, T1;
  Vec4 V0, V1;

  glw_pmtx_mul_prepare(&tm,  m);
  glw_pmtx_mul_vec4(T0, &tm, glw_vec4_make(-1,  1, 0, 1));
  glw_pmtx_mul_vec4(T1, &tm, glw_vec4_make( 1, -1, 0, 1));

  memcpy(&tmp, projection, sizeof(float) * 16);
  glw_pmtx_mul_prepare(&tp, &tmp);

  glw_pmtx_mul_vec4(V0, &tp, T0);
  glw_pmtx_mul_vec4(V1, &tp, T1);

  float w;

  w = glw_vec4_extract(V0, 3);

  r->x1 = roundf((1.0 + (glw_vec4_extract(V0, 0) / w)) * gr->gr_width  / 2.0);
  r->y1 = roundf((1.0 - (glw_vec4_extract(V0, 1) / w)) * gr->gr_height / 2.0);

  w = glw_vec4_extract(V1, 3);

  r->x2 = roundf((1.0 + (glw_vec4_extract(V1, 0) / w)) * gr->gr_width  / 2.0);
  r->y2 = roundf((1.0 - (glw_vec4_extract(V1, 1) / w)) * gr->gr_height / 2.0);
}


/**
 *
 */
void
glw_project(glw_rect_t *r, const glw_rctx_t *rc, const glw_root_t *gr)
{
  glw_project_matrix(r, &rc->rc_mtx, gr);
}


/**
 *
 */
void
glw_lp(float *v, glw_root_t *gr, float target, float alpha)
{
  const float in = *v;

  const int x = in * 1000.0f;
  const float out = in + alpha * (target - in);
  const int y = out * 1000.0f;

  if(x == y) {
    *v = target;
    return;
  }
  *v = out;
  glw_need_refresh(gr, 0);
}


/**
 *
 */
int
glw_attrib_set_float3_clamped(float *dst, const float *src)
{
  float v[3];
  for(int i = 0; i < 3; i++)
    v[i] = GLW_CLAMP(src[i], 0.0f, 1.0f);
  return glw_attrib_set_float3(dst, v);
}

int
glw_attrib_set_float3(float *dst, const float *src)
{
  if(!memcmp(dst, src, sizeof(float) * 3))
    return 0;
  memcpy(dst, src, sizeof(float) * 3);
  return 1;
}

int
glw_attrib_set_float4(float *dst, const float *src)
{
  if(!memcmp(dst, src, sizeof(float) * 4))
    return 0;
  memcpy(dst, src, sizeof(float) * 4);
  return 1;
}

int
glw_attrib_set_rgb(glw_rgb_t *rgb, const float *src)
{
  return glw_attrib_set_float3((float *)rgb, src);
}

int
glw_attrib_set_int16_4(int16_t *dst, const int16_t *src)
{
  if(!memcmp(dst, src, sizeof(int16_t) * 4))
    return 0;
  memcpy(dst, src, sizeof(int16_t) * 4);
  return 1;
}




/**
 *
 */
#ifdef GLW_TRACK_REFRESH
void
glw_need_refresh0(glw_root_t *gr, int how, const char *file, int line)
{
  int flags = GLW_REFRESH_FLAG_LAYOUT;

  if(how != GLW_REFRESH_LAYOUT_ONLY)
    flags |= GLW_REFRESH_FLAG_RENDER;

  if((gr->gr_need_refresh & flags) == flags)
    return;

  gr->gr_need_refresh |= flags;
  tracelog(TRACE_NO_PROP, TRACE_DEBUG,
           "GLW", "%s%srefresh requested by %s:%d",
           flags & GLW_REFRESH_FLAG_LAYOUT ? "layout " : "",
           flags & GLW_REFRESH_FLAG_RENDER ? "render " : "",
           file, line);
}
#endif


static void
glw_update_dynamics_r(glw_t *w, int flags)
{
  glw_t *c;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_update_dynamics_r(c, flags);

  if(w->glw_dynamic_eval & flags)
    glw_view_eval_dynamics(w, flags);
}


static void
glw_update_em(glw_root_t *gr)
{
  if(gr->gr_universe != NULL)
    glw_update_dynamics_r(gr->gr_universe, GLW_VIEW_EVAL_EM);

  glw_style_update_em(gr);
}


static int
glw_set_keyboard_mode(glw_root_t *gr, int on)
{
  if(gr->gr_keyboard_mode == on)
    return 0;

  gr->gr_keyboard_mode = on;
  prop_set(gr->gr_prop_ui, "keyboard", PROP_SET_INT, on);

  if(gr->gr_universe != NULL)
    glw_update_dynamics_r(gr->gr_universe, GLW_VIEW_EVAL_FHP_CHANGE);

  glw_need_refresh(gr, 0);
  return 1;
}
