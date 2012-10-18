/*
 *  libglw, OpenGL interface
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

#include <string.h>
#include <limits.h>

#include "glw.h"
#include "glw_renderer.h"
#include "fileaccess/fileaccess.h"




// #define DEBUG_SHADERS

/**
 *
 */
typedef struct render_job {
  Mtx m;
  const struct glw_backend_texture *t0;
  const struct glw_backend_texture *t1;
  struct glw_program *up;
  struct glw_rgb rgb_mul;
  struct glw_rgb rgb_off;
  float alpha;
  float blur;
  int vertex_offset;
  int16_t num_vertices;
  int16_t width;
  int16_t height;
  char blendmode;
  char frontface;
  char eyespace;
  char flags;
} render_job_t;


/**
 *
 */
static void
prepare_delayed(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;

  gbr->gbr_num_render_jobs = 0;
  gbr->gbr_vertex_offset = 0;
}


/**
 *
 */
static glw_program_t *
get_program(const glw_backend_root_t *gbr,
	    const struct glw_backend_texture *t0,
	    const struct glw_backend_texture *t1,
	    float blur, int flags,
	    glw_program_t *up)
{
  glw_program_t *gp;

  if(up != NULL) {
    if(t0 != NULL)
      glBindTexture(gbr->gbr_primary_texture_mode, t0->tex);
    return up;
  }

  if(t0 == NULL) {

    if(t1 != NULL) {
      gp = gbr->gbr_renderer_flat_stencil;
      glBindTexture(gbr->gbr_primary_texture_mode, t1->tex);

    } else {
      gp = gbr->gbr_renderer_flat;
    }
    

  } else {

    const int doblur = blur > 0.05 || flags & GLW_RENDER_BLUR_ATTRIBUTE;

    if(t1 != NULL) {

      gp = doblur ? gbr->gbr_renderer_tex_stencil_blur :
	gbr->gbr_renderer_tex_stencil;

      glActiveTexture(GL_TEXTURE1);
      glBindTexture(gbr->gbr_primary_texture_mode, t1->tex);
      glActiveTexture(GL_TEXTURE0);

    } else {
      gp = doblur ? gbr->gbr_renderer_tex_blur : gbr->gbr_renderer_tex;
    }

    glBindTexture(gbr->gbr_primary_texture_mode, t0->tex);
  }
  return gp;
}

/**
 *
 */
static void
render_unlocked(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  int i;
  struct render_job *rj = gbr->gbr_render_jobs;

  int64_t ts = showtime_get_ts();

  int current_blendmode = GLW_BLEND_NORMAL;
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  int program_switches = 0;

  const float *vertices = gbr->gbr_vertex_buffer;

  glBindBuffer(GL_ARRAY_BUFFER, gbr->gbr_vbo);
  glBufferData(GL_ARRAY_BUFFER,
	       sizeof(float) * VERTEX_SIZE * gbr->gbr_vertex_offset,
	       vertices, GL_STATIC_DRAW);

  vertices = NULL;
  glVertexAttribPointer(0, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices);
      
  glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices + 4);
      
  glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices + 8);

  for(i = 0; i < gbr->gbr_num_render_jobs; i++, rj++) {

    const struct glw_backend_texture *t0 = rj->t0;
    glw_program_t *gp = get_program(gbr, t0, rj->t1, rj->blur, rj->flags,
				    rj->up);

    if(gp == NULL)
      continue;
    
    if(glw_load_program(gbr, gp)) {
      program_switches++;
    }

    glUniform4f(gp->gp_uniform_color_offset,
		rj->rgb_off.r, rj->rgb_off.g, rj->rgb_off.b, 0);
    
    glw_program_set_uniform_color(gbr, rj->rgb_mul.r, rj->rgb_mul.g,
				  rj->rgb_mul.b, rj->alpha);

    if(gp->gp_uniform_time != -1)
      glUniform1f(gp->gp_uniform_time, gr->gr_time);

    if(gp->gp_uniform_resolution != -1)
      glUniform2f(gp->gp_uniform_resolution, rj->width, rj->height);

    if(gp->gp_uniform_blur != -1 && t0 != NULL)
      glUniform3f(gp->gp_uniform_blur, rj->blur,
		  1.5 / t0->width, 1.5 / t0->height);

    if(rj->eyespace) {
      glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0, glw_identitymtx);
    } else {
      glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0, glw_mtx_get(rj->m));
    }

    if(current_blendmode != rj->blendmode) {
      current_blendmode = rj->blendmode;
      switch(current_blendmode) {
      case GLW_BLEND_NORMAL:
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	break;
	
      case GLW_BLEND_ADDITIVE:
	glBlendFunc(GL_SRC_COLOR, GL_ONE);
	break;
      }
    }
      
    glDrawArrays(GL_TRIANGLES, rj->vertex_offset, rj->num_vertices);
  }
  if(current_blendmode != GLW_BLEND_NORMAL) 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  ts = showtime_get_ts() - ts;
  static int hold;
  
  hold++;
  if(hold < 20)
    return;

  static int cnt;
  static int64_t tssum;

  tssum += ts;
  cnt++;

  // printf("%16d (%d) %d switches\n", (int)ts, (int)(tssum/cnt), program_switches);
}


