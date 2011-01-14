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

#include "glw.h"
#include "glw_renderer.h"
#include "glw_math.h"
#include "fileaccess/fileaccess.h"

static void glw_renderer_shader(glw_renderer_t *gr, glw_root_t *root,
				glw_rctx_t *rc, glw_backend_texture_t *be_tex,
				const glw_rgb_t *rgb, float alpha,
				int flags);

static void glw_renderer_ff    (glw_renderer_t *gr, glw_root_t *root,
				glw_rctx_t *rc, glw_backend_texture_t *be_tex,
				const glw_rgb_t *rgb, float alpha,
				int flags);


// #define DEBUG_SHADERS

static const float identitymtx[16] = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0,
  0,0,0,1};
  
/**
 * return 1 if the extension is found, otherwise 0
 */
static int
check_gl_ext(const uint8_t *s, const char *func)
{
  int l = strlen(func);
  char *v;
  int x;

  v = strstr((const char *)s, func);
  x = v != NULL && v[l] < 33;

  TRACE(TRACE_DEBUG, "OpenGL", "Feature \"%s\" %savailable",
	func, x ? "" : "not ");
  return x;
}



const static float projection[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};


/**
 *
 */
static void
hw_clip_conf(struct glw_rctx *rc, int which, const float v[4])
{
  if(v != NULL) {
    double plane[4];
    int j;
    
    for(j = 0; j < 4; j++)
      plane[j] = v[j];

    glLoadMatrixf(rc->rc_mtx);

    glClipPlane(GL_CLIP_PLANE0 + which, plane);
    glEnable(GL_CLIP_PLANE0 + which);
  } else {
    glDisable(GL_CLIP_PLANE0 + which);
  }
}

/**
 *
 */
int
glw_opengl_init_context(glw_root_t *gr)
{
  GLint tu = 0;
  glw_backend_root_t *gbr = &gr->gr_be;
  const	GLubyte	*s;
  int rectmode;
  /* Check OpenGL extensions we would like to have */

  s = glGetString(GL_EXTENSIONS);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_CULL_FACE);
  gbr->gbr_culling = 1;

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if(check_gl_ext(s, "GL_ARB_texture_non_power_of_two")) {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_NPOT;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    rectmode = 0;

#ifdef GL_TEXTURE_RECTANGLE_ARB
  } else if(check_gl_ext(s, "GL_ARB_texture_rectangle")) {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_RECTANGLE;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_RECTANGLE_ARB;
    rectmode = 1;
#endif

  } else {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_SIMPLE;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    rectmode = 0; // WRONG
    
  }

  glEnable(gbr->gbr_primary_texture_mode);


  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &tu);
  if(tu < 6) {
    TRACE(TRACE_ERROR, "GLW", 
	  "Insufficient number of texture image units %d < 6 "
	  "for GLW video rendering widget.",
	  tu);
    return -1;
  }
  TRACE(TRACE_DEBUG, "GLW", "%d texture image units available", tu);

  const char *vendor   = (const char *)glGetString(GL_VENDOR);
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  TRACE(TRACE_INFO, "GLW", "OpenGL Renderer: '%s' by '%s'", renderer, vendor);

  int use_shaders = 1;

  if(strstr(renderer, "Mesa"))
      use_shaders = 0;

  if(use_shaders) {

    GLuint vs, fs;

    vs = glw_compile_shader("bundle://src/ui/glw/glsl/v1.glsl",
			    GL_VERTEX_SHADER);

    fs = glw_compile_shader("bundle://src/ui/glw/glsl/f_tex.glsl",
			    GL_FRAGMENT_SHADER);
    gbr->gbr_renderer_tex = glw_make_program(gbr, "Texture", vs, fs);
    glDeleteShader(fs);

    fs = glw_compile_shader("bundle://src/ui/glw/glsl/f_flat.glsl",
			    GL_FRAGMENT_SHADER);
    gbr->gbr_renderer_flat = glw_make_program(gbr, "Flat", vs, fs);
    glDeleteShader(fs);

    glDeleteShader(vs);

    gbr->gbr_renderer_draw = glw_renderer_shader;


    // Video renderer

    vs = glw_compile_shader("bundle://src/ui/glw/glsl/yuv2rgb_v.glsl",
			    GL_VERTEX_SHADER);


    fs = glw_compile_shader("bundle://src/ui/glw/glsl/yuv2rgb_1f_norm.glsl",
			    GL_FRAGMENT_SHADER);
    gbr->gbr_yuv2rgb_1f = glw_make_program(gbr, "yuv2rgb_1f_norm", vs, fs);
    glDeleteShader(fs);

    fs = glw_compile_shader("bundle://src/ui/glw/glsl/yuv2rgb_2f_norm.glsl",
			    GL_FRAGMENT_SHADER);
    gbr->gbr_yuv2rgb_2f = glw_make_program(gbr, "yuv2rgb_2f_norm", vs, fs);
    glDeleteShader(fs);

    glDeleteShader(vs);

  } else {

    gr->gr_set_hw_clipper = hw_clip_conf;
    
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    gbr->gbr_renderer_draw = glw_renderer_ff;

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection);
    glMatrixMode(GL_MODELVIEW);

  }
 

  return 0;
}


