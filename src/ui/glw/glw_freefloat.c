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

#include <assert.h>
#include "glw.h"

#define GLW_FREEFLOAT_MAX_VISIBLE 5

typedef struct glw_freefloat {
  glw_t w;
  int xpos;
  int num_visible;
  glw_t *pick;
  glw_t *visible[GLW_FREEFLOAT_MAX_VISIBLE];
  int rand;
} glw_freefloat_t;


#define glw_parent_v  glw_parent_val[0].f
#define glw_parent_s  glw_parent_val[1].f
#define glw_parent_s2 glw_parent_val[2].f
#define glw_parent_a  glw_parent_val[3].f
#define glw_parent_x  glw_parent_val[4].f
#define glw_parent_y  glw_parent_val[5].f 

/**
 *
 */
static int
is_visible(glw_freefloat_t *ff, glw_t *c)
{
  int i;
  for(i = 0; i < GLW_FREEFLOAT_MAX_VISIBLE; i++)
    if(ff->visible[i] == c)
      return 1;
  return 0;
}

/**
 *
 */
static void
glw_freefloat_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_freefloat_t *ff = (glw_freefloat_t *)w;
  glw_t *c;
  int i;
  float a;
  glw_rctx_t rc0;

  for(i = 0; i < ff->num_visible; i++) {
    if((c = ff->visible[i]) == NULL)
      continue;

    rc0 = *rc;
    a = (1 - fabs(-1 + (GLW_MAX(0, -0.1 + c->glw_parent_v * 2.1))));

    rc0.rc_alpha *= a;

    glw_Translatef(&rc0, 
		   c->glw_parent_x,
		   c->glw_parent_y,
		   -5 + c->glw_parent_v * 5);

    glw_Rotatef(&rc0, 
		-30 + c->glw_parent_v * 60,
		fabsf(sin(c->glw_parent_a)),
		fabsf(cos(c->glw_parent_a)),
		0.0);

    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
setup_floater(glw_freefloat_t *ff, glw_t *c)
{
  ff->xpos++;
  c->glw_parent_v = 0;
  c->glw_parent_s = 0.001;
  c->glw_parent_s2 = 0;

  c->glw_parent_a = showtime_get_ts();
  c->glw_parent_x = -1.0 + (ff->xpos % ff->num_visible) * 2 /
    ((float)ff->num_visible - 1);

  ff->rand = ff->rand * 1664525 + 1013904223;

  c->glw_parent_y = (ff->rand & 0xffff) / 32768.0 - 1.0;
}

static int
zsort(const void *A, const void *B)
{
  glw_t *a = *(glw_t **)A;
  glw_t *b = *(glw_t **)B;

  float az = a ? a->glw_parent_v : -100;
  float bz = b ? b->glw_parent_v : -100;

  if(az > bz)
    return 1;
  if(az < bz)
    return -1;
  return 0;
}


/**
 *
 */
static void
glw_freefloat_layout(glw_freefloat_t *ff, glw_rctx_t *rc)
{
  glw_t *w = &ff->w;
  glw_t *c;
  int i, candpos = -1;

  float vmin = 1;

  for(i = 0; i < ff->num_visible; i++) {
    if(ff->visible[i] == NULL) {
      candpos = i;
    } else {
      vmin = GLW_MIN(ff->visible[i]->glw_parent_v, vmin);
    }
  }

  if(vmin > 1.0 / (float)ff->num_visible && candpos != -1) {
    /* Insert new entry */

    if(ff->pick != NULL)
      ff->pick = glw_next_widget(ff->pick);
    
    if(ff->pick == NULL)
      ff->pick = glw_first_widget(w);

    if(ff->pick != NULL && !is_visible(ff, ff->pick)) {
      ff->visible[candpos] = ff->pick;
      setup_floater(ff, ff->pick);
    }
  }

  for(i = 0; i < ff->num_visible; i++) {
    if((c = ff->visible[i]) == NULL)
      continue;

    if(c->glw_parent_v >= 1) {
      ff->visible[i] = NULL;
    } else {
      glw_layout0(c, rc);
    }

    if(c->glw_class->gc_ready ? c->glw_class->gc_ready(c) : 1)
      c->glw_parent_v += c->glw_parent_s;
    c->glw_parent_s += c->glw_parent_s2;
  }

  qsort(ff->visible, ff->num_visible, sizeof(void *), zsort);

  c = ff->pick;

  // Layout next few items to pick, to preload textures, etc

  for(i = 0; i < 3 && c != NULL; i++, c = glw_next_widget(c)) {
    if(!is_visible(ff, c))
      glw_layout0(c, rc);
  }
}


/**
 *
 */
static void
glw_freefloat_retire_child(glw_t *w, glw_t *c)
{
  glw_freefloat_t *ff = (glw_freefloat_t *)w;
  if(is_visible(ff, c)) {
    // This one is visible, keep it for a while
    
    c->glw_parent_s2 = 0.001;

    if(c == ff->pick)
      ff->pick = TAILQ_NEXT(ff->pick, glw_parent_link);
    
    glw_remove_from_parent(c, w);
    return;
  }
  // Destroy at once
  glw_destroy(c);
}

/**
 *
 */
static int
glw_freefloat_callback(glw_t *w, void *opaque, glw_signal_t signal,
		       void *extra)
{
  glw_freefloat_t *ff = (glw_freefloat_t *)w;
  glw_t *c = extra;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    glw_freefloat_layout(ff, extra);
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
    assert(!is_visible(ff, c));
    if(c == ff->pick)
      ff->pick = TAILQ_NEXT(ff->pick, glw_parent_link);
    break;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_freefloat_ctor(glw_t *w)
{
  glw_freefloat_t *ff = (glw_freefloat_t *)w;
  ff->rand = showtime_get_ts();
  ff->num_visible = GLW_FREEFLOAT_MAX_VISIBLE;
}


/**
 *
 */
static glw_class_t glw_freefloat = {
  .gc_name = "freefloat",
  .gc_instance_size = sizeof(glw_freefloat_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_ctor = glw_freefloat_ctor,
  .gc_render = glw_freefloat_render,
  .gc_retire_child = glw_freefloat_retire_child,
  .gc_signal_handler = glw_freefloat_callback,
};

GLW_REGISTER_CLASS(glw_freefloat);
