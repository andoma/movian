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

  GLW_EXT,          /* External widget, rendering is done via callback */
  GLW_ROTATOR,      /* Rotating device */
  GLW_ARRAY,
  GLW_LIST,
  GLW_CUBESTACK,
  GLW_DECK,
  GLW_ZSTACK,
  GLW_EXPANDER,
  GLW_SLIDESHOW,
  GLW_FORM,
  GLW_MIRROR,
  GLW_ANIMATOR,
  GLW_FX_TEXROT,
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
  GLW_ATTRIB_U32,
  GLW_ATTRIB_ALIGNMENT,
  GLW_ATTRIB_FLAGS,
  GLW_ATTRIB_EXTRA,
  GLW_ATTRIB_SLICES,
  GLW_ATTRIB_X_SLICES,
  GLW_ATTRIB_Y_SLICES,
  GLW_ATTRIB_HIDDEN,
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
  GLW_ATTRIB_FOCUS_RGB,
  GLW_ATTRIB_PROPROOT,
  GLW_ATTRIB_TRANSITION_EFFECT,
  GLW_ATTRIB_XFILL,
} glw_attribute_t;

#define GLW_MIRROR_X   0x1
#define GLW_MIRROR_Y   0x2


#define GLW_MODE_XFADE    0
#define GLW_MODE_SLIDE    1


TAILQ_HEAD(glw_queue, glw);
LIST_HEAD(glw_head, glw);
LIST_HEAD(glw_event_map_list, glw_event_map);
LIST_HEAD(glw_prop_sub_list, glw_prop_sub);

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

  GLW_SIGNAL_CHILD_HIDDEN,
  GLW_SIGNAL_CHILD_VISIBLE,

  GLW_SIGNAL_DETACH_CHILD,

  GLW_SIGNAL_NEW_FRAME,

  GLW_SIGNAL_EVENT,
  GLW_SIGNAL_EVENT_BUBBLE,

  GLW_SIGNAL_CHANGED,


  /**
   * Sent to parent to switch current selected child.
   * Parent should NOT send GLW_SIGNAL_SELECTED_UPDATE to the child
   * in this case.
   */
  GLW_SIGNAL_SELECT,

  /**
   * Emitted by parent to child when it has been selected.
   */
  GLW_SIGNAL_SELECTED_UPDATE,

  /**
   * Emitted by parent to child when it has been selected.
   * Low prio update of selected focus. Emitted by widgets
   * when they have nothing else selected and just chooses one
   * based on some internal logic.
   */
  GLW_SIGNAL_SELECTED_UPDATE_ADVISORY,

} glw_signal_t;

struct glw_rctx;

/**
 * Matrix
 */

typedef struct glw_matrix {
  float m[4][4];
} glw_matrix_t;

/**
 * Focus
 */
typedef struct glw_cursor {
  float gc_m[16];
  float gc_m_prim[16];

  float gc_alpha;
  float gc_alpha_prim;

  float gc_aspect;
  float gc_aspect_prim;

} glw_cursor_t;



/**
 * Render context
 */
typedef struct glw_rctx {
  float rc_alpha;
  float rc_aspect;
  float rc_zoom;
  float rc_fullscreen;
  int rc_focused;

  struct glw_form *rc_form;

} glw_rctx_t;


typedef int (glw_callback_t)(struct glw *w, void *opaque, 
			     glw_signal_t signal, void *value);

typedef struct glw_signal_handler {
  LIST_ENTRY(glw_signal_handler) gsh_link;
  glw_callback_t *gsh_func;
  void *gsh_opaque;
  int gsh_pri;
} glw_signal_handler_t;

LIST_HEAD(glw_signal_handler_list, glw_signal_handler);

typedef struct glw {
  glw_class_t glw_class;
  int glw_refcnt;
  LIST_ENTRY(glw) glw_active_link;
  LIST_ENTRY(glw) glw_every_frame_link;

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
#define GLW_CLEAN         0x1     /* Set if displaylist is up to date */
#define GLW_HIDE          0x2     /* Set if hidden */
#define GLW_KEEP_ASPECT   0x4     /* Keep aspect (for bitmaps) */
#define GLW_ZOOMED        0x8     /* widget is zoomed */

#define GLW_DESTROYED     0x20    /* was destroyed but someone
				     is holding references */
#define GLW_RENDER_LINKED 0x40    /* glw_render_link is linked */
#define GLW_BORDER_BLEND  0x80    /* Blend borders to 0 alpha (for
				     bitmaps */

#define GLW_EVERY_FRAME   0x100   /* Want GLW_SIGNAL_NEW_FRAME at all times */



#define GLW_DRAW_SKEL     0x400   /* Draw extra lines to visualize details */

#define GLW_FOCUS_ADJ_ALPHA 0x4000  /* Adjust alpha based on focus */
#define GLW_FOCUS_DRAW_CURSOR 0x8000  /* Draw cursor when we have focus */

#define GLW_SCALE_CHILDS      0x10000 /* Scaledown unselected childs */

#define GLW_DEBUG             0x20000 /* Debug this object */

#define GLW_BORDER_SCALE_CHILDS 0x40000 /* Scale childs to fit within border */

#define GLW_SELECTABLE          0x80000

#define GLW_EXPAND_CHILDS       0x100000 /* Expand childs (for list) */

#define GLW_PASSWORD            0x200000 /* Don't display real contents */

  glw_vertex_t glw_displacement;

  glw_rgb_t glw_col;                 /* Primary widget color */
  glw_rgb_t glw_focus_color;         /* Additive focus color */
  float glw_weight;                  /* Relative weight */
  float glw_weight_extra;  		  
  float glw_aspect;                  /* Aspect */
  const char *glw_caption;           /* Widget caption */
  float glw_alpha;                   /* Alpha set by user */
  float glw_extra;

  uint32_t glw_u32;

  float glw_time;                    /* Time constant */

  glw_alignment_t glw_alignment;

  const char *glw_id;

  float glw_focus;

  struct glw_event_map_list glw_event_maps;		  

  struct glw_prop_sub_list glw_prop_subscriptions;

  struct token *glw_dynamic_expressions;

} glw_t;

