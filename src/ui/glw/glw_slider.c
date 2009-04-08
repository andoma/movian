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
#include "glw_slider.h"

/**
 *
 */
static void
update_value(glw_slider_t *s, float v)
{
  glw_scroll_t gs;

  s->value = GLW_MAX(0, GLW_MIN(1.0, v));
  if(s->p != NULL)
    prop_set_float_ex(s->p, s->sub, s->value * (s->max - s->min) + s->min);

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

  if(!s->fixed_knob_size) {
    if(w->glw_class == GLW_SLIDER_X)
      s->knob_size = rc->rc_size_y / rc->rc_size_x;
    else
      s->knob_size = rc->rc_size_x / rc->rc_size_y;
  }

  s->knob_pos = GLW_LP(4, (-1.0 + s->value * 2) * (1 - s->knob_size),
		       s->knob_pos);
  rc0 = *rc;

  if(w->glw_class == GLW_SLIDER_X)
    rc0.rc_size_x *= s->knob_size;
  else
    rc0.rc_size_y *= s->knob_size;

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_layout0(c, &rc0);
}


/**
 *
 */
static void
glw_slider_render(glw_t *w, glw_rctx_t *rc)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  rc0 = *rc;

  glw_PushMatrix(&rc0, rc);

  if(w->glw_class == GLW_SLIDER_X) {
    glw_Translatef(&rc0, s->knob_pos, 0, 0);
    rc0.rc_size_x *= s->knob_size;
    glw_Scalef(&rc0, s->knob_size, 1.0, 1.0);
  } else {
    glw_Translatef(&rc0, 0, -s->knob_pos, 0);
    rc0.rc_size_y *= s->knob_size;
    glw_Scalef(&rc0, 1.0, s->knob_size, 1.0);
  }

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_render0(c, &rc0);

  glw_PopMatrix();
}


/**
 *
 */
static int
glw_slider_event_y(glw_t *w, event_t *e)
{
  glw_slider_t *s = (glw_slider_t *)w;
  float v = s->value;

  switch(e->e_type) {
  case EVENT_UP:
    v = s->value - s->step;
    break;
  case EVENT_DOWN:
    v = s->value + s->step;
    break;
  default:
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

  switch(e->e_type) {
  case EVENT_LEFT:
    v = s->value - s->step;
    break;
  case EVENT_RIGHT:
    v = s->value + s->step;
    break;
  default:
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
  float v0 = w->glw_class == GLW_SLIDER_X ? gpe->x : -gpe->y;

  if(v0 < s->knob_pos - s->knob_size)
    hitpos = -1;
  else if(v0 > s->knob_pos + s->knob_size)
    hitpos = 1;

  switch(gpe->type) {
  case GLW_POINTER_CLICK:
    if(hitpos == 0) {
      s->grab_delta = s->knob_pos - v0;
      gr->gr_pointer_grab = w;
    } else {
      s->value += hitpos * s->knob_size;
    }
    break;

  case GLW_POINTER_FOCUS_MOTION:
    s->value = GLW_RESCALE(v0 + s->grab_delta, 
			   -1.0 + s->knob_size, 1.0 - s->knob_size);;
    break;

  default:
    return 0;
  }

  update_value(s, s->value);
  return 1;
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
    s->knob_size = m->knob_size;

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

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    glw_slider_layout(w, extra);
    break;

  case GLW_SIGNAL_RENDER:
    glw_slider_render(w, extra);
    break;

  case GLW_SIGNAL_EVENT:
    if(w->glw_class == GLW_SLIDER_X)
      return glw_slider_event_x(w, extra);
    else
      return glw_slider_event_y(w, extra);

  case GLW_SIGNAL_POINTER_EVENT:
    return pointer_event(w, extra);

  case GLW_SIGNAL_DESTROY:
    slider_unbind(s);
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
prop_callback(prop_sub_t *s, prop_event_t event, ...)
{
  glw_slider_t *sl = (glw_slider_t *)s->hps_opaque;
  glw_root_t *gr;
  float v;
  prop_t *p;

  if(sl == NULL)
    return;

  gr = sl->w.glw_root;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
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
  
  if(gr->gr_pointer_grab == &sl->w)
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
}

/**
 *
 */
void
glw_slider_ctor(glw_t *w, int init, va_list ap)
{
  glw_slider_t *s = (glw_slider_t *)w;
  glw_attribute_t attrib;
  prop_t *p;
  const char **pname;
  const char *n;

  if(init) {
    glw_signal_handler_int(w, glw_slider_callback);
    s->min = 0.0;
    s->max = 1.0;
    s->step_i = 0.1;
    s->step = 0.1;
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

      slider_unbind(s);

      s->sub = prop_subscribe(pname, prop_callback, s, 
			      w->glw_root->gr_courier, PROP_SUB_DIRECT_UPDATE,
			      p, w->glw_root->gr_uii.uii_prop);
      break;

    case GLW_ATTRIB_INT_MIN:
      s->min = va_arg(ap, double);
      s->step_i = GLW_RESCALE(s->step, s->min, s->max);
      break;

    case GLW_ATTRIB_INT_MAX:
      s->max = va_arg(ap, double);
      s->step_i = GLW_RESCALE(s->step, s->min, s->max);
      break;

    case GLW_ATTRIB_INT_STEP:
      s->step = va_arg(ap, double);
      s->step_i = GLW_RESCALE(s->step, s->min, s->max);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