/**
 *
 */
static void
shader_render_delayed(struct glw_root *root, 
		      const Mtx m,
		      const struct glw_backend_texture *t0,
		      const struct glw_backend_texture *t1,
		      const struct glw_rgb *rgb_mul,
		      const struct glw_rgb *rgb_off,
		      float alpha, float blur,
		      const float *vertices,
		      int num_vertices,
		      const uint16_t *indices,
		      int num_triangles,
		      int flags,
		      glw_program_t *p,
		      const glw_rctx_t *rc)
{
  glw_backend_root_t *gbr = &root->gr_be;

  if(gbr->gbr_num_render_jobs >= gbr->gbr_render_jobs_capacity) {
    // Need more space
    gbr->gbr_render_jobs_capacity = 100 + gbr->gbr_render_jobs_capacity * 2; 
    gbr->gbr_render_jobs = realloc(gbr->gbr_render_jobs, 
				   sizeof(render_job_t) *
				   gbr->gbr_render_jobs_capacity);
  }

  struct render_job *rj = gbr->gbr_render_jobs + gbr->gbr_num_render_jobs;
  
  if(m == NULL) {
    rj->eyespace = 1;
  } else {
    rj->eyespace = 0;
    glw_mtx_copy(rj->m, m);
  }

  rj->width  = rc->rc_width;
  rj->height = rc->rc_height;

  rj->up = p;
  rj->t0 = t0;
  rj->t1 = t1;

  switch(gbr->gbr_blendmode) {
  case GLW_BLEND_NORMAL:
    rj->rgb_mul = *rgb_mul;
    rj->alpha = alpha;
    break;

  case GLW_BLEND_ADDITIVE:
    rj->rgb_mul.r = rgb_mul->r * alpha;
    rj->rgb_mul.g = rgb_mul->g * alpha;
    rj->rgb_mul.b = rgb_mul->b * alpha;
    rj->alpha = 1;
    break;
  }

  if(rgb_off != NULL)
    rj->rgb_off = *rgb_off;
  else {
    rj->rgb_off.r = 0;
    rj->rgb_off.g = 0;
    rj->rgb_off.b = 0;
  }

  rj->blur = blur;
  rj->blendmode = gbr->gbr_blendmode;
  rj->frontface = gbr->gbr_frontface;

  int vnum = indices ? num_triangles * 3 : num_vertices;

  if(gbr->gbr_vertex_offset + vnum > gbr->gbr_vertex_buffer_capacity) {
    gbr->gbr_vertex_buffer_capacity = 100 + vnum +
      gbr->gbr_vertex_buffer_capacity * 2;

    gbr->gbr_vertex_buffer = realloc(gbr->gbr_vertex_buffer,
				     sizeof(float) * VERTEX_SIZE * 
				     gbr->gbr_vertex_buffer_capacity);
  }

  float *vdst = gbr->gbr_vertex_buffer + gbr->gbr_vertex_offset * VERTEX_SIZE;

  if(indices != NULL) {
    int i;
    for(i = 0; i < num_triangles * 3; i++) {
      const float *v = &vertices[indices[i] * VERTEX_SIZE];
      memcpy(vdst, v, VERTEX_SIZE * sizeof(float));
      vdst += VERTEX_SIZE;
    }
  } else {
    memcpy(vdst, vertices, num_vertices * VERTEX_SIZE * sizeof(float));
  }

  rj->flags = flags;
  rj->vertex_offset = gbr->gbr_vertex_offset;
  rj->num_vertices = vnum;
  gbr->gbr_vertex_offset += vnum;
  gbr->gbr_num_render_jobs++;
}


/**
 * Render function using OpenGL shaders
 */
