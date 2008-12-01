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
#include <sys/mman.h>

#include <GL/gl.h>

#include "glw.h"
#include "glw_bitmap.h"
#include "glw_form.h"



static void
glw_bitmap_dtor(glw_t *w)
{
  glw_bitmap_t *gb = (void *)w;

  if(gb->gb_tex != NULL)
    glw_tex_deref(w->glw_root, gb->gb_tex);
}


static void
bitmap_render_tesselated(float wa, float ta, float vborders[4],
			 float tborders[4], int wireframe, int mirror,
			 float xfill)
{
  float tex[4][2];
  float vex[4][2];
  int x, y;

  vex[0][0] = -1.0;
  vex[3][0] =  1.0 - (1 - xfill) * 2;
  vex[1][0] = (vex[0][0] + 1 / GLW_MAX(wa, 1.0f) * vborders[0]);
  vex[2][0] = (vex[3][0] - 1 / GLW_MAX(wa, 1.0f) * vborders[1]);

  vex[0][1] =  1.0;
  vex[3][1] = -1.0;
  vex[1][1] = (vex[0][1] - GLW_MIN(wa, 1.0f) * vborders[2]);
  vex[2][1] = (vex[3][1] + GLW_MIN(wa, 1.0f) * vborders[3]);

  tex[0][0] = 0.0;
  tex[3][0] = 1.0;
  tex[1][0] = (0.0 + 1 / GLW_MAX(ta, 1.0f) * tborders[0] * 0.5);
  tex[2][0] = (1.0 - 1 / GLW_MAX(ta, 1.0f) * tborders[1] * 0.5);

  tex[0][1] = 0.0;
  tex[3][1] = 1.0;
  tex[1][1] = (0.0 +     GLW_MIN(ta, 1.0f) * tborders[2] * 0.5);
  tex[2][1] = (1.0 -     GLW_MIN(ta, 1.0f) * tborders[3] * 0.5);

  if(mirror & GLW_MIRROR_X)
    for(x = 0; x < 4; x++)
      tex[x][0] = 1.0f - tex[x][0];

  if(mirror & GLW_MIRROR_Y)
    for(y = 0; y < 4; y++)
      tex[y][1] = 1.0f - tex[y][1];

  glBegin(GL_QUADS);

  /* XXX: replace with drawarray */

  for(y = 0; y < 3; y++) {
    for(x = 0; x < 3; x++) {

      glTexCoord2f(tex[x + 0][0], tex[y + 1][1]);
      glVertex3f  (vex[x + 0][0], vex[y + 1][1], 0.0f);

      glTexCoord2f(tex[x + 1][0], tex[y + 1][1]);
      glVertex3f  (vex[x + 1][0], vex[y + 1][1], 0.0f);

      glTexCoord2f(tex[x + 1][0], tex[y + 0][1]);
      glVertex3f  (vex[x + 1][0], vex[y + 0][1], 0.0f);

      glTexCoord2f(tex[x + 0][0], tex[y + 0][1]);
      glVertex3f  (vex[x + 0][0], vex[y + 0][1], 0.0f);
    }
  }

  glEnd();

  if(!wireframe)
    return;

   glDisable(GL_TEXTURE_2D);

  for(y = 0; y < 3; y++) {
    for(x = 0; x < 3; x++) {

      glBegin(GL_LINE_LOOP);
      glVertex3f  (vex[x + 0][0], vex[y + 1][1], 0.0f);
      glVertex3f  (vex[x + 1][0], vex[y + 1][1], 0.0f);
      glVertex3f  (vex[x + 1][0], vex[y + 0][1], 0.0f);
      glVertex3f  (vex[x + 0][0], vex[y + 0][1], 0.0f);
      glEnd();
    }
  }
}


static float border_alpha[4][4] = {
  {0.0, 0.0, 0.0, 0.0},
  {0.0, 1.0, 1.0, 0.0},
  {0.0, 1.0, 1.0, 0.0},
  {0.0, 0.0, 0.0, 0.0}
};


