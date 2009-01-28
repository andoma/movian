/*
 *  GL Widgets, GLW_IMAGE widget
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
#include "glw_image.h"

static void
glw_image_dtor(glw_t *w)
{
  glw_image_t *gi = (void *)w;

  if(gi->gi_tex != NULL)
    glw_tex_deref(w->glw_root, gi->gi_tex);

  if(gi->gi_render_initialized)
    glw_render_free(&gi->gi_gr);
}




static void 
glw_image_render(glw_t *w, glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;
  glw_loadable_texture_t *glt = gi->gi_tex;
  float alpha_self;
  glw_rctx_t rc0;
  glw_t *c;
  float xs, ys;

  if(glt == NULL || glt->glt_state != GLT_STATE_VALID)
    return;

  if(!glw_is_tex_inited(&glt->glt_texture))
    alpha_self = 0;
  else
    alpha_self = rc->rc_alpha * w->glw_alpha * gi->gi_alpha_self;

  if(!gi->gi_border_scaling) {

    rc0 = *rc;

    glw_PushMatrix(&rc0, rc);

    glw_align_1(&rc0, w->glw_alignment);
      
    if(w->glw_flags & GLW_KEEP_ASPECT)
      glw_rescale(&rc0, glt->glt_aspect);

    if(gi->gi_angle != 0)
      glw_Rotatef(&rc0, -gi->gi_angle, 0, 0, 1);

    glw_align_2(&rc0, w->glw_alignment);

    if(glw_is_focusable(w))
      glw_store_matrix(w, &rc0);
    
    if(alpha_self > 0.01)
      glw_render(&gi->gi_gr, &rc0, 
		 GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
		 &glt->glt_texture,
		 w->glw_col.r, w->glw_col.g, w->glw_col.b, alpha_self);

   
    if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
      rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
      glw_render0(c, &rc0);
    }
    glw_PopMatrix();

  } else {

    if(glw_is_focusable(w))
      glw_store_matrix(w, rc);

    if(alpha_self > 0.01)
      glw_render(&gi->gi_gr, rc, 
		 GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
		 &glt->glt_texture,
		 w->glw_col.r, w->glw_col.g, w->glw_col.b, alpha_self);

    if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {

      rc0 = *rc;
      
      glw_PushMatrix(&rc0, rc);
      
      xs = gi->gi_child_xs;
      ys = gi->gi_child_ys;

      glw_Scalef(&rc0, xs, ys, 1.0f);

      rc0.rc_size_x = rc->rc_size_x * xs;
      rc0.rc_size_y = rc->rc_size_y * ys;

      rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
      glw_render0(c, &rc0);

      glw_PopMatrix();
    }
  }
}


/**
 *
 */
static void
glw_image_layout_tesselated(glw_rctx_t *rc, glw_image_t *gi, 
			    glw_loadable_texture_t *glt)
{
  float tex[4][2];
  float vex[4][2];
  int x, y, i = 0;
  float a, b;
  float t_aspect = glt->glt_aspect;

  gi->gi_saved_size_x = rc->rc_size_x;
  gi->gi_saved_size_y = rc->rc_size_y;
 
  a = rc->rc_size_x / rc->rc_size_y;

  /* Texture X coordinates */
  tex[0][0] = 0.0;
  tex[1][0] = gi->gi_tex_left;
  tex[2][0] = gi->gi_tex_right;
  tex[3][0] = 1.0;

  /* Texture Y coordinates */
  tex[0][1] = 0.0;
  tex[1][1] = gi->gi_tex_top;
  tex[2][1] = gi->gi_tex_bottom;
  tex[3][1] = 1.0;


  vex[0][0] = -1.0;
  vex[1][0] = -1.0 + gi->gi_tex_left * 2;
  vex[2][0] =  1 - ((1 - gi->gi_tex_right) * 2);
  vex[3][0] =  1.0;
    
  vex[0][1] =  1.0;
  vex[1][1] =  1 - gi->gi_tex_top * 2;
  vex[2][1] = -1.0 + ((1 - gi->gi_tex_bottom) * 2);
  vex[3][1] = -1.0;


  if(a > t_aspect) {
    
    b = t_aspect / a;
    
    vex[1][0] = -1.0 + gi->gi_tex_left * 2 * b;
    vex[2][0] =  1 - ((1 - gi->gi_tex_right) * 2 * b);
    
  } else {
    
    b = a / t_aspect;
    
    vex[1][1] =  1 - gi->gi_tex_top * 2 *b;
    vex[2][1] = -1.0 + ((1 - gi->gi_tex_bottom) * 2 * b);
  }
  
  if(gi->gi_mirror & GLW_MIRROR_X)
    for(x = 0; x < 4; x++)
      tex[x][0] = 1.0f - tex[x][0];

  if(gi->gi_mirror & GLW_MIRROR_Y)
    for(y = 0; y < 4; y++)
      tex[y][1] = 1.0f - tex[y][1];

    
  glw_render_set_pre(&gi->gi_gr);

  for(y = 0; y < 3; y++) {
    for(x = 0; x < 3; x++) {

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 0][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 0][0], tex[y + 1][1]);
      i++;

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 1][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 1][0], tex[y + 1][1]);
      i++;

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 1][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 1][0], tex[y + 0][1]);
      i++;

      glw_render_vtx_pos(&gi->gi_gr, i, vex[x + 0][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gi->gi_gr, i, tex[x + 0][0], tex[y + 0][1]);
      i++;
    }
  }

  glw_render_set_post(&gi->gi_gr);
    
  gi->gi_child_xs = (vex[2][0] - vex[1][0]) * 0.5f;
  gi->gi_child_ys = (vex[1][1] - vex[2][1]) * 0.5f;
}


