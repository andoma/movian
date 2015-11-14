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
#ifndef GLW_H
#define GLW_H

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>

#include "misc/queue.h"
#include "misc/layout.h"
#include "misc/pool.h"
#include "image/pixmap.h" // for PIXMAP_ROW_ALIGN
#include "prop/prop.h"
#include "main.h"
#include "settings.h"
#include "misc/minmax.h"

#ifdef DEBUG
#define GLW_TRACE(x, ...) do {                                     \
    if(gconf.debug_glw)                                            \
      TRACE(TRACE_DEBUG, "GLW", x, ##__VA_ARGS__);                 \
  } while(0)
#else
#define GLW_TRACE(x, ...)
#endif

// #define GLW_TRACK_REFRESH

// Beware: If you bump these over 16 remember to fix bitmasks too
#define NUM_CLIPPLANES 6

#define NUM_FADERS 0
#define NUM_STENCILERS 0

#define GLW_CURSOR_AUTOHIDE_TIME 3000000

struct event;

typedef struct glw_style glw_style_t;

typedef struct glw_program_args glw_program_args_t;

TAILQ_HEAD(glw_queue, glw);
LIST_HEAD(glw_head, glw);
LIST_HEAD(glw_event_map_list, glw_event_map);
LIST_HEAD(glw_prop_sub_list, glw_prop_sub);
LIST_HEAD(glw_loadable_texture_list, glw_loadable_texture);
TAILQ_HEAD(glw_loadable_texture_queue, glw_loadable_texture);
LIST_HEAD(glw_video_list, glw_video);
LIST_HEAD(glw_style_list, glw_style);
TAILQ_HEAD(glw_view_load_request_queue, glw_view_load_request);

// ------------------- Backends -----------------

#if CONFIG_GLW_BACKEND_OPENGL || CONFIG_GLW_BACKEND_OPENGL_ES
#include "glw_opengl.h"
#elif CONFIG_GLW_BACKEND_GX
#include "glw_gx.h"
#elif CONFIG_GLW_BACKEND_RSX
#include "glw_rsx.h"
#else
#error No backend for glw
#endif

// ------------------- Math mode -----------------

typedef float Vec4[4];
typedef float Vec3[3];


typedef struct {
  Vec4 r[4];
} Mtx;

// ------------------ Helpers ----------------------------

#define GLW_LERP(a, y0, y1) ((y0) + (a) * ((y1) - (y0)))
#define GLW_S(a) (sin(GLW_LERP(a, M_PI * -0.5, M_PI * 0.5)) * 0.5 + 0.5)
#define GLW_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GLW_MAX(a, b) ((a) > (b) ? (a) : (b))
#define GLW_DEG2RAD(a) ((a) * M_PI * 2.0f / 360.0f)
#define GLW_RESCALE(x, min, max) (((x) - (min)) / ((max) - (min)))
#define GLW_SWAP(a, b) do { typeof(a) c = (b); (b) = (a); (a) = (c); } while(0)
#define GLW_CLAMP(x, min, max) GLW_MIN(GLW_MAX((x), (min)), (max))

// -- Flags for dynamic evaluation of view statements --

#define GLW_VIEW_EVAL_LAYOUT     0x1
#define GLW_VIEW_EVAL_ACTIVE     0x2
#define GLW_VIEW_EVAL_FHP_CHANGE 0x4
#define GLW_VIEW_EVAL_OTHER      0x8
#define GLW_VIEW_EVAL_EM         0x10

#define GLW_VIEW_EVAL_PROP       0x100
#define GLW_VIEW_EVAL_KEEP       0x200

// ----------------- Attributes ------------------------

typedef enum {
  GLW_ATTRIB_END = 0,
  GLW_ATTRIB_VALUE,
  GLW_ATTRIB_ARGS,
  GLW_ATTRIB_PROP_PARENT,
  GLW_ATTRIB_PROP_SELF,
  GLW_ATTRIB_PROP_ITEM_MODEL,
  GLW_ATTRIB_PROP_PARENT_MODEL,
  GLW_ATTRIB_ANGLE,
  GLW_ATTRIB_MODE,
  GLW_ATTRIB_TIME,
  GLW_ATTRIB_TRANSITION_TIME,
  GLW_ATTRIB_INT_STEP,
  GLW_ATTRIB_INT_MIN,
  GLW_ATTRIB_INT_MAX,
  GLW_ATTRIB_TRANSITION_EFFECT,
  GLW_ATTRIB_EXPANSION,
  GLW_ATTRIB_CHILD_ASPECT,
  GLW_ATTRIB_CHILD_HEIGHT,
  GLW_ATTRIB_CHILD_WIDTH,
  GLW_ATTRIB_CHILD_TILES_X,
  GLW_ATTRIB_CHILD_TILES_Y,
  GLW_ATTRIB_ALPHA_EDGES,
  GLW_ATTRIB_PRIORITY,
  GLW_ATTRIB_FILL,
  GLW_ATTRIB_SPACING,
  GLW_ATTRIB_X_SPACING,
  GLW_ATTRIB_Y_SPACING,
  GLW_ATTRIB_SATURATION,
  GLW_ATTRIB_CENTER,
  GLW_ATTRIB_ALPHA_FALLOFF,
  GLW_ATTRIB_BLUR_FALLOFF,
  GLW_ATTRIB_RADIUS,
  GLW_ATTRIB_AUDIO_VOLUME,
  GLW_ATTRIB_ASPECT,
  GLW_ATTRIB_CHILD_SCALE,
  GLW_ATTRIB_PARENT_URL,
  GLW_ATTRIB_ALPHA_SELF,
  GLW_ATTRIB_SIZE_SCALE,
  GLW_ATTRIB_SIZE,
  GLW_ATTRIB_MAX_WIDTH,
  GLW_ATTRIB_MAX_LINES,
  GLW_ATTRIB_RGB,
  GLW_ATTRIB_COLOR1,
  GLW_ATTRIB_COLOR2,
  GLW_ATTRIB_SCALING,
  GLW_ATTRIB_TRANSLATION,
  GLW_ATTRIB_ROTATION,
  GLW_ATTRIB_PLANE,
  GLW_ATTRIB_MARGIN,
  GLW_ATTRIB_BORDER,
  GLW_ATTRIB_PADDING,
  GLW_ATTRIB_FONT,
  GLW_ATTRIB_TENTATIVE_VALUE,
  GLW_ATTRIB_num,
} glw_attribute_t;

