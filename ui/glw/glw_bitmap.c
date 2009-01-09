/*
 *  GL Widgets, GLW_BITMAP widget
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

#include <stdlib.h>
#include <string.h>

#include "glw.h"
#include "glw_bitmap.h"

static void
glw_bitmap_dtor(glw_t *w)
{
  glw_bitmap_t *gb = (void *)w;

  if(gb->gb_tex != NULL)
    glw_tex_deref(w->glw_root, gb->gb_tex);

  if(gb->gb_render_initialized)
    glw_render_free(&gb->gb_gr);
}




static void 
glw_bitmap_render(glw_t *w, glw_rctx_t *rc)
{
  glw_bitmap_t *gb = (void *)w;
  glw_texture_t *gt = gb->gb_tex;
  float alpha_self = rc->rc_alpha * w->glw_alpha * gb->gb_alpha_self;
  glw_rctx_t rc0;
  glw_t *c;
  float xs, ys;

  if(gt == NULL || gt->gt_state != GT_STATE_VALID)
    return;

  if(!gb->gb_border_scaling) {

    rc0 = *rc;

    glw_PushMatrix(&rc0, rc);

    switch(w->glw_alignment) {
    case GLW_ALIGN_CENTER:
      break;
    case GLW_ALIGN_DEFAULT:
    case GLW_ALIGN_LEFT:
      glw_Translatef(&rc0, -1.0, 0.0, 0.0f);
      break;
    case GLW_ALIGN_RIGHT:
      glw_Translatef(&rc0, 1.0, 0.0, 0.0f);
      break;
    case GLW_ALIGN_BOTTOM:
      glw_Translatef(&rc0, 0.0, -1.0, 0.0f);
      break;
    case GLW_ALIGN_TOP:
      glw_Translatef(&rc0, 0.0, 1.0, 0.0f);
      break;
    }
      
    if(w->glw_flags & GLW_KEEP_ASPECT)
      glw_rescale(&rc0, gt->gt_aspect);

    if(gb->gb_angle != 0)
      glw_Rotatef(&rc0, -gb->gb_angle, 0, 0, 1);

    switch(w->glw_alignment) {
    case GLW_ALIGN_CENTER:
      break;
    case GLW_ALIGN_DEFAULT:
    case GLW_ALIGN_LEFT:
      glw_Translatef(&rc0, 1.0f, 0.0f, 0.0f);
      break;
    case GLW_ALIGN_RIGHT:
      glw_Translatef(&rc0, -1.0f, 0.0f, 0.0f);
      break;
    case GLW_ALIGN_BOTTOM:
      glw_Translatef(&rc0, 0.0, 1.0, 0.0f);
      break;
    case GLW_ALIGN_TOP:
      glw_Translatef(&rc0, 0.0, -1.0, 0.0f);
      break;
    }

    if(glw_is_focusable(w))
      glw_store_matrix(w, rc);
    
    if(alpha_self > 0.01)
      glw_render(&gb->gb_gr, GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
		 &gt->gt_texture,
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
      glw_render(&gb->gb_gr, GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
		 &gt->gt_texture,
		 w->glw_col.r, w->glw_col.g, w->glw_col.b, alpha_self);

    if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {

      rc0 = *rc;
      
      glw_PushMatrix(&rc0, rc);
      
      xs = gb->gb_child_xs;
      ys = gb->gb_child_ys;

      glw_Scalef(&rc0, xs, ys, 1.0f);

      rc0.rc_scale_x = rc->rc_scale_x * xs;
      rc0.rc_scale_y = rc->rc_scale_y * ys;

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
glw_bitmap_layout_tesselated(glw_rctx_t *rc, glw_bitmap_t *gb, 
			     glw_texture_t *gt)
{
  float tex[4][2];
  float vex[4][2];
  int x, y, i = 0;
  float a, b;
  float t_aspect = gt->gt_aspect;

  gb->gb_saved_scale_x = rc->rc_scale_x;
  gb->gb_saved_scale_y = rc->rc_scale_y;
 
  a = rc->rc_scale_x / rc->rc_scale_y;

  /* Texture X coordinates */
  tex[0][0] = 0.0;
  tex[1][0] = gb->gb_tex_left;
  tex[2][0] = gb->gb_tex_right;
  tex[3][0] = 1.0;

  /* Texture Y coordinates */
  tex[0][1] = 0.0;
  tex[1][1] = gb->gb_tex_top;
  tex[2][1] = gb->gb_tex_bottom;
  tex[3][1] = 1.0;


  vex[0][0] = -1.0;
  vex[1][0] = -1.0 + gb->gb_tex_left * 2;
  vex[2][0] =  1 - ((1 - gb->gb_tex_right) * 2);
  vex[3][0] =  1.0;
    
  vex[0][1] =  1.0;
  vex[1][1] =  1 - gb->gb_tex_top * 2;
  vex[2][1] = -1.0 + ((1 - gb->gb_tex_bottom) * 2);
  vex[3][1] = -1.0;


  if(a > t_aspect) {
    
    b = t_aspect / a;
    
    vex[1][0] = -1.0 + gb->gb_tex_left * 2 * b;
    vex[2][0] =  1 - ((1 - gb->gb_tex_right) * 2 * b);
    
  } else {
    
    b = a / t_aspect;
    
    vex[1][1] =  1 - gb->gb_tex_top * 2 *b;
    vex[2][1] = -1.0 + ((1 - gb->gb_tex_bottom) * 2 * b);
  }
  
  if(gb->gb_mirror & GLW_MIRROR_X)
    for(x = 0; x < 4; x++)
      tex[x][0] = 1.0f - tex[x][0];

  if(gb->gb_mirror & GLW_MIRROR_Y)
    for(y = 0; y < 4; y++)
      tex[y][1] = 1.0f - tex[y][1];

    
  glw_render_set_pre(&gb->gb_gr);

  for(y = 0; y < 3; y++) {
    for(x = 0; x < 3; x++) {

      glw_render_vtx_pos(&gb->gb_gr, i, vex[x + 0][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gb->gb_gr, i, tex[x + 0][0], tex[y + 1][1]);
      i++;

      glw_render_vtx_pos(&gb->gb_gr, i, vex[x + 1][0], vex[y + 1][1], 0.0f);
      glw_render_vtx_st (&gb->gb_gr, i, tex[x + 1][0], tex[y + 1][1]);
      i++;

      glw_render_vtx_pos(&gb->gb_gr, i, vex[x + 1][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gb->gb_gr, i, tex[x + 1][0], tex[y + 0][1]);
      i++;

      glw_render_vtx_pos(&gb->gb_gr, i, vex[x + 0][0], vex[y + 0][1], 0.0f);
      glw_render_vtx_st (&gb->gb_gr, i, tex[x + 0][0], tex[y + 0][1]);
      i++;
    }
  }

  glw_render_set_post(&gb->gb_gr);
    
  gb->gb_child_xs = (vex[2][0] - vex[1][0]) * 0.5f;
  gb->gb_child_ys = (vex[1][1] - vex[2][1]) * 0.5f;
}


/**
 *
 */
static void 
glw_bitmap_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_bitmap_t *gb = (void *)w;
  glw_texture_t *gt = gb->gb_tex;
  glw_t *c;

  rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, w->glw_exp_req);

  if(gt == NULL)
    return;

  glw_tex_layout(w->glw_root, gt);

  if(gt->gt_state == GT_STATE_VALID) {
    if(gb->gb_render_init == 1) {
      gb->gb_render_init = 0;

      if(gb->gb_render_initialized)
	glw_render_free(&gb->gb_gr);

      glw_render_init(&gb->gb_gr, 4 * (gb->gb_border_scaling ? 9 : 1),
		      GLW_RENDER_ATTRIBS_TEX);
      gb->gb_render_initialized = 1;

      if(!gb->gb_border_scaling) {

	glw_render_vtx_pos(&gb->gb_gr, 0, -1.0, -1.0, 0.0);
	glw_render_vtx_st (&gb->gb_gr, 0,  0.0,  1.0);

	glw_render_vtx_pos(&gb->gb_gr, 1,  1.0, -1.0, 0.0);
	glw_render_vtx_st (&gb->gb_gr, 1,  1.0,  1.0);

	glw_render_vtx_pos(&gb->gb_gr, 2,  1.0,  1.0, 0.0);
	glw_render_vtx_st (&gb->gb_gr, 2,  1.0,  0.0);

	glw_render_vtx_pos(&gb->gb_gr, 3, -1.0,  1.0, 0.0);
	glw_render_vtx_st (&gb->gb_gr, 3,  0.0,  0.0);
      } else {
	glw_bitmap_layout_tesselated(rc, gb, gt);
      }
    } else if(gb->gb_border_scaling &&
	      (gb->gb_saved_scale_x != rc->rc_scale_x ||
	       gb->gb_saved_scale_y != rc->rc_scale_y)) {
      glw_bitmap_layout_tesselated(rc, gb, gt);
    }
  }

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_layout0(c, rc);
}



