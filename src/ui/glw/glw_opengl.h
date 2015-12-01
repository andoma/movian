/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#ifndef GLW_OPENGL_H__
#define GLW_OPENGL_H__

#if ENABLE_GLW_BACKEND_OPENGL_ES

#ifdef __APPLE__
#import <OpenGLES/ES2/glext.h>
#else
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

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
struct glw_program {
  char *gp_title;
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
  GLint  gp_uniform_blur;
  GLint  gp_uniform_time;
  GLint  gp_uniform_resolution;

  GLint  gp_uniform_t[6];
};




typedef struct glw_backend_root {

  struct glw_program *gbr_current;

  int gbr_use_stencil_buffer;

  /**
   * Video renderer
   */
  struct glw_program *gbr_yuv2rgb_1f;
  struct glw_program *gbr_yuv2rgb_2f;
  struct glw_program *gbr_rgb2rgb_1f;
  struct glw_program *gbr_rgb2rgb_2f;
  struct glw_program *gbr_yc2rgb_1f;
  struct glw_program *gbr_yc2rgb_2f;

  /**
   *
   */
  struct vdpau_dev *gbr_vdpau_dev;

  struct glw_program *gbr_renderer_tex;
  struct glw_program *gbr_renderer_tex_stencil;
  struct glw_program *gbr_renderer_tex_blur;
  struct glw_program *gbr_renderer_tex_stencil_blur;
  struct glw_program *gbr_renderer_flat;
  struct glw_program *gbr_renderer_flat_stencil;

  GLuint gbr_vbo;

#if ENABLE_VDPAU

  PFNGLVDPAUUNREGISTERSURFACENVPROC     gbr_glVDPAUUnregisterSurfaceNV;
  PFNGLVDPAUUNMAPSURFACESNVPROC         gbr_glVDPAUUnmapSurfacesNV;
  PFNGLVDPAUREGISTEROUTPUTSURFACENVPROC gbr_glVDPAURegisterOutputSurfaceNV;
  PFNGLVDPAUMAPSURFACESNVPROC           gbr_glVDPAUMapSurfacesNV;
  PFNGLVDPAUINITNVPROC                  gbr_glVDPAUInitNV;

#endif




} glw_backend_root_t;

#define GLW_DRAW_TRIANGLES GL_TRIANGLES
#define GLW_DRAW_LINE_LOOP GL_LINE_LOOP

/**
 *
 */
typedef struct glw_backend_texture {
  GLuint textures[3];
  uint16_t width;
  uint16_t height;
  int gltype;
} glw_backend_texture_t;

#define glw_tex_width(gbt) ((gbt)->width)
#define glw_tex_height(gbt) ((gbt)->height)

#define glw_is_tex_inited(n) ((n)->textures[0] != 0)

int glw_opengl_init_context(struct glw_root *gr);

void glw_opengl_fini_context(struct glw_root *gr);

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
int glw_load_program(glw_backend_root_t *gbr, struct glw_program *gp);

void glw_program_set_modelview(glw_backend_root_t *gbr,
			       const struct glw_rctx *rc);

void glw_program_set_uniform_color(glw_backend_root_t *gbr,
				   float r, float g, float b, float a);


int glw_opengl_shaders_init(struct glw_root *gr);

void glw_opengl_shaders_fini(struct glw_root *gr);

void glw_stencil_quad(struct glw_root *gr, const struct glw_rctx *rc);

#endif /* GLW_OPENGL_H__ */
