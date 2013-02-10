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
#include "misc/layout.h"
#include "misc/pool.h"
#include "misc/pixmap.h" // for PIXMAP_ROW_ALIGN
#include "prop/prop.h"
#include "showtime.h"
#include "settings.h"


// Beware: If you bump these over 16 remember to fix bitmasks too
#define NUM_CLIPPLANES 6
#define NUM_FADERS 6

struct event;

TAILQ_HEAD(glw_queue, glw);
LIST_HEAD(glw_head, glw);
LIST_HEAD(glw_event_map_list, glw_event_map);
LIST_HEAD(glw_prop_sub_list, glw_prop_sub);
LIST_HEAD(glw_loadable_texture_list, glw_loadable_texture);
TAILQ_HEAD(glw_loadable_texture_queue, glw_loadable_texture);
LIST_HEAD(glw_video_list, glw_video);

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

// all supported apple devices should support SSE
#if (defined(__x86_64) || defined(__APPLE__)) && !defined(__llvm__)
#define ENABLE_GLW_MATH_SSE 1
#else
#define ENABLE_GLW_MATH_SSE 0
#endif

#if ENABLE_GLW_MATH_SSE
#include <xmmintrin.h>
typedef __m128 Mtx[4];
typedef __m128 Vec4;
typedef __m128 Vec3;
typedef __m128 Vec2;
#else
typedef float Mtx[16];
typedef float Vec4[4];
typedef float Vec3[3];
typedef float Vec2[2];
#endif

// ------------------ Helpers ----------------------------

#define GLW_LERP(a, y0, y1) ((y0) + (a) * ((y1) - (y0)))
#define GLW_S(a) (sin(GLW_LERP(a, M_PI * -0.5, M_PI * 0.5)) * 0.5 + 0.5)
#define GLW_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GLW_MAX(a, b) ((a) > (b) ? (a) : (b))
#define GLW_DEG2RAD(a) ((a) * M_PI * 2.0f / 360.0f)
#define GLW_RESCALE(x, min, max) (((x) - (min)) / ((max) - (min)))
#define GLW_SWAP(a, b) do { typeof(a) c = (b); (b) = (a); (a) = (c); } while(0)
#define GLW_CLAMP(x, min, max) GLW_MIN(GLW_MAX((x), (min)), (max))


typedef enum {
  GLW_ATTRIB_END = 0,
  GLW_ATTRIB_VALUE,
  GLW_ATTRIB_ARGS,
  GLW_ATTRIB_PROP_PARENT,
  GLW_ATTRIB_PROP_SELF,
  GLW_ATTRIB_PROP_MODEL,
  GLW_ATTRIB_ANGLE,
  GLW_ATTRIB_MODE,
  GLW_ATTRIB_TIME,
  GLW_ATTRIB_TRANSITION_TIME,
  GLW_ATTRIB_INT_STEP,
  GLW_ATTRIB_INT_MIN,
  GLW_ATTRIB_INT_MAX,
  GLW_ATTRIB_PROPROOTS3,
  GLW_ATTRIB_TRANSITION_EFFECT,
  GLW_ATTRIB_EXPANSION,
  GLW_ATTRIB_BIND_TO_ID,
  GLW_ATTRIB_CHILD_ASPECT,
  GLW_ATTRIB_CHILD_HEIGHT,
  GLW_ATTRIB_CHILD_WIDTH,
  GLW_ATTRIB_CHILD_TILES_X,
  GLW_ATTRIB_CHILD_TILES_Y,
  GLW_ATTRIB_PAGE,
  GLW_ATTRIB_PAGE_BY_ID,
  GLW_ATTRIB_ALPHA_EDGES,
  GLW_ATTRIB_PRIORITY,
  GLW_ATTRIB_FILL,
  GLW_ATTRIB_SPACING,
  GLW_ATTRIB_SCROLL_THRESHOLD,
  GLW_ATTRIB_X_SPACING,
  GLW_ATTRIB_Y_SPACING,
  GLW_ATTRIB_SATURATION,
  GLW_ATTRIB_CENTER,
  GLW_ATTRIB_ALPHA_FALLOFF,
  GLW_ATTRIB_BLUR_FALLOFF,
  GLW_ATTRIB_RADIUS,
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

/**
 * Image flags
 */
#define GLW_IMAGE_CORNER_TOPLEFT       0x1
#define GLW_IMAGE_CORNER_TOPRIGHT      0x2
#define GLW_IMAGE_CORNER_BOTTOMLEFT    0x4
#define GLW_IMAGE_CORNER_BOTTOMRIGHT   0x8
#define GLW_IMAGE_FIXED_SIZE           0x10
#define GLW_IMAGE_BEVEL_LEFT           0x20
#define GLW_IMAGE_BEVEL_TOP            0x40
#define GLW_IMAGE_BEVEL_RIGHT          0x80
#define GLW_IMAGE_BEVEL_BOTTOM         0x100
#define GLW_IMAGE_SET_ASPECT           0x200
#define GLW_IMAGE_ADDITIVE             0x400
#define GLW_IMAGE_BORDER_ONLY          0x800
#define GLW_IMAGE_BORDER_LEFT          0x1000
#define GLW_IMAGE_BORDER_RIGHT         0x2000
#define GLW_IMAGE_ASPECT_FIXED_BORDERS 0x4000

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
  GLW_POINTER_MOTION_UPDATE,  // Updated (mouse did really move)
  GLW_POINTER_MOTION_REFRESH, // GLW Internal refresh (every frame)
  GLW_POINTER_FOCUS_MOTION,
  GLW_POINTER_SCROLL,
  GLW_POINTER_GONE,
  GLW_POINTER_TOUCH_PRESS,
  GLW_POINTER_TOUCH_RELEASE,
  GLW_POINTER_TOUCH_MOTION,
} glw_pointer_event_type_t;

