/*
 *  GL Widgets, zstack, Display multiple planes on top of each other
 *  Copyright (C) 2008 Andreas Öman
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
#include "glw_zstack.h"

/*
 *
 */
static int
glw_zstack_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c;
  glw_rctx_t *rc = extra, rc0;
  float z, a;
  
  
  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    z = 0;
    a = 1;
    TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {
      if(c->glw_flags & GLW_HIDE)
	continue;

      c->glw_parent_pos.z = GLW_LP(16, c->glw_parent_pos.z, z);
      z -= 1.0;

      c->glw_parent_alpha = GLW_LP(16, c->glw_parent_alpha, a);
      a = a * 0.5;

      glw_layout0(c, rc);
    }

    w->glw_focused = TAILQ_LAST(&w->glw_childs, glw_queue);
    break;
    
  case GLW_SIGNAL_RENDER:
    rc0 = *rc;

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(c->glw_flags & GLW_HIDE)
	continue;

      rc0.rc_alpha = rc->rc_alpha * c->glw_parent_alpha;
      glw_render_T(c, &rc0, rc);
    }
    break;

  case GLW_SIGNAL_EVENT:
    if(w->glw_focused != NULL)
      return glw_signal0(w->glw_focused, GLW_SIGNAL_EVENT, extra);
    break;
  }
  return 0;
}

void 
glw_zstack_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_zstack_callback);
}

