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

  int spacing;

  float center;

  int child_height;

} glw_clist_t;


#define glw_parent_height glw_parent_val[0].i32
#define glw_parent_pos    glw_parent_val[1].f
#define glw_parent_height2 glw_parent_val[2].i32
#define glw_parent_inited  glw_parent_val[3].i32

/**
 *
 */
static void
glw_clist_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_clist_t *l = (glw_clist_t *)w;
  glw_t *c, *n;
  int ypos = 0;
  glw_rctx_t rc0 = *rc;
  float IH = 1.0f / rc->rc_height;
  int itemh0 = l->child_height ?: rc->rc_height * 0.1;
  int itemh;
  int lptrail = 1;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    
    const int tpos = ypos - l->current_pos + rc->rc_height * l->center;
    
    if(c->glw_parent_inited) {
      glw_lp(&c->glw_parent_pos, w->glw_root, tpos, 0.25);
      lptrail = 1;
    } else {
      c->glw_parent_pos = tpos;
      c->glw_parent_inited = 1;
      lptrail = 0;
    }

    int f = glw_filter_constraints(c);
	
    if(f & GLW_CONSTRAINT_Y) {
      itemh = c->glw_req_size_y;
    } else {
      itemh = itemh0;
    }
    c->glw_parent_height2 = itemh;
    ypos += itemh;
    if(c == w->glw_focused) {
      l->current_pos = ypos - itemh / 2;
    }
    ypos += l->spacing;
  }

  if(lptrail)
    glw_lp(&l->trail, w->glw_root, 
	   ypos - l->current_pos + rc->rc_height * l->center,
	   0.25);
  else
    l->trail = ypos - l->current_pos + rc->rc_height * l->center;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    n = glw_next_widget(c);

    rc0.rc_height = (n ? n->glw_parent_pos : l->trail) - c->glw_parent_pos - l->spacing;

    c->glw_parent_height = rc0.rc_height;
    if(c->glw_parent_height < 1)
      continue;
    c->glw_norm_weight = rc0.rc_height * IH;
    rc0.rc_height = c->glw_parent_height2;

    glw_layout0(c, &rc0);
  }
}



/**
 *
 */
static void
render(glw_t *w, const glw_rctx_t *rc)
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
   if(c->glw_parent_height < 1)
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

    int adj = (c->glw_parent_height2 - c->glw_parent_height) / 2;

    glw_reposition(&rc1, 
		   0,
		   rc->rc_height - c->glw_parent_pos + adj,
		   rc->rc_width,
		   rc->rc_height - c->glw_parent_pos - c->glw_parent_height2 + adj);

    glw_render0(c, &rc1);

    if(t != -1)
      glw_clip_disable(w->glw_root, t);
    if(b != -1)
      glw_clip_disable(w->glw_root, b);
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
    w->glw_flags &= ~GLW_FLOATING_FOCUS;
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
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
  glw_clist_t *l = (glw_clist_t *)w;
  w->glw_flags |= GLW_FLOATING_FOCUS;
  l->center = 0.5;
}


/**
 *
 */
static int
glw_clist_set_int(glw_t *w, glw_attribute_t attrib, int value)
{
  glw_clist_t *l = (glw_clist_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_SPACING:
    if(l->spacing == value)
      return 0;

    l->spacing = value;
    break;

  case GLW_ATTRIB_CHILD_HEIGHT:
    if(l->child_height == value)
      return 0;

    l->child_height = value;
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
glw_clist_set_float(glw_t *w, glw_attribute_t attrib, float value)
{
  glw_clist_t *l = (glw_clist_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_CENTER:
    if(l->center == value)
      return 0;

    l->center = value;
    break;

  default:
    return -1;
  }
  return 1;
}


static glw_class_t glw_clist = {
  .gc_name = "clist",
  .gc_instance_size = sizeof(glw_clist_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS | 
  GLW_TRANSFORM_LR_TO_UD,
  .gc_child_orientation = GLW_ORIENTATION_VERTICAL,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING,
  .gc_layout = glw_clist_layout,
  .gc_render = render,
  .gc_ctor = ctor,
  .gc_signal_handler = signal_handler,
  .gc_escape_score = 100,
  .gc_set_int = glw_clist_set_int,
  .gc_set_float = glw_clist_set_float,
};

GLW_REGISTER_CLASS(glw_clist);