/**
 *
 */
static void 
glw_image_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;
  glw_loadable_texture_t *glt = gi->gi_tex;
  glw_t *c;

  rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, w->glw_exp_req);

  if(glt == NULL)
    return;

  glw_tex_layout(w->glw_root, glt);

  if(glt->glt_state == GLT_STATE_VALID) {
    if(gi->gi_render_init == 1) {
      gi->gi_render_init = 0;

      if(gi->gi_render_initialized)
	glw_render_free(&gi->gi_gr);

      glw_render_init(&gi->gi_gr, 4 * (gi->gi_border_scaling ? 9 : 1),
		      GLW_RENDER_ATTRIBS_TEX);
      gi->gi_render_initialized = 1;

      if(!gi->gi_border_scaling) {

	glw_render_vtx_pos(&gi->gi_gr, 0, -1.0, -1.0, 0.0);
	glw_render_vtx_st (&gi->gi_gr, 0,  0.0,  1.0);

	glw_render_vtx_pos(&gi->gi_gr, 1,  1.0, -1.0, 0.0);
	glw_render_vtx_st (&gi->gi_gr, 1,  1.0,  1.0);

	glw_render_vtx_pos(&gi->gi_gr, 2,  1.0,  1.0, 0.0);
	glw_render_vtx_st (&gi->gi_gr, 2,  1.0,  0.0);

	glw_render_vtx_pos(&gi->gi_gr, 3, -1.0,  1.0, 0.0);
	glw_render_vtx_st (&gi->gi_gr, 3,  0.0,  0.0);
      } else {
	glw_image_layout_tesselated(rc, gi, glt);
      }
    } else if(gi->gi_border_scaling &&
	      (gi->gi_saved_size_x != rc->rc_size_x ||
	       gi->gi_saved_size_y != rc->rc_size_y)) {
      glw_image_layout_tesselated(rc, gi, glt);
    }
  }

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_layout0(c, rc);
}



/*
 *
 */
static int
glw_image_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
  glw_t *c;
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_image_layout(w, extra);
    break;
  case GLW_SIGNAL_RENDER:
    glw_image_render(w, extra);
    break;
  case GLW_SIGNAL_DTOR:
    glw_image_dtor(w);
    break;
  case GLW_SIGNAL_EVENT:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_signal0(c, GLW_SIGNAL_EVENT, extra))
	return 1;
    break;
  }
  return 0;
}

/*
 *
 */
void 
glw_image_ctor(glw_t *w, int init, va_list ap)
{
  glw_image_t *gi = (void *)w;
  glw_attribute_t attrib;
  const char *filename = NULL;

  if(init) {
    glw_signal_handler_int(w, glw_image_callback);
    gi->gi_alpha_self = 1;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_TEXTURE_COORDS:
      gi->gi_border_scaling = 1;
      gi->gi_tex_left   = va_arg(ap, double);
      gi->gi_tex_top    = va_arg(ap, double);
      gi->gi_tex_right  = va_arg(ap, double);
      gi->gi_tex_bottom = va_arg(ap, double);
      gi->gi_render_init = 1;
      break;

    case GLW_ATTRIB_ANGLE:
      gi->gi_angle = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ALPHA_SELF:
      gi->gi_alpha_self = va_arg(ap, double);
      break;
      
    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      gi->gi_render_init = 1;
      break;

    case GLW_ATTRIB_MIRROR:
      gi->gi_mirror = va_arg(ap, int);
      gi->gi_render_init = 1;
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);


  if(filename == NULL)
    return;

  if(gi->gi_tex != NULL)
    glw_tex_deref(w->glw_root, gi->gi_tex);

  gi->gi_tex = glw_tex_create(w->glw_root, filename);
}
