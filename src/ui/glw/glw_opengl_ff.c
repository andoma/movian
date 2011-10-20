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
#include "fileaccess/fileaccess.h"

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
hw_set_clip_conf(struct glw_rctx *rc, int which, const Vec4 v)
{
  double plane[4];
    
  plane[0] = glw_vec4_extract(v, 0);
  plane[1] = glw_vec4_extract(v, 1);
  plane[2] = glw_vec4_extract(v, 2);
  plane[3] = glw_vec4_extract(v, 3);

  glLoadMatrixf(glw_mtx_get(rc->rc_mtx));

  glClipPlane(GL_CLIP_PLANE0 + which, plane);
  glEnable(GL_CLIP_PLANE0 + which);
}

/**
 *
 */
static void
hw_clr_clip_conf(struct glw_rctx *rc, int which)
{
  glDisable(GL_CLIP_PLANE0 + which);
}




static void
ff_render(struct glw_root *gr,
	  Mtx m,
	  struct glw_backend_texture *tex,
	  const struct glw_rgb *rgb_mul,
	  const struct glw_rgb *rgb_off,
	  float alpha, float blur,
	  const float *vertices,
	  int num_vertices,
	  const uint16_t *indices,
	  int num_triangles,
	  int flags)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  float r,g,b;

  switch(gbr->be_blendmode) {
  case GLW_BLEND_NORMAL:
    r = rgb_mul->r;
    g = rgb_mul->g;
    b = rgb_mul->b;
    break;
  case GLW_BLEND_ADDITIVE:
    r = rgb_mul->r * alpha;
    g = rgb_mul->g * alpha;
    b = rgb_mul->b * alpha;
    break;
  default:
    return;
  }


  glLoadMatrixf(glw_mtx_get(m) ?: glw_identitymtx);

  glVertexPointer(3, GL_FLOAT, sizeof(float) * VERTEX_SIZE, vertices);

  if(flags & GLW_RENDER_COLOR_ATTRIBUTES) {
    int i;

    if(num_vertices > gr->gr_vtmp_capacity) {
      gr->gr_vtmp_capacity = num_vertices;
      gr->gr_vtmp_buffer = realloc(gr->gr_vtmp_buffer, sizeof(float) *
				   VERTEX_SIZE * gr->gr_vtmp_capacity);
    }
    for(i = 0; i < num_vertices; i++) {
      gr->gr_vtmp_buffer[i * VERTEX_SIZE + 0] =
	vertices[i * VERTEX_SIZE + 5] * r;
      gr->gr_vtmp_buffer[i * VERTEX_SIZE + 1] =
	vertices[i * VERTEX_SIZE + 6] * g;
      gr->gr_vtmp_buffer[i * VERTEX_SIZE + 2] =
	vertices[i * VERTEX_SIZE + 7] * b;
      gr->gr_vtmp_buffer[i * VERTEX_SIZE + 3] =
	vertices[i * VERTEX_SIZE + 8] * alpha;
    }

    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_FLOAT, sizeof(float) * VERTEX_SIZE,
		   gr->gr_vtmp_buffer);
  } else {
    glColor4f(r, g, b, alpha);
  }

  if(rgb_off != NULL) {
    glEnable(GL_COLOR_SUM);
    glSecondaryColor3f(rgb_off->r, rgb_off->g, rgb_off->b);
  }

  if(tex == NULL) {
    glBindTexture(gbr->gbr_primary_texture_mode, 0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  } else {
    glBindTexture(gbr->gbr_primary_texture_mode, tex->tex);
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * VERTEX_SIZE,
		      vertices + 3);
  }

  if(indices != NULL)
    glDrawElements(GL_TRIANGLES, 3 * num_triangles,
		   GL_UNSIGNED_SHORT, indices);
  else
    glDrawArrays(GL_TRIANGLES, 0, num_vertices);

  if(rgb_off != NULL)
    glDisable(GL_COLOR_SUM);

  if(flags & GLW_RENDER_COLOR_ATTRIBUTES)
    glDisableClientState(GL_COLOR_ARRAY);

  if(tex == NULL)
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}


/**
 *
 */
int
glw_opengl_ff_init(glw_root_t *gr)
{
  gr->gr_set_hw_clipper = hw_set_clip_conf;
  gr->gr_clr_hw_clipper = hw_clr_clip_conf;
  
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  gr->gr_render = ff_render;
    
  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf(projection);
  glMatrixMode(GL_MODELVIEW);
  
  prop_set_string(prop_create(gr->gr_uii.uii_prop, "rendermode"),
		  "OpenGL fixed function");
  return 0;
}



