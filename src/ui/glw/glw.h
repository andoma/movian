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

#ifndef GLW_H
#define GLW_H

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>

#include "misc/queue.h"
#include "prop.h"
#include "event.h"
#include "ui/ui.h"
#include "showtime.h"
#include "settings.h"

TAILQ_HEAD(glw_queue, glw);
LIST_HEAD(glw_head, glw);
LIST_HEAD(glw_event_map_list, glw_event_map);
LIST_HEAD(glw_prop_sub_list, glw_prop_sub);
LIST_HEAD(glw_loadable_texture_list, glw_loadable_texture);
TAILQ_HEAD(glw_loadable_texture_queue, glw_loadable_texture);
LIST_HEAD(glw_video_list, glw_video);

#if CONFIG_GLW_BACKEND_OPENGL
#include "glw_opengl.h"
#elif CONFIG_GLW_BACKEND_GX
#include "glw_gx.h"
#else
#error No backend for glw
#endif

#include <ft2build.h>  
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#define GLW_LERP(a, y0, y1) ((y0) + (a) * ((y1) - (y0)))
#define GLW_S(a) (sin(GLW_LERP(a, M_PI * -0.5, M_PI * 0.5)) * 0.5 + 0.5)
#define GLW_LP(a, y0, y1) (((y0) * ((a) - 1.0) + (y1)) / (a))
#define GLW_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GLW_MAX(a, b) ((a) > (b) ? (a) : (b))
#define GLW_DEG2RAD(a) ((a) * M_PI * 2.0f / 360.0f)
#define GLW_RESCALE(x, min, max) (((x) - (min)) / ((max) - (min)))
#define GLW_SWAP(a, b) do { typeof(a) c = (b); (b) = (a); (a) = (c); } while(0)
#define GLW_CLAMP(x, min, max) GLW_MIN(GLW_MAX((x), (min)), (max))


typedef enum {
  GLW_ATTRIB_END = 0,
  GLW_ATTRIB_PARENT,
  GLW_ATTRIB_PARENT_HEAD,
  GLW_ATTRIB_PARENT_BEFORE,
  GLW_ATTRIB_SIGNAL_HANDLER,
  GLW_ATTRIB_WEIGHT,
  GLW_ATTRIB_CAPTION,
  GLW_ATTRIB_VALUE,
  GLW_ATTRIB_SOURCE,
  GLW_ATTRIB_ASPECT,
  GLW_ATTRIB_ALPHA,
  GLW_ATTRIB_ALPHA_SELF,
  GLW_ATTRIB_ANGLE,
  GLW_ATTRIB_ALIGNMENT,
  GLW_ATTRIB_SET_FLAGS,
  GLW_ATTRIB_CLR_FLAGS,
  GLW_ATTRIB_EXTRA,
  GLW_ATTRIB_PREVIEW,
  GLW_ATTRIB_CONTENT,
  GLW_ATTRIB_MODE,
  GLW_ATTRIB_BORDER,
  GLW_ATTRIB_PADDING,
  GLW_ATTRIB_SET_IMAGE_FLAGS,
  GLW_ATTRIB_CLR_IMAGE_FLAGS,
  GLW_ATTRIB_SET_TEXT_FLAGS,
  GLW_ATTRIB_CLR_TEXT_FLAGS,
  GLW_ATTRIB_ID,
  GLW_ATTRIB_RGB,
  GLW_ATTRIB_TIME,
  GLW_ATTRIB_INT_STEP,
  GLW_ATTRIB_INT_MIN,
  GLW_ATTRIB_INT_MAX,
  GLW_ATTRIB_PROPROOTS,
  GLW_ATTRIB_TRANSITION_EFFECT,
  GLW_ATTRIB_EXPANSION,
  GLW_ATTRIB_BIND_TO_PROPERTY,
  GLW_ATTRIB_BIND_TO_ID,
  GLW_ATTRIB_SIZE_SCALE,
  GLW_ATTRIB_SIZE_BIAS,
  GLW_ATTRIB_PIXMAP,
  GLW_ATTRIB_ORIGINATING_PROP,
  GLW_ATTRIB_FOCUS_WEIGHT,
  GLW_ATTRIB_CHILD_ASPECT,
  GLW_ATTRIB_HEIGHT,
  GLW_ATTRIB_WIDTH,
  GLW_ATTRIB_CHILD_HEIGHT,
  GLW_ATTRIB_CHILD_WIDTH,
  GLW_ATTRIB_CHILD_TILES_X,
  GLW_ATTRIB_CHILD_TILES_Y,
  GLW_ATTRIB_FREEZE,
  GLW_ATTRIB_ROTATION,
  GLW_ATTRIB_TRANSLATION,
  GLW_ATTRIB_SCALING,
  GLW_ATTRIB_num,
} glw_attribute_t;

