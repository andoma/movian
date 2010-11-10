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

  GLint  gp_uniform_t[6];

} glw_program_t;


#define NUM_CLIPPLANES 6


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
#ifdef linux
  PFNGLXBINDTEXIMAGEEXTPROC gbr_bind_tex_image;
  PFNGLXRELEASETEXIMAGEEXTPROC gbr_release_tex_image;
#endif
  int gbr_enable_vdpau;

  float gbr_clip[NUM_CLIPPLANES][4];
  int gbr_active_clippers;
  int gbr_soft_clippers;

  struct glw_program *gbr_renderer_tex;
  struct glw_program *gbr_renderer_alpha_tex;
  struct glw_program *gbr_renderer_flat;


  void (*gbr_renderer_draw)(struct glw_renderer *gr, struct glw_root *root,
			    struct glw_rctx *rc,
			    struct glw_backend_texture *be_tex,
			    const struct glw_rgb *rgb, float alpha);

} glw_backend_root_t;

#define glw_renderer_draw(gr, root, rc, be_tex, rgb, alpha) \
  (root)->gr_be.gbr_renderer_draw(gr, root, rc, be_tex, rgb, alpha)


typedef float Mtx[16];

/**
 * Renderer cache
 */
typedef struct glw_renderer_cache {
  union {
    float grc_mtx[16]; // ModelView matrix
    float grc_rgba[4];
  };
  int grc_active_clippers;
  float grc_clip[NUM_CLIPPLANES][4];

  float *grc_array;
  int grc_size;     // In triangles
  int grc_capacity; // In triangles
} glw_renderer_cache_t;

/**
 * Renderer
 */


typedef struct glw_renderer {
  uint16_t gr_vertices;
  uint16_t gr_triangles;
  char gr_static_indices;
  char gr_dirty;
  char gr_blended_attributes;
  char gr_color_attributes;
  unsigned char gr_framecmp;
  unsigned char gr_cacheptr;

  float *gr_array;
  uint16_t *gr_indices;

#define GLW_RENDERER_CACHES 4

  glw_renderer_cache_t *gr_cache[GLW_RENDERER_CACHES];
  
} glw_renderer_t;


/**
 *
 */
typedef struct glw_backend_texture {
  GLuint tex;
  char type;
#define GLW_TEXTURE_TYPE_NORMAL   0
#define GLW_TEXTURE_TYPE_ALPHA    1
#define GLW_TEXTURE_TYPE_NO_ALPHA 2
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

glw_program_t *glw_make_program(glw_backend_root_t *gbr,
				const char *title, GLuint vs, GLuint fs);

void glw_load_program(glw_backend_root_t *gbr, glw_program_t *gp);

void glw_program_set_modelview(glw_backend_root_t *gbr, struct glw_rctx *rc);

void glw_program_set_uniform_color(glw_backend_root_t *gbr,
				   float r, float g, float b, float a);

#endif /* GLW_OPENGL_H__ */