#define glw_get_u32(w) ((w)->glw_u32)

typedef struct glw_vertex_anim {
  int gva_flags;
#define GLW_VERTEX_ANIM_SIN_LERP 0x1

  glw_vertex_t gva_prev;
  glw_vertex_t gva_next;
  float gva_i;

} glw_vertex_anim_t;


typedef struct glw_image_load_ctrl {
  const char *url;
  int got_thumb;
  int want_thumb;
  void *data;
  size_t datasize;
  int codecid;              /* LAVC codec id */
} glw_image_load_ctrl_t;


#define GLW_TEXT_UTF8     0x1
#define GLW_TEXT_EDITABLE 0x2

void glw_flush(void);

void glw_layout(glw_t *w, glw_rctx_t *rc);

void glw_render(glw_t *w, glw_rctx_t *rc);

glw_t *glw_create(glw_class_t class, ...) __attribute__((__sentinel__(0)));

void glw_set(glw_t *w, ...)__attribute__((__sentinel__(0)));

void glw_rescale(float s_aspect, float t_aspect);

void glw_destroy(glw_t *w);

void *glw_get_opaque(glw_t *w, glw_callback_t *func);

void glw_reaper(void);

int glw_init(void (*ffmpeglockmgr)(int lock),
	     int (*imageloader)(glw_image_load_ctrl_t *ctrl),
	     const void *(*rawloader)(const char *filename, size_t *sizeptr),
	     void (*rawunloader)(const void *data),
	     int concurrency);

int glw_selected_visible_callback(glw_t *w, glw_signal_t signal, ...);

int glw_selected_weight_scaler_callback(glw_t *w, glw_signal_t signal, ...);

glw_t *glw_find_by_tag(struct glw_head *hash, uint32_t tag);

struct glw_head *glw_taghash_create(void);

void glw_destroy_childs(glw_t *w);

glw_t *glw_find_by_class(glw_t *p, glw_class_t class);

int glw_signal(glw_t *w, glw_signal_t sig, void *extra);

void glw_hide(glw_t *w);

void glw_unhide(glw_t *w);

extern void (*glw_ffmpeglockmgr)(int lock);

void glw_child_signal(glw_t *w, glw_signal_t sig);

int glw_nav_signal(glw_t *w, glw_signal_t sig);

void glw_set_active(glw_t *w);

void glw_lock(void);

void glw_unlock(void);

void glw_ref(glw_t *w);

void glw_deref(glw_t *w);

void glw_cond_wait(hts_cond_t *c);

glw_t *glw_find_by_id(glw_t *w, const char *id, int deepsearch);

void glw_detach(glw_t *w);

int glw_is_selected(glw_t *w);

int glw_get_text(glw_t *w, char *buf, size_t buflen);

int glw_get_int(glw_t *w, int *result);

void glw_vertex_anim_fwd(glw_vertex_anim_t *gva, float v);

void glw_vertex_anim_read(glw_vertex_anim_t *gva, glw_vertex_t *t);

void glw_vertex_anim_set(glw_vertex_anim_t *gva, glw_vertex_t *t);

void glw_vertex_anim_init(glw_vertex_anim_t *gva, float x, float y, float z,
			  int flags);

void glw_vertex_anim_set3f(glw_vertex_anim_t *gva, float x, float y, float z);

float glw_vertex_anim_read_i(glw_vertex_anim_t *gva);

#define GLW_VERTEX_ANIM_SIN_INITIALIZER(xx, yy, zz)  	\
 { .gva_flags = GLW_VERTEX_ANIM_SIN_LERP,		\
   .gva_prev.x = xx,					\
   .gva_prev.y = yy,					\
   .gva_prev.z = zz,					\
   .gva_next.x = xx,					\
   .gva_next.y = yy,					\
   .gva_next.z = zz}

#define GLW_VERTEX_ANIM_LINEAR_INITIALIZER(xx, yy, zz)  \
 { .gva_prev.x = xx,					\
   .gva_prev.y = yy,					\
   .gva_prev.z = zz,					\
   .gva_next.x = xx,					\
   .gva_next.y = yy,					\
   .gva_next.z = zz}


const char *glw_bitmap_get_filename(glw_t *w);


#define GLW_SYSFEATURE_PBO       0x1
#define GLW_SYSFEATURE_VBO       0x2
#define GLW_SYSFEATURE_FRAG_PROG 0x4
#define GLW_SYSFEATURE_TNPO2     0x8

extern int glw_sysfeatures; /* Contains the GLW_SYSFEATURE_ -flags */

/**
 *
 */
void glw_cursor_render(glw_cursor_t *gc);

void glw_set_framerate(float r);

/**
 * Models
 */
glw_t *glw_model_create(const char *src, glw_t *parent, int flags,
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

#endif /* GLW_H */
