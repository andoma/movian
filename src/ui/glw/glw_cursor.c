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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "glw.h"
#include "glw_texture.h"
#include "glw_cursor.h"
#include "glw_event.h"




static float cursor_alpha[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};


static float cursor_red[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};

static float cursor_green[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};

static float cursor_blue[5][5] = {
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};


static const float cursor_tex[5] = {
  0.0f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f
};


static void
set_cursor_col(int y, int x, float r)
{
  cursor_red[y][x] =   GLW_MIN(0.6 + r, 1.0f);
  cursor_green[y][x] = GLW_MIN(0.8 + r, 1.0f);
}

void
glw_cursor_layout_frame(glw_root_t *gr)
{
  static float v;
  float r;

#define F(v) (0.5 + 0.5 * (v))

  r = F(sin(GLW_DEG2RAD(v + 0)));
  set_cursor_col(0, 0, r);
  set_cursor_col(0, 1, r);
  set_cursor_col(1, 0, r);
  set_cursor_col(1, 1, r);

  r = F(sin(GLW_DEG2RAD(v + 45)));
  set_cursor_col(0, 2, r);
  set_cursor_col(1, 2, r);
  
  r = F(sin(GLW_DEG2RAD(v + 90)));
  set_cursor_col(0, 3, r);
  set_cursor_col(0, 4, r);
  set_cursor_col(1, 3, r);
  set_cursor_col(1, 4, r);

  r = F(sin(GLW_DEG2RAD(v + 135)));
  set_cursor_col(2, 3, r);
  set_cursor_col(2, 4, r);

  r = F(sin(GLW_DEG2RAD(v + 180)));
  set_cursor_col(3, 3, r);
  set_cursor_col(3, 4, r);
  set_cursor_col(4, 3, r);
  set_cursor_col(4, 4, r);

  r = F(sin(GLW_DEG2RAD(v + 225)));
  set_cursor_col(3, 2, r);
  set_cursor_col(4, 2, r);

  r = F(sin(GLW_DEG2RAD(v + 270)));
  set_cursor_col(3, 0, r);
  set_cursor_col(3, 1, r);
  set_cursor_col(4, 0, r);
  set_cursor_col(4, 1, r);

  r = F(sin(GLW_DEG2RAD(v + 315)));
  set_cursor_col(2, 0, r);
  set_cursor_col(2, 1, r);

#undef F

  v+=1;


}



/**
 *
 */
static void
glw_cursor_draw(glw_root_t *gr, glw_cursor_painter_t *gcp, 
		glw_rctx_t *rc, float xscale, float yscale,
		glw_loadable_texture_t *glt)
{
  float vex[5][2];
  int x, y, i = 0;
  float v, alpha;
  glw_renderer_t *r = &gcp->gcp_renderer;

  v = yscale;

  vex[0][1] =  1.0f + v * 0.5;
  vex[1][1] =  1.0f - v * 0.5;
  vex[2][1] =  0.0f;
  vex[3][1] = -1.0f + v * 0.5;
  vex[4][1] = -1.0f - v * 0.5;

  v = xscale;
  
  vex[0][0] = -1.0f - v * 0.5;
  vex[1][0] = -1.0f + v * 0.5;
  vex[2][0] =  0.0f;
  vex[3][0] =  1.0f - v * 0.5;
  vex[4][0] =  1.0f + v * 0.5;
  
  alpha = rc->rc_alpha * gcp->gcp_alpha_prim;

  glw_render_set_pre(r);

  for(y = 0; y < 4; y++) {
    for(x = 0; x < 4; x++) {

      if(x > 0 && x < 3 && y > 0 && y < 3)
	continue;

      glw_render_vts_col(r, i,
			 cursor_red  [y + 1][x + 0], 
			 cursor_green[y + 1][x + 0], 
			 cursor_blue [y + 1][x + 0], 
			 cursor_alpha[y + 1][x + 0] * alpha);
      glw_render_vtx_pos(r, i++,
			 vex[x + 0][0], vex[y + 1][1], 0.0f);

      glw_render_vts_col(r, i,
			 cursor_red  [y + 1][x + 1], 
			 cursor_green[y + 1][x + 1], 
			 cursor_blue [y + 1][x + 1], 
			 cursor_alpha[y + 1][x + 1] * alpha);

      glw_render_vtx_pos(r, i++,
			 vex[x + 1][0], vex[y + 1][1], 0.0f);

      glw_render_vts_col(r, i,
			 cursor_red  [y + 0][x + 1], 
			 cursor_green[y + 0][x + 1], 
			 cursor_blue [y + 0][x + 1], 
			 cursor_alpha[y + 0][x + 1] * alpha);
      glw_render_vtx_pos(r, i++,
			 vex[x + 1][0], vex[y + 0][1], 0.0f);
     
      glw_render_vts_col(r, i,
			 cursor_red  [y + 0][x + 0], 
			 cursor_green[y + 0][x + 0], 
			 cursor_blue [y + 0][x + 0], 
			 cursor_alpha[y + 0][x + 0] * alpha);

      glw_render_vtx_pos(r, i++,
			 vex[x + 0][0], vex[y + 0][1], 0.0f);
    }
  }
  glw_render_set_post(r);

  glw_render(r, gr, rc, 
	     GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX_COLOR,
	     &glt->glt_texture, 1, 1, 1, 1);
}