/*
 *
 */
static int
glw_bitmap_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
  glw_t *c;
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_bitmap_layout(w, extra);
    break;
  case GLW_SIGNAL_RENDER:
    glw_bitmap_render(w, extra);
    break;
  case GLW_SIGNAL_DTOR:
    glw_bitmap_dtor(w);
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
glw_bitmap_ctor(glw_t *w, int init, va_list ap)
{
  glw_bitmap_t *gb = (void *)w;
  glw_attribute_t attrib;
  const char *filename = NULL;

  if(init) {
    w->glw_alignment = GLW_ALIGN_CENTER;
    glw_signal_handler_int(w, glw_bitmap_callback);
    gb->gb_alpha_self = 1;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_TEXTURE_COORDS:
      gb->gb_border_scaling = 1;
      gb->gb_tex_left   = va_arg(ap, double);
      gb->gb_tex_top    = va_arg(ap, double);
      gb->gb_tex_right  = va_arg(ap, double);
      gb->gb_tex_bottom = va_arg(ap, double);
      gb->gb_render_init = 1;
      break;

    case GLW_ATTRIB_ANGLE:
      gb->gb_angle = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ALPHA_SELF:
      gb->gb_alpha_self = va_arg(ap, double);
      break;
      
    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      gb->gb_render_init = 1;
      break;

    case GLW_ATTRIB_MIRROR:
      gb->gb_mirror = va_arg(ap, int);
      gb->gb_render_init = 1;
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);


  if(filename == NULL)
    return;

  if(gb->gb_tex != NULL)
    glw_tex_deref(w->glw_root, gb->gb_tex);

  gb->gb_tex = glw_tex_create(w->glw_root, filename);
}


const char *
glw_bitmap_get_filename(glw_t *w) {
  if (w->glw_class != GLW_BITMAP)
    return NULL;

  return (const char *)((glw_bitmap_t *)w)->gb_tex->gt_filename;
}