/**
 * Text flags
 */
#define GTB_PASSWORD      0x1   /* Don't display real contents */
#define GTB_ELLIPSIZE     0x2
#define GTB_BOLD          0x4
#define GTB_ITALIC        0x8
#define GTB_OUTLINE       0x10
#define GTB_PERMANENT_CURSOR 0x20
#define GTB_OSK_PASSWORD     0x40   /* Password for on screen keyboard */

typedef struct glw_vertex {
  float x, y, z;
} glw_vertex_t;

typedef struct glw_rgb {
  float r, g, b;
} glw_rgb_t;

typedef struct glw_rect {
  int x1, x2;
  int y1, y2;
} glw_rect_t;

/**
 * Image flags
 */
#define GLW_IMAGE_CORNER_TOPLEFT       0x1
#define GLW_IMAGE_CORNER_TOPRIGHT      0x2
#define GLW_IMAGE_CORNER_BOTTOMLEFT    0x4
#define GLW_IMAGE_CORNER_BOTTOMRIGHT   0x8

// Defines the overlapping flags between GLW_TEX_ and GLW_IMAGE_
#define GLW_IMAGE_TEX_OVERLAP      0xff

#define GLW_IMAGE_FIXED_SIZE           0x100
#define GLW_IMAGE_BEVEL_LEFT           0x200
#define GLW_IMAGE_BEVEL_TOP            0x400
#define GLW_IMAGE_BEVEL_RIGHT          0x800
#define GLW_IMAGE_BEVEL_BOTTOM         0x1000
#define GLW_IMAGE_SET_ASPECT           0x2000
#define GLW_IMAGE_ADDITIVE             0x4000
#define GLW_IMAGE_BORDER_ONLY          0x8000
#define GLW_IMAGE_BORDER_LEFT          0x10000
#define GLW_IMAGE_BORDER_RIGHT         0x20000

/**
 * Video flags
 */
#define GLW_VIDEO_PRIMARY       0x1
#define GLW_VIDEO_NO_AUDIO      0x2


typedef enum {
  GLW_POINTER_LEFT_PRESS,
  GLW_POINTER_LEFT_RELEASE,
  GLW_POINTER_RIGHT_PRESS,
  GLW_POINTER_RIGHT_RELEASE,

  GLW_POINTER_TOUCH_START,
  GLW_POINTER_TOUCH_MOVE,
  GLW_POINTER_TOUCH_END,
  GLW_POINTER_TOUCH_CANCEL,

  GLW_POINTER_MOTION_UPDATE,  // Updated (mouse did really move)
  GLW_POINTER_MOTION_REFRESH, // GLW Internal refresh (every frame)
  GLW_POINTER_FOCUS_MOTION,
  GLW_POINTER_FINE_SCROLL,
  GLW_POINTER_SCROLL,
  GLW_POINTER_GONE,
} glw_pointer_event_type_t;

typedef struct glw_pointer_event {
  float x, y;
  float delta_x;
  float delta_y;
  glw_pointer_event_type_t type;
  int64_t ts;
} glw_pointer_event_t;



/**
 * XXX Document these
 */
typedef enum {
  GLW_SIGNAL_NONE,
  GLW_SIGNAL_DESTROY,
  GLW_SIGNAL_ACTIVE,
  GLW_SIGNAL_INACTIVE,

  GLW_SIGNAL_CHILD_CREATED,
  GLW_SIGNAL_CHILD_DESTROYED,
  GLW_SIGNAL_CHILD_MOVED,

  /**
   * Sent to widget when its focused child is changed.
   * Argument is newly focused child.
   */
  GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE,
  GLW_SIGNAL_FOCUS_CHILD_AUTOMATIC,

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
   * Emitted by a widget when it can be scrolled / moved
   */ 
  GLW_SIGNAL_CAN_SCROLL_CHANGED,

  /**
   *
   */ 
  GLW_SIGNAL_FULLWINDOW_CONSTRAINT_CHANGED,

  /**
   * Emitted when gc_status will return something else than it just did.
   * (Ie, when the output of it changes)
   * Only done by widget classes that actually implements gc_ready
   */
  GLW_SIGNAL_STATUS_CHANGED,


  /**
   * Emitted when gc_can_select_child maybe will return a different value
   * than it previously did
   */
  GLW_SIGNAL_RESELECT_CHANGED,


  /**
   * Emitted to a widget when it's requested to move itself
   * extra is 'glw_move_op_t'
   */
  GLW_SIGNAL_MOVE,

  /**
   *
   */
  GLW_SIGNAL_WRAP_CHECK,

  GLW_SIGNAL_num,

} glw_signal_t;


typedef struct {
  float knob_size;
  float position;
} glw_slider_metrics_t;

typedef struct {
  float value;
} glw_scroll_t;

typedef struct {
  int steps;
  int did_move;
} glw_move_op_t;


typedef int (glw_callback_t)(struct glw *w, void *opaque, 
			     glw_signal_t signal, void *value);


typedef enum {
  GLW_ORIENTATION_NONE,
  GLW_ORIENTATION_HORIZONTAL,
  GLW_ORIENTATION_VERTICAL,
} glw_orientation_t;

typedef enum {
  GLW_STATUS_IDLE,
  GLW_STATUS_LOADING,
  GLW_STATUS_LOADED,
  GLW_STATUS_ERROR,
} glw_widget_status_t;

