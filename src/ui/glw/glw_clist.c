/*
 *  GL Widgets, centered list widget
 *  Copyright (C) 2011 Andreas Öman
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

typedef struct glw_clist {
  glw_t w;

  float child_aspect;

  int total_size;
  int current_pos;
  int page_size;

  glw_t *scroll_to_me;

  glw_t *suggested;
  int suggest_cnt;

  glw_slider_metrics_t metrics;

  int16_t saved_height;
  int16_t saved_width;

  float trail;

} glw_clist_t;

#define glw_parent_height glw_parent_val[0].i32
#define glw_parent_pos    glw_parent_val[1].f
#define glw_parent_


/**
 *
 */
static int
layout(glw_clist_t *l, glw_rctx_t *rc)
{
  glw_t *c, *w = &l->w, *n;
  int ypos = 0;
  glw_rctx_t rc0 = *rc;
  float IH = 1.0f / rc->rc_height;
  int itemh = rc->rc_height / 10;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    c->glw_parent_pos = GLW_LP(8, c->glw_parent_pos, 
			       ypos - l->current_pos + rc->rc_height / 2);
    ypos += itemh;
    if(c == w->glw_focused) {
      l->current_pos = ypos;
      ypos += itemh;
    }
  }
  l->trail = GLW_LP(8, l->trail, 
		    ypos - l->current_pos + rc->rc_height / 2);


  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    n = glw_next_widget(c);

    rc0.rc_height = (n ? n->glw_parent_pos : l->trail) - c->glw_parent_pos;
    c->glw_parent_height = rc0.rc_height;
    c->glw_norm_weight = rc0.rc_height * IH;

    glw_layout0(c, &rc0);
  }

  return 0;
}



/**
 *
 */
static void
render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0, rc1;
  int t, b;
  float y;

  if(rc->rc_alpha < 0.01f)
    return;

  glw_store_matrix(w, rc);

  rc0 = *rc;
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    y = c->glw_parent_pos;
    if(y + c->glw_parent_height < 0 || y > rc->rc_height) {
      c->glw_flags |= GLW_CLIPPED;
      continue;
    } else {
      c->glw_flags &= ~GLW_CLIPPED;
    }

    if(y < 0)
      t = glw_clip_enable(w->glw_root, rc, GLW_CLIP_TOP, 0);
    else
      t = -1;

    if(y + c->glw_parent_height > rc->rc_height)
      b = glw_clip_enable(w->glw_root, rc, GLW_CLIP_BOTTOM, 0);
    else
      b = -1;

    rc1 = rc0;
    glw_reposition(&rc1, 
		   0,
		   rc->rc_height - c->glw_parent_pos,
		   rc->rc_width,
		   rc->rc_height - c->glw_parent_pos - c->glw_parent_height);

    glw_render0(c, &rc1);

    if(t != -1)
      glw_clip_disable(w->glw_root, rc, t);
    if(b != -1)
      glw_clip_disable(w->glw_root, rc, b);
  }
}


/**
 *
 */
static int
signal_handler(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
    break;

  case GLW_SIGNAL_POINTER_EVENT:
    break;

  case GLW_SIGNAL_LAYOUT:
    layout((glw_clist_t *)w, extra);
    break;

  case GLW_SIGNAL_EVENT_BUBBLE:
    w->glw_flags2 &= ~GLW2_FLOATING_FOCUS;
    break;

  }
  return 0;
}


/**
 *
 */
static void
ctor(glw_t *w)
{
  w->glw_flags2 |= GLW2_FLOATING_FOCUS;
}


static glw_class_t glw_clist = {
  .gc_name = "clist",
  .gc_instance_size = sizeof(glw_clist_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS | 
  GLW_TRANSFORM_LR_TO_UD,
  .gc_child_orientation = GLW_ORIENTATION_VERTICAL,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING,

  .gc_render = render,
  .gc_ctor = ctor,
  .gc_signal_handler = signal_handler,
  .gc_escape_score = 100,
};

GLW_REGISTER_CLASS(glw_clist);
