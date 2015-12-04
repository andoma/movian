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
#include "glw_texture.h"

typedef struct glw_clip {
  glw_t w;

  float gc_clipping[4];
  float gc_pixel_clipping[4];

  float gc_alpha_outside;
  float gc_sharpness_outside;

} glw_clip_t;



static int
setval(float *vp, float v)
{
  if(*vp == v)
    return GLW_SET_NO_CHANGE;
  *vp = v;
  return GLW_SET_RERENDER_REQUIRED;
}


/**
 *
 */
static int
glw_clip_set_float_unresolved(glw_t *w, const char *a, float value,
                              glw_style_t *gs)
{
  glw_clip_t *gc = (glw_clip_t *)w;

  if(!strcmp(a, "left"))
    return setval(&gc->gc_clipping[0], value);
  if(!strcmp(a, "top"))
    return setval(&gc->gc_clipping[1], value);
  if(!strcmp(a, "right"))
    return setval(&gc->gc_clipping[2], value);
  if(!strcmp(a, "bottom"))
    return setval(&gc->gc_clipping[3], value);

  if(!strcmp(a, "leftPx"))
    return setval(&gc->gc_pixel_clipping[0], value);
  if(!strcmp(a, "topPx"))
    return setval(&gc->gc_pixel_clipping[1], value);
  if(!strcmp(a, "rightPx"))
    return setval(&gc->gc_pixel_clipping[2], value);
  if(!strcmp(a, "bottomPx"))
    return setval(&gc->gc_pixel_clipping[3], value);

  if(!strcmp(a, "alphaOutside"))
    return setval(&gc->gc_alpha_outside, value);

  if(!strcmp(a, "blurOutside"))
    return setval(&gc->gc_alpha_outside, 1.0f - GLW_CLAMP(value, 0, 1));

  return GLW_SET_NOT_RESPONDING;
}


/**
 *
 */
static void
glw_clip_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;

  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
}


/**
 *
 */
static void
glw_clip_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_clip_t *gc = (glw_clip_t *)w;
  glw_root_t *gr = w->glw_root;
  int clippers[4];

  for(int i = 0; i < 4; i++) {
    if(gc->gc_clipping[i] > 0)
      clippers[i] = glw_clip_enable(gr, rc, i, gc->gc_clipping[i],
                                    gc->gc_alpha_outside,
                                    gc->gc_sharpness_outside);
    else if(gc->gc_pixel_clipping[i] > 0) {
      clippers[i] = glw_clip_enable(gr, rc, i, gc->gc_pixel_clipping[i] /
                                    (i & 1 ? rc->rc_height: rc->rc_width),
                                    gc->gc_alpha_outside,
                                    gc->gc_sharpness_outside);
    } else {
      clippers[i] = -1;
    }
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_render0(c, rc);

  for(int i = 0; i < 4; i++) {
    glw_clip_disable(w->glw_root, clippers[i]);
  }
}

static int
glw_clip_callback(glw_t *w, void *opaque, glw_signal_t signal,
		  void *extra)
{
  switch(signal) {
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
  .gc_layout = glw_clip_layout,
  .gc_render = glw_clip_render,
  .gc_signal_handler = glw_clip_callback,
  .gc_set_float_unresolved = glw_clip_set_float_unresolved,
};



GLW_REGISTER_CLASS(glw_clip);




#if NUM_FADERS > 0

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
static int
glw_fade_set_float4(glw_t *w, glw_attribute_t attrib, const float *v)
{
  glw_fade_t *gf = (glw_fade_t *)w;
  float tmp[4];
  float l;
  switch(attrib) {
  case GLW_ATTRIB_PLANE:
    l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

    tmp[0] = v[0] / l;
    tmp[1] = v[1] / l;
    tmp[2] = v[2] / l;
    tmp[3] = v[3];
    gf->gf_run = 1;
    return glw_attrib_set_float4(gf->gf_plane, tmp);
  default:
    return -1;
  }
}

/**
 *
 */
static void
glw_fade_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;

  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
}




