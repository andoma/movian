/*
 *  Layout engine
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

#include <string.h>
#include <math.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"
#include "menu.h"

glw_t *overlay_container;

static int overlay_callback(glw_t *w, void *opaque, glw_signal_t signal, ...);

/**
 * Create overlay handling widget
 */
void
layout_overlay_create(void)
{
  overlay_container =
    glw_create(GLW_EXT,
	       GLW_ATTRIB_SIGNAL_HANDLER, overlay_callback, NULL, 0,
	       NULL);
}

/**
 *
 */
static int
overlay_child_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}

/**
 *
 */
typedef struct overlay_animator {
  glw_vertex_anim_t oa_anim;


} overlay_animator_t;


/**
 *
 */
static int
overlay_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  glw_t *c;
  glw_rctx_t *rc, rc0;
  float a, b = 1.0 - 0.9 * layout_switcher_alpha;
  overlay_animator_t *oa;
  glw_vertex_t xyz;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_RENDER:
    rc = va_arg(ap, void *);
 
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

      oa = glw_get_opaque(c, overlay_child_callback);
      glw_vertex_anim_read(&oa->oa_anim, &xyz);

      if(xyz.z < 0.01)
	continue;

      glPushMatrix();
      glTranslatef(xyz.x, xyz.y, 0.88 + xyz.z);
      rc0 = *rc;
      rc0.rc_alpha *= xyz.z * b;
      glw_render(c, &rc0);
      glPopMatrix();
    }
    break;

  case GLW_SIGNAL_LAYOUT:
    rc = va_arg(ap, void *);
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      oa = glw_get_opaque(c, overlay_child_callback);
      a = c->glw_flags & GLW_HIDE ? 0 : 1;
      glw_vertex_anim_set3f(&oa->oa_anim, 0, 0, a);
      glw_vertex_anim_fwd(&oa->oa_anim, 0.02);
      glw_layout(c, rc);
    }
    break;

  case GLW_SIGNAL_CHILD_CREATED:
    c = va_arg(ap, void *);
    oa = calloc(1, sizeof(overlay_animator_t));

    glw_vertex_anim_init(&oa->oa_anim, 0, 0, 0.0,
			 GLW_VERTEX_ANIM_SIN_LERP);
    glw_set(c, 
	    GLW_ATTRIB_SIGNAL_HANDLER, overlay_child_callback, oa, 0,
	    NULL);
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
    c = va_arg(ap, void *);

    oa = calloc(1, sizeof(overlay_animator_t));
    oa = glw_get_opaque(c, overlay_child_callback);
    free(oa);
    break;

  default:
    break;
  }

  va_end(ap);
  return 0;
}

/**
 *
 */
void
layout_overlay_render(float aspect)
{
  glw_rctx_t rc0;
  if(TAILQ_FIRST(&overlay_container->glw_childs) == NULL)
    return;

  memset(&rc0, 0, sizeof(rc0));
  rc0.rc_selected = 1;
  rc0.rc_zoom  = 1.0f;
  rc0.rc_alpha = 1.0;
  rc0.rc_aspect = aspect;

  glw_layout(overlay_container, &rc0);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(0.0f, 1.0f, 4.3f,
	    0.0f, 1.0f, 1.0f,
	    0.0f, 1.0f, 0.0f);

  glPushMatrix();
  glTranslatef(0, 1, 0);
  glw_render(overlay_container, &rc0);
  glPopMatrix();

}
