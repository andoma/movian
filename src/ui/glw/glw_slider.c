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
#include "event.h"
#include "glw.h"

/**
 *
 */
typedef struct {
  glw_t w;

  prop_sub_t *sub;
  prop_t *p;
  prop_t *tentative_output;

  glw_t *bound_widget;

  float knob_pos_px;
  float knob_size_fixed;
  float value;

  float min, max, step, step_i;
  float grab_delta;

  int16_t knob_size_px;
  int16_t slider_size_px;
  char fixed_knob_size;
  char interpolate;
  char keystep;
  char knob_over_edges;

} glw_slider_t;


/**
 *
 */
static glw_class_t glw_slider_x, glw_slider_y;


/**
 *
 */
static void
update_value_delta(glw_slider_t *s, float d)
{
  if(s->p != NULL) {
    s->interpolate = 2;
    prop_add_float(s->p, d * (s->max - s->min));
  } else {
    s->value = GLW_MAX(s->min, GLW_MIN(s->max, s->value + d));
    s->interpolate = 1;
    if(s->bound_widget != NULL) {
      glw_scroll_t gs;
      gs.value = s->value;
      glw_signal0(s->bound_widget, GLW_SIGNAL_SCROLL, &gs);
    }
    glw_need_refresh(s->w.glw_root, 0);
  }
}

/**
 *
 */
static void
update_value(glw_slider_t *s, float v, int tentative)
{
  v = GLW_MAX(0, GLW_MIN(1.0, v));
  const float scaled = v * (s->max - s->min) + s->min;
  s->value = v;
  s->interpolate = 0;

  if(tentative && s->tentative_output != NULL) {
    prop_set_float(s->tentative_output, scaled);
    return;
  }
  prop_set_void(s->tentative_output);

  if(s->p != NULL) {
    prop_set_float(s->p, scaled);
  } else {
    if(s->bound_widget != NULL) {
      glw_scroll_t gs;
      gs.value = s->value;
      glw_signal0(s->bound_widget, GLW_SIGNAL_SCROLL, &gs);
    }
  }
}


/**
 *
 */
static void
glw_slider_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;
  glw_rctx_t rc0;
  int f;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL ||
     rc->rc_width == 0 || rc->rc_height == 0)
    return;

  f = glw_filter_constraints(c);

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

  const int knobsize = s->knob_over_edges ? 0 : s->knob_size_px;
  if(w->glw_class == &glw_slider_x) {
    p = s->value * (rc->rc_width - knobsize) + knobsize / 2;
    rc0.rc_width  = s->knob_size_px;
    s->slider_size_px = rc->rc_width;
  } else {
    p = (1 - s->value) * (rc->rc_height - knobsize) + knobsize / 2;
    rc0.rc_height  = s->knob_size_px;
    s->slider_size_px = rc->rc_height;
  }

  if(s->interpolate)
    glw_lp(&s->knob_pos_px, w->glw_root, p, 0.25);
  else
    s->knob_pos_px = p;

  glw_layout0(c, &rc0);

  if(w->glw_class != &glw_slider_x)
    return;

  c = TAILQ_NEXT(c, glw_parent_link);
  if(c == NULL)
    return;


  rc0 = *rc;
  glw_reposition(&rc0, 0,
                 rc->rc_height,
                 s->knob_pos_px,
                 0);
  glw_layout0(c, &rc0);
}


/**
 *
 */
static void
glw_slider_render_x(glw_t *w, const glw_rctx_t *rc)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  if(glw_is_focusable_or_clickable(w))
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

  glw_render0(c, &rc0);

  c = TAILQ_NEXT(c, glw_parent_link);
  if(c == NULL)
    return;

  rc0 = *rc;
  glw_reposition(&rc0, 0,
                 rc->rc_height,
                 s->knob_pos_px,
                 0);
  glw_render0(c, &rc0);
}


/**
 *
 */
static void
glw_slider_render_y(glw_t *w, const glw_rctx_t *rc)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  if(glw_is_focusable_or_clickable(w))
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

  glw_render0(c, &rc0);
}


/**
 *
 */
static int
glw_slider_event_y(glw_t *w, event_t *e)
{
  glw_slider_t *s = (glw_slider_t *)w;
  float d;

  if(!s->keystep)
    return 0;

  if(event_is_action(e, ACTION_UP)) {
    d = -s->step;
  } else if(event_is_action(e, ACTION_DOWN)) {
    d = s->step;
  } else {
    return 0;
  }

  update_value_delta(s, d);
  return 1;
}


