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
#include <string.h>
#include <limits.h>

#include "glw.h"
#include "glw_renderer.h"
#include "fileaccess/fileaccess.h"

#if ENABLE_GLW_BACKEND_OPENGL
const static float projection[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};
#endif

typedef struct render_state {
  const struct glw_backend_texture *t0;
  const struct glw_backend_texture *t1;
  int texload_skips;
} render_state_t;


/**
 *
 */
static void
use_program(glw_backend_root_t *gbr, glw_program_t *gp)
{
  if(gbr->gbr_current == gp)
    return;

  gbr->gbr_current = gp;
  glUseProgram(gp ? gp->gp_program : 0);
}


/**
 *
 */
static glw_program_t *
load_program(glw_root_t *gr,
             const struct glw_backend_texture *t0,
             const struct glw_backend_texture *t1,
             float blur, int flags,
             glw_program_args_t *gpa,
             render_state_t *rs,
             const glw_render_job_t *rj)
{
  glw_program_t *gp;
  glw_backend_root_t *gbr = &gr->gr_be;

  if(unlikely(gpa != NULL)) {

    rs->t0 = NULL;
    rs->t1 = NULL;

    if(unlikely(t1 != NULL)) {

      if(gpa->gpa_load_texture != NULL) {
        // Program has specialized code to load textures, run it
        gpa->gpa_load_texture(gr, gpa->gpa_prog, gpa->gpa_aux, t1, 1);
      } else {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, t1->textures[0]);
        glActiveTexture(GL_TEXTURE0);
      }
    }

    if(t0 != NULL) {

      if(gpa->gpa_load_texture != NULL) {
        // Program has specialized code to load textures, run it
        gpa->gpa_load_texture(gr, gpa->gpa_prog, gpa->gpa_aux, t0, 0);
      } else {
        glBindTexture(GL_TEXTURE_2D, t0->textures[0]);
      }
    }

    use_program(gbr, gpa->gpa_prog);

    if(gpa->gpa_load_uniforms != NULL)
      gpa->gpa_load_uniforms(gr, gpa->gpa_prog, gpa->gpa_aux, rj);

    return gpa->gpa_prog;
  }

  if(t0 == NULL) {

    if(t1 != NULL) {
      gp = gbr->gbr_renderer_flat_stencil;

      if(rs->t0 != t1) {
        glBindTexture(GL_TEXTURE_2D, t1->textures[0]);
        rs->t0 = t1;
      } else {
        rs->texload_skips++;
      }

    } else {
      gp = gbr->gbr_renderer_flat;
    }

  } else {

    const int doblur = blur > 0.05 || flags & GLW_RENDER_BLUR_ATTRIBUTE;

    if(t1 != NULL) {

      gp = doblur ? gbr->gbr_renderer_tex_stencil_blur :
	gbr->gbr_renderer_tex_stencil;

      if(rs->t1 != t1) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, t1->textures[0]);
        glActiveTexture(GL_TEXTURE0);
        rs->t1 = t1;
      } else {
        rs->texload_skips++;
      }

    } else if(doblur) {
      gp = gbr->gbr_renderer_tex_blur;
    } else {
      gp = gbr->gbr_renderer_tex;
    }

    if(rs->t0 != t0) {
      glBindTexture(GL_TEXTURE_2D, t0->textures[0]);
      rs->t0 = t0;
    } else {
      rs->texload_skips++;
    }
  }
  use_program(gbr, gp);
  return gp;
}


/**
 *
 */