static void
glw_fade_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_fade_t *gf = (glw_fade_t *)w;
  int fader = -1;

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
static int
fader_set_float(glw_t *w, glw_attribute_t attrib, float value,
                glw_style_t *gs)
{
  glw_fade_t *gf = (glw_fade_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_ALPHA_FALLOFF:
    if(gf->gf_alpha_falloff == value)
      return 0;

    gf->gf_alpha_falloff = value;
    break;

  case GLW_ATTRIB_BLUR_FALLOFF:
    if(gf->gf_blur_falloff == value)
      return 0;

    gf->gf_blur_falloff = value;
    break;

  default:
    return -1;
  }
  return 1;
}




static glw_class_t glw_fader = {
  .gc_name = "fader",
  .gc_instance_size = sizeof(glw_fade_t),
  .gc_layout = glw_fade_layout,
  .gc_render = glw_fade_render,
  .gc_signal_handler = glw_fade_callback,
  .gc_set_float4 = glw_fade_set_float4,
  .gc_set_float = fader_set_float,
};



GLW_REGISTER_CLASS(glw_fader);

#endif


#if NUM_STENCILERS > 0


typedef struct glw_stencil {
  glw_t w;

  glw_loadable_texture_t *gs_tex;
  int16_t gs_border[4];

  float gs_rotate[4];
  float gs_scale[3];

} glw_stencil_t;


/**
 *
 */
static void
glw_stencil_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_stencil_t *gs = (glw_stencil_t *)w;
  glw_loadable_texture_t *glt = gs->gs_tex;

  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  if(glt == NULL)
    return;

  glw_tex_layout(w->glw_root, glt);
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
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
	     gs->gs_scale[0],
	     gs->gs_scale[1],
	     gs->gs_scale[2]);

  if(gs->gs_rotate[0])
    glw_Rotatef(&rc0,
		gs->gs_rotate[0],
		gs->gs_rotate[1],
		gs->gs_rotate[2],
		gs->gs_rotate[3]);
  

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
stencil_set_source(glw_t *w, rstr_t *filename, glw_style_t *style)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;
  
  if(gs->gs_tex != NULL)
    glw_tex_deref(w->glw_root, gs->gs_tex);

  gs->gs_tex = glw_tex_create(w->glw_root, filename, 0, -1, -1, 0, 0, 0);
}



/**
 *
 */
static int
stencil_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v,
                    glw_style_t *style)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_BORDER:
    return glw_attrib_set_int16_4(gs->gs_border, v);
  default:
    return -1;
  }
}


/**
 *
 */
static int
set_float3(glw_t *w, glw_attribute_t attrib, const float *xyz,
           glw_style_t *style)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_SCALING:
    return glw_attrib_set_float3(gs->gs_scale, xyz) * GLW_REFRESH_LAYOUT_ONLY;
  default:
    return -1;
  }
}


/**
 *
 */
static int
set_float4(glw_t *w, glw_attribute_t attrib, const float *vector)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_ROTATION:
    return glw_attrib_set_float4(gs->gs_rotate, vector) *
      GLW_REFRESH_LAYOUT_ONLY;
  default:
    return -1;
  }
}


/**
 *
 */
static void 
glw_stencil_ctor(glw_t *w)
{
  glw_stencil_t *gs = (glw_stencil_t *)w;

  gs->gs_scale[0] = 1.0f;
  gs->gs_scale[1] = 1.0f;
  gs->gs_scale[2] = 1.0f;

}




static glw_class_t glw_stencil = {
  .gc_name = "stencil",
  .gc_instance_size = sizeof(glw_stencil_t),
  .gc_layout = glw_stencil_layout,
  .gc_render = glw_stencil_render,
  .gc_signal_handler = glw_stencil_callback,
  .gc_ctor = glw_stencil_ctor,
  .gc_set_source = stencil_set_source,
  .gc_set_int16_4 = stencil_set_int16_4,
  .gc_set_float4 = set_float4,
  .gc_set_float3 = set_float3,
};

GLW_REGISTER_CLASS(glw_stencil);
 
#endif
