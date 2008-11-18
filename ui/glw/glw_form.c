/*
 *  GL Widgets, Forms
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <GL/gl.h>

#include "glw.h"
#include "glw_bitmap.h"
#include "glw_form.h"

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



static float cursor_tex[5] = {
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
  glw_texture_t *gt;

  if(gr->gr_cursor_gt == NULL)
    gr->gr_cursor_gt = glw_tex_create(gr, "theme://images/cursor.png");

  gt = gr->gr_cursor_gt;

  glw_tex_layout(gr, gt);


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
glw_cursor_draw(glw_root_t *gr, float alpha, float xscale, float yscale)
{
  glw_texture_t *gt = gr->gr_cursor_gt;
  float vex[5][2];
  int x, y;
  float v;

  if(gt == NULL || gt->gt_texture == 0)
    return;

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


  alpha = alpha * 0.75;
  
  glActiveTextureARB(GL_TEXTURE0_ARB);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, gt->gt_texture);

 glBegin(GL_QUADS);

  /* XXX: replace with drawarray */

  for(y = 0; y < 4; y++) {
    for(x = 0; x < 4; x++) {

      if(x > 0 && x < 3 && y > 0 && y < 3)
	continue;
      
      glColor4f(cursor_red  [y + 1][x + 0], 
		cursor_green[y + 1][x + 0], 
		cursor_blue [y + 1][x + 0], 
		cursor_alpha[y + 1][x + 0] * alpha);
      glTexCoord2f(cursor_tex[x + 0], cursor_tex[y + 1]);
      glVertex3f  (vex[x + 0][0], vex[y + 1][1], 0.0f);

      glColor4f(cursor_red  [y + 1][x + 1], 
		cursor_green[y + 1][x + 1], 
		cursor_blue [y + 1][x + 1], 
		cursor_alpha[y + 1][x + 1] * alpha);
      glTexCoord2f(cursor_tex[x + 1], cursor_tex[y + 1]);
      glVertex3f  (vex[x + 1][0], vex[y + 1][1], 0.0f);

      glColor4f(cursor_red  [y + 0][x + 1], 
		cursor_green[y + 0][x + 1], 
		cursor_blue [y + 0][x + 1], 
		cursor_alpha[y + 0][x + 1] * alpha);
      glTexCoord2f(cursor_tex[x + 1], cursor_tex[y + 0]);
      glVertex3f  (vex[x + 1][0], vex[y + 0][1], 0.0f);

      glColor4f(cursor_red  [y + 0][x + 0], 
		cursor_green[y + 0][x + 0], 
		cursor_blue [y + 0][x + 0], 
		cursor_alpha[y + 0][x + 0] * alpha);
      glTexCoord2f(cursor_tex[x + 0], cursor_tex[y + 0]);
      glVertex3f  (vex[x + 0][0], vex[y + 0][1], 0.0f);
    }
  }

  glEnd();
  glDisable(GL_TEXTURE_2D);
}


/**
 *
 */
static void
gff_render(glw_root_t *gr, glw_form_focus_t *gff, float aspect)
{
  int i;
  float xs, ys;
  //  float x, y;

  for(i = 0; i < 16; i++)
    gff->gff_m_prim[i] = GLW_LP(5, gff->gff_m_prim[i], gff->gff_m[i]);

  gff->gff_alpha_prim  = GLW_LP(5, gff->gff_alpha_prim, gff->gff_alpha);
  gff->gff_aspect_prim = GLW_LP(5, gff->gff_aspect_prim, gff->gff_aspect);

  glPushMatrix();
  glLoadMatrixf(gff->gff_m_prim);

  aspect = 1.0;;

  if(aspect > 0) {
    xs = 1 / aspect;
    ys = 1;
  } else {
    xs = 1;
    ys = aspect;
  }


  glw_cursor_draw(gr, gff->gff_alpha_prim, 
		  xs / (100 * fabs(gff->gff_m_prim[0])),
		  ys / (100 * fabs(gff->gff_m_prim[5]))
		  );
  glPopMatrix();
}



/**
 *
 */
void
glw_form_alpha_update(glw_t *w, glw_rctx_t *rc)
{
  w->glw_focus = GLW_LP(8, w->glw_focus, rc->rc_focused ? 1 : 0);
}


/**
 *
 */
float
glw_form_alpha_get(glw_t *w)
{
  if(w->glw_flags & GLW_FOCUS_ADJ_ALPHA)
    return w->glw_focus * 0.5 + 0.5;
  return 1.0;
}



/**
 *
 */
void
glw_form_cursor_set(glw_rctx_t *rc)
{
  glw_form_t *gf = rc->rc_form;
  glw_form_focus_t *gff;

  if(gf == NULL)
    return;

  gff = gf->gf_current_focus;
  assert(gff != NULL);

  glGetFloatv(GL_MODELVIEW_MATRIX, gff->gff_m);
  gff->gff_alpha  = rc->rc_alpha;
  gff->gff_aspect = rc->rc_aspect;
}


/**
 *
 */
static int
glw_form_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  glw_form_t *gf = (void *)w;
  glw_form_focus_t *gff;
  glw_rctx_t *rc;
  glw_root_t *gr = w->glw_root;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_DTOR:
    while((gff = TAILQ_FIRST(&gf->gf_focuses)) != NULL) {
      TAILQ_REMOVE(&gf->gf_focuses, gff, gff_link);
      free(gff);
    }
    break;

  case GLW_SIGNAL_LAYOUT:
    rc = extra;
    gf->gf_current_focus = TAILQ_FIRST(&gf->gf_focuses);

    if(c != NULL)
      glw_layout0(c, rc);

    break;

  case GLW_SIGNAL_RENDER:
    rc = extra;

    if(rc->rc_form != NULL && rc->rc_focused && rc->rc_form->gf_current_focus)
      rc->rc_form->gf_current_focus->gff_alpha = 0;

    if((gff = gf->gf_current_focus) == NULL) {
      gff = calloc(1, sizeof(glw_form_focus_t));
      TAILQ_INSERT_TAIL(&gf->gf_focuses, gff, gff_link);
      gf->gf_current_focus = gff;
    }

    rc->rc_form = gf;

    gff->gff_alpha = 0;

    if(c != NULL)
      glw_render0(c, rc);

    gff_render(gr, gff, rc->rc_aspect);

    gf->gf_current_focus = TAILQ_NEXT(gff, gff_link);
    break;

  case GLW_SIGNAL_EVENT:
    if(c != NULL)
      return glw_signal0(c, GLW_SIGNAL_EVENT, extra);
    break;
  }
  return 0;

}



/**
 *
 */
void 
glw_form_ctor(glw_t *w, int init, va_list ap)
{
  glw_form_t *gf = (void *)w;

  if(init) {
    w->glw_flags |= GLW_SELECTABLE;
    TAILQ_INIT(&gf->gf_focuses);
    glw_signal_handler_int(w, glw_form_callback);
  }
}

