/*
 *  GL Widgets, OpenGL rendering
 *  Copyright (C) 2009 Andreas Öman
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

/**
 * 
 */
void
glw_render_init(glw_renderer_t *gr, int vertices, int attribs)
{
  gr->gr_stride = 3 + 
    (attribs >= GLW_RENDER_ATTRIBS_TEX       ? 2 : 0) +
    (attribs >= GLW_RENDER_ATTRIBS_TEX_COLOR ? 4 : 0);
    
  gr->gr_buffer = malloc(sizeof(float) * gr->gr_stride * vertices);
  gr->gr_vertices = vertices;
}


/**
 * 
 */
void
glw_render_free(glw_renderer_t *gr)
{
  free(gr->gr_buffer);
}


/**
 * 
 */
void
glw_render_vtx_pos(glw_renderer_t *gr, int vertex,
		   float x, float y, float z)
{
  gr->gr_buffer[vertex * gr->gr_stride + 0] = x;
  gr->gr_buffer[vertex * gr->gr_stride + 1] = y;
  gr->gr_buffer[vertex * gr->gr_stride + 2] = z;
}

/**
 * 
 */
void
glw_render_vtx_st(glw_renderer_t *gr, int vertex,
		  float s, float t)
{
  gr->gr_buffer[vertex * gr->gr_stride + 3] = s;
  gr->gr_buffer[vertex * gr->gr_stride + 4] = t;
}

/**
 * 
 */
void
glw_render_vts_col(glw_renderer_t *gr, int vertex,
		   float r, float g, float b, float a)
{
  gr->gr_buffer[vertex * gr->gr_stride + 5] = r;
  gr->gr_buffer[vertex * gr->gr_stride + 6] = g;
  gr->gr_buffer[vertex * gr->gr_stride + 7] = b;
  gr->gr_buffer[vertex * gr->gr_stride + 8] = a;
}



/**
 * 
 */
void
glw_render(glw_renderer_t *gr, glw_rctx_t *rc, int mode, int attribs,
	   glw_backend_texture_t *be_tex,
	   float r, float g, float b, float a)
{
  int i;
  float *buf = gr->gr_buffer;
  
  switch(attribs) {
  case GLW_RENDER_ATTRIBS_NONE:

    glDisable(GL_TEXTURE_2D);

    glColor4f(r, g, b, a);
    glBegin(mode);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      glVertex3f(buf[0], buf[1], buf[2]);
      buf += gr->gr_stride;
    }
    glEnd();

    glEnable(GL_TEXTURE_2D);
    break;

  case GLW_RENDER_ATTRIBS_TEX:
    glBindTexture(GL_TEXTURE_2D, *be_tex);
  
    glColor4f(r, g, b, a);
    glBegin(mode);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      glTexCoord2f(buf[3], buf[4]);
      glVertex3f(buf[0], buf[1], buf[2]);
      buf += gr->gr_stride;
    }
    glEnd();
    break;

  case GLW_RENDER_ATTRIBS_TEX_COLOR:
    glBindTexture(GL_TEXTURE_2D, *be_tex);
  
    glBegin(mode);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      glColor4f(buf[5], buf[6], buf[7], buf[8]);
      glTexCoord2f(buf[3], buf[4]);
      glVertex3f(buf[0], buf[1], buf[2]);
      buf += gr->gr_stride;
    }
    glEnd();
    break;
  }
}