/**
 *
 */
void
glw_rctx_init(glw_rctx_t *rc, int width, int height)
{
  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_width  = width;
  rc->rc_height = height;
  rc->rc_alpha = 1.0f;

  memset(&rc->rc_mtx, 0, sizeof(Mtx));
  
  rc->rc_mtx[0]  = 1;
  rc->rc_mtx[5]  = 1;
  rc->rc_mtx[10] = 1;
  rc->rc_mtx[15] = 1;

  glw_Translatef(rc, 0, 0, -1 / tan(45 * M_PI / 360));
}




/**
 *
 */
void
glw_wirebox(glw_root_t *gr, glw_rctx_t *rc)
{
#if CONFIG_GLW_BACKEND_OPENGL
  glw_backend_root_t *gbr = &gr->gr_be;
  glw_load_program(gbr, gbr->gbr_renderer_flat);
  glw_program_set_modelview(gbr, rc);
  glw_program_set_uniform_color(gbr, 1,1,1,1);
  glDisable(GL_TEXTURE_2D);
  glBegin(GL_LINE_LOOP);
  glColor4f(1,1,1,1);
  glVertex3f(-1.0, -1.0, 0.0);
  glVertex3f( 1.0, -1.0, 0.0);
  glVertex3f( 1.0,  1.0, 0.0);
  glVertex3f(-1.0,  1.0, 0.0);
  glEnd();
  glEnable(GL_TEXTURE_2D);
#endif
}


/**
 *
 */