/**
 * Text flags
 */
#define GTB_PASSWORD      0x1   /* Don't display real contents */
#define GTB_ELLIPSIZE     0x2


#define GLW_MODE_XFADE    0
#define GLW_MODE_SLIDE    1

typedef struct glw_vertex {
  float x, y, z;
} glw_vertex_t;

typedef struct glw_rgb {
  float r, g, b;
} glw_rgb_t;

typedef enum {
  GLW_ALIGN_NONE = 0,
  GLW_ALIGN_CENTER,
  GLW_ALIGN_LEFT,
  GLW_ALIGN_RIGHT,
  GLW_ALIGN_BOTTOM,
  GLW_ALIGN_TOP,
} glw_alignment_t;


/**
 * Image flags
 */
#define GLW_IMAGE_MIRROR_X      0x1
#define GLW_IMAGE_MIRROR_Y      0x2
#define GLW_IMAGE_BORDER_LEFT   0x4
#define GLW_IMAGE_BORDER_RIGHT  0x8
#define GLW_IMAGE_BORDER_TOP    0x10
#define GLW_IMAGE_BORDER_BOTTOM 0x20
#define GLW_IMAGE_INFRONT       0x40

/**
 * XXX Document these
 */
typedef enum {
  GLW_SIGNAL_NONE,
  GLW_SIGNAL_DESTROY,
  GLW_SIGNAL_DTOR,
  GLW_SIGNAL_ACTIVE,
  GLW_SIGNAL_INACTIVE,
  GLW_SIGNAL_LAYOUT,

  GLW_SIGNAL_CHILD_CREATED,
  GLW_SIGNAL_CHILD_DESTROYED,

  GLW_SIGNAL_DETACH_CHILD,

  GLW_SIGNAL_NEW_FRAME,

  GLW_SIGNAL_EVENT_BUBBLE,

  GLW_SIGNAL_EVENT,

  GLW_SIGNAL_CHANGED,


  /**
   * Sent to parent to switch currently selected child.
   * Parent should NOT send GLW_SIGNAL_SELECTED_UPDATE to the child
   * in this case.
   */
  GLW_SIGNAL_SELECT,

  /**
   * Emitted by parent to child when it has been selected.
   */
  GLW_SIGNAL_SELECTED_UPDATE,

  /**
   * Sent to widget when its focused child is changed.
   * Argument is newly focused child.
   */
  GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE,
  GLW_SIGNAL_FOCUS_CHILD_AUTOMATIC,

  /**
   *
   */
  GLW_SIGNAL_POINTER_EVENT,

  /**
   *
   */
  GLW_SIGNAL_SLIDER_METRICS,

  /**
   *
   */
  GLW_SIGNAL_SCROLL,

  /**
   * Sent to a widget when it enters or leaves the current 
   * "path of focus / hover / pressed"
   */
  GLW_SIGNAL_FHP_PATH_CHANGED,

  /**
   * Sent to a widget froms its child when the layout constraints changes
   */
  GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED,

  GLW_SIGNAL_CHILD_HIDDEN,
  GLW_SIGNAL_CHILD_UNHIDDEN,

  /**
   * Emitted by a widget when it needs scrolling
   */ 
  GLW_SIGNAL_NEED_SCROLL_CHANGED,

 /**
   * Emitted by a widget when it needs scrolling
   */ 
  GLW_SIGNAL_FULLSCREEN_CONSTRAINT_CHANGED,

} glw_signal_t;


typedef struct {
  float knob_size;
  float position;
} glw_slider_metrics_t;

typedef struct {
  float value;
} glw_scroll_t;




typedef int (glw_callback_t)(struct glw *w, void *opaque, 
			     glw_signal_t signal, void *value);


typedef enum {
  GLW_ORIENTATION_NONE,
  GLW_ORIENTATION_HORIZONTAL,
  GLW_ORIENTATION_VERTICAL,
} glw_orientation_t;


/**
 * GLW class definitions
 */