static void
render_unlocked(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  render_state_t rs = {0};

  int64_t ts = arch_get_ts();

  int current_blendmode = GLW_BLEND_NORMAL;
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
		      GL_ONE_MINUS_DST_ALPHA, GL_ONE);

  const float *vertices = gr->gr_vertex_buffer;

  glBindBuffer(GL_ARRAY_BUFFER, gbr->gbr_vbo);
  glBufferData(GL_ARRAY_BUFFER,
	       sizeof(float) * VERTEX_SIZE * gr->gr_vertex_offset,
	       vertices, GL_STATIC_DRAW);

  int current_frontface = GLW_CCW;
  glFrontFace(GL_CCW);

  vertices = NULL;
  glVertexAttribPointer(0, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices);

  glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices + 4);

  glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices + 8);

  for(int j = 0; j < gr->gr_num_render_jobs; j++) {
    const glw_render_order_t *ro = gr->gr_render_order + j;
    const glw_render_job_t *rj = ro->job;

    if(unlikely(rj->num_vertices == 0))
      continue;

    const struct glw_backend_texture *t0 = rj->t0;

    glw_program_t *gp =
      load_program(gr, t0, rj->t1, rj->blur, rj->flags, rj->gpa, &rs, rj);

    if(gbr->gbr_use_stencil_buffer)
      glStencilFunc(GL_GEQUAL, ro->zindex, 0xFF);

    if(unlikely(gp == NULL)) {

#if ENABLE_GLW_BACKEND_OPENGL
      if(rj->eyespace) {
        glLoadMatrixf(glw_identitymtx);
      } else {
        glLoadMatrixf(glw_mtx_get(rj->m));
      }

      glBegin(GL_QUADS);
      if(t0 != NULL)
        glTexCoord2i(0, t0->height);
      glVertex3i(-1, -1, 0);

      if(t0 != NULL)
        glTexCoord2i(t0->width, t0->height);
      glVertex3i(1, -1, 0);

      if(t0 != NULL)
        glTexCoord2i(t0->width, 0);
      glVertex3i(1, 1, 0);

      if(t0 != NULL)
        glTexCoord2i(0, 0);
      glVertex3i(-1, 1, 0);

      glEnd();

      glDisable(t0->gltype);
#endif
      continue;

    } else {

      glUniform4f(gp->gp_uniform_color_offset,
                  rj->rgb_off.r, rj->rgb_off.g, rj->rgb_off.b, 0);

      glUniform4f(gbr->gbr_current->gp_uniform_color,
                  rj->rgb_mul.r, rj->rgb_mul.g, rj->rgb_mul.b, rj->alpha);

      if(gp->gp_uniform_time != -1)
        glUniform1f(gp->gp_uniform_time, gr->gr_time_sec);

      if(gp->gp_uniform_resolution != -1)
        glUniform3f(gp->gp_uniform_resolution, rj->width, rj->height, 1);

      if(gp->gp_uniform_blur != -1 && t0 != NULL)
        glUniform3f(gp->gp_uniform_blur, rj->blur,
                    1.5 / t0->width, 1.5 / t0->height);

      if(rj->eyespace) {
        glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0, glw_identitymtx);
      } else {
        glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0, glw_mtx_get(rj->m));
      }
    }
    if(unlikely(current_blendmode != rj->blendmode)) {
      current_blendmode = rj->blendmode;
      switch(current_blendmode) {
      case GLW_BLEND_NORMAL:
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			    GL_ONE_MINUS_DST_ALPHA, GL_ONE);

	break;

      case GLW_BLEND_ADDITIVE:
	glBlendFuncSeparate(GL_SRC_COLOR, GL_ONE,
                            GL_ONE_MINUS_DST_ALPHA, GL_ONE);
	break;
      }
    }


    if(unlikely(current_frontface != rj->frontface)) {
      current_frontface = rj->frontface;
      glFrontFace(current_frontface == GLW_CW ? GL_CW : GL_CCW);
    }

    glDrawArrays(rj->primitive_type, rj->vertex_offset, rj->num_vertices);
  }
  if(current_blendmode != GLW_BLEND_NORMAL) {
    glBlendFuncSeparate(GL_SRC_COLOR, GL_ONE,
			GL_ONE_MINUS_DST_ALPHA, GL_ONE);
  }
  ts = arch_get_ts() - ts;
  static int hold;
  
  hold++;
  if(hold < 20)
    return;
