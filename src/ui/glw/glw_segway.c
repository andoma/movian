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

#include "misc/strtab.h"





/**
 *
 */
typedef struct glw_segway {
  glw_t w;
  enum {
    SEGWAY_DIRECTION_LEFT = 1,
    SEGWAY_DIRECTION_RIGHT,
  } direction;

  int req_width;
  float actual_width;
  float alpha;
  int actual_width_rounded;
} glw_segway_t;


/**
 *
 */
static void
glw_segway_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_segway_t *s = (glw_segway_t *)w;
  glw_t *c;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  s->req_width = 0;
  int f = glw_filter_constraints(c);

  if(f & GLW_CONSTRAINT_X) {
    s->req_width = glw_req_width(c);
  } else if(f & GLW_CONSTRAINT_W && c->glw_req_weight < 0) {
    s->req_width = rc->rc_height * -c->glw_req_weight;
  }

  glw_rctx_t rc0 = *rc;
  float alpha;
  if(s->req_width == 0 || !s->direction) {
    rc0.rc_width = w->glw_root->gr_width;
    rc0.rc_segwayed = 1;
    alpha = 0;
  } else {
    rc0.rc_width = s->req_width;
    alpha = 1;
  }

  glw_lp(&s->alpha, w->glw_root, alpha, 0.25);
  glw_lp(&s->actual_width, w->glw_root, s->req_width, 0.25);
  s->actual_width_rounded = rintf(s->actual_width);

  rc0.rc_alpha *= s->alpha;
  glw_layout0(c, &rc0);
  glw_set_constraints(w, s->actual_width_rounded, 0, 0, GLW_CONSTRAINT_X);
}


/**
 *
 */
static void
glw_segway_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;
  glw_segway_t *s = (glw_segway_t *)w;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  if(s->req_width == 0 || s->direction == 0)
    return;

  rc0 = *rc;

  int displacement = s->req_width - s->actual_width_rounded;

  switch(s->direction) {

  case SEGWAY_DIRECTION_LEFT:
    glw_reposition(&rc0, -displacement, rc->rc_height,
                   rc->rc_width, 0);
    break;
  case SEGWAY_DIRECTION_RIGHT:
    glw_reposition(&rc0, displacement * 0, rc->rc_height,
                   rc->rc_width + 1 * displacement, 0);
    break;
  }
  rc0.rc_width = s->req_width;
  rc0.rc_alpha *= s->alpha;
  glw_render0(c, &rc0);
}

static struct strtab segway_directions[] = {
  { "left",    SEGWAY_DIRECTION_LEFT},
  { "right",   SEGWAY_DIRECTION_RIGHT},
};

/**
 *
 */
static int
glw_segway_set_rstr_unresolved(glw_t *w, const char *a, rstr_t *value,
                               glw_style_t *gs)
{
  if(!strcmp(a, "direction")) {
    glw_segway_t *s = (glw_segway_t *)w;
    s->direction = str2val(rstr_get(value), segway_directions);
    return GLW_SET_RERENDER_REQUIRED;
  }
  return GLW_SET_NOT_RESPONDING;
}

static void
glw_segway_ctor(glw_t *w)
{
  glw_set_constraints(w, 0, 0, 0, GLW_CONSTRAINT_X);
}


static glw_class_t glw_segway = {
  .gc_name = "segway",
  .gc_instance_size = sizeof(glw_segway),
  .gc_ctor = glw_segway_ctor,
  .gc_layout = glw_segway_layout,
  .gc_render = glw_segway_render,
  .gc_set_rstr_unresolved = glw_segway_set_rstr_unresolved,
};

GLW_REGISTER_CLASS(glw_segway);