typedef struct glw_pointer_event {
  float x, y;
  float delta_y;
  float vel_x, vel_y;
  glw_pointer_event_type_t type;
  int flags;
} glw_pointer_event_t;



/**
 * XXX Document these
 */
typedef enum {
  GLW_SIGNAL_NONE,
  GLW_SIGNAL_DESTROY,
  GLW_SIGNAL_ACTIVE,
  GLW_SIGNAL_INACTIVE,
  GLW_SIGNAL_LAYOUT,

  GLW_SIGNAL_CHILD_CREATED,
  GLW_SIGNAL_CHILD_DESTROYED,
  GLW_SIGNAL_CHILD_MOVED,

  GLW_SIGNAL_EVENT_BUBBLE,

  GLW_SIGNAL_EVENT,

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
   * Emitted by a widget when it can be scrolled / moved
   */ 
  GLW_SIGNAL_CAN_SCROLL_CHANGED,

  /**
   *
   */ 
  GLW_SIGNAL_FULLWINDOW_CONSTRAINT_CHANGED,

  /**
   * Emitted when gc_ready will return something else than it just did.
   * (Ie, when the output of it changes)
   * Only done by widget classes that actually implements gc_ready
   */
  GLW_SIGNAL_READINESS,


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
} glw_move_op_t;


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
#define GLW_CAN_HIDE_CHILDS            0x2
#define GLW_TRANSFORM_LR_TO_UD         0x8
#define GLW_UNCONSTRAINED              0x10

  /**
   * If the widget arranges its childer in horizontal or vertical order
   * it should be defined here
   */
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


  /**
   * Constructor
   */
  void (*gc_ctor)(struct glw *w);

  /**
   * Set attributes GLW_ATTRIB_... for the widget.
   */
  void (*gc_set)(struct glw *w, va_list ap);

  /**
   * Ask widget to render itself in the current render context
   */
  void (*gc_render)(struct glw *w, const struct glw_rctx *rc);

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
  int (*gc_ready)(struct glw *w);

  /**
   *
   */
  void (*gc_suggest_focus)(struct glw *w, struct glw *c);

  /**
   * Send a GLW_SIGNAL_... to all listeners
   */
  glw_callback_t *gc_signal_handler;


  /**
   * Custom code for iterating of a widget's children when delivering
   * pointer events
   */ 
  int (*gc_gpe_iterator)(struct glw_root *gr, struct glw *r, 
			 glw_pointer_event_t *gpe, struct glw **hp,
			 Vec3 p, Vec3 dir);

  /**
   * Get a text representing the widget
   */
  const char *(*gc_get_text)(struct glw *w);

  /**
   *
   */
  float (*gc_get_child_pos)(struct glw *w, struct glw *c);

  float gc_escape_score;

  /**
   * How to initialize glw_alignment when creating an instance
   */
  int gc_default_alignment;

  /**
   * Given child c, return how many items to steps in order to move one column
   */
  int (*gc_get_next_row)(struct glw *w, struct glw *c, int rev);

  /**
   * Select a child
   */
  void (*gc_select_child)(struct glw *w, struct glw *c, struct prop *origin);

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
  void (*gc_set_rgb)(struct glw *w, const float *rgb);

  /**
   *
   */
  void (*gc_set_color1)(struct glw *w, const float *rgb);

  /**
   *
   */
  void (*gc_set_color2)(struct glw *w, const float *rgb);

  /**
   *
   */
  void (*gc_set_translation)(struct glw *w, const float *xyz);

  /**
   *
   */
  void (*gc_set_scaling)(struct glw *w, const float *xyz);

  /**
   *
   */
  void (*gc_set_border)(struct glw *w, const int16_t *v);


  /**
   *
   */
  void (*gc_set_padding)(struct glw *w, const int16_t *v);

  /**
   *
   */
  void (*gc_set_margin)(struct glw *w, const int16_t *v);

  /**
   *
   */
  void (*gc_set_clipping)(struct glw *w, const float *v);

  /**
   *
   */
  void (*gc_set_plane)(struct glw *w, const float *v);

  /**
   *
   */
  void (*gc_set_rotation)(struct glw *w, const float *v);

  /**
   *
   */
  void (*gc_mod_image_flags)(struct glw *w, int set, int clr);

  /**
   *
   */
  void (*gc_mod_video_flags)(struct glw *w, int set, int clr);

  /**
   *
   */
  void (*gc_mod_text_flags)(struct glw *w, int set, int clr);

  /**
   *
   */
  void (*gc_mod_flags2)(struct glw *w, int set, int clr);

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
  void (*gc_set_font)(struct glw *w, rstr_t *str);

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
  void (*gc_set_source)(struct glw *w, rstr_t *url);

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
  void (*gc_set_alpha_self)(struct glw *w, float a);

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
  void (*gc_set_size_scale)(struct glw *w, const float v);

  /**
   *
   */
  void (*gc_set_default_size)(struct glw *w, int px);

  /**
   *
   */
  void (*gc_set_min_size)(struct glw *w, int px);

  /**
   *
   */
  void (*gc_set_max_lines)(struct glw *w, int lines);

  /**
   *
   */
  const char *(*gc_get_identity)(struct glw *w);

  /**
   * Registration link
   */
  LIST_ENTRY(glw_class) gc_link;

} glw_class_t;