#define glw_parent_data(c, type) ((type *)((char *)c + (c)->glw_class->gc_instance_size))

/**
 * GLW class definitions
 */
typedef struct glw_class {

  const char *gc_name;
  size_t gc_instance_size;
  size_t gc_parent_data_size;

  int gc_flags;
#define GLW_NAVIGATION_SEARCH_BOUNDARY 0x1
#define GLW_CAN_HIDE_CHILDS            0x2
#define GLW_UNCONSTRAINED              0x4

  /**
   * Constructor
   */
  void (*gc_ctor)(struct glw *w);

  /**
   * Set attributes for the widget.
   *
   * Return values are a bit special here
   *
   *  -1 = Widget does not respond to the attribute assignment using
   *       the type (The value type is inferred from the function call)
   *       GLW attribute assignment code may retry with a differnt type
   *       Typicall GLW tries to assign floats using float callback first
   *       and if that does not respond it will retry using int. Same goes
   *       (but vice versa) for int assignment.
   *
   *   0 = Widget reponds to attribute but value did not change.
   *
   *   1 = Widget responds and a rerender is required. This also implies
   *       a re-layout.
   *
   *   2 = Widget responds but only a layout is required (the layout code
   *       will figure out if a re-render is required)
   *
   */

#define GLW_SET_NOT_RESPONDING    -1
#define GLW_SET_NO_CHANGE          0
#define GLW_SET_RERENDER_REQUIRED  1
#define GLW_SET_LAYOUT_ONLY        2

  int (*gc_set_int)(struct glw *w, glw_attribute_t a, int value,
                    glw_style_t *gs);

  int (*gc_set_float)(struct glw *w, glw_attribute_t a, float value,
                      glw_style_t *gs);

  int (*gc_set_em)(struct glw *w, glw_attribute_t a, float value);

  int (*gc_set_rstr)(struct glw *w, glw_attribute_t a, rstr_t *value,
                     glw_style_t *gs);

  int (*gc_set_prop)(struct glw *w, glw_attribute_t a, prop_t *p);

  void (*gc_set_roots)(struct glw *w, prop_t *self, prop_t *parent,
                       prop_t *clone);

  int (*gc_bind_to_id)(struct glw *w, const char *id);

  int (*gc_set_float3)(struct glw *w, glw_attribute_t a, const float *vector,
                       glw_style_t *gs);

  int (*gc_set_float4)(struct glw *w, glw_attribute_t a, const float *vector);

  int (*gc_set_int16_4)(struct glw *w, glw_attribute_t a, const int16_t *v,
                        glw_style_t *gs);

  /**
   * Set attributes for the widget based on (unresolved) name only.
   *
   * This lets widgets respond to their own attributes.
   *
   * See comment above for return values (GLW_SET_*)
   */
  int (*gc_set_int_unresolved)(struct glw *w, const char *a, int value,
                               glw_style_t *gs);

  int (*gc_set_float_unresolved)(struct glw *w, const char *a, float value,
                                 glw_style_t *gs);

  int (*gc_set_rstr_unresolved)(struct glw *w, const char *a, rstr_t *value,
                                glw_style_t *gs);

  /**
   * Ask widget to render itself in the current render context
   */
  void (*gc_render)(struct glw *w, const struct glw_rctx *rc);

  /**
   * Ask widget to layout itself in current render context
   */
  void (*gc_layout)(struct glw *w, const struct glw_rctx *rc);

  /**
   * Ask widget to retire the given child
   */
  void (*gc_retire_child)(struct glw *w, struct glw *c);

  /**
   * Finalize a widget
   */
  void (*gc_dtor)(struct glw *w);

  /**
   * Invoked every new frame
   */
  void (*gc_newframe)(struct glw *w, int flags);

  /**
   * Return true if the widget is ready to be displayed
   * (ie, texture is loaded, etc)
   */
  glw_widget_status_t (*gc_status)(struct glw *w);

  /**
   *
   */
  void (*gc_suggest_focus)(struct glw *w, struct glw *c);

  /**
   *
   */
  int (*gc_send_event)(struct glw *w, struct event *e);

  /**
   *
   */
  int (*gc_bubble_event)(struct glw *w, struct event *e);

  /**
   *
   */
  int (*gc_pointer_event)(struct glw *w, const glw_pointer_event_t *gpe);


  /**
   * Send a GLW_SIGNAL_... to all listeners
   */
  glw_callback_t *gc_signal_handler;


  /**
   * Get a text representing the widget
   */
  const char *(*gc_get_text)(struct glw *w);

  /**
   * How to initialize glw_alignment when creating an instance
   */
  int gc_default_alignment;

  /**
   * Select a child
   *
   * Return 1 if something was changed, otherwise return 0
   */
  int (*gc_select_child)(struct glw *w, struct glw *c, struct prop *origin);

  /**
   * detachable widget control
   */
  void (*gc_detach_control)(struct glw *w, int on);

  /**
   * Return true if next/prev child can be selected
   */
  int (*gc_can_select_child)(struct glw *w, int next);

  /**
   *
   */
  void (*gc_get_rctx)(struct glw *w, struct glw_rctx *rc);

  /**
   *
   */
  void (*gc_mod_image_flags)(struct glw *w, int set, int clr,
                             glw_style_t *gs);

  /**
   *
   */
  void (*gc_mod_video_flags)(struct glw *w, int set, int clr);

  /**
   *
   */
  void (*gc_mod_text_flags)(struct glw *w, int set, int clr,
                            glw_style_t *gs);

  /**
   *
   */
  void (*gc_mod_flags2)(struct glw *w, int set, int clr);

  /**
   *
   */
  void (*gc_mod_flags2_always)(struct glw *w, int set, int clr,
                               glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_caption)(struct glw *w, const char *str, int type);

  /**
   *
   */
  void (*gc_update_text)(struct glw *w, const char *str);

  /**
   *
   */
  void (*gc_set_fs)(struct glw *w, rstr_t *str);

  /**
   *
   */
  void (*gc_bind_to_property)(struct glw *w,
			      prop_t *p,
			      const char **pname,
			      prop_t *view,
			      prop_t *args,
			      prop_t *clone);

  /**
   *
   */
  void (*gc_set_source)(struct glw *w, rstr_t *url, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_alt)(struct glw *w, rstr_t *url);

  /**
   *
   */
  void (*gc_set_sources)(struct glw *w, rstr_t **urls);

  /**
   *
   */
  void (*gc_set_how)(struct glw *w, const char *how);

  /**
   *
   */
  void (*gc_set_desc)(struct glw *w, const char *desc);

  /**
   *
   */
  void (*gc_set_focus_weight)(struct glw *w, float f, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_alpha)(struct glw *w, float f, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_blur)(struct glw *w, float f, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_weight)(struct glw *w, float f, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_width)(struct glw *w, int v, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_height)(struct glw *w, int v, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_align)(struct glw *w, int v, glw_style_t *gs);

  /**
   *
   */
  void (*gc_set_hidden)(struct glw *w, int v, glw_style_t *gs);

  /**
   *
   */
  void (*gc_freeze)(struct glw *w);

  /**
   *
   */
  void (*gc_thaw)(struct glw *w);

  /**
   *
   */
  const char *(*gc_get_identity)(struct glw *w, char *tmp, size_t tmpsize);

  /**
   * Registration link
   */
  LIST_ENTRY(glw_class) gc_link;

} glw_class_t;