#if 0
  static int cnt;
  static int64_t tssum;

  tssum += ts;
  cnt++;

  printf("%16d (%d) %d saved texloads\n", (int)ts, (int)(tssum/cnt),
         rs.texload_skips);
#endif
}


/**
 *
 */
static GLuint
glw_compile_shader(const char *path, int type, glw_root_t *gr)
{
  GLint v, len;
  GLuint s;
  char log[4096];
  buf_t *b;

  if((b = fa_load(path,
                   FA_LOAD_VPATHS(gr->gr_vpaths),
                   FA_LOAD_ERRBUF(log, sizeof(log)),
                   NULL)) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s",
	  path, log);
    return 0;
  }

  b = buf_make_writable(b);
  char *src = buf_str(b);
  s = glCreateShader(type);
  glShaderSource(s, 1, (const char **)&src, NULL);

  glCompileShader(s);
  glGetShaderInfoLog(s, sizeof(log), &len, log);
  glGetShaderiv(s, GL_COMPILE_STATUS, &v);

  buf_release(b);

  if(!v) {
    TRACE(TRACE_ERROR, "GLW", "Unable to compile shader %s", path);
    TRACE(TRACE_ERROR, "GLW", "%s", log);
    return 0;
  }
  return s;
}


/**
 *
 */
static glw_program_t *
glw_link_program(glw_backend_root_t *gbr, const char *title,
		 GLuint vs, GLuint fs)
{
  char log[4096];
  GLsizei len;
  GLint v;
  GLuint p;
  int i;

  p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);

  glBindAttribLocation(p, 0, "a_position");
  glBindAttribLocation(p, 1, "a_color");
  glBindAttribLocation(p, 2, "a_texcoord");


  glLinkProgram(p);

  glGetProgramInfoLog(p, sizeof(log), &len, log); 

  glGetProgramiv(p, GL_LINK_STATUS, &v);
  if(!v) {
    TRACE(TRACE_ERROR, "GLW", "Unable to link shader %s", title);
    TRACE(TRACE_ERROR, "GLW", "%s", log);
    return NULL;
  }

  glw_program_t *gp = calloc(1, sizeof(glw_program_t));

  gp->gp_title = strdup(title);
  gp->gp_program = p;

  glUseProgram(p);
  gbr->gbr_current = gp;


  gp->gp_attribute_position = 0;
  gp->gp_attribute_color    = 1;
  gp->gp_attribute_texcoord = 2;

  gp->gp_uniform_modelview  = glGetUniformLocation(p, "u_modelview");
  gp->gp_uniform_color      = glGetUniformLocation(p, "u_color");
  gp->gp_uniform_colormtx   = glGetUniformLocation(p, "u_colormtx");
  gp->gp_uniform_blend      = glGetUniformLocation(p, "u_blend");
  gp->gp_uniform_color_offset= glGetUniformLocation(p, "u_color_offset");
  gp->gp_uniform_blur        = glGetUniformLocation(p, "u_blur");

  gp->gp_uniform_time        = glGetUniformLocation(p, "iGlobalTime");
  gp->gp_uniform_resolution  = glGetUniformLocation(p, "iResolution");

#ifdef DEBUG_SHADERS
  printf("Loaded %s\n", title);
  printf("  a_position     = %d\n", gp->gp_attribute_position);
  printf("  a_texcoord     = %d\n", gp->gp_attribute_texcoord);
  printf("  a_color        = %d\n", gp->gp_attribute_color);

  printf("  u_modelview = %d\n", gp->gp_uniform_modelview);
  printf("  u_color     = %d\n", gp->gp_uniform_color);
  printf("  u_colormtx  = %d\n", gp->gp_uniform_colormtx);
  printf("  u_blend     = %d\n", gp->gp_uniform_blend);
  printf("  u_color_offset = %d\n", gp->gp_uniform_color_offset);
  printf("  u_blur         = %d\n", gp->gp_uniform_blur);
