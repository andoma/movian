/*
 *  GL Widgets, GLW_CONTAINER -widgets
 *  Copyright (C) 2007 Andreas Öman
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
 
typedef struct glw_clip {
  glw_t w;

  float gc_clipping[4];

} glw_clip_t;


/**
 *
 */
static void
set_clipping(glw_t *w, const float *v)
{
  glw_clip_t *gc = (glw_clip_t *)w;
  memcpy(gc->gc_clipping, v, sizeof(float) * 4);
}

/**
 *
 */
static int
glw_clip_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;

  if(w->glw_alpha < 0.01)
    return 0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
  return 0;
}




static void
glw_clip_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_clip_t *gc = (glw_clip_t *)w;

  if(w->glw_flags & GLW_DEBUG)
    glw_wirebox(w->glw_root, rc);

  int l = gc->gc_clipping[0] >= 0 ? 
    glw_clip_enable(w->glw_root, rc, GLW_CLIP_LEFT, gc->gc_clipping[0]) : -1;
  int t = gc->gc_clipping[1] >= 0 ? 
    glw_clip_enable(w->glw_root, rc, GLW_CLIP_TOP, gc->gc_clipping[1]) : -1;
  int r = gc->gc_clipping[2] >= 0 ? 
    glw_clip_enable(w->glw_root, rc, GLW_CLIP_RIGHT, gc->gc_clipping[2]) : -1;
  int b = gc->gc_clipping[3] >= 0 ? 
    glw_clip_enable(w->glw_root, rc, GLW_CLIP_BOTTOM, gc->gc_clipping[3]) : -1;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_render0(c, rc);

  glw_clip_disable(w->glw_root, rc, l);
  glw_clip_disable(w->glw_root, rc, r);
  glw_clip_disable(w->glw_root, rc, t);
  glw_clip_disable(w->glw_root, rc, b);

}

static int
glw_clip_callback(glw_t *w, void *opaque, glw_signal_t signal,
		  void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_clip_layout(w, extra);
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    glw_copy_constraints(w, extra);
    return 1;
  default:
    return 0;
  }
}



static glw_class_t glw_clip = {
  .gc_name = "clip",
  .gc_instance_size = sizeof(glw_clip_t),
  .gc_render = glw_clip_render,
  .gc_signal_handler = glw_clip_callback,
  .gc_set_clipping = set_clipping,
};



GLW_REGISTER_CLASS(glw_clip);






  
typedef struct glw_fade {
  glw_t w;
  int gf_run;
  float gf_plane[4];
  float gf_alpha_falloff;
  float gf_blur_falloff;
} glw_fade_t;


/**
 *
 */
static void
set_plane(glw_t *w, const float *v)
{
  glw_fade_t *gf = (glw_fade_t *)w;

  float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  
  gf->gf_plane[0] = v[0] / l;
  gf->gf_plane[1] = v[1] / l;
  gf->gf_plane[2] = v[2] / l;
  gf->gf_plane[3] = v[3];

  gf->gf_run = 1;
}

/**
 *
 */
static int
glw_fade_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;

  if(w->glw_alpha < 0.01)
    return 0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
  return 0;
}




static void
glw_fade_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_fade_t *gf = (glw_fade_t *)w;
  int fader = -1;
  if(w->glw_flags & GLW_DEBUG)
    glw_wirebox(w->glw_root, rc);

  if(gf->gf_run)
    fader = glw_fader_enable(w->glw_root, rc, gf->gf_plane,
			     gf->gf_alpha_falloff, gf->gf_blur_falloff);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_render0(c, rc);

  glw_fader_disable(w->glw_root, rc, fader);

}

static int
glw_fade_callback(glw_t *w, void *opaque, glw_signal_t signal,
		  void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_fade_layout(w, extra);
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    glw_copy_constraints(w, extra);
    return 1;
  default:
    return 0;
  }
}


/**
 *
 */
static void 
fader_set(glw_t *w, va_list ap)
{
  glw_attribute_t attrib;
  glw_fade_t *gf = (glw_fade_t *)w;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_ALPHA_FALLOFF:
      gf->gf_alpha_falloff = va_arg(ap, double);
      break;

    case GLW_ATTRIB_BLUR_FALLOFF:
      gf->gf_blur_falloff = va_arg(ap, double);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}




static glw_class_t glw_fader = {
  .gc_name = "fader",
  .gc_instance_size = sizeof(glw_fade_t),
  .gc_render = glw_fade_render,
  .gc_signal_handler = glw_fade_callback,
  .gc_set_plane = set_plane,
  .gc_set = fader_set,
};



GLW_REGISTER_CLASS(glw_fader);






 