static void
bitmap_render_border_blended(float wa, float alpha, float vborders[4],
			 float tborders[4])
{
  float tex[4][2];
  float vex[4][2];
  int x, y;

  vex[0][1] =  1.0;
  vex[3][1] = -1.0;

  vex[1][1] = vex[0][1] - vborders[2];
  vex[2][1] = vex[3][1] + vborders[3];

  tex[0][1] = 0.0;
  tex[3][1] = 1.0;

  tex[1][1] = tex[0][1] + vborders[2] * 0.5;
  tex[2][1] = tex[3][1] - vborders[3] * 0.5;
  
  vex[0][0] = -1.0;
  vex[3][0] =  1.0;
  vex[1][0] = vex[0][0] + (tborders[0] / wa);
  vex[2][0] = vex[3][0] - (tborders[0] / wa);

  tex[0][0] = 0.0;
  tex[3][0] = 1.0;
  tex[1][0] = tex[0][0] + (tborders[1] * 0.5) / wa;
  tex[2][0] = tex[3][0] - (tborders[1] * 0.5) / wa;

  glBegin(GL_TRIANGLES);

  /* XXX: replace with drawarray */

  for(y = 0; y < 3; y++) {
    for(x = 0; x < 3; x++) {

      if((x == 0 && y == 0) || (x == 2 && y == 2)) {

	glTexCoord2f(tex[x + 0][0], tex[y + 1][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 0][y + 1] * alpha);
	glVertex3f  (vex[x + 0][0], vex[y + 1][1], 0.0f);

	glTexCoord2f(tex[x + 1][0], tex[y + 1][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 1][y + 1] * alpha);
	glVertex3f  (vex[x + 1][0], vex[y + 1][1], 0.0f);

	glTexCoord2f(tex[x + 0][0], tex[y + 0][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 0][y + 0] * alpha);
	glVertex3f  (vex[x + 0][0], vex[y + 0][1], 0.0f);



	glTexCoord2f(tex[x + 0][0], tex[y + 0][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 0][y + 0] * alpha);
	glVertex3f  (vex[x + 0][0], vex[y + 0][1], 0.0f);

	glTexCoord2f(tex[x + 1][0], tex[y + 1][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 1][y + 1] * alpha);
	glVertex3f  (vex[x + 1][0], vex[y + 1][1], 0.0f);


	glTexCoord2f(tex[x + 1][0], tex[y + 0][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 1][y + 0] * alpha);
	glVertex3f  (vex[x + 1][0], vex[y + 0][1], 0.0f);

      } else {

	glTexCoord2f(tex[x + 0][0], tex[y + 1][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 0][y + 1] * alpha);
	glVertex3f  (vex[x + 0][0], vex[y + 1][1], 0.0f);

	glTexCoord2f(tex[x + 1][0], tex[y + 1][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 1][y + 1] * alpha);
	glVertex3f  (vex[x + 1][0], vex[y + 1][1], 0.0f);

	glTexCoord2f(tex[x + 1][0], tex[y + 0][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 1][y + 0] * alpha);
	glVertex3f  (vex[x + 1][0], vex[y + 0][1], 0.0f);

	glTexCoord2f(tex[x + 0][0], tex[y + 1][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 0][y + 1] * alpha);
	glVertex3f  (vex[x + 0][0], vex[y + 1][1], 0.0f);

	glTexCoord2f(tex[x + 1][0], tex[y + 0][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 1][y + 0] * alpha);
	glVertex3f  (vex[x + 1][0], vex[y + 0][1], 0.0f);

	glTexCoord2f(tex[x + 0][0], tex[y + 0][1]);
	glColor4f(1.0f, 1.0f, 1.0f, border_alpha[x + 0][y + 0] * alpha);
	glVertex3f  (vex[x + 0][0], vex[y + 0][1], 0.0f);
      }
    }
  }

  glEnd();

}




