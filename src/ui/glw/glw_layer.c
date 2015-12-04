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
#include "glw_transitions.h"


typedef struct glw_layer_item {
  float alpha;
  float z;
  int layer;
} glw_layer_item_t;


/**
 *
 */
static void
glw_layer_select_child(glw_t *w)
{
  glw_t *c;

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link)
    if(!(c->glw_flags & (GLW_HIDDEN | GLW_RETIRED)))
      break;

  w->glw_selected = c;

  if(c != NULL)
    glw_focus_open_path_close_all_other(c);
}


/**
 *
 */
static void
glw_layer_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c, *p;
  glw_rctx_t rc0;
  float z, a;
  int layer = 0;

  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;
  rc0 = *rc;

  for(c = TAILQ_LAST(&w->glw_childs, glw_queue); c != NULL; c = p) {
    p = TAILQ_PREV(c, glw_queue, glw_parent_link);

    z = 1.0;
    a = 1.0;

    glw_layer_item_t *cd = glw_parent_data(c, glw_layer_item_t);

    cd->layer = layer;

    if(c->glw_flags & GLW_RETIRED) {
      a = 0;

      if(cd->z > 0.99) {
        glw_destroy(c);
        continue;
      }

    } else if(!(c->glw_flags & GLW_HIDDEN)) {
      layer++;

      z = 0.0;
      a = 1;
    } else {
      a = 0;
    }


    glw_lp(&cd->z,     w->glw_root, z, 0.25);
    glw_lp(&cd->alpha, w->glw_root, a, 0.8);

    rc0.rc_layer = cd->layer + rc->rc_layer;

    if(cd->alpha > GLW_ALPHA_EPSILON)
      glw_layout0(c, &rc0);
  }
}


/**
 *
 */
static int
glw_layer_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c = extra;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_CHILD_CREATED:
    glw_parent_data(c, glw_layer_item_t)->z = 1.0f;

    glw_layer_select_child(w);
    break;

  case GLW_SIGNAL_CHILD_HIDDEN:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    glw_layer_select_child(w);
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_layer_retire_child(glw_t *w, glw_t *c)
{
  c->glw_flags |= GLW_RETIRED;
  glw_layer_select_child(w);
}


/**
 *
 */
static void
glw_layer_render(glw_t *w, const glw_rctx_t *rc)
{
  int zmax = 0;
  glw_t *c;
  glw_rctx_t rc0 = *rc;

  rc0.rc_zmax = &zmax;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    glw_layer_item_t *cd = glw_parent_data(c, glw_layer_item_t);

    rc0.rc_alpha = rc->rc_alpha * cd->alpha * w->glw_alpha;
    if(rc0.rc_alpha < GLW_ALPHA_EPSILON)
      continue;
    rc0.rc_layer = cd->layer + rc->rc_layer;
    //    glw_Translatef(&rc0, 0, 0, cd->z);

    rc0.rc_zindex = MAX(zmax, rc->rc_zindex);
    glw_render0(c, &rc0);
    glw_zinc(&rc0);
  }
  *rc->rc_zmax = MAX(*rc->rc_zmax, zmax);
}


/**
 *
 */
static glw_class_t glw_layer = {
  .gc_name = "layer",
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_instance_size = sizeof(glw_t),
  .gc_parent_data_size = sizeof(glw_layer_item_t),
  .gc_layout = glw_layer_layout,
  .gc_render = glw_layer_render,
  .gc_retire_child = glw_layer_retire_child,
  .gc_signal_handler = glw_layer_callback,
};

GLW_REGISTER_CLASS(glw_layer);