void glw_register_class(glw_class_t *gc);

#define GLW_REGISTER_CLASS(n) \
static void  __attribute__((constructor)) widgetclassinit ## n(void) \
{ static int cnt; if(!cnt) glw_register_class(&n); cnt++; }

const glw_class_t *glw_class_find_by_name(const char *name);

typedef struct glw_program glw_program_t;

/**
 * GLW root context
 */
typedef struct glw_root {
  prop_t *gr_prop_ui;
  prop_t *gr_prop_nav;

  int gr_stop;
  prop_sub_t *gr_evsub;

  pool_t *gr_token_pool;
  pool_t *gr_clone_pool;

  int gr_frames;

  struct glw *gr_universe;

  LIST_HEAD(, glw_cached_view) gr_views;

  const char *gr_vpaths[5];
  char *gr_skin;

  hts_thread_t gr_thread;
  hts_mutex_t gr_mutex;
  prop_courier_t *gr_courier;

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

  float gr_mouse_x;
  float gr_mouse_y;
  int gr_mouse_valid;

  struct glw_video_list gr_video_decoders;
  int64_t gr_ui_start;        // Timestamp UI was initialized
  int64_t gr_frame_start;     // Timestamp when we started rendering frame
  int64_t gr_frame_start_avtime; // AVtime when start rendering frame
  int64_t gr_hz_sample;
  prop_t *gr_is_fullscreen;   // Set if our window is in fullscreen

  float gr_time;

  /**
   * Screensaver
   */

  int gr_screensaver_counter; // In frames
  int gr_screensaver_force_enable;
  prop_t *gr_screensaver_active;

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


  struct glw_loadable_texture_list gr_tex_list;

  int gr_normalized_texture_coords;

  /**
   * Root focus leader
   */
  struct glw *gr_pointer_grab;
  struct glw *gr_pointer_hover;
  struct glw *gr_pointer_press;
  struct glw *gr_current_focus;
  int gr_delayed_focus_leave;
  prop_t *gr_last_focused_interactive;
  prop_t *gr_pointer_visible;
  int gr_focus_work;

  /**
   * Backend specifics
   */ 
  glw_backend_root_t gr_be;
  void (*gr_be_prepare)(struct glw_root *gr);
  void (*gr_be_render_unlocked)(struct glw_root *gr);

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
  int gr_active_clippers;

  int16_t gr_stencil_border[4];
  float gr_stencil_edge[4];
  int16_t gr_stencil_width;
  int16_t gr_stencil_height;
  Vec4 gr_stencil[2];
  const struct glw_backend_texture *gr_stencil_texture;

  Vec4 gr_fader[NUM_FADERS];
  float gr_fader_alpha[NUM_FADERS];
  float gr_fader_blur[NUM_FADERS];

  int gr_active_faders;


  char gr_need_sw_clip;          /* Set if software clipping is needed
				    at the moment */


  void (*gr_set_hw_clipper)(const struct glw_rctx *rc, int which,
			    const Vec4 vec);
  void (*gr_clr_hw_clipper)(int which);
  void (*gr_render)(struct glw_root *gr,
		    const Mtx m,
		    const struct glw_backend_texture *t0,
		    const struct glw_backend_texture *t1,
		    const struct glw_rgb *rgb_mul,
		    const struct glw_rgb *rgb_off,
		    float alpha, float blur,
		    const float *vertices,
		    int num_vertices,
		    const uint16_t *indices,
		    int num_triangles,
		    int flags,
		    glw_program_t *p,
		    const struct glw_rctx *rc);

#define GLW_RENDER_COLOR_ATTRIBUTES 0x1 /* set if the color attributes
					   are != [1,1,1,1] */

#define GLW_RENDER_BLUR_ATTRIBUTE   0x2 /* set if pos.w != 1 (sharpness)
					 * ie, the triangle should be blurred
					 */


  float *gr_vtmp_buffer;  // temporary buffer for emitting vertices
  int gr_vtmp_cur;
  int gr_vtmp_capacity;

  int gr_random;

  // On Screen Keyboard

  void (*gr_open_osk)(struct glw_root *gr, 
		      const char *title, const char *str, struct glw *w,
		      int password);


  struct glw *gr_osk_widget;
  prop_sub_t *gr_osk_text_sub;
  prop_sub_t *gr_osk_ev_sub;


} glw_root_t;