typedef struct glw_class {

  const char *gc_name;
  size_t gc_instance_size;
  int gc_flags;
#define GLW_NAVIGATION_SEARCH_BOUNDARY 0x1

  glw_orientation_t gc_child_orientation;

  /**
   * Controls how navigation will filter a widget's children
   * when searching for focus
   */
  enum {
    GLW_NAV_DESCEND_ALL,
    GLW_NAV_DESCEND_SELECTED,
    GLW_NAV_DESCEND_FOCUSED,
  } gc_nav_descend_mode;

  /**
   * Controls how navigation will search among a widget's children 
   * when searching for focus
   */
  enum {
    GLW_NAV_SEARCH_NONE,
    GLW_NAV_SEARCH_BY_ORIENTATION,
    GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING,
    GLW_NAV_SEARCH_ARRAY,  // Special for array, not very elegant
  } gc_nav_search_mode;


  void (*gc_set)(struct glw *w, int init, va_list ap);
  void (*gc_render)(struct glw *w, struct glw_rctx *rc);

  glw_callback_t *gc_signal_handler;

  LIST_ENTRY(glw_class) gc_link;

} glw_class_t;

void glw_register_class(glw_class_t *gc);

#define GLW_REGISTER_CLASS(n) \
static void  __attribute__((constructor)) widgetclassinit ## n(void) \
{ glw_register_class(&n); }

const glw_class_t *glw_class_find_by_name(const char *name);


/**
 * GLW root context
 */
typedef struct glw_root {
  uii_t gr_uii;

  struct glw *gr_universe;

  LIST_HEAD(, glw_cached_view) gr_views;

  const char *gr_theme;

  hts_thread_t gr_thread;
  hts_mutex_t gr_mutex;
  prop_courier_t *gr_courier;

  struct glw_queue gr_destroyer_queue;

  int gr_frameduration;

  struct glw_head gr_active_list;
  struct glw_head gr_active_flush_list;
  struct glw_head gr_active_dummy_list;
  struct glw_head gr_every_frame_list;

  int gr_width;
  int gr_height;

  float gr_mouse_x;
  float gr_mouse_y;
  int gr_mouse_valid;

  /**
   * Font renderer
   */
  LIST_HEAD(,  glw_text_bitmap) gr_gtbs;
  TAILQ_HEAD(, glw_text_bitmap) gr_gtb_render_queue;
  hts_cond_t gr_gtb_render_cond;
  FT_Face gr_gtb_face;
  int gr_fontsize;
  int gr_fontsize_px;
  prop_t *gr_fontsize_prop;

  /**
   * Image/Texture loader
   */
  hts_mutex_t gr_tex_mutex;
  hts_cond_t gr_tex_load_cond;

  struct glw_loadable_texture_list gr_tex_active_list;
  struct glw_loadable_texture_list gr_tex_flush_list;
  struct glw_loadable_texture_queue gr_tex_rel_queue;
  struct glw_loadable_texture_queue gr_tex_load_queue[2];
  struct glw_loadable_texture_list gr_tex_list;

  int gr_normalized_texture_coords;

  /**
   * Root focus leader
   */
  struct glw *gr_pointer_grab;
  struct glw *gr_pointer_hover;
  struct glw *gr_pointer_press;
  struct glw *gr_current_focus;
  prop_t *gr_last_focused_interactive;

  /**
   * Backend specifics
   */ 
  glw_backend_root_t gr_be;

  /**
   * Settings
   */
  prop_t *gr_settings;        // Root prop

  char *gr_settings_instance; // Name of configuration file

  htsmsg_t *gr_settings_store;  // Loaded settings

  setting_t *gr_setting_fontsize;

} glw_root_t;


void glw_settings_save(void *opaque, htsmsg_t *msg);


/**
 * Render context
 */
typedef struct glw_rctx {
  float rc_alpha;

  float rc_size_x;
  float rc_size_y;

  struct glw_cursor_painter *rc_cursor_painter;

  int rc_inhibit_shadows; // Used when rendering low res passes in bloom filter

  /**
   * Backend specifics
   */ 
  glw_backend_rctx_t rc_be;

} glw_rctx_t;


#ifdef CONFIG_GLW_BACKEND_GX
#include "glw_gx_ops.h"
#endif


/**
 * Signal handler
 */