/**
 *
 */
static void
gcp_render(glw_root_t *gr, glw_cursor_painter_t *gcp, glw_rctx_t *rc,
	   glw_loadable_texture_t *glt)
{
  int i;
  float size_x = rc->rc_size_x;
  float size_y = rc->rc_size_y;

  glw_rctx_t rc0 = *rc;

  for(i = 0; i < 16; i++)
    gcp->gcp_m_prim[i] = GLW_LP(5, gcp->gcp_m_prim[i], gcp->gcp_m[i]);

  gcp->gcp_alpha_prim  = GLW_LP(5, gcp->gcp_alpha_prim, gcp->gcp_alpha);

  glw_PushMatrix(&rc0, rc);

  glw_LoadMatrixf(&rc0, gcp->gcp_m_prim);

  glw_cursor_draw(gr, gcp, &rc0,
		  10 / size_x / fabs(gcp->gcp_m_prim[0]),
		  10 / size_y / fabs(gcp->gcp_m_prim[5]),
		  glt);

  glw_PopMatrix();
}

/**
 *
 */
static void
gcp_setup_renderer(glw_cursor_painter_t *gcp)
{
  int x, y, v = 0;
  glw_renderer_t *r = &gcp->gcp_renderer;

  gcp->gcp_renderer_inited = 1;

  glw_render_init(r, 48, GLW_RENDER_ATTRIBS_TEX_COLOR);

  glw_render_set_pre(r);

  for(y = 0; y < 4; y++) {
    for(x = 0; x < 4; x++) {

      if(x > 0 && x < 3 && y > 0 && y < 3)
	continue;

      glw_render_vtx_st(r, v++, cursor_tex[x + 0], cursor_tex[y + 1]);
      glw_render_vtx_st(r, v++, cursor_tex[x + 1], cursor_tex[y + 1]);
      glw_render_vtx_st(r, v++, cursor_tex[x + 1], cursor_tex[y + 0]);
      glw_render_vtx_st(r, v++, cursor_tex[x + 0], cursor_tex[y + 0]);
    }
  }
  glw_render_set_post(r);
}




/**
 *
 */
static int
glw_cursor_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  glw_cursor_t *gf = (void *)w;
  glw_rctx_t *rc;
  glw_root_t *gr = w->glw_root;
  glw_cursor_painter_t *gcp = &gf->gcp;
  glw_loadable_texture_t *glt = gf->tex;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_DTOR:
    if(gcp->gcp_renderer_inited)
      glw_render_free(&gcp->gcp_renderer);
    break;

  case GLW_SIGNAL_LAYOUT:
    if(glt == NULL)
      break;

    glw_tex_layout(gr, gf->tex);

    gcp = &gf->gcp;
    gcp->gcp_alpha = 0;

    if(gcp->gcp_renderer_inited == 0)
      gcp_setup_renderer(gcp);

    gf->render_cycle = 0;

    if(c != NULL)
      glw_layout0(c, extra);
    break;

  case GLW_SIGNAL_RENDER:

    if(glt == NULL || !glw_is_tex_inited(&glt->glt_texture))
      break;

    rc = extra;

    if(gf->render_cycle == 0)
      rc->rc_cursor_painter = &gf->gcp;
    else
      rc->rc_cursor_painter = NULL;

    if(c != NULL)
      glw_render0(c, rc);

    if(gf->render_cycle == 0)
      gcp_render(gr, &gf->gcp, rc, glt);

    gf->render_cycle++;
    break;
  }
  return 0;
}



/**
 *
 */
void 
glw_cursor_ctor(glw_t *w, int init, va_list ap)
{
  glw_cursor_t *gc = (glw_cursor_t *)w;
  glw_root_t *gr = w->glw_root;
  glw_attribute_t attrib;
  const char *filename = NULL;

  if(init)
    glw_signal_handler_int(w, glw_cursor_callback);

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;
    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(filename == NULL)
    return;

  if(gc->tex != NULL)
    glw_tex_deref(gr, gc->tex);

  gc->tex = glw_tex_create(gr, filename);

}

