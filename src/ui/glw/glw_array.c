/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "glw.h"


typedef struct glw_array {
  glw_t w;

  float filtered_pos;
  int total_size;
  int current_pos;
  int page_size;

  glw_t *scroll_to_me;

  glw_slider_metrics_t metrics;
  
  int child_width_fixed;
  int child_height_fixed;

  int child_tiles_x;
  int child_tiles_y;

  int child_width_px;
  int child_height_px;

  int xentries;

  int16_t saved_height;
  int16_t saved_width;

  int16_t xspacing;
  int16_t yspacing;

  int16_t margin[4];
  int16_t border[4];

  int16_t scroll_threshold;

  char noclip;

  int num_visible_childs;

  float alpha_falloff;
  float blur_falloff;

} glw_array_t;

#define glw_parent_pos_x glw_parent_val[0].i32
#define glw_parent_pos_y glw_parent_val[1].i32

#define glw_parent_pos_fx glw_parent_val[2].f
#define glw_parent_pos_fy glw_parent_val[3].f

#define glw_parent_inst   glw_parent_val[4].i32
#define glw_parent_height glw_parent_val[5].i32

#define glw_parent_col    glw_parent_val[6].i32

/**
 *
 */
static void
glw_array_update_metrics(glw_array_t *a)
{
  float v;
  int do_update = 0;

  a->w.glw_flags &= ~GLW_UPDATE_METRICS;

  v = GLW_MIN(1.0f, (float)a->page_size / a->total_size);

  if(v != a->metrics.knob_size) {
    do_update = 1;
    a->metrics.knob_size = v;
  }
  
  v = GLW_MAX(0, (float)a->current_pos / (a->total_size - a->page_size));

  if(v != a->metrics.position) {
    do_update = 1;
    a->metrics.position = v;
  }
  
  if(!do_update)
    return;

  if(a->total_size > a->page_size && !(a->w.glw_flags & GLW_CAN_SCROLL)) {
    a->w.glw_flags |= GLW_CAN_SCROLL;
    glw_signal0(&a->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
    
  } else if(a->total_size <= a->page_size &&
	    a->w.glw_flags & GLW_CAN_SCROLL) {
    a->w.glw_flags &= ~GLW_CAN_SCROLL;
    glw_signal0(&a->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
  }

  glw_signal0(&a->w, GLW_SIGNAL_SLIDER_METRICS, &a->metrics);
}


/**
 *
 */
static void
glw_array_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c, *prev = NULL;
  glw_rctx_t rc0 = *rc;
  int column = 0;
  int topedge = 1;
  float xspacing = 0, yspacing = 0;
  int height, width, rows;
  int xpos = 0, ypos = 0;

  glw_reposition(&rc0,
		 a->margin[0] + a->border[0],
		 rc->rc_height - (a->margin[1] + a->border[1]),
		 rc->rc_width - (a->margin[2] + a->border[2]),
		 a->margin[3] + a->border[3]);

  height = rc0.rc_height;
  width = rc0.rc_width;

  if(a->child_tiles_x && a->child_tiles_y) {

    xspacing = a->xspacing;
    yspacing = a->yspacing;

    a->xentries = a->child_tiles_x;
    int yentries = a->child_tiles_y;

    if(yentries == 0) {
      yentries = a->xentries * height / (width ?: 1);
    } else if(a->xentries == 0) {
      a->xentries = yentries * width / (height ?: 1);
    }

    a->child_width_px  = (rc0.rc_width - (a->xentries - 1) * xspacing) /
      (a->xentries ?: 1);

    a->child_height_px = (rc0.rc_height - (yentries - 1) * yspacing) /
      (yentries ?: 1);

    if(a->child_width_fixed && a->child_width_px > a->child_width_fixed) {
      int e = a->child_width_px - a->child_width_fixed;
      xspacing += (e * a->xentries) / (a->xentries - 1);
      a->child_width_px = a->child_width_fixed;
    }

    if(a->child_height_fixed && a->child_height_px > a->child_height_fixed) {
      int e = a->child_height_px - a->child_height_fixed;
      yspacing += (e * yentries) / (yentries - 1);
      a->child_height_px = a->child_height_fixed;
    }
      
    if(a->num_visible_childs < a->child_tiles_x)
      xpos = (a->child_tiles_x - a->num_visible_childs) * 
	(xspacing + a->child_width_px) / 2;

    rows = (a->num_visible_childs - 1) / a->child_tiles_x + 1;

    if(w->glw_alignment == LAYOUT_ALIGN_CENTER && rows < a->child_tiles_y)
      ypos = (a->child_tiles_y - rows) * (yspacing + a->child_height_px) / 2;

  } else if(a->child_tiles_x) {

    xspacing = a->xspacing;
    yspacing = a->yspacing;

    a->xentries = a->child_tiles_x;

    a->child_width_px  = (rc0.rc_width - (a->xentries - 1) * xspacing) /
      (a->xentries ?: 1);

    a->child_height_px = a->child_width_px;

  } else {

    int width  = a->child_width_fixed  ?: 100;
    int height = a->child_height_fixed ?: 100;

    a->xentries = GLW_MAX(1, rc0.rc_width  / width);
    int yentries = GLW_MAX(1, rc0.rc_height / height);

    a->child_width_px  = width;
    a->child_height_px = height;

    int xspill = rc0.rc_width  - (a->xentries * width);
    int yspill = rc0.rc_height - (yentries * height);

    xspacing = xspill / (a->xentries + 1);
    yspacing = yspill / (yentries + 1);
  }

  if(a->saved_height != rc0.rc_height) {
    a->saved_height = rc0.rc_height;
    a->page_size = rc0.rc_height;
    a->w.glw_flags |= GLW_UPDATE_METRICS;

    if(w->glw_focused != NULL)
      a->scroll_to_me = w->glw_focused;
  }


  a->current_pos = GLW_MAX(0, GLW_MIN(a->current_pos,
				      a->total_size - a->page_size));

  glw_lp(&a->filtered_pos, w->glw_root, a->current_pos, 0.25);


  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    if(c->glw_flags & GLW_CONSTRAINT_D) {
      if(column != 0) {
	ypos += rc0.rc_height + yspacing;
	column = 0;
	topedge = 0;
      }

      rc0.rc_width  = width;

      if(c->glw_flags & GLW_CONSTRAINT_Y) {
	rc0.rc_height = c->glw_req_size_y;
      } else {
	rc0.rc_height = a->child_height_px;
      }
      c->glw_parent_col = -1;

    } else {
      rc0.rc_width  = a->child_width_px;
      rc0.rc_height = a->child_height_px;
      c->glw_parent_col = column;
    }

    c->glw_parent_pos_y = ypos;
    c->glw_parent_pos_x = column * (xspacing + a->child_width_px) + xpos;
    c->glw_parent_height = rc0.rc_height;

    if(c->glw_parent_inst) {
      c->glw_parent_pos_fy = c->glw_parent_pos_y;
      c->glw_parent_pos_fx = c->glw_parent_pos_x;
      c->glw_parent_inst = 0;
    } else {
      glw_lp(&c->glw_parent_pos_fy, w->glw_root, c->glw_parent_pos_y, 0.25);
      glw_lp(&c->glw_parent_pos_fx, w->glw_root, c->glw_parent_pos_x, 0.25);
    }

    if(ypos - a->filtered_pos > -height &&
       ypos - a->filtered_pos <  height * 2)
      glw_layout0(c, &rc0);

    if(c == a->scroll_to_me) {
      a->scroll_to_me = NULL;
     
      if(ypos - a->filtered_pos < 0) {
	a->current_pos = ypos;
	a->w.glw_flags |= GLW_UPDATE_METRICS;
      } else if(ypos - a->filtered_pos + rc0.rc_height > height) {
	a->current_pos = ypos + rc0.rc_height - height;
	a->w.glw_flags |= GLW_UPDATE_METRICS;
      }
    }

    if(column == 0) {
      c->glw_flags2 |= GLW2_LEFT_EDGE;
    } else {
      c->glw_flags2 &= ~GLW2_LEFT_EDGE;

      if(prev != NULL) {
	prev->glw_flags2 |= GLW2_RIGHT_EDGE;
      } else {
	prev->glw_flags2 &= ~GLW2_RIGHT_EDGE;
      }
    }

    if(topedge) {
      c->glw_flags2 |= GLW2_TOP_EDGE;
    } else {
      c->glw_flags2 &= ~GLW2_TOP_EDGE;
    }

    c->glw_flags2 &= ~GLW2_BOTTOM_EDGE; // Will be set later

    if(c->glw_flags & GLW_CONSTRAINT_D) {
      ypos += rc0.rc_height + yspacing;
      column = 0;
      topedge = 0;
      
    } else {
      column++;
      if(column == a->xentries) {
	ypos += a->child_height_px + yspacing;
	column = 0;
	topedge = 0;
      }
    }
    prev = c;
  }

  if(column != 0)
    ypos += a->child_height_px;


  glw_t *last = TAILQ_LAST(&w->glw_childs, glw_queue);
  if(last != NULL) {
    last->glw_flags2 |= GLW2_BOTTOM_EDGE | GLW2_RIGHT_EDGE;
    c = last;
    while((c = TAILQ_PREV(c, glw_queue, glw_parent_link)) != NULL) {
      if(c->glw_parent_pos_y == last->glw_parent_pos_y)
	c->glw_flags2 |= GLW2_BOTTOM_EDGE;
      else
	break;
    }
  }

 if(a->total_size != ypos) {
    a->total_size = ypos;
    a->w.glw_flags |= GLW_UPDATE_METRICS;
  }

  if(a->w.glw_flags & GLW_UPDATE_METRICS)
    glw_array_update_metrics(a);
}