typedef struct glw_signal_handler {
  LIST_ENTRY(glw_signal_handler) gsh_link;
  glw_callback_t *gsh_func;
  void *gsh_opaque;
  int16_t gsh_pri;
  int16_t gsh_defer_remove;
} glw_signal_handler_t;

LIST_HEAD(glw_signal_handler_list, glw_signal_handler);


/**
 * GL widget
 */
typedef struct glw {
  const glw_class_t *glw_class;
  glw_root_t *glw_root;
  int glw_refcnt;
  prop_t *glw_originating_prop;  /* Source prop we spawned from */

  LIST_ENTRY(glw) glw_active_link;
  LIST_ENTRY(glw) glw_every_frame_link;

  struct glw_signal_handler_list glw_signal_handlers;

  struct glw *glw_parent;
  TAILQ_ENTRY(glw) glw_parent_link;
  struct glw_queue glw_childs;

  struct glw_queue glw_render_list;
  TAILQ_ENTRY(glw) glw_render_link;
		   
  struct glw *glw_selected;
  struct glw *glw_focused;

  /** 
   * All the glw_parent stuff is operated by this widgets
   * parents. That is, a widget may never touch these themselfs
   */		  
  float glw_parent_alpha;
  glw_vertex_t glw_parent_pos;
  glw_vertex_t glw_parent_scale;
  float glw_parent_misc[4];

  /**
   * Layout contraints
   */
  int16_t glw_req_size_x;
  int16_t glw_req_size_y;
  float glw_req_aspect;
  float glw_req_weight;

  int glw_flags;

#define GLW_ACTIVE              0x1
#define GLW_DESTROYED           0x2     /* was destroyed but someone
					   is holding references */
#define GLW_RENDER_LINKED       0x4     /* glw_render_link is linked */
#define GLW_EVERY_FRAME         0x8     /* Want GLW_SIGNAL_NEW_FRAME
					   at all times */

#define GLW_DEBUG               0x10    /* Debug this object */
#define GLW_FOCUS_BLOCKED       0x20
#define GLW_UPDATE_METRICS      0x40

#define GLW_IN_FOCUS_PATH       0x80
#define GLW_IN_PRESSED_PATH     0x100
#define GLW_IN_HOVER_PATH       0x200

#define GLW_DESTROYING          0x400  /* glw_destroy() has been called */


#define GLW_HIDDEN              0x800

#define GLW_DETACHED            0x1000
#define GLW_NO_INITIAL_TRANS    0x2000
#define GLW_CONSTRAINT_CONFED   0x4000
#define GLW_NEED_SCROLL         0x8000

#define GLW_CONSTRAINT_X        0x10000
#define GLW_CONSTRAINT_Y        0x20000
#define GLW_CONSTRAINT_A        0x40000
#define GLW_CONSTRAINT_W        0x80000
#define GLW_CONSTRAINT_F        0x100000

  // We rely on shifts to filter these against each other so they
  // must be consecutive, see glw_filter_constraints()
#define GLW_CONSTRAINT_IGNORE_X 0x200000
#define GLW_CONSTRAINT_IGNORE_Y 0x400000
#define GLW_CONSTRAINT_IGNORE_A 0x800000
#define GLW_CONSTRAINT_IGNORE_W 0x1000000
#define GLW_CONSTRAINT_IGNORE_F 0x2000000

#define GLW_CONSTRAINT_FLAGS (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y | \
                              GLW_CONSTRAINT_A | GLW_CONSTRAINT_W | \
			      GLW_CONSTRAINT_F )

#define GLW_FOCUS_ON_CLICK      0x4000000

#define GLW_SHADOW              0x8000000

#define GLW_LEFT_EDGE          0x10000000
#define GLW_TOP_EDGE           0x20000000
#define GLW_RIGHT_EDGE         0x40000000
#define GLW_BOTTOM_EDGE        0x80000000

  float glw_norm_weight;             /* Relative weight (normalized) */
  float glw_alpha;                   /* Alpha set by user */
  float glw_extra;

  float glw_focus_weight;

  glw_alignment_t glw_alignment;

  char *glw_id;

  struct glw_event_map_list glw_event_maps;		  

  struct glw_prop_sub_list glw_prop_subscriptions;

  struct token *glw_dynamic_expressions;

  float *glw_matrix;

} glw_t;


#define glw_have_x_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_X) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_X))
#define glw_have_y_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_Y) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_Y))
#define glw_have_a_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_A) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_A))
#define glw_have_w_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_W) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_W))
#define glw_have_f_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_F) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_F))

