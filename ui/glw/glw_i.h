/*
 *  GL Widgets, Common internal includes
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

#ifndef __GLW_I_H__
#define __GLW_I_H__

extern float glw_framerate;

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
  case GLW_ATTRIB_U32:				\
  case GLW_ATTRIB_ALIGNMENT:			\
  case GLW_ATTRIB_FLAGS:			\
  case GLW_ATTRIB_SLICES:                       \
  case GLW_ATTRIB_X_SLICES:                     \
  case GLW_ATTRIB_Y_SLICES:                     \
  case GLW_ATTRIB_HIDDEN:                       \
  case GLW_ATTRIB_MODE:                         \
  case GLW_ATTRIB_MIRROR:                       \
  case GLW_ATTRIB_ORIENTATION:                  \
  case GLW_ATTRIB_INT:                          \
  case GLW_ATTRIB_INT_STEP:                     \
  case GLW_ATTRIB_INT_MIN:                      \
  case GLW_ATTRIB_INT_MAX:                      \
  case GLW_ATTRIB_TRANSITION_EFFECT:            \
    (void)va_arg(ap, int);			\
    break;					\
  case GLW_ATTRIB_TEXTURE_BORDERS:              \
  case GLW_ATTRIB_VERTEX_BORDERS:               \
    (void)va_arg(ap, double);			\
  case GLW_ATTRIB_DISPLACEMENT:                 \
  case GLW_ATTRIB_RGB:                          \
  case GLW_ATTRIB_FOCUS_RGB:                    \
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

glw_t *glw_create0(glw_class_t class, va_list ap);

glw_t *glw_create_i(glw_class_t class, ...) __attribute__((__sentinel__(0)));

#define glw_lock_assert() glw_lock_check(__FILE__, __LINE__)

void glw_lock_check(const char *file, const int line);

int glw_attrib_set0(glw_t *w, int init, va_list ap);

void glw_set_i(glw_t *w, ...);

void glw_set_active0(glw_t *w);

void glw_destroy0(glw_t *w);

int glw_child_prev0(glw_t *w);

int glw_child_next0(glw_t *w);

int glw_drop_signal0(glw_t *w, glw_signal_t signal, void *opaque);

void glw_deref0(glw_t *w);

void glw_hide0(glw_t *w);

void glw_unhide0(glw_t *w);

int glw_get_text0(glw_t *w, char *buf, size_t buflen);

int glw_get_int0(glw_t *w, int *result);

glw_t *glw_get_prev_n(glw_t *c, int count);

glw_t *glw_get_next_n(glw_t *c, int count);

glw_t *glw_get_prev_n_all(glw_t *c, int count);

glw_t *glw_get_next_n_all(glw_t *c, int count);

glw_t *glw_find_by_id0(glw_t *w, const char *id, int deepsearch);

void glw_signal_handler_clean(glw_t *w);

#define GLW_SIGNAL_PRI_INTERNAL 100

void glw_signal_handler_register(glw_t *w, glw_callback_t *func, void *opaque, 
				 int pri);

void glw_signal_handler_unregister(glw_t *w, glw_callback_t *func,
				   void *opaque);

#define glw_signal_handler_int(w, func) \
 glw_signal_handler_register(w, func, NULL, GLW_SIGNAL_PRI_INTERNAL)

int glw_signal0(glw_t *w, glw_signal_t sig, void *extra);

static inline int 
glw_is_select_candidate(glw_t *w)
{
  return w->glw_selected != NULL || w->glw_flags & GLW_SELECTABLE;
}

int glw_navigate(glw_t *w, void *extra);

void glw_event_init(void);

void glw_event_map_add(glw_t *w, glw_event_type_t inevent,
		       const char *target, glw_event_type_t outevent,
		       const char *appmethod);

void glw_event_map_destroy(glw_event_map_t *gem);

int glw_event_map_intercept(glw_t *w, glw_event_t *ge);

extern void (*glw_ffmpeglockmgr)(int lock);
extern int (*glw_imageloader)(glw_image_load_ctrl_t *ctrl);
extern const void *(*glw_rawloader)(const char *filename, size_t *sizeptr);
extern void (*glw_rawunload)(const void *data);

void glw_prop_subscription_destroy_list(struct glw_prop_sub_list *l);


/**
 * Render interface abstraction
 */

void glw_check_system_features(void);

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

#endif /* __GLW_I_H__ */
