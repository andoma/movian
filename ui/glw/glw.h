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

#include <libhts/htsq.h>
#include <libhts/htsthreads.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>
#include "prop.h"
#include "event.h"
#include "ui/ui.h"

#include <GL/gl.h>

#include <ft2build.h>  
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

extern FT_Library glw_text_library;


#define GLW_LERP(a, y0, y1) ((y0) + (a) * ((y1) - (y0)))
#define GLW_S(a) (sin(GLW_LERP(a, M_PI * -0.5, M_PI * 0.5)) * 0.5 + 0.5)
#define GLW_LP(a, y0, y1) (((y0) * ((a) - 1.0) + (y1)) / (a))
#define GLW_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GLW_MAX(a, b) ((a) > (b) ? (a) : (b))
#define GLW_DEG2RAD(a) ((a) * M_PI * 2.0f / 360.0f)

typedef enum {
  GLW_DUMMY,        /* Emtpy placeholder, wont render anything */
  GLW_MODEL,
  GLW_CONTAINER,    /* Automatic layout */
  GLW_CONTAINER_X,  /* Horizonal container */
  GLW_CONTAINER_Y,  /* Vertical container */
  GLW_CONTAINER_Z,  /* Depth container */
  GLW_BITMAP,
  GLW_LABEL,
  GLW_TEXT,
  GLW_INTEGER,
  GLW_ROTATOR,      /* Rotating device */
  GLW_LIST,
  GLW_DECK,
  GLW_EXPANDER,
  GLW_FORM,
  GLW_MIRROR,
  GLW_ANIMATOR,
  GLW_FX_TEXROT,
  GLW_VIDEO,
} glw_class_t;


typedef enum {
  GLW_ATTRIB_END = 0,
  GLW_ATTRIB_PARENT,
  GLW_ATTRIB_PARENT_HEAD,
  GLW_ATTRIB_SIGNAL_HANDLER,
  GLW_ATTRIB_WEIGHT,
  GLW_ATTRIB_CAPTION,
  GLW_ATTRIB_INT,
  GLW_ATTRIB_SOURCE,
  GLW_ATTRIB_ASPECT,
  GLW_ATTRIB_ALPHA,
  GLW_ATTRIB_ANGLE,
  GLW_ATTRIB_ALIGNMENT,
  GLW_ATTRIB_SET_FLAGS,
  GLW_ATTRIB_CLR_FLAGS,
  GLW_ATTRIB_EXTRA,
  GLW_ATTRIB_SLICES,
  GLW_ATTRIB_X_SLICES,
  GLW_ATTRIB_Y_SLICES,
  GLW_ATTRIB_PREVIEW,
  GLW_ATTRIB_CONTENT,
  GLW_ATTRIB_MODE,
  GLW_ATTRIB_TEXTURE_BORDERS,
  GLW_ATTRIB_VERTEX_BORDERS,
  GLW_ATTRIB_MIRROR,
  GLW_ATTRIB_ID,
  GLW_ATTRIB_DISPLACEMENT,
  GLW_ATTRIB_ORIENTATION,
  GLW_ATTRIB_RGB,
  GLW_ATTRIB_EXPAND,
  GLW_ATTRIB_TIME,
  GLW_ATTRIB_INT_STEP,
  GLW_ATTRIB_INT_MIN,
  GLW_ATTRIB_INT_MAX,
  GLW_ATTRIB_INTPTR,
  GLW_ATTRIB_PROPROOT,
  GLW_ATTRIB_TRANSITION_EFFECT,
  GLW_ATTRIB_XFILL,
  GLW_ATTRIB_FOCUSABLE,
} glw_attribute_t;

#define GLW_MIRROR_X   0x1
#define GLW_MIRROR_Y   0x2


#define GLW_MODE_XFADE    0
#define GLW_MODE_SLIDE    1


TAILQ_HEAD(glw_queue, glw);
LIST_HEAD(glw_head, glw);
LIST_HEAD(glw_event_map_list, glw_event_map);
LIST_HEAD(glw_prop_sub_list, glw_prop_sub);
LIST_HEAD(glw_texture_list, glw_texture);
LIST_HEAD(glw_video_list, glw_video);


typedef struct glw_vertex {
  float x, y, z;
} glw_vertex_t;

typedef struct glw_rgb {
  float r, g, b;
} glw_rgb_t;

typedef enum {
  GLW_ALIGN_DEFAULT,
  GLW_ALIGN_CENTER,
  GLW_ALIGN_LEFT,
  GLW_ALIGN_RIGHT,
  GLW_ALIGN_BOTTOM,
  GLW_ALIGN_TOP,
} glw_alignment_t;