#define glw_filter_constraints(f) \
 (((f) & GLW_CONSTRAINT_FLAGS) & ~(((f) >> 5) & GLW_CONSTRAINT_FLAGS))


int glw_init(glw_root_t *gr, const char *theme, ui_t *ui, int primary,
	     const char *instance, const char *instance_title );

void glw_flush0(glw_root_t *gr);

void *glw_get_opaque(glw_t *w, glw_callback_t *func);

void glw_prepare_frame(glw_root_t *gr);

void glw_cond_wait(glw_root_t *gr, hts_cond_t *c);

void glw_detach0(glw_t *w);

void glw_move(glw_t *w, glw_t *b);

void glw_remove_from_parent(glw_t *w, glw_t *p);

void glw_lock(glw_root_t *gr);

void glw_unlock(glw_root_t *gr);

void glw_dispatch_event(uii_t *uii, event_t *e);


/**
 *
 */
#define glw_is_focusable(w) ((w)->glw_focus_weight > 0)

#define glw_is_focused(w) (!!((w)->glw_flags & GLW_IN_FOCUS_PATH))

#define glw_is_hovered(w) (!!((w)->glw_flags & GLW_IN_HOVER_PATH))

#define glw_is_pressed(w) (!!((w)->glw_flags & GLW_IN_PRESSED_PATH))

void glw_store_matrix(glw_t *w, glw_rctx_t *rc);

void glw_focus_set(glw_root_t *gr, glw_t *w, int interactive);

void glw_focus_open_path(glw_t *w);

void glw_focus_open_path_close_all_other(glw_t *w);

void glw_focus_close_path(glw_t *w);

void glw_focus_crawl(glw_t *w, int forward);

int glw_focus_step(glw_t *w, int forward);

void glw_focus_set_current_by_path(glw_t *w, int interactive);


/**
 * Clipping
 */
typedef enum {
  GLW_CLIP_TOP,
  GLW_CLIP_BOTTOM,
  GLW_CLIP_LEFT,
  GLW_CLIP_RIGHT,
} glw_clip_boundary_t;

int glw_clip_enable(glw_rctx_t *rc, glw_clip_boundary_t gcb);

void glw_clip_disable(glw_rctx_t *rc, int which);


/**
 * Views
 */
glw_t *glw_view_create(glw_root_t *gr, const char *src, 
		       glw_t *parent, struct prop *prop,
		       struct prop *prop_parent, int cache);

/**
 * Transitions
 */
typedef enum {
  GLW_TRANS_BLEND,
  GLW_TRANS_FLIP_HORIZONTAL,
  GLW_TRANS_FLIP_VERTICAL,
  GLW_TRANS_SLIDE_HORIZONTAL,
  GLW_TRANS_SLIDE_VERTICAL,
  GLW_TRANS_num,
} glw_transition_type_t;


static inline void
glw_flush_render_list(glw_t *w)
{
  glw_t *c;
  TAILQ_FOREACH(c, &w->glw_render_list, glw_render_link)
    c->glw_flags &= ~GLW_RENDER_LINKED;
  TAILQ_INIT(&w->glw_render_list);
}


static inline void
glw_link_render_list(glw_t *w, glw_t *c)
{
  TAILQ_INSERT_TAIL(&w->glw_render_list, c, glw_render_link);
  c->glw_flags |= GLW_RENDER_LINKED;
}


/*
 *
 */

