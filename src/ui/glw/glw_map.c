/*
 *  GL Widgets, GLW_MAP -widgets
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
#include <assert.h>
#include "glw_map.h"

/**
 *
 */
static int
glw_map_constraints(glw_map_t *map, glw_t *skip)
{
  glw_t *c;

  memset(map->childs, 0, GLW_POS_num * sizeof(glw_t *));
  TAILQ_FOREACH(c, &map->w.glw_childs, glw_parent_link) {
    assert(c->glw_req_position >= 0 && c->glw_req_position < GLW_POS_num);

    if(map->childs[c->glw_req_position])
      continue;

    map->childs[c->glw_req_position] = c;
  }
  return 1;
}


/**
 *
 */
static void
glw_map_layout1(glw_t *p, glw_rctx_t *rc, glw_t *c,
		float x1, float x2, float y1, float y2)
{
  glw_rctx_t rc0;

  if(c == NULL)
    return;

  rc0 = *rc;

  rc0.rc_size_x = x2 - x1;
  rc0.rc_size_y = y2 - y1;

  x1 = x1 / rc->rc_size_x * 2 - 1;
  x2 = x2 / rc->rc_size_x * 2 - 1;
  y1 = y1 / rc->rc_size_y * 2 - 1;
  y2 = y2 / rc->rc_size_y * 2 - 1;

  c->glw_parent_scale.x = (x2 - x1) * 0.5;
  c->glw_parent_scale.y = (y2 - y1) * 0.5;
  c->glw_parent_scale.z = 1.0f;

  c->glw_parent_pos.x = (x2 + x1) * 0.5;
  c->glw_parent_pos.y = (y2 + y1) * 0.5;
  c->glw_parent_pos.z = 0.0;

  glw_layout0(c, &rc0);
  glw_link_render_list(p, c);
}


/**
 *
 */
static int
glw_map_layout(glw_map_t *map, glw_rctx_t *rc)
{
  int px_north;
  int px_south;
  int px_west;
  int px_east;

  glw_flush_render_list(&map->w);

  if(map->w.glw_alpha < 0.01)
    return 0;

  px_north = rc->rc_size_y - (map->childs[GLW_POS_NORTH] != NULL ?
			      map->childs[GLW_POS_NORTH]->glw_req_size_y : 0);

  px_south = map->childs[GLW_POS_SOUTH] != NULL ?
    map->childs[GLW_POS_SOUTH]->glw_req_size_y : 0;

  px_west = map->childs[GLW_POS_WEST] != NULL ?
    map->childs[GLW_POS_WEST]->glw_req_size_x : 0;

  px_east = rc->rc_size_x - (map->childs[GLW_POS_EAST] != NULL ? 
			     map->childs[GLW_POS_EAST]->glw_req_size_x : 0);

  glw_map_layout1(&map->w, rc, map->childs[GLW_POS_NONE],
		  px_west, px_east, px_south, px_north);

  glw_map_layout1(&map->w, rc, map->childs[GLW_POS_NORTH],
		  0, rc->rc_size_x, px_north, rc->rc_size_y);

  glw_map_layout1(&map->w, rc, map->childs[GLW_POS_SOUTH],
		  0, rc->rc_size_x, 0, px_south);

  glw_map_layout1(&map->w, rc, map->childs[GLW_POS_WEST],
		  0, px_west, px_south, px_north);

  glw_map_layout1(&map->w, rc, map->childs[GLW_POS_EAST],
		  px_east, rc->rc_size_x, px_south, px_north);
  return 0;
}


/**
 *
 */
static void
glw_map_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  glw_rctx_t rc0 = *rc;

  if(alpha < 0.01)
    return;

  rc0.rc_alpha = alpha;
  
  TAILQ_FOREACH(c, &w->glw_render_list, glw_render_link)
    glw_render_TS(c, &rc0, rc);
}


/**
 *
 */
static int
glw_map_callback(glw_t *w, void *opaque, glw_signal_t signal,
		       void *extra)
{
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_map_layout((glw_map_t *)w, extra);
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    return glw_map_constraints((glw_map_t *)w, NULL);
  case GLW_SIGNAL_CHILD_DESTROYED:
    return glw_map_constraints((glw_map_t *)w, extra);

  case GLW_SIGNAL_RENDER:
    glw_map_render(w, extra);
    break;

  case GLW_SIGNAL_EVENT:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_signal0(c, GLW_SIGNAL_EVENT, extra))
	return 1;
    break;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
void 
glw_map_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_map_callback);
}