typedef enum {
  GLW_ORIENTATION_UNKNOWN = 0,
  GLW_ORIENTATION_VERTICAL,
  GLW_ORIENTATION_HORIZONTAL,
} glw_orientation_t;

typedef enum {
  GLW_FOCUS_NONE,
  GLW_FOCUS_LEADER,
  GLW_FOCUS_TARGET,
} glw_focus_mode_t;

/**
 * XXX Document these
 */
typedef enum {
  GLW_SIGNAL_NONE,
  GLW_SIGNAL_DESTROY,
  GLW_SIGNAL_DTOR,
  GLW_SIGNAL_INACTIVE,
  GLW_SIGNAL_LAYOUT,
  GLW_SIGNAL_RENDER,

  GLW_SIGNAL_CHILD_CREATED,
  GLW_SIGNAL_CHILD_DESTROYED,

  GLW_SIGNAL_DETACH_CHILD,

  GLW_SIGNAL_NEW_FRAME,

  GLW_SIGNAL_EVENT_BUBBLE,

  GLW_SIGNAL_EVENT,

  GLW_SIGNAL_CHANGED,


  /**
   * Sent to parent to switch current focused child.
   * Parent should NOT send GLW_SIGNAL_FOCUSED_UPDATE to the child
   * in this case.
   */
  GLW_SIGNAL_SELECT,

  /**
   * Emitted by parent to child when it has been selected.
   */
  GLW_SIGNAL_SELECTED_UPDATE,

} glw_signal_t;


/**
 * GLW root context
 */
typedef struct glw_root {
  uii_t gr_uii;

  hts_thread_t gr_thread;
  hts_mutex_t gr_mutex;
  prop_courier_t *gr_courier;


  int gr_sysfeatures;
#define GLW_SYSFEATURE_PBO       0x1
#define GLW_SYSFEATURE_VBO       0x2
#define GLW_SYSFEATURE_FRAG_PROG 0x4
#define GLW_SYSFEATURE_TNPO2     0x8
  
  struct glw_queue gr_destroyer_queue;
  
  float gr_framerate;

  struct glw_head gr_active_list;
  struct glw_head gr_active_flush_list;
  struct glw_head gr_active_dummy_list;
  struct glw_head gr_every_frame_list;


  /**
   * Font renderer
   */
  LIST_HEAD(,  glw_text_bitmap) gr_gtbs;
  TAILQ_HEAD(, glw_text_bitmap) gr_gtb_render_queue;
  hts_cond_t gr_gtb_render_cond;
  FT_Face gr_gtb_face;
  
  /**
   * Image/Texture loader
   */
  struct glw_texture_list gr_tex_active_list;
  struct glw_texture_list gr_tex_flush_list;

  hts_mutex_t gr_tex_mutex;
  TAILQ_HEAD(, glw_texture) gr_tex_rel_queue;

  hts_cond_t gr_tex_load_cond;
  TAILQ_HEAD(, glw_texture) gr_tex_load_queue[2];

  LIST_HEAD(, glw_texture) gr_tex_list;

  /**
   * Cursor and form
   */
  struct glw_texture *gr_cursor_gt;


  /**
   * Root focus leader
   */
  struct glw_queue gr_focus_childs;

  /**
   * Video decoder and renderer
   */
  GLuint gr_yuv2rbg_prog;
  GLuint gr_yuv2rbg_2mix_prog;
  struct glw_video_list gr_video_decoders;


} glw_root_t;



/**
 * Render context
 */
typedef struct glw_rctx {
  float rc_alpha;
  float rc_aspect;
  float rc_zoom;
  float rc_fullscreen;

  struct glw_cursor_painter *rc_cursor_painter;

} glw_rctx_t;


typedef int (glw_callback_t)(struct glw *w, void *opaque, 
			     glw_signal_t signal, void *value);

/**
 * Signal handler
 */
typedef struct glw_signal_handler {
  LIST_ENTRY(glw_signal_handler) gsh_link;
  glw_callback_t *gsh_func;
  void *gsh_opaque;
  int gsh_pri;
} glw_signal_handler_t;

LIST_HEAD(glw_signal_handler_list, glw_signal_handler);


/**
 * GL widget
 */
