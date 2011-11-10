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

#if ENABLE_GLW_BACKEND_OPENGL_ES

#include <GLES2/gl2.h>

#else

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

#endif


struct glw_rgb;
struct glw_rctx;
struct glw_root;
struct glw_backend_root;
struct glw_renderer;
struct glw_backend_texture;

/**
 * OpenGL shader program
 */
typedef struct glw_program {
  const char *gp_title;
  GLuint gp_program;

  // Attributes
  GLint  gp_attribute_position;
  GLint  gp_attribute_texcoord;
  GLint  gp_attribute_color;
 
  // Uniforms
  GLint  gp_uniform_modelview;
  GLint  gp_uniform_color;
  GLint  gp_uniform_colormtx;
  GLint  gp_uniform_blend;
  GLint  gp_uniform_color_offset;
  GLint  gp_uniform_blur_amount;

  GLint  gp_uniform_t[6];

} glw_program_t;




typedef struct glw_backend_root {

  enum {
    GLW_OPENGL_TEXTURE_SIMPLE,
    GLW_OPENGL_TEXTURE_RECTANGLE,
    GLW_OPENGL_TEXTURE_NPOT
  } gbr_texmode;

  int gbr_primary_texture_mode; // GL_TEXTURE_2D or GL_TEXTURE_RECTANGLE_EXT

  struct glw_program *gbr_current;

  
  /**
   * Video renderer
   */
  struct glw_program *gbr_yuv2rgb_1f;
  struct glw_program *gbr_yuv2rgb_2f;

  /**
   *
   */
  struct vdpau_dev *gbr_vdpau_dev;
#if ENABLE_GLW_FRONTEND_X11
  PFNGLXBINDTEXIMAGEEXTPROC gbr_bind_tex_image;
  PFNGLXRELEASETEXIMAGEEXTPROC gbr_release_tex_image;
#endif

  struct glw_program *gbr_renderer_tex;
  struct glw_program *gbr_renderer_tex_blur;
  struct glw_program *gbr_renderer_flat;

  int gbr_culling;

  int be_blendmode;

} glw_backend_root_t;


/**
 *
 */
typedef struct glw_backend_texture {
  GLuint tex;
  uint16_t width;
  uint16_t height;
  char type;
#define GLW_TEXTURE_TYPE_NORMAL   0
#define GLW_TEXTURE_TYPE_NO_ALPHA 1
} glw_backend_texture_t;



#define glw_can_tnpo2(gr) (gr->gr_be.gbr_texmode != GLW_OPENGL_TEXTURE_SIMPLE)

#define glw_is_tex_inited(n) ((n)->tex != 0)

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
GLuint glw_compile_shader(const char *url, int type);

glw_program_t *glw_make_program(glw_backend_root_t *gbr,
				const char *title, GLuint vs, GLuint fs);

void glw_load_program(glw_backend_root_t *gbr, glw_program_t *gp);

void glw_program_set_modelview(glw_backend_root_t *gbr, struct glw_rctx *rc);

void glw_program_set_uniform_color(glw_backend_root_t *gbr,
				   float r, float g, float b, float a);


int glw_opengl_ff_init(struct glw_root *gr);

int glw_opengl_shaders_init(struct glw_root *gr);

#endif /* GLW_OPENGL_H__ */