/**
 * Render context
 */
typedef struct glw_rctx {
  float rc_alpha;
  float rc_sharpness;

  int16_t rc_width;
  int16_t rc_height;

  struct glw_cursor_painter *rc_cursor_painter;

  uint8_t rc_inhibit_shadows; // Used when rendering low res passes in bloom filter
  uint8_t rc_inhibit_matrix_store; // Avoid storing matrix in mirrored view, etc
  uint8_t rc_layer;
  uint8_t rc_overscanning;

  // Current ModelView Matrix
  Mtx rc_mtx;

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

  TAILQ_ENTRY(glw) glw_render_link;
		   
  struct glw *glw_selected;
  struct glw *glw_focused;

  /** 
   * All the glw_parent stuff is operated by this widgets
   * parents. That is, a widget should never touch these themselfs
   * TODO: Allocate these dynamically based on parent class
   *
   * glw_array current has the most items here now
   */
  union { 
    int i32;
    float f;
    void *ptr;
  } glw_parent_val[7];


  /**
   * Layout contraints
   */
  int16_t glw_req_size_x;
  int16_t glw_req_size_y;
  float glw_req_weight;

  int glw_flags;

#define GLW_ACTIVE               0x1
#define GLW_AUTOREFOCUSABLE      0x2
#define GLW_NAV_FOCUSABLE        0x4     /* Widget is focusable when navigating
					    with keyboard input */

#define GLW_DEBUG                0x8     /* Debug this object */
#define GLW_FOCUS_BLOCKED        0x10
#define GLW_UPDATE_METRICS       0x20

#define GLW_IN_FOCUS_PATH        0x40
#define GLW_IN_PRESSED_PATH      0x80
#define GLW_IN_HOVER_PATH        0x100

#define GLW_DESTROYING           0x200  /* glw_destroy() has been called */


#define GLW_HIDDEN               0x400

#define GLW_RETIRED              0x800
#define GLW_NO_INITIAL_TRANS     0x1000
#define GLW_CAN_SCROLL           0x2000

#define GLW_CONSTRAINT_X         0x10000
#define GLW_CONSTRAINT_Y         0x20000
#define GLW_CONSTRAINT_W         0x40000
#define GLW_CONSTRAINT_D         0x80000

  // We rely on shifts to filter these against each other so they
  // must be consecutive, see glw_filter_constraints()
#define GLW_CONSTRAINT_IGNORE_X  0x100000
#define GLW_CONSTRAINT_IGNORE_Y  0x200000
#define GLW_CONSTRAINT_IGNORE_W  0x400000
#define GLW_CONSTRAINT_IGNORE_D  0x800000

#define GLW_CONSTRAINT_FLAGS (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y | \
                              GLW_CONSTRAINT_W | GLW_CONSTRAINT_D )