/**
 *
 */
static int
glw_slider_event_x(glw_t *w, event_t *e)
{
  glw_slider_t *s = (glw_slider_t *)w;
  float d;

  if(!s->keystep)
    return 0;

  if(event_is_action(e, ACTION_LEFT)) {
    d = -s->step_i;
  } else if(event_is_action(e, ACTION_RIGHT)) {
    d = s->step_i;
  } else {
    return 0;
  }
  update_value_delta(s, d);
  return 1;
}


/**
 *
 */
static int
pointer_event(glw_t *w, const glw_pointer_event_t *gpe)
{
  glw_root_t *gr = w->glw_root;
  glw_slider_t *s = (glw_slider_t *)w;
  int hitpos = 0;
  float v0 = w->glw_class == &glw_slider_x ? gpe->x : -gpe->y;
  float v = 0;
  float knob_pos;
  float knob_size = (float)s->knob_size_px / s->slider_size_px;
  int tentative = 0;

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
  case GLW_POINTER_TOUCH_START:
    if(w->glw_flags2 & GLW2_ALWAYS_GRAB_KNOB) {
      v = GLW_RESCALE(v0 + s->grab_delta,
		      -1.0 + knob_size, 1.0 - knob_size);
      gr->gr_pointer_grab = w;
    } else if(hitpos == 0) {
      s->grab_delta = knob_pos - v0;
      gr->gr_pointer_grab = w;
      v = s->value;
    } else {
      update_value_delta(s, hitpos * knob_size);
      return 0;
    }
    tentative = 1;
    break;

  case GLW_POINTER_FOCUS_MOTION:
    if(knob_size == 1.0)
      break;
    v = GLW_RESCALE(v0 + s->grab_delta,
		    -1.0 + knob_size, 1.0 - knob_size);
    tentative = 1;
    break;

  case GLW_POINTER_LEFT_RELEASE:
  case GLW_POINTER_TOUCH_END:
    v = s->value;
    break;

  default:
    return 0;
  }
  update_value(s, v, tentative);
  glw_need_refresh(s->w.glw_root, 0);
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
    s->interpolate = 0;
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

  case GLW_SIGNAL_DESTROY:
    slider_unbind(s);
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    c = extra;
    if(c == TAILQ_FIRST(&w->glw_childs)) {
      if(w->glw_class == &glw_slider_y) {
        glw_set_constraints(w, c->glw_req_size_x, 0, 0, GLW_CONSTRAINT_X);
      } else {
        glw_set_constraints(w, 0, c->glw_req_size_y, 0, GLW_CONSTRAINT_Y);
      }
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
    break;

  case PROP_SET_FLOAT:
    if(grabbed)
      return;
    v = va_arg(ap, double);
    break;

  case PROP_SET_INT:
    if(grabbed)
      return;
    v = va_arg(ap, int);
    break;

  case PROP_VALUE_PROP:
    prop_ref_dec(sl->p);
    sl->p = prop_ref_inc(va_arg(ap, prop_t *));
    return;

  default:
    return;
  }

  if(sl->max - sl->min == 0)
    return;

  v = GLW_RESCALE(v, sl->min, sl->max);
  sl->value = GLW_MAX(0, GLW_MIN(1.0, v));
  glw_need_refresh(sl->w.glw_root, 0);
  if(sl->interpolate)
    sl->interpolate--;
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
  glw_signal_handler_register(t, slider_bound_callback, s);
  t->glw_flags |= GLW_UPDATE_METRICS;
}


/**
 *
 */
static void
glw_slider_ctor(glw_t *w)
{
  glw_slider_t *s = (glw_slider_t *)w;
  s->max = 1.0;
  s->step_i = 0.1;
  s->step = 0.1;
  s->keystep = 1;
}


/**
 *
 */
