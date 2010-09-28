/*
 *  GL Widgets, Slider
 *  Copyright (C) 2009 Andreas Öman
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

#include "glw.h"

/**
 *
 */
typedef struct {
  glw_t w;

  float knob_pos_px;
  float knob_size_fixed;
  float value;

  float min, max, step, step_i;

  int16_t knob_size_px;
  int16_t slider_size_px;
  char fixed_knob_size;

  prop_sub_t *sub;
  prop_t *p;
  float grab_delta;

  glw_t *bound_widget;

} glw_slider_t;


/**
 *
 */
static glw_class_t glw_slider_x, glw_slider_y;

/**
 *
 */
static void
update_value(glw_slider_t *s, float v)
{
  glw_scroll_t gs;

  s->value = GLW_MAX(0, GLW_MIN(1.0, v));
  if(s->p != NULL) {
    prop_set_float_ex(s->p, s->sub, s->value * (s->max - s->min) + s->min);
  }
  if(s->bound_widget != NULL) {
    gs.value = s->value;
    glw_signal0(s->bound_widget, GLW_SIGNAL_SCROLL, &gs);
  }
}


/**
 *
 */
static void
glw_slider_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;
  glw_rctx_t rc0;
  int f;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL || 
     rc->rc_width == 0 || rc->rc_height == 0)
    return;

  f = glw_filter_constraints(c->glw_flags);

  if(s->fixed_knob_size) {
    if(w->glw_class == &glw_slider_x) {
      s->knob_size_px = s->knob_size_fixed * rc->rc_width;
    } else {
      s->knob_size_px = s->knob_size_fixed * rc->rc_height;
    }

  } else if(f & GLW_CONSTRAINT_X && w->glw_class == &glw_slider_x) {
    s->knob_size_px = c->glw_req_size_x;
  } else if(f & GLW_CONSTRAINT_Y && w->glw_class == &glw_slider_y) {
    s->knob_size_px = c->glw_req_size_y;
  } else if(w->glw_class == &glw_slider_x) {
    s->knob_size_px = rc->rc_height;
  } else {
    s->knob_size_px = rc->rc_width;
  }

  int p;

  rc0 = *rc;

  if(w->glw_class == &glw_slider_x) {
    p = s->value * (rc->rc_width - s->knob_size_px) + s->knob_size_px / 2;
    rc0.rc_width  = s->knob_size_px;
    s->slider_size_px = rc->rc_width;
  } else {
    p = (1 - s->value) *
      (rc->rc_height - s->knob_size_px) + s->knob_size_px / 2;
    rc0.rc_height  = s->knob_size_px;
    s->slider_size_px = rc->rc_height;
  }

  s->knob_pos_px = GLW_LP(4, p, s->knob_pos_px);

  glw_layout0(c, &rc0);
}


/**
 *
 */
static void
glw_slider_render_x(glw_t *w, glw_rctx_t *rc)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  glw_reposition(&rc0,
		 s->knob_pos_px - s->knob_size_px / 2,
		 rc->rc_height,
		 s->knob_pos_px + s->knob_size_px / 2,
		 0);

  c->glw_class->gc_render(c, &rc0);
}


/**
 *
 */
static void
glw_slider_render_y(glw_t *w, glw_rctx_t *rc)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  glw_reposition(&rc0,
		 0,
		 s->knob_pos_px + s->knob_size_px / 2,
		 rc->rc_width,
		 s->knob_pos_px - s->knob_size_px / 2);

  c->glw_class->gc_render(c, &rc0);
}


/**
 *
 */
static int
glw_slider_event_y(glw_t *w, event_t *e)
{
  glw_slider_t *s = (glw_slider_t *)w;
  float v = s->value;

  if(event_is_action(e, ACTION_UP)) {

    v = s->value - s->step;

  } else if(event_is_action(e, ACTION_DOWN)) {

    v = s->value + s->step;

  } else {

    return 0;

  }

  update_value(s, v);
  return 1;
}


/**
 *
 */