typedef struct glw {
  glw_class_t glw_class;
  glw_root_t *glw_root;
  int glw_refcnt;
  LIST_ENTRY(glw) glw_active_link;
  LIST_ENTRY(glw) glw_every_frame_link;

  LIST_ENTRY(glw) glw_tmp_link;
  float glw_tmp_value;

  struct glw_signal_handler_list glw_signal_handlers;

  struct glw *glw_parent;
  TAILQ_ENTRY(glw) glw_parent_link;
  struct glw_queue glw_childs;

  struct glw_queue glw_render_list;
  TAILQ_ENTRY(glw) glw_render_link;
		   
  struct glw *glw_selected;

  /** 
   * All the glw_parent stuff is operated by this widgets
   * parents. That is, a widget may never touch these themselfs
   */		  
  float glw_parent_alpha;
  glw_vertex_t glw_parent_pos;
  glw_vertex_t glw_parent_scale;
  float glw_parent_misc[4];

  int glw_flags;  

#define GLW_KEEP_ASPECT         0x2     /* Keep aspect (for bitmaps) */
#define GLW_DESTROYED           0x4     /* was destroyed but someone
					   is holding references */
#define GLW_RENDER_LINKED       0x8     /* glw_render_link is linked */
#define GLW_BORDER_BLEND        0x10    /* Blend borders to 0 alpha
					   (for bitmaps */
#define GLW_EVERY_FRAME         0x20    /* Want GLW_SIGNAL_NEW_FRAME
					   at all times */
#define GLW_DRAW_SKEL           0x40    /* Draw extra lines to
					    visualize details */
#define GLW_FOCUS_DRAW_CURSOR   0x100   /* Draw cursor when we have focus */
#define GLW_SCALE_CHILDS        0x200   /* Scaledown unfocuseded childs */
#define GLW_DEBUG               0x400   /* Debug this object */
#define GLW_BORDER_SCALE_CHILDS 0x800   /* Scale childs to fit within border */
#define GLW_EXPAND_CHILDS       0x2000  /* Expand childs (for list) */
#define GLW_PASSWORD            0x4000  /* Don't display real contents */

  glw_vertex_t glw_displacement;

  glw_rgb_t glw_col;                 /* Primary widget color */
  float glw_weight;                  /* Relative weight */
  float glw_aspect;                  /* Aspect */
  const char *glw_caption;           /* Widget caption */
  float glw_alpha;                   /* Alpha set by user */
  float glw_extra;

  float glw_time;                    /* Time constant */

  glw_alignment_t glw_alignment;

  const char *glw_id;

  struct glw_event_map_list glw_event_maps;		  

  struct glw_prop_sub_list glw_prop_subscriptions;

  struct token *glw_dynamic_expressions;

  float *glw_matrix;


  /* Focus */
  TAILQ_ENTRY(glw) glw_focus_parent_link;
  struct glw *glw_focus_parent;

  glw_focus_mode_t glw_focus_mode;

  struct glw_queue glw_focus_childs; /* Only used if GLW_FOCUS_LEADER is set.
					First is the currently focused one */

} glw_t;


#define GLW_TEXT_EDITABLE 0x2

int glw_init(glw_root_t *gr);

void glw_flush0(glw_root_t *gr);

void glw_rescale(float s_aspect, float t_aspect);

void *glw_get_opaque(glw_t *w, glw_callback_t *func);

void glw_set_active0(glw_t *w);

void glw_reaper0(glw_root_t *gr);

void glw_cond_wait(glw_root_t *gr, hts_cond_t *c);

void glw_detach0(glw_t *w);

const char *glw_bitmap_get_filename(glw_t *w);

void glw_lock(glw_root_t *gr);

void glw_unlock(glw_root_t *gr);


/**
 *
 */
#define glw_is_focusable(w) ((w)->glw_focus_mode == GLW_FOCUS_TARGET)

int glw_is_focused(glw_t *w);

void glw_store_matrix(glw_t *w, glw_rctx_t *rc);

glw_t *glw_get_indirectly_focused_child(glw_t *w);

void glw_focus_set(glw_t *w);


/**
 * Models
 */
glw_t *glw_model_create(glw_root_t *gr, const char *src, 
			glw_t *parent, int flags,
			struct prop *prop);

#define GLW_MODEL_CACHE 0x1

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


/**
 * temp
 */
uii_t *glw_start(const char *arg);




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
  case GLW_ATTRIB_SIGNAL_HANDLER:               \
    (void)va_arg(ap, void *);			\
    (void)va_arg(ap, void *);			\
    (void)va_arg(ap, int);			\
    break;                                      \
  case GLW_ATTRIB_PARENT:			\
  case GLW_ATTRIB_PARENT_HEAD:			\
  case GLW_ATTRIB_SOURCE:			\
  case GLW_ATTRIB_CAPTION:			\
  case GLW_ATTRIB_PREVIEW:			\
  case GLW_ATTRIB_CONTENT:			\
  case GLW_ATTRIB_ID:         			\
  case GLW_ATTRIB_INTPTR:         		\
  case GLW_ATTRIB_PROPROOT:         		\
    (void)va_arg(ap, void *);			\
    break;					\
  case GLW_ATTRIB_ALIGNMENT:			\
  case GLW_ATTRIB_SET_FLAGS:			\
  case GLW_ATTRIB_CLR_FLAGS:			\
  case GLW_ATTRIB_SLICES:                       \
  case GLW_ATTRIB_X_SLICES:                     \
  case GLW_ATTRIB_Y_SLICES:                     \
  case GLW_ATTRIB_MODE:                         \
  case GLW_ATTRIB_MIRROR:                       \
  case GLW_ATTRIB_ORIENTATION:                  \
  case GLW_ATTRIB_INT:                          \
  case GLW_ATTRIB_INT_STEP:                     \
  case GLW_ATTRIB_INT_MIN:                      \
  case GLW_ATTRIB_INT_MAX:                      \
  case GLW_ATTRIB_TRANSITION_EFFECT:            \
  case GLW_ATTRIB_FOCUSABLE:                    \
    (void)va_arg(ap, int);			\
    break;					\
  case GLW_ATTRIB_TEXTURE_BORDERS:              \
  case GLW_ATTRIB_VERTEX_BORDERS:               \
    (void)va_arg(ap, double);			\
  case GLW_ATTRIB_DISPLACEMENT:                 \
  case GLW_ATTRIB_RGB:                          \
    (void)va_arg(ap, double);			\
    (void)va_arg(ap, double);			\
  case GLW_ATTRIB_WEIGHT:			\
  case GLW_ATTRIB_ASPECT:			\
  case GLW_ATTRIB_ALPHA:			\
  case GLW_ATTRIB_ANGLE:			\
  case GLW_ATTRIB_EXTRA:			\
  case GLW_ATTRIB_EXPAND:                       \
  case GLW_ATTRIB_TIME:                         \
  case GLW_ATTRIB_XFILL:                        \
    (void)va_arg(ap, double);			\
    break;					\
  }						\
} while(0)


