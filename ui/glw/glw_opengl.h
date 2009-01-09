/*
 *  GL Widgets, common stuff
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

#ifndef GLW_OPENGL_H__
#define GLW_OPENGL_H__

#include <GL/gl.h>
#include <GL/glu.h>

typedef struct glw_backend_root {

  int gbr_sysfeatures;
#define GLW_OPENGL_PBO       0x1
#define GLW_OPENGL_VBO       0x2
#define GLW_OPENGL_FRAG_PROG 0x4
#define GLW_OPENGL_TNPO2     0x8
  
  /**
   * Video decoder and renderer
   */
  GLuint gbr_yuv2rbg_prog;
  GLuint gbr_yuv2rbg_2mix_prog;
  struct glw_video_list gbr_video_decoders;

} glw_backend_root_t;

typedef GLuint glw_backend_texture_t;

#define glw_PushMatrix(newrc, oldrc) glPushMatrix()

#define glw_PopMatrix() glPopMatrix()

#define glw_Translatef(rc, x, y, z) glTranslatef(x, y, z)

#define glw_Scalef(rc, x, y, z) glScalef(x, y, z)

#define glw_Rotatef(rc, a, x, y, z) glRotatef(a, x, y, z)


/**
 * Renderer
 */
typedef struct glw_renderer {
  int gr_vertices;
  float *gr_buffer;
  int gr_stride;
} glw_renderer_t;

#define GLW_RENDER_MODE_QUADS     GL_QUADS
#define GLW_RENDER_MODE_LINESTRIP GL_LINESTRIP

#define glw_render_set_pre(gr)

#define glw_render_set_post(gr)


#endif /* GLW_OPENGL_H__ */