#define GLW_ATTRIB_CHEW(attrib, ap)		\
do {						\
  switch(attrib) {				\
  case GLW_ATTRIB_END:				\
    break;					\
  case GLW_ATTRIB_num:                          \
    abort();                                    \
    break;                                      \
  case GLW_ATTRIB_SIGNAL_HANDLER:               \
    (void)va_arg(ap, void *);			\
    (void)va_arg(ap, void *);			\
    (void)va_arg(ap, int);			\
    break;                                      \
  case GLW_ATTRIB_PARENT_BEFORE:		\
  case GLW_ATTRIB_BIND_TO_PROPERTY:		\
  case GLW_ATTRIB_PROPROOTS:         		\
    (void)va_arg(ap, void *);			\
  case GLW_ATTRIB_PARENT:			\
  case GLW_ATTRIB_PARENT_HEAD:			\
  case GLW_ATTRIB_SOURCE:			\
  case GLW_ATTRIB_CAPTION:			\
  case GLW_ATTRIB_PREVIEW:			\
  case GLW_ATTRIB_CONTENT:			\
  case GLW_ATTRIB_ID:         			\
  case GLW_ATTRIB_BIND_TO_ID: 			\
  case GLW_ATTRIB_PIXMAP: 			\
  case GLW_ATTRIB_ORIGINATING_PROP: 		\
    (void)va_arg(ap, void *);			\
    break;					\
  case GLW_ATTRIB_ALIGNMENT:			\
  case GLW_ATTRIB_FREEZE:			\
  case GLW_ATTRIB_SET_FLAGS:			\
  case GLW_ATTRIB_CLR_FLAGS:			\
  case GLW_ATTRIB_MODE:                         \
  case GLW_ATTRIB_SET_IMAGE_FLAGS:              \
  case GLW_ATTRIB_CLR_IMAGE_FLAGS:              \
  case GLW_ATTRIB_SET_TEXT_FLAGS:               \
  case GLW_ATTRIB_CLR_TEXT_FLAGS:               \
  case GLW_ATTRIB_TRANSITION_EFFECT:            \
  case GLW_ATTRIB_CHILD_TILES_X:                \
  case GLW_ATTRIB_CHILD_TILES_Y:                \
  case GLW_ATTRIB_CHILD_HEIGHT:                 \
  case GLW_ATTRIB_CHILD_WIDTH:                  \
    (void)va_arg(ap, int);			\
    break;					\
  case GLW_ATTRIB_BORDER:                       \
  case GLW_ATTRIB_PADDING:                      \
  case GLW_ATTRIB_ROTATION:			\
    (void)va_arg(ap, double);			\
  case GLW_ATTRIB_RGB:                          \
  case GLW_ATTRIB_SCALING:                      \
  case GLW_ATTRIB_TRANSLATION:			\
    (void)va_arg(ap, double);			\
    (void)va_arg(ap, double);			\
  case GLW_ATTRIB_WEIGHT:			\
  case GLW_ATTRIB_ASPECT:			\
  case GLW_ATTRIB_ALPHA:			\
  case GLW_ATTRIB_ALPHA_SELF:			\
  case GLW_ATTRIB_ANGLE:			\
  case GLW_ATTRIB_EXTRA:			\
  case GLW_ATTRIB_TIME:                         \
  case GLW_ATTRIB_EXPANSION:                    \
  case GLW_ATTRIB_VALUE:                        \
  case GLW_ATTRIB_INT_STEP:                     \
  case GLW_ATTRIB_INT_MIN:                      \
  case GLW_ATTRIB_INT_MAX:                      \
  case GLW_ATTRIB_SIZE_SCALE:                   \
  case GLW_ATTRIB_SIZE_BIAS:                    \
  case GLW_ATTRIB_FOCUS_WEIGHT:                 \
  case GLW_ATTRIB_CHILD_ASPECT:                 \
  case GLW_ATTRIB_HEIGHT:                       \
  case GLW_ATTRIB_WIDTH:                        \
    (void)va_arg(ap, double);			\
    break;					\
  }						\
} while(0)

int glw_signal0(glw_t *w, glw_signal_t sig, void *extra);

int glw_widget_unproject(const float *m, float *x, float *y, 
			 const float *p, const float *dir);

glw_t *glw_create0(glw_root_t *gr, const glw_class_t *class, va_list ap);

glw_t *glw_create_i(glw_root_t *gr, 
		    const glw_class_t *class, ...)
  __attribute__((__sentinel__(0)));

#define glw_lock_assert() glw_lock_check(__FILE__, __LINE__)

void glw_lock_check(const char *file, const int line);

int glw_attrib_set0(glw_t *w, int init, va_list ap);

void glw_set_i(glw_t *w, ...) __attribute__((__sentinel__(0)));

void glw_destroy0(glw_t *w);

void glw_unref(glw_t *w);

#define glw_ref(w) ((w)->glw_refcnt++)

int glw_get_text0(glw_t *w, char *buf, size_t buflen);

int glw_get_int0(glw_t *w, int *result);

glw_t *glw_get_prev_n(glw_t *c, int count);

glw_t *glw_get_next_n(glw_t *c, int count);

glw_t *glw_get_prev_n_all(glw_t *c, int count);

glw_t *glw_get_next_n_all(glw_t *c, int count);