static void
shader_render(struct glw_root *root, 
	      const Mtx m,
	      const struct glw_backend_texture *t0,
	      const struct glw_backend_texture *t1,
	      const struct glw_rgb *rgb_mul,
	      const struct glw_rgb *rgb_off,
	      float alpha, float blur,
	      const float *vertices,
	      int num_vertices,
	      const uint16_t *indices,
	      int num_triangles,
	      int flags,
	      glw_program_t *up,
	      const glw_rctx_t *rc)
{
  glw_backend_root_t *gbr = &root->gr_be;
  glw_program_t *gp = get_program(gbr, t0, t1, blur, flags, up);

  if(gp == NULL)
    return;

  glw_load_program(gbr, gp);

  if(rgb_off != NULL)
    glUniform4f(gp->gp_uniform_color_offset,
		rgb_off->r, rgb_off->g, rgb_off->b, 0);
  else
    glUniform4f(gp->gp_uniform_color_offset, 0,0,0,0);

  switch(gbr->gbr_blendmode) {
  case GLW_BLEND_NORMAL:
    glw_program_set_uniform_color(gbr, rgb_mul->r, rgb_mul->g, rgb_mul->b,
				  alpha);
    break;
  case GLW_BLEND_ADDITIVE:
    glw_program_set_uniform_color(gbr, 
				  rgb_mul->r * alpha,
				  rgb_mul->g * alpha,
				  rgb_mul->b * alpha,
				  1);
    break;
  }

  if(gp->gp_uniform_time != -1)
    glUniform1f(gp->gp_uniform_time, root->gr_time);

  if(gp->gp_uniform_resolution != -1)
    glUniform2f(gp->gp_uniform_resolution, rc->rc_width, rc->rc_height);


  if(gp->gp_uniform_blur != -1 && t0 != NULL)
    glUniform3f(gp->gp_uniform_blur, blur, 1.5 / t0->width, 1.5 / t0->height);

  glUniformMatrix4fv(gp->gp_uniform_modelview, 1, 0,
		     glw_mtx_get(m) ?: glw_identitymtx);

  glVertexAttribPointer(gp->gp_attribute_position,
			4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices);

  glVertexAttribPointer(gp->gp_attribute_color,
			4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			vertices + 4);

  if(gp->gp_attribute_texcoord != -1)
    glVertexAttribPointer(gp->gp_attribute_texcoord,
			  4, GL_FLOAT, 0, sizeof(float) * VERTEX_SIZE,
			  vertices + 8);

  if(indices != NULL)
    glDrawElements(GL_TRIANGLES, num_triangles * 3, GL_UNSIGNED_SHORT, indices);
  else
    glDrawArrays(GL_TRIANGLES, 0, num_vertices);
}



/**
 *
 */
static GLuint
glw_compile_shader(const char *path, int type, glw_root_t *gr)
{
  char *src;
  GLint v, len;
  GLuint s;
  char log[4096];
  
  if((src = fa_load(path, NULL, gr->gr_vpaths, log, sizeof(log), NULL, 0,
		    NULL, NULL)) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s",
	  path, log);
    return 0;
  }
  
  s = glCreateShader(type);
  glShaderSource(s, 1, (const char **)&src, NULL);
  
  glCompileShader(s);
  glGetShaderInfoLog(s, sizeof(log), &len, log); 
  glGetShaderiv(s, GL_COMPILE_STATUS, &v);
    
  free(src);

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
  gp->gp_uniform_time        = glGetUniformLocation(p, "time");
  gp->gp_uniform_resolution  = glGetUniformLocation(p, "resolution");

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
    gp->gp_uniform_t[i]         = glGetUniformLocation(p, name);
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
int
glw_load_program(glw_backend_root_t *gbr, glw_program_t *gp)
{
  if(gbr->gbr_current == gp)
    return 0;

  gbr->gbr_current = gp;

  if(gp == NULL) {
    glUseProgram(0);
    return 1;
  }

  glUseProgram(gp->gp_program);
  return 1;
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
  snprintf(path, sizeof(path), "%s/src/ui/glw/glsl/%s", \
	   showtime_dataroot(), FILENAME);

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


  // Video renderer

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

  if(1) {
    gr->gr_render = shader_render;
  } else {
    gr->gr_render = shader_render_delayed;
    gr->gr_be_prepare = prepare_delayed;
    gr->gr_be_render_unlocked = render_unlocked;
    gbr->gbr_delayed_rendering = 1;
    glGenBuffers(1, &gbr->gbr_vbo);
  }

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);

  prop_set_string(prop_create(gr->gr_prop, "rendermode"),
		  "OpenGL VP/FP shaders");
  return 0;
}