const static float top_plane[4] = {0,-1,0,1};
const static float bottom_plane[4] = {0,1,0,1};

/**
 *
 */
static void
glw_array_render_one(glw_array_t *a, glw_t *c, int width, int height,
		     const glw_rctx_t *rc0, const glw_rctx_t *rc2)
{
  glw_rctx_t rc3;
  const float y = c->glw_parent_pos_fy - a->filtered_pos;
  int ct, cb, ft, fb;
  glw_root_t *gr = a->w.glw_root;
  
  int ch = c->glw_parent_height;
  int cw = a->child_width_px;

  if(c->glw_flags & GLW_CONSTRAINT_D)
    cw = width;

  if(y + ch * 2 < 0 || y - ch > height) {
    c->glw_flags |= GLW_CLIPPED;
    return;
  } else {
    c->glw_flags &= ~GLW_CLIPPED;
  }

  ct = cb = ft = fb = -1;
  
  if(a->noclip) {
    if(y < 0) 
      ft = glw_fader_enable(gr, rc0, top_plane,
			    a->alpha_falloff, a->blur_falloff);
    
    if(y + ch > height)
      ft = glw_fader_enable(gr, rc0, bottom_plane,
			    a->alpha_falloff, a->blur_falloff);
    
  } else {
    if(y < 0)
      ct = glw_clip_enable(gr, rc0, GLW_CLIP_TOP, 0);
    
    if(y + ch > height)
      cb = glw_clip_enable(gr, rc0, GLW_CLIP_BOTTOM, 0);
  }


  rc3 = *rc2;
  glw_reposition(&rc3,
		 c->glw_parent_pos_fx,
		 height - c->glw_parent_pos_fy,
		 c->glw_parent_pos_fx + cw,
		 height - c->glw_parent_pos_fy - ch);

  glw_render0(c, &rc3);

  if(ct != -1)
    glw_clip_disable(gr, ct);
  if(cb != -1)
    glw_clip_disable(gr, cb);
  if(ft != -1)
    glw_fader_disable(gr, ft);
  if(fb != -1)
    glw_fader_disable(gr, fb);
}