static int
glw_slider_event_x(glw_t *w, event_t *e)
{
  glw_slider_t *s = (glw_slider_t *)w;
  float v = s->value;

  if(event_is_action(e, ACTION_LEFT)) {

    v = s->value - s->step_i;

  } else if(event_is_action(e, ACTION_RIGHT)) {

    v = s->value + s->step_i;

  } else {

    return 0;

  }

  update_value(s, v);
  return 1;
}



/**
 *
 */
static int
pointer_event(glw_t *w, glw_pointer_event_t *gpe)
{
  glw_root_t *gr = w->glw_root;
  glw_slider_t *s = (glw_slider_t *)w;
  int hitpos = 0;
  float v0 = w->glw_class == &glw_slider_x ? gpe->x : -gpe->y;
  float knob_pos;
  float knob_size = (float)s->knob_size_px / s->slider_size_px;
  
  if(w->glw_class == &glw_slider_x) {
    knob_pos = -1 + 2.0 * (float)s->knob_pos_px  / s->slider_size_px;
  } else {
    knob_pos =  1 - 2.0 * (float)s->knob_pos_px  / s->slider_size_px;
  }

  if(v0 < knob_pos - knob_size)
    hitpos = -1;
  else if(v0 > knob_pos + knob_size)
    hitpos = 1;
  
  switch(gpe->type) {
  case GLW_POINTER_LEFT_PRESS:
    if(hitpos == 0) {
      s->grab_delta = knob_pos - v0;
      gr->gr_pointer_grab = w;
    } else {
      s->value += hitpos * knob_size;
    }
    break;

  case GLW_POINTER_FOCUS_MOTION:
    if(knob_size == 1.0)
      break;
    
    s->value = GLW_RESCALE(v0 + s->grab_delta, 
			   -1.0 + knob_size, 1.0 - knob_size);
    break;

  default:
    return 0;
  }

  update_value(s, s->value);
  return 0;
}


/**
 *
 */
static int
slider_bound_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_slider_t *s = opaque;
  glw_slider_metrics_t *m = extra;

  switch(signal) {

  case GLW_SIGNAL_DESTROY:
    s->bound_widget = NULL;
    break;

  case GLW_SIGNAL_SLIDER_METRICS:
    s->fixed_knob_size = 1;
    s->value = m->position;
    s->knob_size_fixed = m->knob_size;

    if((s->knob_size_fixed != 1.0) == !(s->w.glw_flags & GLW_CAN_SCROLL)) {
      s->w.glw_flags ^= GLW_CAN_SCROLL;
      glw_signal0(&s->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
    }
    break;

  default:
    break;
  }
  return 0;
}



/**
 *
 */
static void
slider_unbind(glw_slider_t *s)
{
  if(s->sub != NULL)
    prop_unsubscribe(s->sub);

  if(s->p != NULL) {
    prop_ref_dec(s->p);
    s->p = NULL;
  }

  if(s->bound_widget != NULL) {
    glw_signal_handler_unregister(s->bound_widget, slider_bound_callback, s);
    s->bound_widget = NULL;
  }
}


/**
 *
 */
