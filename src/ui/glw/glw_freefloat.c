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


typedef struct glw_freefloat_item {
  float v;
  float s;
  float s2;
  float a;
  float x;
  float y;
} glw_freefloat_item_t;

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
  int zmax = 0;

  for(i = 0; i < ff->num_visible; i++) {
    if((c = ff->visible[i]) == NULL)
      continue;

    glw_freefloat_item_t *cd = glw_parent_data(c, glw_freefloat_item_t);

    rc0 = *rc;
    rc0.rc_zmax = &zmax;

    a = (1 - fabs(-1 + (GLW_MAX(0, -0.1 + cd->v * 2.1))));

    rc0.rc_alpha *= a;

    glw_Translatef(&rc0, 
		   cd->x,
		   cd->y,
		   -5 + cd->v * 5);

    glw_Rotatef(&rc0, 
		-30 + cd->v * 60,
		fabsf(sinf(cd->a)),
		fabsf(cosf(cd->a)),
		0.0);

    rc0.rc_zindex = MAX(zmax, rc->rc_zindex);
    glw_render0(c, &rc0);
    zmax = MAX(zmax, rc0.rc_zindex + 1);
  }
  *rc->rc_zmax = MAX(*rc->rc_zmax, zmax);
}


/**
 *
 */
static void
setup_floater(glw_freefloat_t *ff, glw_t *c)
{
  ff->xpos++;
  glw_freefloat_item_t *cd = glw_parent_data(c, glw_freefloat_item_t);
  cd->v = 0;
  cd->s = 0.001;
  cd->s2 = 0;

  cd->a = arch_get_ts();
  cd->x = -1.0 + (ff->xpos % ff->num_visible) * 2 /
    ((float)ff->num_visible - 1);

  ff->rand = ff->rand * 1664525 + 1013904223;

  cd->y = (ff->rand & 0xffff) / 32768.0 - 1.0;
}

static int
zsort(const void *A, const void *B)
{
  glw_t *a = *(glw_t **)A;
  glw_t *b = *(glw_t **)B;

  glw_freefloat_item_t *ad = a ? glw_parent_data(a, glw_freefloat_item_t):NULL;
  glw_freefloat_item_t *bd = b ? glw_parent_data(b, glw_freefloat_item_t):NULL;

  float az = ad ? ad->v : -100;
  float bz = bd ? bd->v : -100;

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
glw_freefloat_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_freefloat_t *ff = (glw_freefloat_t *)w;
  glw_t *c;
  int i, candpos = -1;

  float vmin = 1;

  for(i = 0; i < ff->num_visible; i++) {
    if(ff->visible[i] == NULL) {
      candpos = i;
    } else {
      glw_freefloat_item_t *cd =
        glw_parent_data(ff->visible[i], glw_freefloat_item_t);
      vmin = GLW_MIN(cd->v, vmin);
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

    glw_freefloat_item_t *cd = glw_parent_data(c, glw_freefloat_item_t);

    if(cd->v >= 1) {
      ff->visible[i] = NULL;
    } else {
      glw_layout0(c, rc);
    }

    if(c->glw_class->gc_status ? (c->glw_class->gc_status(c) ==
                                  GLW_STATUS_LOADED) : 1)
      cd->v += cd->s;
    cd->s += cd->s2;
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

    glw_freefloat_item_t *cd = glw_parent_data(c, glw_freefloat_item_t);

    cd->s2 = 0.001;

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
  ff->rand = arch_get_ts();
  ff->num_visible = GLW_FREEFLOAT_MAX_VISIBLE;
}


/**
 *
 */
static glw_class_t glw_freefloat = {
  .gc_name = "freefloat",
  .gc_instance_size = sizeof(glw_freefloat_t),
  .gc_parent_data_size = sizeof(glw_freefloat_item_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_ctor = glw_freefloat_ctor,
  .gc_layout = glw_freefloat_layout,
  .gc_render = glw_freefloat_render,
  .gc_retire_child = glw_freefloat_retire_child,
  .gc_signal_handler = glw_freefloat_callback,
};

GLW_REGISTER_CLASS(glw_freefloat);