int glw_event(glw_root_t *gr, event_t *e);

int glw_event_to_widget(glw_t *w, event_t *e, int local);

typedef enum {
  GLW_POINTER_CLICK,
  GLW_POINTER_MOTION_UPDATE,  // Updated (mouse did really move)
  GLW_POINTER_MOTION_REFRESH, // GLW Internal refresh (every frame)
  GLW_POINTER_FOCUS_MOTION,
  GLW_POINTER_RELEASE,
  GLW_POINTER_SCROLL,
  GLW_POINTER_GONE,
} glw_pointer_event_type_t;

typedef struct glw_pointer_event {
  float x, y;
  float delta_y;
  glw_pointer_event_type_t type;
  int flags;
} glw_pointer_event_t;

void glw_pointer_event(glw_root_t *gr, glw_pointer_event_t *gpe);


int glw_navigate(glw_t *w, event_t *e, int local);

glw_t *glw_find_neighbour(glw_t *w, const char *id);

#define GLW_SIGNAL_PRI_INTERNAL 100

void glw_signal_handler_register(glw_t *w, glw_callback_t *func, void *opaque, 
				 int pri);

void glw_signal_handler_unregister(glw_t *w, glw_callback_t *func,
				   void *opaque);

#define glw_signal_handler_int(w, func) \
 glw_signal_handler_register(w, func, NULL, GLW_SIGNAL_PRI_INTERNAL)

int glw_signal0(glw_t *w, glw_signal_t sig, void *extra);

#define glw_render0(w, rc) ((w)->glw_class->gc_render(w, rc))

static inline void 
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


void glw_select(glw_t *p, glw_t *c);

int glw_check_system_features(glw_root_t *gr);

void glw_render_T(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc);

void glw_render_TS(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc);

void glw_rescale(glw_rctx_t *rc, float t_aspect);

extern const glw_vertex_t align_vertices[];

static inline void
glw_align_1(glw_rctx_t *rc, glw_alignment_t a, glw_alignment_t def)
{
  if(a == GLW_ALIGN_NONE)
    a = def;

  if(a != GLW_ALIGN_CENTER)
    glw_Translatef(rc, 
		   align_vertices[a].x, 
		   align_vertices[a].y, 
		   align_vertices[a].z);
}

static inline void
glw_align_2(glw_rctx_t *rc, glw_alignment_t a, glw_alignment_t def)
{
  if(a == GLW_ALIGN_NONE)
    a = def;

  if(a != GLW_ALIGN_CENTER)
    glw_Translatef(rc, 
		   -align_vertices[a].x, 
		   -align_vertices[a].y, 
		   -align_vertices[a].z);
}

/**
 * Render interface abstraction
 */

#define GLW_RENDER_ATTRIBS_NONE       0
#define GLW_RENDER_ATTRIBS_TEX        1
#define GLW_RENDER_ATTRIBS_TEX_COLOR  2


void glw_render_init(glw_renderer_t *gr, int vertices, int attribs);

void glw_render_set_vertices(glw_renderer_t *gr, int vertices);

void glw_render_free(glw_renderer_t *gr);

void glw_render_vtx_pos(glw_renderer_t *gr, int vertex,
			float x, float y, float z);

void glw_render_vtx_st(glw_renderer_t *gr, int vertex,
		       float s, float t);

void glw_render_vts_col(glw_renderer_t *gr, int vertex,
			float r, float g, float b, float a);

void glw_render(glw_renderer_t *gr, glw_root_t *root, glw_rctx_t *rc, 
		int mode, int attribs,
		glw_backend_texture_t *be_tex,
		float r, float g, float b, float a);

/**
 * Global flush interface 
 */
typedef struct glw_gf_ctrl {
  LIST_ENTRY(glw_gf_ctrl) link;
  void (*flush)(void *opaque);
  void *opaque;
} glw_gf_ctrl_t;

void glw_gf_register(glw_gf_ctrl_t *ggc);

void glw_gf_unregister(glw_gf_ctrl_t *ggc);

void glw_gf_do(void);

void glw_font_change_size(void *opaque, int fontsize);

/**
 *
 */

void glw_set_constraints(glw_t *w, int x, int y, float a, float weight,
			 int flags, int conf);

void glw_copy_constraints(glw_t *w, glw_t *src);

void glw_clear_constraints(glw_t *w);

int glw_array_get_xentries(glw_t *w);

#endif /* GLW_H */