#define GLW_CLIPPED              0x1000000 

#define GLW_HOMOGENOUS           0x2000000

#define GLW_FOCUS_ON_CLICK       0x4000000


#define GLW_CONSTRAINT_CONF_W    0x10000000
#define GLW_CONSTRAINT_CONF_X    0x20000000
#define GLW_CONSTRAINT_CONF_Y    0x40000000
#define GLW_CONSTRAINT_CONF_D    0x80000000


  int glw_flags2;
#define GLW2_ENABLED        0x1
#define GLW2_FLOATING_FOCUS 0x2
#define GLW2_ALWAYS_LAYOUT  0x4
#define GLW2_ALWAYS_GRAB_KNOB 0x8
#define GLW2_AUTOHIDE        0x10
#define GLW2_SHADOW          0x20
#define GLW2_AUTOFADE        0x40
#define GLW2_EXPEDITE_SUBSCRIPTIONS     0x80
#define GLW2_AUTOMARGIN                 0x100
#define GLW2_REVERSE_RENDER             0x200

#define GLW2_LEFT_EDGE            0x10000000
#define GLW2_TOP_EDGE             0x20000000
#define GLW2_RIGHT_EDGE           0x40000000
#define GLW2_BOTTOM_EDGE          0x80000000


  float glw_norm_weight;             /* Relative weight (normalized) */
  float glw_alpha;                   /* Alpha set by user */
  float glw_sharpness;               /* 1-Blur set by user */

  float glw_focus_weight;

  uint8_t glw_alignment;

  char *glw_id;

  struct glw_event_map_list glw_event_maps;		  

  struct glw_prop_sub_list glw_prop_subscriptions;

  struct token *glw_dynamic_expressions;

  Mtx *glw_matrix;

  struct glw_clone *glw_clone;

} glw_t;


#define glw_have_x_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_X) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_X))
#define glw_have_y_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_Y) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_Y))
#define glw_have_w_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_W) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_W))
#define glw_have_f_constraint(w) (((w)->glw_flags & GLW_CONSTRAINT_F) \
                       && !((w)->glw_flags & GLW_CONSTRAINT_IGNORE_F))

#define glw_filter_constraints(f) \
 (((f) & GLW_CONSTRAINT_FLAGS) & ~(((f) >> 4) & GLW_CONSTRAINT_FLAGS))


int glw_init(glw_root_t *gr);

void glw_fini(glw_root_t *gr);

void glw_load_universe(glw_root_t *gr);

void glw_unload_universe(glw_root_t *gr);

void glw_flush(glw_root_t *gr);

void *glw_get_opaque(glw_t *w, glw_callback_t *func);

#define GLW_REINITIALIZE_VDPAU 0x1
#define GLW_NO_FRAMERATE_UPDATE 0x2

void glw_prepare_frame(glw_root_t *gr, int flags);

void glw_post_scene(glw_root_t *gr);

void glw_reap(glw_root_t *gr);

void glw_cond_wait(glw_root_t *gr, hts_cond_t *c);

void glw_retire_child(glw_t *w);

void glw_move(glw_t *w, glw_t *b);

void glw_remove_from_parent(glw_t *w, glw_t *p);

void glw_lock(glw_root_t *gr);

void glw_unlock(glw_root_t *gr);


/**
 *
 */