static void 
bind_to_property(glw_t *w, prop_t *p, const char **pname,
		 prop_t *view, prop_t *args, prop_t *clone)
{
  glw_slider_t *s = (glw_slider_t *)w;
  slider_unbind(s);

  s->sub = prop_subscribe(PROP_SUB_DIRECT_UPDATE | PROP_SUB_SEND_VALUE_PROP,
			  PROP_TAG_NAME_VECTOR, pname,
			  PROP_TAG_CALLBACK, prop_callback, s,
			  PROP_TAG_COURIER, w->glw_root->gr_courier, 
			  PROP_TAG_NAMED_ROOT, p, "self",
			  PROP_TAG_NAMED_ROOT, view, "view",
			  PROP_TAG_NAMED_ROOT, args, "args",
			  PROP_TAG_NAMED_ROOT, clone, "clone",
			  PROP_TAG_ROOT, w->glw_root->gr_prop_ui,
			  PROP_TAG_NAMED_ROOT, w->glw_root->gr_prop_nav, "nav",
                          PROP_TAG_NAMED_ROOT, w->glw_root->gr_prop_core, "core",
			  NULL);
}


/**
 *
 */
static int
glw_slider_set_float(glw_t *w, glw_attribute_t attrib, float value,
                     glw_style_t *gs)
{
  glw_slider_t *s = (glw_slider_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_INT_MIN:
    if(s->min == value)
      return 0;

    s->min = value;
    s->step_i = s->step / (s->max - s->min);
    break;

  case GLW_ATTRIB_INT_MAX:
    if(s->max == value)
      return 0;

    s->max = value;
    s->step_i = s->step / (s->max - s->min);
    break;

  case GLW_ATTRIB_INT_STEP:
    if(s->step == value)
      return 0;

    s->step = value;
    s->step_i = s->step / (s->max - s->min);
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_slider_bind_id(glw_t *w, const char *id)
{
  glw_slider_t *s = (glw_slider_t *)w;

  slider_unbind(s);
  slider_bind_by_id(s, id);
  return 1;
}

/**
 *
 */
static int
glw_slider_set_int_unresolved(glw_t *w, const char *a, int value,
                              glw_style_t *gs)
{
  glw_slider_t *s = (glw_slider_t *)w;

  if(!strcmp(a, "keyStep")) {
    s->keystep = value;
    return GLW_SET_NO_CHANGE;
  }
  if(!strcmp(a, "knobOverEdges")) {
    if(s->knob_over_edges == value)
      return GLW_SET_NO_CHANGE;
    s->knob_over_edges = value;
    return GLW_SET_RERENDER_REQUIRED;
  }
  return GLW_SET_NOT_RESPONDING;
}

/**
 *
 */
static int
glw_slider_set_prop(glw_t *w, glw_attribute_t attrib, prop_t *p)
{
  glw_slider_t *gs = (glw_slider_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_TENTATIVE_VALUE:
    prop_ref_dec(gs->tentative_output);
    gs->tentative_output = prop_ref_inc(p);
    return 0;

  default:
    return -1;
  }
  return 1;
}


static glw_class_t glw_slider_x = {
  .gc_name = "slider_x",
  .gc_instance_size = sizeof(glw_slider_t),
  .gc_layout = glw_slider_layout,
  .gc_render = glw_slider_render_x,
  .gc_set_float = glw_slider_set_float,
  .gc_bind_to_id = glw_slider_bind_id,
  .gc_ctor = glw_slider_ctor,
  .gc_signal_handler = glw_slider_callback,
  .gc_bind_to_property = bind_to_property,
  .gc_send_event = glw_slider_event_x,
  .gc_pointer_event = pointer_event,
  .gc_set_int_unresolved = glw_slider_set_int_unresolved,
  .gc_set_prop = glw_slider_set_prop,
};

static glw_class_t glw_slider_y = {
  .gc_name = "slider_y",
  .gc_instance_size = sizeof(glw_slider_t),
  .gc_layout = glw_slider_layout,
  .gc_render = glw_slider_render_y,
  .gc_set_float = glw_slider_set_float,
  .gc_bind_to_id = glw_slider_bind_id,
  .gc_ctor = glw_slider_ctor,
  .gc_signal_handler = glw_slider_callback,
  .gc_bind_to_property = bind_to_property,
  .gc_send_event = glw_slider_event_y,
  .gc_pointer_event = pointer_event,
  .gc_set_int_unresolved = glw_slider_set_int_unresolved,
  .gc_set_prop = glw_slider_set_prop,
};

GLW_REGISTER_CLASS(glw_slider_x);
GLW_REGISTER_CLASS(glw_slider_y);