void
glw_wirecube(glw_root_t *gr, glw_rctx_t *rc)
{
#if CONFIG_GLW_BACKEND_OPENGL
  glw_backend_root_t *gbr = &gr->gr_be;

  glw_load_program(gbr, gbr->gbr_renderer_flat);
  glw_program_set_modelview(gbr, rc);
  glw_program_set_uniform_color(gbr, 1,1,1,1);

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0, -1.0, -1.0);
  glVertex3f( 1.0, -1.0, -1.0);
  glVertex3f( 1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0, -1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0, -1.0,  1.0);
  glVertex3f( 1.0, -1.0,  1.0);
  glVertex3f( 1.0,  1.0,  1.0);
  glVertex3f(-1.0,  1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0, -1.0,  1.0);
  glVertex3f(-1.0, -1.0, -1.0);
  glVertex3f(-1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f( 1.0, -1.0,  1.0);
  glVertex3f( 1.0, -1.0, -1.0);
  glVertex3f( 1.0,  1.0, -1.0);
  glVertex3f( 1.0,  1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f( 1.0, -1.0,  1.0);
  glVertex3f( 1.0, -1.0, -1.0);
  glVertex3f(-1.0, -1.0, -1.0);
  glVertex3f(-1.0, -1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f( 1.0,  1.0,  1.0);
  glVertex3f( 1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0,  1.0);
  glEnd();
#endif
}


static void
set_face_culling(glw_backend_root_t *gbr, int flags)
{
  if(!(flags & GLW_IMAGE_DUAL_SIDED) == gbr->gbr_culling) {
    if(gbr->gbr_culling) {
      glEnable(GL_CULL_FACE);
    } else {
      glDisable(GL_CULL_FACE);
    }
    gbr->gbr_culling = !gbr->gbr_culling;
  }
}


/**
 * 
 */
static void
glw_renderer_ff(glw_renderer_t *gr, glw_root_t *root,
		glw_rctx_t *rc, glw_backend_texture_t *be_tex,
		const glw_rgb_t *rgb, float alpha, int flags)
{
  glw_backend_root_t *gbr = &root->gr_be;

  set_face_culling(gbr, flags);

  glLoadMatrixf(rc->rc_mtx);

  glVertexPointer(3, GL_FLOAT, sizeof(float) * 9, gr->gr_array);

  if(gr->gr_color_attributes) {

    glEnableClientState(GL_COLOR_ARRAY);

    if((rgb == NULL || 
	(rgb->r > 0.99 && rgb->g > 0.99 && rgb->b > 0.99)) && alpha > 0.99) {
      glColorPointer(4, GL_FLOAT, sizeof(float) * 9, gr->gr_array + 5);

    } else {
      float r = rgb ? rgb->r : 1;
      float g = rgb ? rgb->g : 1;
      float b = rgb ? rgb->b : 1;

      int id = glw_renderer_get_cache_id(root, gr);
      glw_renderer_cache_t *grc;
      
      if(gr->gr_dirty || gr->gr_cache[id] == NULL ||
	 gr->gr_cache[id]->grc_rgba[0] != r ||
	 gr->gr_cache[id]->grc_rgba[1] != g ||
	 gr->gr_cache[id]->grc_rgba[2] != b ||
	 gr->gr_cache[id]->grc_rgba[3] != alpha) {

	int i;

	if(gr->gr_cache[id] == NULL)
	  gr->gr_cache[id] = calloc(1, sizeof(glw_renderer_cache_t));
	grc = gr->gr_cache[id];

	if(grc->grc_capacity != gr->gr_vertices) {
	  grc->grc_capacity = gr->gr_vertices;
	  grc->grc_array = realloc(grc->grc_array,
				   sizeof(float) * 4 * gr->gr_vertices);
	}
	  
	float *A = grc->grc_array;
	float *B = gr->gr_array;

	for(i = 0; i < gr->gr_vertices; i++) {
	  *A++ = r * B[5];
	  *A++ = g * B[6];
	  *A++ = b * B[7];
	  *A++ = alpha * B[8];
	  B += 9;
	}

	gr->gr_cache[id]->grc_rgba[0] = r;
	gr->gr_cache[id]->grc_rgba[1] = g;
	gr->gr_cache[id]->grc_rgba[2] = b;
	gr->gr_cache[id]->grc_rgba[3] = alpha;

      } else {
	grc = gr->gr_cache[id];
      }

      glColorPointer(4, GL_FLOAT, 0, grc->grc_array);
    }

  } else if(rgb) {
    glColor4f(rgb->r, rgb->g, rgb->b, alpha);
  } else {
    glColor4f(1, 1, 1, alpha);
  }

  if(be_tex == NULL) {
    glBindTexture(gbr->gbr_primary_texture_mode, 0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  } else {
    glBindTexture(gbr->gbr_primary_texture_mode, be_tex->tex);
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 9, gr->gr_array+3);
  }


  
  glDrawElements(GL_TRIANGLES, 3 * gr->gr_triangles, GL_UNSIGNED_SHORT,
		 gr->gr_indices);

  if(be_tex == NULL)
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  if(gr->gr_color_attributes)
    glDisableClientState(GL_COLOR_ARRAY);

  gr->gr_dirty = 0;
}



/**
 * 
 */
static void
glw_renderer_shader(glw_renderer_t *gr, glw_root_t *root, glw_rctx_t *rc, 
		    glw_backend_texture_t *be_tex,
		    const glw_rgb_t *rgb, float alpha, int flags)
{
  glw_backend_root_t *gbr = &root->gr_be;
  glw_program_t *gp;
  int reenable_blend = 0;

  set_face_culling(gbr, flags);

  if(be_tex == NULL) {
    gp = gbr->gbr_renderer_flat;
  } else {

    switch(be_tex->type) {
    case GLW_TEXTURE_TYPE_NORMAL:
      gp = gbr->gbr_renderer_tex;
      break;

    case GLW_TEXTURE_TYPE_NO_ALPHA:
      gp = gbr->gbr_renderer_tex;
      if(alpha > 0.99 && !gr->gr_blended_attributes &&
	 !(flags & GLW_IMAGE_ADDITIVE)) {
	glDisable(GL_BLEND);
	reenable_blend = 1;
      }
      break;

    default:
      return;
    }
    glBindTexture(gbr->gbr_primary_texture_mode, be_tex->tex);
  }

  if(gp == NULL)
    return;

  if(flags & GLW_IMAGE_ADDITIVE)
    glw_blendmode(GLW_BLEND_ADDITIVE);

  glw_load_program(gbr, gp);

  alpha = GLW_CLAMP(alpha, 0, 1);

  if(rgb != NULL)
    glw_program_set_uniform_color(gbr, 
				  GLW_CLAMP(rgb->r, 0, 1),
				  GLW_CLAMP(rgb->g, 0, 1),
				  GLW_CLAMP(rgb->b, 0, 1),
				  alpha);
  else
    glw_program_set_uniform_color(gbr, 1, 1, 1, alpha);

  if(root->gr_active_clippers) {
    int cacheid = glw_renderer_get_cache_id(root, gr);

    if(gr->gr_dirty || gr->gr_cache[cacheid] == NULL ||
       memcmp(gr->gr_cache[cacheid]->grc_mtx,
	      rc->rc_mtx, sizeof(float) * 16) ||
       glw_renderer_clippers_cmp(gr->gr_cache[cacheid], root)) {

      glw_renderer_clip_tesselate(gr, root, rc, cacheid);
    }
    
    glw_program_set_modelview(gbr, NULL);

    float *A = gr->gr_cache[cacheid]->grc_array;

    glVertexAttribPointer(gp->gp_attribute_position,
			  3, GL_FLOAT, 0, sizeof(float) * 9, A);
    glVertexAttribPointer(gp->gp_attribute_color,
			  4, GL_FLOAT, 0, sizeof(float) * 9, A + 5);

    if(gp->gp_attribute_texcoord != -1)
      glVertexAttribPointer(gp->gp_attribute_texcoord,
			    2, GL_FLOAT, 0, sizeof(float) * 9, A + 3);

    glDrawArrays(GL_TRIANGLES, 0, 3 * gr->gr_cache[cacheid]->grc_size);

  } else {

    glw_program_set_modelview(gbr, rc);

    glVertexAttribPointer(gp->gp_attribute_position,
			  3, GL_FLOAT, 0, sizeof(float) * 9, gr->gr_array);
    glVertexAttribPointer(gp->gp_attribute_color,
			  4, GL_FLOAT, 0, sizeof(float) * 9, gr->gr_array + 5);
    if(gp->gp_attribute_texcoord != -1)
      glVertexAttribPointer(gp->gp_attribute_texcoord,
			    2, GL_FLOAT, 0, sizeof(float) * 9, gr->gr_array+3);

    glDrawElements(GL_TRIANGLES, 3 * gr->gr_triangles, GL_UNSIGNED_SHORT,
		   gr->gr_indices);
  }

  gr->gr_dirty = 0;

  if(reenable_blend)
    glEnable(GL_BLEND);

  if(flags & GLW_IMAGE_ADDITIVE)
    glw_blendmode(GLW_BLEND_NORMAL);
}


/**
 *
 */
void
glw_Rotatef(glw_rctx_t *rc, float a, float x, float y, float z)
{
  float s = sinf(GLW_DEG2RAD(a));
  float c = cosf(GLW_DEG2RAD(a));
  float t = 1.0 - c;
  float n = 1 / sqrtf(x*x + y*y + z*z);
  float m[16];
  float *o = rc->rc_mtx;
  float p[16];

  x *= n;
  y *= n;
  z *= n;
  
  m[ 0] = t * x * x + c;
  m[ 4] = t * x * y - s * z;
  m[ 8] = t * x * z + s * y;
  m[12] = 0;

  m[ 1] = t * y * x + s * z;
  m[ 5] = t * y * y + c;
  m[ 9] = t * y * z - s * x;
  m[13] = 0;

  m[ 2] = t * z * x - s * y;
  m[ 6] = t * z * y + s * x;
  m[10] = t * z * z + c;
  m[14] = 0;

  p[0]  = o[0]*m[0]  + o[4]*m[1]  + o[8]*m[2];
  p[4]  = o[0]*m[4]  + o[4]*m[5]  + o[8]*m[6];
  p[8]  = o[0]*m[8]  + o[4]*m[9]  + o[8]*m[10];
  p[12] = o[0]*m[12] + o[4]*m[13] + o[8]*m[14] + o[12];
 
  p[1]  = o[1]*m[0]  + o[5]*m[1]  + o[9]*m[2];
  p[5]  = o[1]*m[4]  + o[5]*m[5]  + o[9]*m[6];
  p[9]  = o[1]*m[8]  + o[5]*m[9]  + o[9]*m[10];
  p[13] = o[1]*m[12] + o[5]*m[13] + o[9]*m[14] + o[13];
  
  p[2]  = o[2]*m[0]  + o[6]*m[1]  + o[10]*m[2];
  p[6]  = o[2]*m[4]  + o[6]*m[5]  + o[10]*m[6];
  p[10] = o[2]*m[8]  + o[6]*m[9]  + o[10]*m[10];
  p[14] = o[2]*m[12] + o[6]*m[13] + o[10]*m[14] + o[14];

  p[ 3] = 0;
  p[ 7] = 0;
  p[11] = 0;
  p[15] = 1;

  memcpy(o, p, sizeof(float) * 16);
}



/**
 *
 */
GLuint
glw_compile_shader(const char *url, int type)
{
  char *src;
  struct fa_stat st;
  GLint v, len;
  GLuint s;
  char log[4096];
  
  if((src = fa_quickload(url, &st, NULL, log, sizeof(log))) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s\n",
	  url, log);
    return 0;
  }
  
  s = glCreateShader(type);
  glShaderSource(s, 1, (const char **)&src, NULL);
  
  glCompileShader(s);
  glGetShaderInfoLog(s, sizeof(log), &len, log); 
  glGetShaderiv(s, GL_COMPILE_STATUS, &v);
    
  free(src);

  if(!v) {
    TRACE(TRACE_ERROR, "GLW", "Unable to compile shader %s", url);
    TRACE(TRACE_ERROR, "GLW", "%s", log);
    return 0;
  }
  return s;
}


/**
 *
 */
glw_program_t *
glw_make_program(glw_backend_root_t *gbr, const char *title,
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

  gp->gp_attribute_position = glGetAttribLocation(p, "a_position");
  gp->gp_attribute_texcoord = glGetAttribLocation(p, "a_texcoord");
  gp->gp_attribute_color    = glGetAttribLocation(p, "a_color");

  gp->gp_uniform_modelview  = glGetUniformLocation(p, "u_modelview");
  gp->gp_uniform_color      = glGetUniformLocation(p, "u_color");
  gp->gp_uniform_colormtx   = glGetUniformLocation(p, "u_colormtx");
  gp->gp_uniform_blend      = glGetUniformLocation(p, "u_blend");
  
#ifdef DEBUG_SHADERS
  printf("Loaded %s\n", title);
  printf("  a_position  = %d\n", gp->gp_attribute_position);
  printf("  a_texcoord  = %d\n", gp->gp_attribute_texcoord);
  printf("  a_color     = %d\n", gp->gp_attribute_color);

  printf("  u_modelview = %d\n", gp->gp_uniform_modelview);
  printf("  u_color     = %d\n", gp->gp_uniform_color);
  printf("  u_colormtx  = %d\n", gp->gp_uniform_colormtx);
  printf("  u_blend     = %d\n", gp->gp_uniform_blend);
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
void
glw_load_program(glw_backend_root_t *gbr, glw_program_t *gp)
{
  if(gbr->gbr_current == gp)
    return;

  if(gbr->gbr_current != NULL) {
    glw_program_t *old = gbr->gbr_current;
    if(old->gp_attribute_position != -1)
      glDisableVertexAttribArray(old->gp_attribute_position);
    if(old->gp_attribute_texcoord != -1)
      glDisableVertexAttribArray(old->gp_attribute_texcoord);
    if(old->gp_attribute_color != -1)
      glDisableVertexAttribArray(old->gp_attribute_color);
  }

  gbr->gbr_current = gp;

  if(gp == NULL) {
    glUseProgram(0);
    return;
  }

  glUseProgram(gp->gp_program);

  if(gp->gp_attribute_position != -1)
      glEnableVertexAttribArray(gp->gp_attribute_position);
  if(gp->gp_attribute_texcoord != -1)
    glEnableVertexAttribArray(gp->gp_attribute_texcoord);
  if(gp->gp_attribute_color != -1)
    glEnableVertexAttribArray(gp->gp_attribute_color);
}



/**
 *
 */
void
glw_program_set_modelview(glw_backend_root_t *gbr, glw_rctx_t *rc)
{
  const float *m = rc ? rc->rc_mtx : identitymtx;
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


