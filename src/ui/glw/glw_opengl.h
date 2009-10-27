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

#define GL_GLEXT_PROTOTYPES
#define GLX_GLXEXT_PROTOTYPES


#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif


struct glw_root;
struct glw_rctx;

typedef struct glw_backend_root {

  int gbr_sysfeatures;
#define GLW_OPENGL_PBO       0x1
#define GLW_OPENGL_FRAG_PROG 0x2

  enum {
    GLW_OPENGL_TEXTURE_SIMPLE,
    GLW_OPENGL_TEXTURE_RECTANGLE,
    GLW_OPENGL_TEXTURE_NPOT
  } gbr_texmode;

  int gbr_primary_texture_mode; // GL_TEXTURE_2D or GL_TEXTURE_RECTANGLE_EXT

  
  /**
   * Video decoder and renderer
   */
  GLuint gbr_yuv2rbg_prog;
  GLuint gbr_yuv2rbg_2mix_prog;
  struct glw_video_list gbr_video_decoders;

} glw_backend_root_t;


typedef struct {
  int gbr_active_clippers; // Used by glw_clip()

} glw_backend_rctx_t;


typedef GLuint glw_backend_texture_t;

#define glw_PushMatrix(newrc, oldrc) glPushMatrix()

#define glw_PopMatrix() glPopMatrix()

#define glw_Translatef(rc, x, y, z) glTranslatef(x, y, z)

#define glw_Scalef(rc, x, y, z) glScalef(x, y, z)

#define glw_Rotatef(rc, a, x, y, z) glRotatef(a, x, y, z)

#define glw_LoadMatrixf(rc, src) glLoadMatrixf(src)

/**
 * Renderer
 */
typedef struct glw_renderer {
  int gr_vertices;
  float *gr_buffer;
  int gr_stride;
} glw_renderer_t;

#define GLW_RENDER_MODE_QUADS      GL_QUADS
#define GLW_RENDER_MODE_LINE_STRIP GL_LINE_STRIP
#define GLW_RENDER_MODE_LINE_LOOP  GL_LINE_LOOP

#define glw_render_set_pre(gr)

#define glw_render_set_post(gr)

#define glw_can_tnpo2(gr) (gr->gr_be.gbr_texmode != GLW_OPENGL_TEXTURE_SIMPLE)

#define glw_is_tex_inited(n) (*(n) != 0)

int glw_opengl_init_context(struct glw_root *gr);

/**
 * Render to texture support
 */
typedef struct {

  GLuint grtt_framebuffer;
  glw_backend_texture_t grtt_texture;
  
  int grtt_width;
  int grtt_height;

  GLint grtt_viewport[4];  // Saved viewport

} glw_rtt_t;

void glw_rtt_init(struct glw_root *gr, glw_rtt_t *grtt, int width, int height,
		  int alpha);

void glw_rtt_enter(struct glw_root *gr, glw_rtt_t *grtt, struct glw_rctx *rc0);

void glw_rtt_restore(struct glw_root *gr, glw_rtt_t *grtt);

void glw_rtt_destroy(struct glw_root *gr, glw_rtt_t *grtt);

#define glw_rtt_texture(grtt) ((grtt)->grtt_texture)


/**
 *
 */

#define GLW_BLEND_ADDITIVE GL_SRC_ALPHA, GL_ONE
#define GLW_BLEND_NORMAL   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA

#define glw_blendmode(m) glBlendFunc(m)

#endif /* GLW_OPENGL_H__ */