void glw_register_class(glw_class_t *gc);

#define GLW_REGISTER_CLASS(n) INITIALIZER(glw_init_ ## n) { \
    glw_register_class(&n); }

const glw_class_t *glw_class_find_by_name(const char *name);

typedef struct glw_program glw_program_t;

/**
 * GLW root context
 */
typedef struct glw_root {
  prop_t *gr_prop_ui;
  prop_t *gr_prop_nav;
  prop_t *gr_prop_core;

  void (*gr_prop_dispatcher)(prop_courier_t *pc, int timeout);
  int gr_prop_maxtime;

  int gr_keyboard_mode;
  int gr_skin_scale_adjustment;
  int gr_reduce_cpu;
  prop_sub_t *gr_evsub;
  prop_sub_t *gr_scalesub;

  pool_t *gr_token_pool;
  pool_t *gr_clone_pool;
  pool_t *gr_style_binding_pool;
  int gr_gem_id_tally;

  int gr_frames;

  struct glw *gr_universe;

  LIST_HEAD(, glw_cached_view) gr_views;

  const char *gr_vpaths[5];
  char *gr_skin;

  hts_thread_t gr_thread;
  hts_mutex_t gr_mutex;
  prop_courier_t *gr_courier;

  struct glw_style_list gr_all_styles;

  int gr_style_tally;

  struct glw_queue gr_destroyer_queue;

  int gr_frameduration;
  float gr_framerate;

  struct glw_head gr_active_list;
  struct glw_head gr_active_flush_list;
  struct glw_head gr_active_dummy_list;
  struct glw_head gr_every_frame_list;

  int gr_width;
  int gr_height;

  prop_t *gr_prop_width;
  prop_t *gr_prop_height;
  prop_t *gr_prop_aspect;

  float gr_mouse_x;
  float gr_mouse_y;
  int gr_mouse_valid;

  struct glw_video_list gr_video_decoders;
  int64_t gr_ui_start;        // Timestamp UI was initialized
  int64_t gr_frame_start;     // Timestamp when we started rendering frame
  int64_t gr_frame_start_avtime; // AVtime when start rendering frame
  int64_t gr_hz_sample;
  prop_t *gr_is_fullscreen;   // Set if our window is in fullscreen

  uint64_t gr_time_usec;
  double gr_time_sec;

  int gr_need_refresh;
  int64_t gr_scheduled_refresh;

  /**
   * Screensaver
   */

  int64_t gr_screensaver_reset_at;

  int gr_screensaver_force_enable;
  prop_t *gr_screensaver_active;
  int gr_inhibit_screensaver;
  prop_sub_t *gr_disable_screensaver_sub;
  
  /**
   * View loader
   */

  hts_thread_t gr_view_loader_thread;
  int gr_view_loader_run;
  hts_cond_t gr_view_loader_cond;
  struct glw_view_load_request_queue gr_view_load_requests;
  struct glw_view_load_request_queue gr_view_eval_requests;


  /**
   * Font renderer
   */
  LIST_HEAD(,  glw_text_bitmap) gr_gtbs;
  TAILQ_HEAD(, glw_text_bitmap) gr_gtb_render_queue;
  TAILQ_HEAD(, glw_text_bitmap) gr_gtb_dim_queue;
  hts_cond_t gr_gtb_work_cond;
  hts_thread_t gr_font_thread;
  int gr_font_thread_running;

  rstr_t *gr_default_font;
  int gr_font_domain;

  /**
   * Image/Texture loader
   */
  int gr_tex_threads_running;
#define GLW_TEXTURE_THREADS 6
  hts_thread_t gr_tex_threads[GLW_TEXTURE_THREADS];

  LIST_HEAD(,  glw_image) gr_icons;
  hts_cond_t gr_tex_load_cond;

#define LQ_SKIN      0
#define LQ_TENTATIVE  1
#define LQ_THUMBS     2
#define LQ_OTHER      3
#define LQ_REFRESH    4
#define LQ_num        5

  struct glw_loadable_texture_queue gr_tex_load_queue[LQ_num];

  struct glw_loadable_texture_list gr_tex_active_list;
  struct glw_loadable_texture_list gr_tex_flush_list;
  struct glw_loadable_texture_queue gr_tex_rel_queue;

  struct {
    struct glw_loadable_texture_queue q;
    int size;
    int limit;
  } gr_tex_stash[2];

  struct glw_loadable_texture_list gr_tex_list;

  /**
   * Root focus leader
   */
  struct glw *gr_pointer_grab;
  struct glw *gr_pointer_hover;
  struct glw *gr_pointer_press;
  float gr_pointer_press_x;
  float gr_pointer_press_y;
  struct glw *gr_current_focus;
  struct glw *gr_last_focus;
  int gr_delayed_focus_leave;
  prop_t *gr_last_focused_interactive;
  prop_t *gr_pointer_visible;
  int gr_focus_work;

  struct glw *gr_current_cursor;
  void (*gr_cursor_focus_tracker)(struct glw *w, const struct glw_rctx *rc,
                                  struct glw *cursor);

  rstr_t *gr_pending_focus;

  /**
   * Backend specifics
   */
  glw_backend_root_t gr_be;
  void (*gr_be_render_unlocked)(struct glw_root *gr);
  struct pixmap *(*gr_br_read_pixels)(struct glw_root *gr);
  /**
   * Settings
   */
  int gr_underscan_v;
  int gr_underscan_h;
  int gr_current_size;

  // Base offsets, should be set by frontend
  int gr_base_underscan_v;
  int gr_base_underscan_h;

  setting_t *gr_setting_screensaver;


  /**
   * Rendering
   */
  Vec4 gr_clip[NUM_CLIPPLANES];
  float gr_clip_alpha_out[NUM_CLIPPLANES];
  float gr_clip_sharpness_out[NUM_CLIPPLANES];

  int gr_active_clippers;

  char gr_need_sw_clip;          /* Set if software clipping is needed
				    at the moment */


#if NUM_STENCILERS > 0
  int16_t gr_stencil_border[4];
  float gr_stencil_edge[4];
  int16_t gr_stencil_width;
  int16_t gr_stencil_height;
  Vec4 gr_stencil[2];
  const struct glw_backend_texture *gr_stencil_texture;
#endif
  
#if NUM_FADERS > 0
  Vec4 gr_fader[NUM_FADERS];
  float gr_fader_alpha[NUM_FADERS];
  float gr_fader_blur[NUM_FADERS];
  int gr_active_faders;
#endif

  int gr_num_render_jobs;
  int gr_render_jobs_capacity;
  struct glw_render_job *gr_render_jobs;
  struct glw_render_order *gr_render_order;

  float *gr_vertex_buffer;
  int gr_vertex_buffer_capacity;
  int gr_vertex_offset;

  int gr_blendmode;
  int gr_frontface;

#define GLW_RENDER_COLOR_ATTRIBUTES 0x1 /* set if the color attributes
					   are != [1,1,1,1] */

#define GLW_RENDER_BLUR_ATTRIBUTE   0x2 /* set if pos.w != 1 (sharpness)
					 * ie, the triangle should be blurred
					 */

#define GLW_RENDER_COLOR_OFFSET     0x4


  float *gr_vtmp_buffer;  // temporary buffer for emitting vertices
  int gr_vtmp_cur;
  int gr_vtmp_capacity;

  int gr_random;

  int gr_zmax;

  // On Screen Keyboard

  void (*gr_open_osk)(struct glw_root *gr, 
		      const char *title, const char *str, struct glw *w,
		      int password);


  struct glw *gr_osk_widget;
  prop_sub_t *gr_osk_text_sub;
  prop_sub_t *gr_osk_ev_sub;
  char *gr_osk_revert;

  // Backdrop render helper

  int gr_can_externalize;

#define GLW_MAX_EXTERNALIZED 4

  int gr_externalize_cnt;
  struct glw *gr_externalized[GLW_MAX_EXTERNALIZED];

  void *gr_private;

  uint8_t gr_left_pressed; // For pointer -> touch converter

  struct glw_rec *gr_rec;

} glw_root_t;