/**
 *
 */
static void
glw_array_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c;
  glw_rctx_t rc0, rc1, rc2;


  if(rc->rc_alpha < 0.01f)
    return;

  rc0 = *rc;
  glw_reposition(&rc0, a->margin[0], rc->rc_height - a->margin[1],
		 rc->rc_width  - a->margin[2], a->margin[3]);

  glw_store_matrix(w, &rc0);
  rc1 = rc0;

  glw_reposition(&rc1,
		 a->border[0], rc->rc_height - a->border[1],
		 rc->rc_width  - a->border[2], a->border[3]);

  int width = rc1.rc_width;
  int height = rc1.rc_height;

  rc2 = rc1;
  
  glw_Translatef(&rc2, 0, 2.0f * a->filtered_pos / height, 0);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    if(w->glw_focused != c)
      glw_array_render_one(a, c, width, height, &rc0, &rc2);
  }
  
  // Render the focused widget last so it stays on top
  // until we have decent Z ordering
  if(w->glw_focused)
    glw_array_render_one(a, w->glw_focused, width, height, &rc0, &rc2);
}


/**
 *
 */
static void
glw_array_scroll(glw_array_t *a, glw_scroll_t *gs)
{
  a->current_pos = GLW_MAX(gs->value * (a->total_size - a->page_size), 0);
}



/**
 * This is a helper to make sure we can show items in list that are not
 * focusable even if they are at the top
 */
static void
scroll_to_me(glw_array_t *a, glw_t *c)
{
  while(c != NULL && c->glw_parent_col != 0)
    c = TAILQ_PREV(c, glw_queue, glw_parent_link);

  if(c == NULL)
    return;

  glw_t *d = c, *e = c;

  while(1) {
    d = TAILQ_PREV(d, glw_queue, glw_parent_link);
    if(d == NULL) {
      c = e;
      break;
    }

    if(d->glw_flags & GLW_HIDDEN)
      continue;
    if(glw_is_child_focusable(d))
      break;
    e = d;
  }
  a->scroll_to_me = c;
}