static void 
glw_bitmap_render(glw_t *w, glw_rctx_t *rc)
{
  glw_bitmap_t *gb = (void *)w;
  glw_texture_t *gt = gb->gb_tex;
  float a = rc->rc_alpha * w->glw_alpha * glw_form_alpha_get(w);
  glw_rctx_t rc0;
  glw_t *c;
  int pop = 0;

 
  if(gt != NULL && gt->gt_state == GT_STATE_VALID && a > 0.01) {

    glActiveTextureARB(GL_TEXTURE0_ARB);

    glColor4f(w->glw_col.r, w->glw_col.g, w->glw_col.b, a);
    
    glPushMatrix();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gt->gt_texture);

    if(!(w->glw_flags & GLW_KEEP_ASPECT)) {

      if(w->glw_flags & GLW_FOCUS_DRAW_CURSOR && rc->rc_focused)
	glw_form_cursor_set(rc);

      bitmap_render_tesselated(rc->rc_aspect, gt->gt_aspect, 
			       gb->gb_vborders, gb->gb_tborders,
			       gb->gb_head.glw_flags & GLW_DRAW_SKEL,
			       gb->gb_mirror, gb->gb_xfill);

    } else {

      switch(w->glw_alignment) {
      case GLW_ALIGN_CENTER:
	break;
      case GLW_ALIGN_DEFAULT:
      case GLW_ALIGN_LEFT:
	glTranslatef(-1.0, 0.0, 0.0f);
	break;
      case GLW_ALIGN_RIGHT:
	glTranslatef(1.0, 0.0, 0.0f);
	break;
      case GLW_ALIGN_BOTTOM:
	glTranslatef(0.0, -1.0, 0.0f);
	break;
      case GLW_ALIGN_TOP:
	glTranslatef(0.0, 1.0, 0.0f);
	break;
      }
      
      glw_rescale(rc->rc_aspect, gt->gt_aspect);

      if(gb->gb_angle != 0)
	glRotatef(-gb->gb_angle, 0, 0, 1);

      switch(w->glw_alignment) {
      case GLW_ALIGN_CENTER:
	break;
      case GLW_ALIGN_DEFAULT:
      case GLW_ALIGN_LEFT:
	glTranslatef(1.0f, 0.0f, 0.0f);
	break;
      case GLW_ALIGN_RIGHT:
	glTranslatef(-1.0f, 0.0f, 0.0f);
	break;
      case GLW_ALIGN_BOTTOM:
	glTranslatef(0.0, 1.0, 0.0f);
	break;
      case GLW_ALIGN_TOP:
	glTranslatef(0.0, -1.0, 0.0f);
	break;
      }

      if(w->glw_flags & GLW_FOCUS_DRAW_CURSOR && rc->rc_focused)
	glw_form_cursor_set(rc);
      

      if(w->glw_flags & GLW_BORDER_BLEND) {

	bitmap_render_border_blended(gt->gt_aspect, a, 
				     gb->gb_vborders, gb->gb_tborders);

      } else {

	glBegin(GL_QUADS);

	glTexCoord2f(0.0, 1.0);
	glVertex3f( -1.0, -1.0f, 0.0f);

	glTexCoord2f(1.0, 1.0);
	glVertex3f( 1.0, -1.0f, 0.0f);

	glTexCoord2f(1.0, 0.0);
	glVertex3f( 1.0, 1.0f, 0.0f);

	glTexCoord2f(0.0, 0.0);
	glVertex3f( -1.0, 1.0f, 0.0f);

	glEnd();
      }

      if(gb->gb_head.glw_flags & GLW_DRAW_SKEL) {
 	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_LINE_LOOP);
	glVertex3f( -1.0, -1.0f, 0.0f);
	glVertex3f( 1.0, -1.0f, 0.0f);
	glVertex3f( 1.0, 1.0f, 0.0f);
	glVertex3f( -1.0, 1.0f, 0.0f);
	glEnd();
 	glEnable(GL_BLEND);
      }
    }

    glPopMatrix();
  }

  c = TAILQ_FIRST(&w->glw_childs);
  if(c != NULL) {
    rc0 = *rc;

    if(w->glw_flags & GLW_BORDER_SCALE_CHILDS && 
       !(w->glw_flags & GLW_KEEP_ASPECT)) {

      float x1, x2, y1, y2, xs, ys;
      
      pop = 1;
      glPushMatrix();
      
      x1 = -1.0 + 1 / GLW_MAX(rc->rc_aspect, 1.0f) * gb->gb_vborders[0];
      x2 =  1.0 - 1 / GLW_MAX(rc->rc_aspect, 1.0f) * gb->gb_vborders[1];

      y1 =  1.0 - GLW_MIN(rc->rc_aspect, 1.0f)     * gb->gb_vborders[2];
      y2 = -1.0 + GLW_MIN(rc->rc_aspect, 1.0f)     * gb->gb_vborders[3];

      xs = (x2 - x1) * 0.5f;
      ys = (y1 - y2) * 0.5f;

      glScalef(xs, ys, 1.0f);

      rc0.rc_aspect *= xs / ys;
    }
    rc0.rc_alpha = rc->rc_alpha * glw_form_alpha_get(w);

    glw_render0(c, &rc0);

    if(pop)
      glPopMatrix();
  }
}





static void 
glw_bitmap_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_bitmap_t *gb = (void *)w;
  glw_texture_t *gt = gb->gb_tex;
  glw_t *c;

  glw_form_alpha_update(w, rc);

  if(gt != NULL)
    glw_tex_layout(w->glw_root, gt);

  c = TAILQ_FIRST(&w->glw_childs);


  if(c != NULL) {
    if(glw_is_focus_candidate(c))
      w->glw_focused = c;
    glw_layout0(c, rc);
  }
}



/*
 *
 */
static int
glw_bitmap_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
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
    return glw_navigate(w, extra);
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
    gb->gb_xfill = 1.0f;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_VERTEX_BORDERS:
      gb->gb_vborders[0] = va_arg(ap, double);
      gb->gb_vborders[1] = va_arg(ap, double);
      gb->gb_vborders[2] = va_arg(ap, double);
      gb->gb_vborders[3] = va_arg(ap, double);
      break;

    case GLW_ATTRIB_TEXTURE_BORDERS:
      gb->gb_tborders[0] = va_arg(ap, double);
      gb->gb_tborders[1] = va_arg(ap, double);
      gb->gb_tborders[2] = va_arg(ap, double);
      gb->gb_tborders[3] = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ANGLE:
      gb->gb_angle = va_arg(ap, double);
      break;
      
    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;

    case GLW_ATTRIB_XFILL:
      gb->gb_xfill = va_arg(ap, double);
      gb->gb_xfill = GLW_MAX(0, GLW_MIN(gb->gb_xfill, 1));
      break;

    case GLW_ATTRIB_MIRROR:
      gb->gb_mirror = va_arg(ap, int);
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
