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
#include "glw.h"
#include "glw_navigation.h"

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


typedef struct glw_clist_item {
  float pos;
  int16_t height;
  int16_t height2;
  uint8_t inited;
} glw_clist_item_t;

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
  int itemh0 = l->child_height ?: rc->rc_height * 0.1;
  int itemh;
  int lptrail = 1;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_clist_item_t *cd = glw_parent_data(c, glw_clist_item_t);
    const int tpos = ypos - l->current_pos + rc->rc_height * l->center;
    
    if(cd->inited) {
      glw_lp(&cd->pos, w->glw_root, tpos, 0.25);
      lptrail = 1;
    } else {
      cd->pos = tpos;
      cd->inited = 1;
      lptrail = 0;
    }

    int f = glw_filter_constraints(c);
	
    if(f & GLW_CONSTRAINT_Y) {
      itemh = c->glw_req_size_y;
    } else {
      itemh = itemh0;
    }
    cd->height2 = itemh;
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

    glw_clist_item_t *cd = glw_parent_data(c, glw_clist_item_t);

    n = glw_next_widget(c);

    rc0.rc_height = (n ? glw_parent_data(n, glw_clist_item_t)->pos :
                     l->trail) - cd->pos - l->spacing;

    cd->height = rc0.rc_height;
    if(cd->height < 1)
      continue;
    rc0.rc_height = cd->height2;

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

  if(rc->rc_alpha < GLW_ALPHA_EPSILON)
    return;

  glw_store_matrix(w, rc);

  rc0 = *rc;
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_clist_item_t *cd = glw_parent_data(c, glw_clist_item_t);
   if(cd->height < 1)
      continue;
 
    y = cd->pos;
    if(y + cd->height < 0 || y > rc->rc_height) {
      c->glw_flags |= GLW_CLIPPED;
      continue;
    } else {
      c->glw_flags &= ~GLW_CLIPPED;
    }

    if(y < 0)
      t = glw_clip_enable(w->glw_root, rc, GLW_CLIP_TOP, 0, 0, 1);
    else
      t = -1;

    if(y + cd->height > rc->rc_height)
      b = glw_clip_enable(w->glw_root, rc, GLW_CLIP_BOTTOM, 0, 0, 1);
    else
      b = -1;

    rc1 = rc0;

    int adj = (cd->height2 - cd->height) / 2;

    glw_reposition(&rc1, 
		   0,
		   rc->rc_height - cd->pos + adj,
		   rc->rc_width,
		   rc->rc_height - cd->pos - cd->height2 + adj);

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
glw_clist_set_int(glw_t *w, glw_attribute_t attrib, int value,
                  glw_style_t *gs)
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
glw_clist_set_float(glw_t *w, glw_attribute_t attrib, float value,
                    glw_style_t *gs)
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
  .gc_parent_data_size = sizeof(glw_clist_item_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_layout = glw_clist_layout,
  .gc_render = render,
  .gc_ctor = ctor,
  .gc_signal_handler = signal_handler,
  .gc_set_int = glw_clist_set_int,
  .gc_set_float = glw_clist_set_float,
  .gc_bubble_event = glw_navigate_vertical,
};

GLW_REGISTER_CLASS(glw_clist);