void glw_render0(glw_t *w, glw_rctx_t *rc);

void glw_layout0(glw_t *w, glw_rctx_t *rc);

glw_t *glw_create0(glw_root_t *gr, glw_class_t class, va_list ap);

glw_t *glw_create_i(glw_root_t *gr, 
		    glw_class_t class, ...) __attribute__((__sentinel__(0)));

#define glw_lock_assert() glw_lock_check(__FILE__, __LINE__)

void glw_lock_check(const char *file, const int line);

int glw_attrib_set0(glw_t *w, int init, va_list ap);

void glw_set_i(glw_t *w, ...);

void glw_destroy0(glw_t *w);

void glw_deref0(glw_t *w);

int glw_get_text0(glw_t *w, char *buf, size_t buflen);

int glw_get_int0(glw_t *w, int *result);

glw_t *glw_get_prev_n(glw_t *c, int count);

glw_t *glw_get_next_n(glw_t *c, int count);

glw_t *glw_get_prev_n_all(glw_t *c, int count);

glw_t *glw_get_next_n_all(glw_t *c, int count);

int glw_event(glw_root_t *gr, event_t *e);

#define GLW_SIGNAL_PRI_INTERNAL 100

void glw_signal_handler_register(glw_t *w, glw_callback_t *func, void *opaque, 
				 int pri);

void glw_signal_handler_unregister(glw_t *w, glw_callback_t *func,
				   void *opaque);

#define glw_signal_handler_int(w, func) \
 glw_signal_handler_register(w, func, NULL, GLW_SIGNAL_PRI_INTERNAL)

int glw_signal0(glw_t *w, glw_signal_t sig, void *extra);

#define glw_render0(w, rc) glw_signal0(w, GLW_SIGNAL_RENDER, rc)
#define glw_layout0(w, rc) glw_signal0(w, GLW_SIGNAL_LAYOUT, rc)


/**
 * Render interface abstraction
 */

void glw_check_system_features(glw_root_t *gr);

void glw_render_T(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc);

void glw_render_TS(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc);

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





#endif /* GLW_H */
