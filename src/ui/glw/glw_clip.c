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
#include "glw_texture.h"
 
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
glw_clip_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_clip_t *gc = (glw_clip_t *)w;

  if(w->glw_flags2 & GLW2_DEBUG)
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

  glw_clip_disable(w->glw_root, l);
  glw_clip_disable(w->glw_root, r);
  glw_clip_disable(w->glw_root, t);
  glw_clip_disable(w->glw_root, b);

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
glw_fade_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_fade_t *gf = (glw_fade_t *)w;
  int fader = -1;
  if(w->glw_flags2 & GLW2_DEBUG)
    glw_wirebox(w->glw_root, rc);

  if(gf->gf_run)
    fader = glw_fader_enable(w->glw_root, rc, gf->gf_plane,
			     gf->gf_alpha_falloff, gf->gf_blur_falloff);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_render0(c, rc);

  glw_fader_disable(w->glw_root, fader);

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




  
typedef struct glw_stencil {
  glw_t w;

  glw_loadable_texture_t *gs_tex;
  int16_t gs_border[4];

  float gs_rotate_a;
  float gs_rotate_x;
  float gs_rotate_y;
  float gs_rotate_z;

  float gs_scale_x;
  float gs_scale_y;
  float gs_scale_z;

} glw_stencil_t;


/**
 *
 */
static int
glw_stencil_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_stencil_t *gs = (glw_stencil_t *)w;
  glw_loadable_texture_t *glt = gs->gs_tex;

  if(w->glw_alpha < 0.01)
    return 0;

  if(glt == NULL)
    return 0;

  glw_tex_layout(w->glw_root, glt);
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
  return 0;
}


/**
 *
 */
static void
glw_stencil_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_stencil_t *gs = (glw_stencil_t *)w;
  glw_loadable_texture_t *glt = gs->gs_tex;
  glw_rctx_t rc0 = *rc;

  glw_Scalef(&rc0, 
	     gs->gs_scale_x,
	     gs->gs_scale_y,
	     gs->gs_scale_z);

  if(gs->gs_rotate_a)
    glw_Rotatef(&rc0, 
		gs->gs_rotate_a,
		gs->gs_rotate_x,
		gs->gs_rotate_y,
		gs->gs_rotate_z);
  

  if(glt == NULL)
    return;

  if(glt->glt_state != GLT_STATE_VALID)
    return;
  
  glw_stencil_enable(w->glw_root, &rc0, &glt->glt_texture, gs->gs_border);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_render0(c, rc);

  glw_stencil_disable(w->glw_root);
  if(w->glw_flags2 & GLW2_DEBUG)
    glw_wirebox(w->glw_root, &rc0);
}


/**
 *
 */
static int
glw_stencil_callback(glw_t *w, void *opaque, glw_signal_t signal,
		  void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_stencil_layout(w, extra);
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
stencil_set_source(glw_t *w, rstr_t *filename)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;
  
  if(gs->gs_tex != NULL)
    glw_tex_deref(w->glw_root, gs->gs_tex);

  gs->gs_tex = glw_tex_create(w->glw_root, filename, 0, -1, -1, 0, 0, 0);
}



/**
 *
 */
static void
set_border(glw_t *w, const int16_t *v)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;

  memcpy(gs->gs_border, v, sizeof(int16_t) * 4);
}


/**
 *
 */
static void
set_rotation(glw_t *w, const float *v)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;
  
  gs->gs_rotate_a = v[0];
  gs->gs_rotate_x = v[1];
  gs->gs_rotate_y = v[2];
  gs->gs_rotate_z = v[3];
}


/**
 *
 */
static void
set_scaling(glw_t *w, const float *xyz)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;

  gs->gs_scale_x = xyz[0];
  gs->gs_scale_y = xyz[1];
  gs->gs_scale_z = xyz[2];
}


/**
 *
 */
static void 
glw_stencil_ctor(glw_t *w)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;

  gs->gs_scale_x = 1;
  gs->gs_scale_y = 1;
  gs->gs_scale_z = 1;
}




static glw_class_t glw_stencil = {
  .gc_name = "stencil",
  .gc_instance_size = sizeof(glw_stencil_t),
  .gc_render = glw_stencil_render,
  .gc_signal_handler = glw_stencil_callback,
  .gc_ctor = glw_stencil_ctor,
  .gc_set_source = stencil_set_source,
  .gc_set_border = set_border,
  .gc_set_rotation = set_rotation,
  .gc_set_scaling = set_scaling,
};

GLW_REGISTER_CLASS(glw_stencil);
 