#endif

  for(i = 0; i < 6; i++) {
    char name[8];

    snprintf(name, sizeof(name), "u_t%d", i);
    gp->gp_uniform_t[i]  = glGetUniformLocation(p, name);


    if(gp->gp_uniform_t[i] == -1) {
      snprintf(name, sizeof(name), "iChannel%d", i);
      gp->gp_uniform_t[i]  = glGetUniformLocation(p, name);
    }

    if(gp->gp_uniform_t[i] != -1)
      glUniform1i(gp->gp_uniform_t[i], i);
#ifdef DEBUG_SHADERS
    printf("  u_t%d       = %d\n", i, gp->gp_uniform_t[i]);
#endif
  }

  return gp;
}


/**
 *
 */
void
glw_program_set_modelview(glw_backend_root_t *gbr, const glw_rctx_t *rc)
{
  const float *m = rc ? glw_mtx_get(rc->rc_mtx) : glw_identitymtx;
  glUniformMatrix4fv(gbr->gbr_current->gp_uniform_modelview, 1, 0, m);
}

/**
 *
 */
void
glw_program_set_uniform_color(glw_backend_root_t *gbr,
			      float r, float g, float b, float a)
{
  glUniform4f(gbr->gbr_current->gp_uniform_color, r, g, b, a);
}


#define SHADERPATH(FILENAME) \
  snprintf(path, sizeof(path), "dataroot://res/shaders/glsl/%s", FILENAME);

/**
 *
 */
glw_program_t *
glw_make_program(glw_root_t *gr, 
		 const char *vertex_shader,
		 const char *fragment_shader)
{
  GLuint vs, fs;
  char path[512];
  glw_program_t *p;


  SHADERPATH("v1.glsl");
  vs = glw_compile_shader(vertex_shader ?: path, GL_VERTEX_SHADER, gr);
  if(vs == 0)
    return NULL;
  fs = glw_compile_shader(fragment_shader, GL_FRAGMENT_SHADER, gr);
  if(fs == 0) {
    glDeleteShader(vs);
    return NULL;
  }

  p = glw_link_program(&gr->gr_be, "user shader", vs, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);

  return p;
}



/**
 *
 */
void
glw_destroy_program(struct glw_root *gr, struct glw_program *gp)
{
  if(gp == NULL)
    return;
  free(gp->gp_title);
  glDeleteProgram(gp->gp_program);
  free(gp);
}


/**
 *
 */
