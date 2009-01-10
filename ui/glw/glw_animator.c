/*
 *  GL Widgets, Animator
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
#include "glw_animator.h"
#include "glw_transitions.h"

#define glw_parent_anim_cur glw_parent_misc[0]
#define glw_parent_anim_tgt glw_parent_misc[1]

/**
 *
 */
static int
glw_animator_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c, *n;
  glw_animator_t *a = (void *)w;
  glw_rctx_t *rc = extra, rc0;
  float alpha;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, w->glw_exp_req);

    a->delta = 1 / (w->glw_time * (1000000 / w->glw_root->gr_frameduration));

    for(c = TAILQ_FIRST(&w->glw_childs); c != NULL; c = n) {
      n = TAILQ_NEXT(c, glw_parent_link);

      c->glw_parent_anim_cur = 
	GLW_MIN(c->glw_parent_anim_cur + a->delta, c->glw_parent_anim_tgt);
      
      if(c->glw_parent_anim_cur == 1)
	glw_destroy0(c);
      else
	glw_layout0(c, rc);
    }
    return 0;

  case GLW_SIGNAL_RENDER:
    alpha = rc->rc_alpha * w->glw_alpha;
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

      rc0 = *rc;
      if(c->glw_parent_anim_cur == 0) {
	rc0.rc_alpha = alpha;
	glw_render0(c, &rc0);
	continue;
      }

      glw_PushMatrix(&rc0, rc);
      glw_transition_render(a->efx_conf, c->glw_parent_anim_cur, alpha, &rc0);
      glw_render0(c, &rc0);
      glw_PopMatrix();
    }
    return 0;
  
  case GLW_SIGNAL_DETACH_CHILD:
    c = extra;
    c->glw_parent_anim_tgt = 1;
    return 1;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;
    c->glw_parent_anim_cur = -1;
    c->glw_parent_anim_tgt = 0;
    
    TAILQ_FOREACH(n, &w->glw_childs, glw_parent_link) {
      if(c == n)
	continue;
      n->glw_parent_anim_tgt = 1;
    }
    break;
  }
  return 0;
}

/**
 *
 */
void 
glw_animator_ctor(glw_t *w, int init, va_list ap)
{
  glw_animator_t *a = (void *)w;

  glw_attribute_t attrib;
  const char *filename = NULL;
  glw_t *c;

  if(init) {
    glw_signal_handler_int(w, glw_animator_callback);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_TRANSITION_EFFECT:
      a->efx_conf = va_arg(ap, int);
      break;

    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;
    case GLW_ATTRIB_PROPROOT:
      a->prop = va_arg(ap, void *);
      /* REFcount ?? */
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(filename != NULL) {

    if(*filename) 
	glw_model_create(w->glw_root, filename, w, 0, a->prop);
    else {
      /* Fade out all */
      TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
	c->glw_parent_anim_tgt = 1;
    }
  }
}