/**
 * Render context
 */
typedef struct glw_rctx {

  int *rc_zmax;

  // Current ModelView Matrix
  Mtx rc_mtx;

  float rc_alpha;
  float rc_sharpness;

  int16_t rc_width;
  int16_t rc_height;

  int16_t rc_zindex; // higher number is infront lower numbers (just as HTML)

  uint8_t rc_layer;

  // Used when rendering low res passes in bloom filter
  uint8_t rc_inhibit_shadows : 1;

  // Avoid storing matrix in mirrored view, etc
  uint8_t rc_inhibit_matrix_store : 1;

  uint8_t rc_overscanning : 1;

  uint8_t rc_invisible : 1;    /* Not really visible in UI
                                * Set when items are preloaded, etc
                                */

} glw_rctx_t;



#if ENABLE_GLW_MATH_SSE
#include "glw_math_sse.h"
#else
#include "glw_math_c.h"
#endif


/**
 * Signal handler
 */
typedef struct glw_signal_handler {
  LIST_ENTRY(glw_signal_handler) gsh_link;
  glw_callback_t *gsh_func;
  void *gsh_opaque;
  int16_t gsh_defer_remove;
} glw_signal_handler_t;

LIST_HEAD(glw_signal_handler_list, glw_signal_handler);


/**
 *
 */
typedef struct glw_styleset {
  unsigned int gss_refcount;
  int gss_numstyles;
  struct glw_style *gss_styles[0];
} glw_styleset_t;


/**
 *
 */

LIST_HEAD(glw_style_binding_list, glw_style_binding);

typedef struct glw_style_binding {
  LIST_ENTRY(glw_style_binding) gsb_style_link;
  LIST_ENTRY(glw_style_binding) gsb_widget_link;
  struct glw *gsb_widget;
  struct glw_style *gsb_style;
  int gsb_mark;
} glw_style_binding_t;


/**
 * GL widget
 */