int
glw_opengl_shaders_init(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  char path[512];
  GLuint vs, fs;

  SHADERPATH("v1.glsl");
  vs = glw_compile_shader(path, GL_VERTEX_SHADER, gr);

  SHADERPATH("f_tex.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_renderer_tex = glw_link_program(gbr, "Texture", vs, fs);
  glDeleteShader(fs);

  SHADERPATH("f_tex_stencil.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_renderer_tex_stencil = 
    glw_link_program(gbr, "TextureStencil", vs, fs);
  glDeleteShader(fs);

  SHADERPATH("f_tex_blur.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_renderer_tex_blur = glw_link_program(gbr, "TextureBlur", vs, fs);
  glDeleteShader(fs);

  SHADERPATH("f_tex_stencil_blur.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_renderer_tex_stencil_blur =
    glw_link_program(gbr, "TextureStencilBlur", vs, fs);
  glDeleteShader(fs);

  SHADERPATH("f_flat.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_renderer_flat = glw_link_program(gbr, "Flat", vs, fs);
  glDeleteShader(fs);

  SHADERPATH("f_flat_stencil.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_renderer_flat_stencil = glw_link_program(gbr, "FlatStencil", vs, fs);
  glDeleteShader(fs);

  glDeleteShader(vs);

  //    gbr->gbr_renderer_draw = glw_renderer_shader;


  // yuv2rgb Video renderer

  SHADERPATH("yuv2rgb_v.glsl");
  vs = glw_compile_shader(path, GL_VERTEX_SHADER, gr);


  SHADERPATH("yuv2rgb_1f_norm.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_yuv2rgb_1f = glw_link_program(gbr, "yuv2rgb_1f_norm", vs, fs);
  glDeleteShader(fs);

  SHADERPATH("yuv2rgb_2f_norm.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_yuv2rgb_2f = glw_link_program(gbr, "yuv2rgb_2f_norm", vs, fs);
  glDeleteShader(fs);
  glDeleteShader(vs);

  // rgb2rgb Video renderer

  SHADERPATH("rgb2rgb_v.glsl");
  vs = glw_compile_shader(path, GL_VERTEX_SHADER, gr);


  SHADERPATH("rgb2rgb_1f_norm.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_rgb2rgb_1f = glw_link_program(gbr, "rgb2rgb_1f_norm", vs, fs);
  glDeleteShader(fs);

  SHADERPATH("rgb2rgb_2f_norm.glsl");
  fs = glw_compile_shader(path, GL_FRAGMENT_SHADER, gr);
  gbr->gbr_rgb2rgb_2f = glw_link_program(gbr, "rgb2rgb_2f_norm", vs, fs);
  glDeleteShader(fs);
  glDeleteShader(vs);

  gr->gr_be_render_unlocked = render_unlocked;
  glGenBuffers(1, &gbr->gbr_vbo);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);

  prop_set_string(prop_create(gr->gr_prop_ui, "rendermode"),
		  "OpenGL VP/FP shaders");


#if ENABLE_GLW_BACKEND_OPENGL
  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf(projection);
  glMatrixMode(GL_MODELVIEW);
#endif

  return 0;
}


/**
 *
 */
void
glw_opengl_shaders_fini(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;

  glw_destroy_program(gr, gbr->gbr_yuv2rgb_1f);
  glw_destroy_program(gr, gbr->gbr_yuv2rgb_2f);
  glw_destroy_program(gr, gbr->gbr_rgb2rgb_1f);
  glw_destroy_program(gr, gbr->gbr_rgb2rgb_2f);

  glw_destroy_program(gr, gbr->gbr_renderer_tex);
  glw_destroy_program(gr, gbr->gbr_renderer_tex_stencil);
  glw_destroy_program(gr, gbr->gbr_renderer_tex_blur);
  glw_destroy_program(gr, gbr->gbr_renderer_tex_stencil_blur);
  glw_destroy_program(gr, gbr->gbr_renderer_flat);
  glw_destroy_program(gr, gbr->gbr_renderer_flat_stencil);
}

/**
 *
 */
static const float stencilquad[6][VERTEX_SIZE] = {
  {-1, -1, 0, 0, 1,1,1,1, 0,0,0,0},
  { 1, -1, 0, 0, 1,1,1,1, 0,0,0,0},
  { 1,  1, 0, 0, 1,1,1,1, 0,0,0,0},

  {-1, -1, 0, 0, 1,1,1,1, 0,0,0,0},
  { 1,  1, 0, 0, 1,1,1,1, 0,0,0,0},
  { -1, 1, 0, 0, 1,1,1,1, 0,0,0,0},
};


/**
 *
 */
void
glw_stencil_quad(glw_root_t *gr, const glw_rctx_t *rc)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  glw_program_t *gp = gbr->gbr_renderer_flat;

  glStencilFunc(GL_NEVER, rc->rc_zindex, 0xFF);
  glStencilOp(GL_REPLACE, GL_KEEP, GL_KEEP);
  glStencilMask(0xff);

  glUseProgram(gp->gp_program);

  glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0, glw_mtx_get(rc->rc_mtx));

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glFrontFace(GL_CCW);

  const float *vertices = &stencilquad[0][0];

  glVertexAttribPointer(0, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
                       vertices);

  glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
                       vertices + 4);

  glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
                       vertices + 8);

  glDrawArrays(GL_TRIANGLES, 0, 6);

  glUseProgram(0);
  gbr->gbr_current = NULL;
}