#define glw_is_focusable(w) ((w)->glw_focus_weight > 0)

#define glw_is_focused(w) (!!((w)->glw_flags & GLW_IN_FOCUS_PATH))

#define glw_is_hovered(w) (!!((w)->glw_flags & GLW_IN_HOVER_PATH))

#define glw_is_pressed(w) (!!((w)->glw_flags & GLW_IN_PRESSED_PATH))

void glw_store_matrix(glw_t *w, const glw_rctx_t *rc);

#define GLW_FOCUS_SET_AUTOMATIC    0
#define GLW_FOCUS_SET_AUTOMATIC_FF 1
#define GLW_FOCUS_SET_INTERACTIVE  2
#define GLW_FOCUS_SET_SUGGESTED    3

void glw_focus_set(glw_root_t *gr, glw_t *w, int how);

void glw_focus_open_path(glw_t *w);

void glw_focus_open_path_close_all_other(glw_t *w);

void glw_focus_close_path(glw_t *w);

void glw_focus_crawl(glw_t *w, int forward, int interactive);

int glw_focus_step(glw_t *w, int forward);

void glw_focus_suggest(glw_t *w);

glw_t *glw_focus_by_path(glw_t *w);

void glw_set_focus_weight(glw_t *w, float f);

int glw_is_child_focusable(glw_t *w);


/**
 * Clipping
 */
typedef enum {
  GLW_CLIP_TOP,
  GLW_CLIP_BOTTOM,
  GLW_CLIP_LEFT,
  GLW_CLIP_RIGHT,
} glw_clip_boundary_t;

int glw_clip_enable(glw_root_t *gr, const glw_rctx_t *rc,
		    glw_clip_boundary_t gcb, float distance);

void glw_clip_disable(glw_root_t *gr, int which);

int glw_fader_enable(glw_root_t *gr, const glw_rctx_t *rc, const float *plane,
		     float alphafo, float blurfo);

void glw_fader_disable(glw_root_t *gr, int which);

void glw_stencil_enable(glw_root_t *gr, const glw_rctx_t *rc,
			const struct glw_backend_texture *tex,
			const int16_t *border);

void glw_stencil_disable(glw_root_t *gr);


static inline void
glw_lp(float *v, const glw_root_t *gr, float t, float alpha)
{
  *v = *v + alpha * (t - *v);
}



/**
 * Views
 */
glw_t *glw_view_create(glw_root_t *gr, rstr_t *url, 
		       glw_t *parent, struct prop *prop,
		       struct prop *prop_parent, prop_t *args,
		       struct prop *prop_clone, int cache, int nofail);

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


/**
 *
 */
#define GLW_ATTRIB_CHEW(attrib, ap)		\
do {						\
  switch((unsigned int)attrib) {		\
  case GLW_ATTRIB_END:				\
    break;					\
  case GLW_ATTRIB_num ... UINT32_MAX:           \
    abort();                                    \
  case GLW_ATTRIB_PROPROOTS3:         		\
    (void)va_arg(ap, void *);			\
    (void)va_arg(ap, void *);			\
  case GLW_ATTRIB_ARGS:				\
  case GLW_ATTRIB_PROP_PARENT:			\
  case GLW_ATTRIB_PROP_SELF:			\
  case GLW_ATTRIB_PROP_MODEL:			\
  case GLW_ATTRIB_BIND_TO_ID: 			\
  case GLW_ATTRIB_PAGE_BY_ID:			\
    (void)va_arg(ap, void *);			\
    break;					\
  case GLW_ATTRIB_MODE:                         \
  case GLW_ATTRIB_TRANSITION_EFFECT:            \
  case GLW_ATTRIB_CHILD_TILES_X:                \
  case GLW_ATTRIB_CHILD_TILES_Y:                \
  case GLW_ATTRIB_CHILD_HEIGHT:                 \
  case GLW_ATTRIB_CHILD_WIDTH:                  \
  case GLW_ATTRIB_PAGE:                         \
  case GLW_ATTRIB_ALPHA_EDGES:                  \
  case GLW_ATTRIB_PRIORITY:                     \
  case GLW_ATTRIB_SPACING:                      \
  case GLW_ATTRIB_X_SPACING:                    \
  case GLW_ATTRIB_Y_SPACING:                    \
  case GLW_ATTRIB_SCROLL_THRESHOLD:             \
  case GLW_ATTRIB_RADIUS:			\
    (void)va_arg(ap, int);			\
    break;					\
  case GLW_ATTRIB_ANGLE:			\
  case GLW_ATTRIB_TIME:                         \
  case GLW_ATTRIB_TRANSITION_TIME:              \
  case GLW_ATTRIB_EXPANSION:                    \
  case GLW_ATTRIB_VALUE:                        \
  case GLW_ATTRIB_INT_STEP:                     \
  case GLW_ATTRIB_INT_MIN:                      \
  case GLW_ATTRIB_INT_MAX:                      \
  case GLW_ATTRIB_CHILD_ASPECT:                 \
  case GLW_ATTRIB_FILL:                         \
  case GLW_ATTRIB_SATURATION:                   \
  case GLW_ATTRIB_CENTER:                       \
  case GLW_ATTRIB_ALPHA_FALLOFF:                \
  case GLW_ATTRIB_BLUR_FALLOFF:                 \
    (void)va_arg(ap, double);			\
    break;					\
  }						\
} while(0)