typedef struct glw {
  const glw_class_t *glw_class;
  glw_root_t *glw_root;
  prop_t *glw_originating_prop;  /* Source prop we spawned from */

  LIST_ENTRY(glw) glw_active_link;
  LIST_ENTRY(glw) glw_every_frame_link;

  struct glw_signal_handler_list glw_signal_handlers;

  struct glw *glw_parent;
  TAILQ_ENTRY(glw) glw_parent_link;
  struct glw_queue glw_childs;

  struct glw *glw_selected;
  struct glw *glw_focused;

  rstr_t *glw_id_rstr;

  struct glw_event_map_list glw_event_maps;

  struct glw_prop_sub_list glw_prop_subscriptions;

  struct token *glw_dynamic_expressions;

  Mtx *glw_matrix;

  struct glw_clone *glw_clone;

  /**
   * Styling
   */
  glw_styleset_t *glw_styles; // List of available styles
  struct glw_style_binding_list glw_style_bindings; // Bound styles

  int glw_refcnt;

  /**
   * Layout contraints
   */
  int16_t glw_req_size_x;
  int16_t glw_req_size_y;
  float glw_req_weight;

  int glw_flags;

#define GLW_CONSTRAINT_X         0x1
#define GLW_CONSTRAINT_Y         0x2
#define GLW_CONSTRAINT_W         0x4
#define GLW_CONSTRAINT_D         0x8

#define GLW_CONSTRAINT_FLAGS (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y | \
                              GLW_CONSTRAINT_W | GLW_CONSTRAINT_D )

#define GLW_ACTIVE               0x10
#define GLW_FLOATING_FOCUS       0x20
#define GLW_FOCUS_BLOCKED        0x40
#define GLW_UPDATE_METRICS       0x80

#define GLW_IN_FOCUS_PATH        0x100
#define GLW_IN_PRESSED_PATH      0x200
#define GLW_IN_HOVER_PATH        0x400
#define GLW_DESTROYING           0x800

#define GLW_HIDDEN               0x1000
#define GLW_RETIRED              0x2000
#define GLW_CAN_SCROLL           0x4000
#define GLW_MARK                 0x8000

#define GLW_CLIPPED              0x1000000
#define GLW_FHP_SPILL_TO_CHILDS  0x4000000



#define GLW_CONSTRAINT_CONF_W    0x10000000
#define GLW_CONSTRAINT_CONF_X    0x20000000
#define GLW_CONSTRAINT_CONF_Y    0x40000000
#define GLW_CONSTRAINT_CONF_D    0x80000000


  int glw_flags2;

#define GLW2_CONSTRAINT_IGNORE_X    GLW_CONSTRAINT_X
#define GLW2_CONSTRAINT_IGNORE_Y    GLW_CONSTRAINT_Y
#define GLW2_CONSTRAINT_IGNORE_W    GLW_CONSTRAINT_W
#define GLW2_CONSTRAINT_IGNORE_D    GLW_CONSTRAINT_D
#define GLW2_ENABLED                0x10
#define GLW2_ALWAYS_GRAB_KNOB       0x40
#define GLW2_AUTOHIDE               0x80
#define GLW2_SHADOW                 0x100
#define GLW2_AUTOFADE               0x200
#define GLW2_EXPEDITE_SUBSCRIPTIONS 0x400
#define GLW2_AUTOMARGIN             0x800
#define GLW2_NO_INITIAL_TRANS       0x2000
#define GLW2_FOCUS_ON_CLICK         0x4000
#define GLW2_AUTOREFOCUSABLE        0x8000
#define GLW2_NAV_FOCUSABLE          0x10000 /* Widget is focusable when
                                               navigating with keyboard input */
#define GLW2_HOMOGENOUS             0x20000
#define GLW2_DEBUG                  0x40000     /* Debug this object */
#define GLW2_NAV_WRAP               0x200000
#define GLW2_AUTO_FOCUS_LIMIT       0x400000
#define GLW2_CURSOR                 0x800000
#define GLW2_POSITIONAL_NAVIGATION  0x1000000
#define GLW2_CLICKABLE              0x2000000 // Widget is clickable
#define GLW2_FHP_SPILL              0x4000000


  float glw_alpha;                   /* Alpha set by user */
  float glw_sharpness;               /* 1-Blur set by user */

  float glw_focus_weight;

  int16_t glw_zoffset;

  uint8_t glw_alignment;

  uint8_t glw_dynamic_eval;   // GLW_VIEW_EVAL_ -flags

#ifdef DEBUG
  rstr_t *glw_file;
  int glw_line;
#endif
} glw_t;

static __inline int
glw_filter_constraints(const glw_t *w)
{
  return (w->glw_flags & ~w->glw_flags2) & GLW_CONSTRAINT_FLAGS;
}

#define GLW_INIT_KEYBOARD_MODE 0x1

int glw_init(glw_root_t *gr);

int glw_init2(glw_root_t *gr, int flags);

int glw_init4(glw_root_t *gr,
              void (*dispatcher)(prop_courier_t *pc, int timeout),
              prop_courier_t *courier,
              int flags);

void glw_fini(glw_root_t *gr);

void glw_load_universe(glw_root_t *gr);

void glw_unload_universe(glw_root_t *gr);

void glw_flush(glw_root_t *gr);

void *glw_get_opaque(glw_t *w, glw_callback_t *func);

#define GLW_REINITIALIZE_VDPAU 0x1
#define GLW_NO_FRAMERATE_UPDATE 0x2

void glw_prepare_frame(glw_root_t *gr, int flags);

void glw_idle(glw_root_t *gr);

void glw_post_scene(glw_root_t *gr);

void glw_reap(glw_root_t *gr);

void glw_cond_wait(glw_root_t *gr, hts_cond_t *c);

void glw_retire_child(glw_t *w);

void glw_move(glw_t *w, glw_t *b);

void glw_remove_from_parent(glw_t *w, glw_t *p);

#define glw_lock(gr) hts_mutex_lock(&(gr)->gr_mutex);