/**
 *
 */
static int
glw_array_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    scroll_to_me(a, extra);
    w->glw_flags &= ~GLW_FLOATING_FOCUS;
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    c = extra;
    a->num_visible_childs++;
    c->glw_parent_inst = 1;
    break;


  case GLW_SIGNAL_CHILD_DESTROYED:
    if(a->scroll_to_me == extra)
      a->scroll_to_me = NULL;
  case GLW_SIGNAL_CHILD_HIDDEN:
    a->num_visible_childs--;
    break;

  case GLW_SIGNAL_SCROLL:
    glw_array_scroll(a, extra);
    break;

  case GLW_SIGNAL_CHILD_MOVED:
    scroll_to_me(a, w->glw_focused);
    break;

  }
  return 0;
}


/**
 *
 */
static int
glw_array_pointer_event(glw_t *w, const glw_pointer_event_t *gpe)
{
  glw_array_t *a = (glw_array_t *)w;

  if(gpe->type == GLW_POINTER_SCROLL) {
    a->current_pos += a->page_size * gpe->delta_y;
    a->w.glw_flags |= GLW_UPDATE_METRICS;
    return 1;
  }
  return 0;
}

/**
 *
 */
static void
glw_array_ctor(glw_t *w)
{
  w->glw_flags |= GLW_FLOATING_FOCUS;
}

/**
 *
 */
static int
glw_array_set_int(glw_t *w, glw_attribute_t attrib, int value)
{
  glw_array_t *a = (glw_array_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_CHILD_HEIGHT:
    if(a->child_height_fixed == value)
      return 0;
    a->child_height_fixed = value;
    break;

  case GLW_ATTRIB_CHILD_WIDTH:
    if(a->child_width_fixed == value)
      return 0;
    a->child_width_fixed  = value;
    break;

  case GLW_ATTRIB_CHILD_TILES_X:
    if(a->child_tiles_x == value)
      return 0;
    a->child_tiles_x = value;
    break;

  case GLW_ATTRIB_CHILD_TILES_Y:
    if(a->child_tiles_y == value)
      return 0;

    a->child_tiles_y = value;
    break;

  case GLW_ATTRIB_X_SPACING:
    if(a->xspacing == value)
      return 0;
    a->xspacing = value;
    break;

  case GLW_ATTRIB_Y_SPACING:
    if(a->yspacing == value)
      return 0;

    a->yspacing = value;
    break;

  case GLW_ATTRIB_SCROLL_THRESHOLD:
    if(a->scroll_threshold == value)
      return 0;
    a->scroll_threshold = value;
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
glw_array_set_float(glw_t *w, glw_attribute_t attrib, float value)
{
  glw_array_t *a = (glw_array_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_ALPHA_FALLOFF:
    if(a->alpha_falloff == value)
      return 0;

    a->alpha_falloff = value;
    a->noclip = 1;
    break;

  case GLW_ATTRIB_BLUR_FALLOFF:
    if(a->blur_falloff == value)
      return 0;

    a->noclip = 1;
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
glw_array_get_next_row(glw_t *w, glw_t *c, int reverse)
{
  int current_col = c->glw_parent_col;
  int cnt = 0;
  if(reverse) {
    while((c = glw_get_prev_n(c, 1)) != NULL) {
      cnt++;
      if(c->glw_parent_col == current_col)
	return cnt;
    }
  } else {
    while((c = glw_get_next_n(c, 1)) != NULL) {
      cnt++;
      if(c->glw_parent_col == current_col)
	return cnt;
    }
  }

  return 0;
}


/**
 *
 */
static int
glw_array_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v)
{
  glw_array_t *a = (glw_array_t *)w;
  switch(attrib) {
  case GLW_ATTRIB_MARGIN:
    return glw_attrib_set_int16_4(a->margin, v);
  case GLW_ATTRIB_BORDER:
    return glw_attrib_set_int16_4(a->border, v);
  default:
    return -1;
  }
}


/**
 *
 */
static glw_class_t glw_array = {
  .gc_name = "array",
  .gc_instance_size = sizeof(glw_array_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_ARRAY,
  .gc_render = glw_array_render,
  .gc_ctor = glw_array_ctor,
  .gc_set_int = glw_array_set_int,
  .gc_set_float = glw_array_set_float,
  .gc_signal_handler = glw_array_callback,
  .gc_get_next_row = glw_array_get_next_row,
  .gc_set_int16_4 = glw_array_set_int16_4,
  .gc_layout = glw_array_layout,
  .gc_pointer_event = glw_array_pointer_event,
};

GLW_REGISTER_CLASS(glw_array);