const char *glw_get_a_name(glw_t *w);

const char *glw_get_path(glw_t *w);

void glw_print_tree(glw_t *w);

int glw_widget_unproject(Mtx m, float *xp, float *yp,
			 const Vec3 p, const Vec3 dir);

glw_t *glw_create(glw_root_t *gr, const glw_class_t *class,
		  glw_t *parent, glw_t *before, prop_t *originator);

#define glw_lock_assert() glw_lock_check(__FILE__, __LINE__)

void glw_lock_check(const char *file, const int line);

void glw_set(glw_t *w, ...) __attribute__((__sentinel__(0)));

void glw_destroy(glw_t *w);

void glw_suspend_subscriptions(glw_t *w);

void glw_unref(glw_t *w);

#define glw_ref(w) ((w)->glw_refcnt++)

glw_t *glw_get_prev_n(glw_t *c, int count);

glw_t *glw_get_next_n(glw_t *c, int count);

int glw_event(glw_root_t *gr, struct event *e);

int glw_event_to_widget(glw_t *w, struct event *e, int local);

void glw_inject_event(glw_root_t *gr, event_t *e);

void glw_pointer_event(glw_root_t *gr, glw_pointer_event_t *gpe);

int glw_pointer_event0(glw_root_t *gr, glw_t *w, glw_pointer_event_t *gpe, 
		       glw_t **hp, Vec3 p, Vec3 dir);


int glw_navigate(glw_t *w, struct event *e, int local);

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

void glw_layout0(glw_t *w, glw_rctx_t *rc);

void glw_rctx_init(glw_rctx_t *rc, int width, int height, int overscan);

int glw_check_system_features(glw_root_t *gr);

void glw_scale_to_aspect(glw_rctx_t *rc, float t_aspect);

void glw_reposition(glw_rctx_t *rc, int left, int top, int right, int bottom);

void glw_repositionf(glw_rctx_t *rc, float left, float top,
		     float right, float bottom);

void glw_align_1(glw_rctx_t *rc, int a);

void glw_align_2(glw_rctx_t *rc, int a);

void glw_wirebox(glw_root_t *gr, const glw_rctx_t *rc);

void glw_wirecube(glw_root_t *gr, const glw_rctx_t *rc);

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

void glw_update_size(glw_root_t *gr);

/**
 *
 */

void glw_set_constraints(glw_t *w, int x, int y, float weight,
			 int flags);

void glw_conf_constraints(glw_t *w, int x, int y, float weight,
			  int conf);

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


/**
 *
 */
#define GLW_BLEND_NORMAL   0
#define GLW_BLEND_ADDITIVE 1

void glw_blendmode(struct glw_root *gr, int mode);

#define GLW_CW  0
#define GLW_CCW 1
void glw_frontface(struct glw_root *gr, int how);

struct glw_program *glw_make_program(struct glw_root *gr, 
				     const char *vertex_shader,
				     const char *fragment_shader);

void glw_destroy_program(struct glw_root *gr, struct glw_program *gp);


// text bitmap semi-private stuff

void glw_gtb_set_caption_raw(glw_t *w, uint32_t *uc, int len);

extern const float glw_identitymtx[16];

void glw_icon_flush(glw_root_t *gr);

#endif /* GLW_H */