static int
glw_slider_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    glw_slider_layout(w, extra);
    break;

  case GLW_SIGNAL_EVENT:
    if(w->glw_class == &glw_slider_x)
      return glw_slider_event_x(w, extra);
    else
      return glw_slider_event_y(w, extra);

  case GLW_SIGNAL_POINTER_EVENT:
    return pointer_event(w, extra);

  case GLW_SIGNAL_DESTROY:
    slider_unbind(s);
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    c = extra;
    
    if(w->glw_class == &glw_slider_y) {
      glw_set_constraints(w, c->glw_req_size_x, 0, 0, GLW_CONSTRAINT_X, 0);
    } else {
      glw_set_constraints(w, 0, c->glw_req_size_y, 0, GLW_CONSTRAINT_Y, 0);
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
prop_callback(void *opaque, prop_event_t event, ...)
{
  glw_slider_t *sl = opaque;
  glw_root_t *gr;
  float v;
  prop_t *p;
  int grabbed;
  if(sl == NULL)
    return;

  gr = sl->w.glw_root;
  va_list ap;
  va_start(ap, event);
  grabbed = gr->gr_pointer_grab == &sl->w;

  switch(event) {
  case PROP_SET_VOID:
    if(grabbed)
      gr->gr_pointer_grab = NULL;

    v = 0;
    p = va_arg(ap, prop_t *);
    break;

  case PROP_SET_FLOAT:
    v = va_arg(ap, double);
    p = va_arg(ap, prop_t *);
    break;

  case PROP_SET_INT:
    v = va_arg(ap, int);
    p = va_arg(ap, prop_t *);
    break;

  default:
    return;
  }

  if(sl->p != NULL)
    prop_ref_dec(sl->p);

  sl->p = p;
  if(p != NULL)
    prop_ref_inc(p);
  
  if(grabbed)
    return;

  if(sl->max - sl->min == 0)
    return;

  v = GLW_RESCALE(v, sl->min, sl->max);
  sl->value = GLW_MAX(0, GLW_MIN(1.0, v));
}

/**
 *
 */
static void 
slider_bind_by_id(glw_slider_t *s, const char *name)
{
  glw_t *t = glw_find_neighbour(&s->w, name);

  if(t == NULL)
    return;

  s->bound_widget = t;
  glw_signal_handler_register(t, slider_bound_callback, s, 1000);
  t->glw_flags |= GLW_UPDATE_METRICS;
}

/**
 *
 */
static void
glw_slider_set(glw_t *w, int init, va_list ap)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_attribute_t attrib;
  prop_t *p, *view, *args;
  const char **pname;
  const char *n;

  if(init) {
    glw_signal_handler_int(w, glw_slider_callback);
    s->min = 0.0;
    s->max = 1.0;
    s->step_i = 0.1;
    s->step = 0.1;
    s->knob_pos_px = 0;
  }

  do {
    attrib = va_arg(ap, int);

    switch(attrib) {
    case GLW_ATTRIB_BIND_TO_ID:
      slider_unbind(s);
      n = va_arg(ap, const char *);

      slider_bind_by_id(s, n);
      break;

    case GLW_ATTRIB_BIND_TO_PROPERTY:
      p = va_arg(ap, prop_t *);
      pname = va_arg(ap, void *);
      view = va_arg(ap, void *);
      args = va_arg(ap, void *);

      slider_unbind(s);

      s->sub = prop_subscribe(PROP_SUB_DIRECT_UPDATE,
			      PROP_TAG_NAME_VECTOR, pname,
			      PROP_TAG_CALLBACK, prop_callback, s,
			      PROP_TAG_COURIER, w->glw_root->gr_courier, 
			      PROP_TAG_NAMED_ROOT, p, "self",
			      PROP_TAG_NAMED_ROOT, view, "view",
			      PROP_TAG_NAMED_ROOT, args, "args",
			      PROP_TAG_ROOT, w->glw_root->gr_uii.uii_prop,
			      NULL);
      break;

    case GLW_ATTRIB_INT_MIN:
      s->min = va_arg(ap, double);
      s->step_i = s->step / (s->max - s->min);
      break;

    case GLW_ATTRIB_INT_MAX:
      s->max = va_arg(ap, double);
      s->step_i = s->step / (s->max - s->min);
      break;

    case GLW_ATTRIB_INT_STEP:
      s->step = va_arg(ap, double);
      s->step_i = s->step / (s->max - s->min);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}




static glw_class_t glw_slider_x = {
  .gc_name = "slider_x",
  .gc_instance_size = sizeof(glw_slider_t),
  .gc_render = glw_slider_render_x,
  .gc_set = glw_slider_set,
  .gc_signal_handler = glw_slider_callback,
};

static glw_class_t glw_slider_y = {
  .gc_name = "slider_y",
  .gc_instance_size = sizeof(glw_slider_t),
  .gc_render = glw_slider_render_y,
  .gc_set = glw_slider_set,
  .gc_signal_handler = glw_slider_callback,
};

GLW_REGISTER_CLASS(glw_slider_x);
GLW_REGISTER_CLASS(glw_slider_y);