#define glw_unlock(gr) hts_mutex_unlock(&(gr)->gr_mutex);

void glw_set_weight(glw_t *w, float v, glw_style_t *origin);

void glw_set_alpha(glw_t *w, float v, glw_style_t *origin);

void glw_set_blur(glw_t *w, float v, glw_style_t *origin);

void glw_set_width(glw_t *w, int v, glw_style_t *origin);

void glw_set_height(glw_t *w, int v, glw_style_t *origin);

void glw_set_align(glw_t *w, int v, glw_style_t *origin);

void glw_set_hidden(glw_t *w, int v, glw_style_t *origin);

void glw_set_divider(glw_t *w, int v);



/**
 *
 */
#define glw_is_focusable(w) ((w)->glw_focus_weight > 0)

#define glw_is_focusable_or_clickable(w) \
  (((w)->glw_focus_weight > 0) || (w)->glw_flags2 & GLW2_CLICKABLE)

#define glw_is_focused(w) (!!((w)->glw_flags & GLW_IN_FOCUS_PATH))

#define glw_is_hovered(w) (!!((w)->glw_flags & GLW_IN_HOVER_PATH))

#define glw_is_pressed(w) (!!((w)->glw_flags & GLW_IN_PRESSED_PATH))

void glw_store_matrix(glw_t *w, const glw_rctx_t *rc);

#define GLW_FOCUS_SET_AUTOMATIC    0
#define GLW_FOCUS_SET_AUTOMATIC_FF 1
#define GLW_FOCUS_SET_INTERACTIVE  2
#define GLW_FOCUS_SET_SUGGESTED    3

int glw_focus_set(glw_root_t *gr, glw_t *w, int how,
                  const char *whom); // Return 1 if we managed to set focus

void glw_focus_check_pending(glw_t *w);

void glw_focus_open_path(glw_t *w);

void glw_focus_open_path_close_all_other(glw_t *w);

void glw_focus_close_path(glw_t *w);

void glw_focus_crawl(glw_t *w, int forward, int interactive);

int glw_focus_step(glw_t *w, int forward);

void glw_focus_suggest(glw_t *w);

glw_t *glw_focus_by_path(glw_t *w);

void glw_set_focus_weight(glw_t *w, float f, glw_style_t *gs);

int glw_is_child_focusable(glw_t *w);

glw_t *glw_get_focusable_child(glw_t *w);

int glw_focus_child(glw_t *w);


/**
 * Clipping
 */
typedef enum {
  GLW_CLIP_LEFT,
  GLW_CLIP_TOP,
  GLW_CLIP_RIGHT,
  GLW_CLIP_BOTTOM,
} glw_clip_boundary_t;

int glw_clip_enable(glw_root_t *gr, const glw_rctx_t *rc,
		    glw_clip_boundary_t gcb, float distance,
                    float alpha_out, float sharpness_out);

void glw_clip_disable(glw_root_t *gr, int which);

#if NUM_FADERS > 0
int glw_fader_enable(glw_root_t *gr, const glw_rctx_t *rc, const float *plane,
		     float alphafo, float blurfo);

void glw_fader_disable(glw_root_t *gr, int which);
#endif

#if NUM_STENCILERS > 0
void glw_stencil_enable(glw_root_t *gr, const glw_rctx_t *rc,
			const struct glw_backend_texture *tex,
			const int16_t *border);

void glw_stencil_disable(glw_root_t *gr);
#endif

void glw_lp(float *v, glw_root_t *gr, float t, float alpha);


/**
 * Views
 */
glw_t *glw_view_create(glw_root_t *gr, rstr_t *url, rstr_t *alturl,
                       glw_t *parent, prop_t *prop, prop_t *prop_parent,
                       prop_t *args, prop_t *prop_clone,
                       rstr_t *file, int line);

void glw_view_eval_signal(glw_t *w, glw_signal_t sig);

void glw_view_eval_layout(glw_t *w, const glw_rctx_t *rc, int mask);

void glw_view_eval_dynamics(glw_t *w, int flags);

/**
 * Transitions
 */
typedef enum {
  GLW_TRANS_NONE,
  GLW_TRANS_BLEND,
  GLW_TRANS_FLIP_HORIZONTAL,
  GLW_TRANS_FLIP_VERTICAL,
  GLW_TRANS_SLIDE_HORIZONTAL,
  GLW_TRANS_SLIDE_VERTICAL,
  GLW_TRANS_num,
} glw_transition_type_t;



const char *glw_get_path(glw_t *w);

const char *glw_get_name(glw_t *w);

void glw_print_tree(glw_t *w);

int glw_widget_unproject(const Mtx *m, float *xp, float *yp,
			 const Vec3 p, const Vec3 dir);

glw_t *glw_create(glw_root_t *gr, const glw_class_t *class,
                  glw_t *parent, glw_t *before, prop_t *originator,
                  rstr_t *file, int line);

#define glw_lock_assert() glw_lock_check(__FILE__, __LINE__)

void glw_lock_check(const char *file, const int line);

void glw_destroy(glw_t *w);

void glw_destroy_childs(glw_t *w);

void glw_suspend_subscriptions(glw_t *w);

void glw_unref(glw_t *w);

#define glw_ref(w) ((w)->glw_refcnt++)

glw_t *glw_get_prev_n(glw_t *c, int count);

glw_t *glw_get_next_n(glw_t *c, int count);

int glw_event(glw_root_t *gr, struct event *e);

int glw_root_event_handler(glw_root_t *gr, event_t *e);

int glw_event_to_widget(glw_t *w, struct event *e);

void glw_inject_event(glw_root_t *gr, event_t *e);

void glw_pointer_event(glw_root_t *gr, glw_pointer_event_t *gpe);

int glw_pointer_event0(glw_root_t *gr, glw_t *w, glw_pointer_event_t *gpe,
		       glw_t **hp, Vec3 p, Vec3 dir);

