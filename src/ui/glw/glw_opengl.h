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
#include <OpenGL/gl.h>

#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER GL_PIXEL_UNPACK_BUFFER_ARB
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER GL_PIXEL_PACK_BUFFER_ARB
#endif

#else
#include <GL/gl.h>
#endif

#ifdef linux
#include <GL/glx.h>
#include <GL/glxext.h>
#endif

#define NUM_CLIPPLANES 6

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
   * Video renderer
   */
  GLuint gbr_yuv2rbg_1f_prog;
  GLint  gbr_yuv2rbg_1f_colormtx;
  GLint  gbr_yuv2rbg_1f_alpha;

  GLuint gbr_yuv2rbg_2f_prog;
  GLint  gbr_yuv2rbg_2f_colormtx;
  GLint  gbr_yuv2rbg_2f_alpha;
  GLint  gbr_yuv2rbg_2f_blend;

  /**
   *
   */
  struct vdpau_dev *gbr_vdpau_dev;
#ifdef linux
  PFNGLXBINDTEXIMAGEEXTPROC gbr_bind_tex_image;
  PFNGLXRELEASETEXIMAGEEXTPROC gbr_release_tex_image;
#endif
  int gbr_enable_vdpau;

  float gbr_clip[NUM_CLIPPLANES][4];
  int gbr_active_clippers;

  /**
   *
   */
  GLuint gbr_dp_shader;
  GLuint gbr_dp;
  GLuint gbr_dp_ucolor;

} glw_backend_root_t;


typedef struct {
  float gbr_mtx[16]; // ModelView matrix
} glw_backend_rctx_t;


typedef GLuint glw_backend_texture_t;


/**
 * Renderer tesselation cache
 */
typedef struct glw_renderer_tc {
  float grt_mtx[16]; // ModelView matrix
  int grt_active_clippers;
  float grt_clip[NUM_CLIPPLANES][4];

  float *grt_array; // Tesselated output [3+4+2] elements / vertex
  int grt_size;     // In triangles
  int grt_capacity; // In triangles
} glw_renderer_tc_t;

/**
 * Renderer
 */


typedef struct glw_renderer {
  uint16_t gr_vertices;
  uint16_t gr_triangles;
  char gr_static_indices;
  char gr_dirty;
  unsigned char gr_framecmp;
  unsigned char gr_cacheptr;

  float *gr_array;
  uint16_t *gr_indices;

#define GLW_RENDERER_CACHES 4

  glw_renderer_tc_t *gr_tc[GLW_RENDERER_CACHES];
  
} glw_renderer_t;


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

/**
 *
 */
#define GLW_CW  GL_CW
#define GLW_CCW GL_CCW

#define glw_frontface(x) glFrontFace(x) 


/**
 *
 */
GLuint glw_compile_shader(const char *url, int type);

GLuint glw_link_program(const char *title, GLuint vs, GLuint fs);


#endif /* GLW_OPENGL_H__ */