int glw_pointer_event_deliver(glw_t *w, glw_pointer_event_t *gpe);


glw_t *glw_find_neighbour(glw_t *w, const char *id);

void glw_signal_handler_register(glw_t *w, glw_callback_t *func, void *opaque);

void glw_signal_handler_unregister(glw_t *w, glw_callback_t *func,
				   void *opaque);

void glw_signal0(glw_t *w, glw_signal_t sig, void *extra);

static __inline int
glw_send_event2(glw_t *w, event_t *e)
{
  const glw_class_t *gc = w->glw_class;
  return gc->gc_send_event != NULL && gc->gc_send_event(w, e);
}

static __inline int
glw_send_pointer_event(glw_t *w, const glw_pointer_event_t *gpe)
{
  const glw_class_t *gc = w->glw_class;
  return gc->gc_pointer_event != NULL && gc->gc_pointer_event(w, gpe);
}

static __inline int
glw_bubble_event2(glw_t *w, event_t *e)
{
  const glw_class_t *gc = w->glw_class;
  return gc->gc_bubble_event != NULL && gc->gc_bubble_event(w, e);
}

void glw_layout0(glw_t *w, const glw_rctx_t *rc);

void glw_rctx_init(glw_rctx_t *rc, int width, int height, int overscan,
                   int *zmax);

int glw_check_system_features(glw_root_t *gr);

void glw_scale_to_aspect(glw_rctx_t *rc, float t_aspect);

void glw_reposition(glw_rctx_t *rc, int left, int top, int right, int bottom);

void glw_repositionf(glw_rctx_t *rc, float left, float top,
		     float right, float bottom);

void glw_align_1(glw_rctx_t *rc, int a);

void glw_align_2(glw_rctx_t *rc, int a);

void glw_wirebox(glw_root_t *gr, const glw_rctx_t *rc);

void glw_renderer_render(glw_root_t *gr);

void glw_render_zoffset(glw_t *w, const glw_rctx_t *rc);

static inline void glw_render0(glw_t *w, const glw_rctx_t *rc)
{
  if(unlikely(w->glw_zoffset != 0)) {
    glw_render_zoffset(w, rc);
  } else {
    w->glw_class->gc_render(w, rc);
  }

  if(unlikely(w->glw_flags2 & GLW2_DEBUG))
    glw_wirebox(w->glw_root, rc);
}

static inline void glw_zinc(glw_rctx_t *rc)
{
  rc->rc_zindex++;
  *rc->rc_zmax = MAX(*rc->rc_zmax, rc->rc_zindex);
}



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

/**
 *
 */

void glw_set_constraints(glw_t *w, int x, int y, float weight,
			 int flags);

void glw_conf_constraints(glw_t *w, int x, int y, float weight,
			  int conf);

void glw_mod_constraints(glw_t *w, int x, int y, float weight, int flags,
                         int modflags);

void glw_copy_constraints(glw_t *w, glw_t *src);

void glw_clear_constraints(glw_t *w);

glw_t *glw_next_widget(glw_t *w);

glw_t *glw_prev_widget(glw_t *w);

glw_t *glw_first_widget(glw_t *w);

glw_t *glw_last_widget(glw_t *w);

void glw_set_fullscreen(glw_root_t *gr, int fullscreen);

int glw_kill_screensaver(glw_root_t *gr);

void glw_hide(glw_t *w);

void glw_unhide(glw_t *w);

void glw_mod_flags2(glw_t *w, int set, int clr);

int glw_attrib_set_float3_clamped(float *dst, const float *src);

int glw_attrib_set_float3(float *dst, const float *src);

int glw_attrib_set_rgb(glw_rgb_t *rgb, const float *src);

int glw_attrib_set_float4(float *dst, const float *src);

int glw_attrib_set_int16_4(int16_t *dst, const int16_t *src);


/**
 *
 */
#define GLW_BLEND_NORMAL   0
#define GLW_BLEND_ADDITIVE 1

void glw_blendmode(struct glw_root *gr, int mode);

#define GLW_CW  0
#define GLW_CCW 1

void glw_frontface(struct glw_root *gr, char how);

struct glw_program *glw_make_program(struct glw_root *gr, 
				     const char *vertex_shader,
				     const char *fragment_shader);

void glw_destroy_program(struct glw_root *gr, struct glw_program *gp);


// text bitmap semi-private stuff

void glw_gtb_set_caption_raw(glw_t *w, uint32_t *uc, int len);

extern const float glw_identitymtx[16];

void glw_icon_flush(glw_root_t *gr);

int glw_image_get_details(glw_t *w, char *path, size_t pathlen, float *alpha);

void glw_project_matrix(glw_rect_t *r, const Mtx *m, const glw_root_t *gr);

void glw_project(glw_rect_t *r, const glw_rctx_t *rc, const glw_root_t *gr);

#define GLW_REFRESH_FLAG_LAYOUT 0x1
#define GLW_REFRESH_FLAG_RENDER 0x2

#define GLW_REFRESH_LAYOUT_ONLY 2

#ifdef GLW_TRACK_REFRESH

#define glw_need_refresh(gr, how) \
  glw_need_refresh0(gr, how, __FILE__, __LINE__)

void glw_need_refresh0(glw_root_t *gr, int how, const char *file, int line);

#else

static __inline void
glw_need_refresh(glw_root_t *gr, int how)
{
  int flags = GLW_REFRESH_FLAG_LAYOUT;

  if(how != GLW_REFRESH_LAYOUT_ONLY)
    flags |= GLW_REFRESH_FLAG_RENDER;
  gr->gr_need_refresh |= flags;
}

#endif

static __inline void
glw_schedule_refresh(glw_root_t *gr, int64_t when)
{
  gr->gr_scheduled_refresh = MIN(gr->gr_scheduled_refresh, when);
}

#endif /* GLW_H */


