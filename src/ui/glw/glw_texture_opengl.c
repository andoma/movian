/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include "glw.h"
#include "glw_texture.h"

/**
 * Free texture (always invoked in main rendering thread)
 */
void
glw_tex_backend_free_render_resources(glw_root_t *gr, 
				      glw_loadable_texture_t *glt)
{
  if(glt->glt_texture.tex != 0) {
    glDeleteTextures(1, &glt->glt_texture.tex);
    glt->glt_texture.tex = 0;
  }
}


/**
 * Free resources created by glw_tex_backend_decode()
 */
void
glw_tex_backend_free_loader_resources(glw_loadable_texture_t *glt)
{
  if(glt->glt_pixmap != NULL) {
    pixmap_release(glt->glt_pixmap);
    glt->glt_pixmap = NULL;
  }
}


/**
 * Invoked on every frame when status == VALID
 */
void
glw_tex_backend_layout(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  void *p;
  int m = gr->gr_be.gbr_primary_texture_mode;

  if(glt->glt_texture.tex != 0)
    return;

  p = glt->glt_pixmap->pm_data;

  glGenTextures(1, &glt->glt_texture.tex);
  glBindTexture(m, glt->glt_texture.tex);


  switch(glt->glt_format) {
  case GL_RGB:
    glt->glt_texture.type = GLW_TEXTURE_TYPE_NO_ALPHA;
    break;

  default:
    glt->glt_texture.type = GLW_TEXTURE_TYPE_NORMAL;
    break;
  }

  glt->glt_texture.width  = glt->glt_xs;
  glt->glt_texture.height = glt->glt_ys;

  glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  int wrapmode = glt->glt_flags & GLW_TEX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(m, GL_TEXTURE_WRAP_S, wrapmode);
  glTexParameteri(m, GL_TEXTURE_WRAP_T, wrapmode);

  if(glt->glt_tex_width && glt->glt_tex_height) {

    glTexImage2D(m, 0, glt->glt_format, glt->glt_tex_width, glt->glt_tex_height,
		 0, glt->glt_format, GL_UNSIGNED_BYTE, NULL);

    glTexSubImage2D(m, 0, 0, 0, 
		    glt->glt_xs, glt->glt_ys, 
		    glt->glt_format, GL_UNSIGNED_BYTE,
		    p);

    glt->glt_s = (float)glt->glt_xs / (float)glt->glt_tex_width;
    glt->glt_t = (float)glt->glt_ys / (float)glt->glt_tex_height;

  } else {
    glt->glt_s = 1;
    glt->glt_t = 1;

    glTexImage2D(m, 0, glt->glt_format, 
		 glt->glt_xs, glt->glt_ys,
		 0, glt->glt_format,
		 GL_UNSIGNED_BYTE, p);
  }


  glBindTexture(m, 0);

  glw_tex_backend_free_loader_resources(glt);
}




int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt, pixmap_t *pm)
{
  int size;

  switch(pm->pm_type) {
  default:
    return 0;

  case PIXMAP_RGB24:
    glt->glt_format = GL_RGB;
    size = pm->pm_width * pm->pm_height * 4;
    break;


  case PIXMAP_BGR32:
    glt->glt_format = GL_RGBA;
    size = pm->pm_width * pm->pm_height * 4;
    break;
    
  case PIXMAP_IA:
    glt->glt_format = GL_LUMINANCE_ALPHA;
    size = pm->pm_width * pm->pm_height * 2;
    break;

  case PIXMAP_I:
    glt->glt_format = GL_LUMINANCE;
    size = pm->pm_width * pm->pm_height;
    break;
  }
  
  if(glt->glt_pixmap != NULL) 
    pixmap_release(glt->glt_pixmap);

  glt->glt_pixmap = pixmap_dup(pm);

  return size;
}

/**
 *
 */
void
glw_tex_upload(glw_root_t *gr, glw_backend_texture_t *tex, 
	       const pixmap_t *pm, int flags)
{
  int format;
  int m = gr->gr_be.gbr_primary_texture_mode;

  if(tex->tex == 0) {
    glGenTextures(1, &tex->tex);
    glBindTexture(m, tex->tex);
    glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    int m2 = flags & GLW_TEX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(m, GL_TEXTURE_WRAP_S, m2);
    glTexParameteri(m, GL_TEXTURE_WRAP_T, m2);
  } else {
    glBindTexture(m, tex->tex);
  }
  
  switch(pm->pm_type) {
  case PIXMAP_BGR32:
    format     = GL_RGBA;
    tex->type  = GLW_TEXTURE_TYPE_NORMAL;
    break;

  case PIXMAP_RGB24:
    format     = GL_RGB;
    tex->type  = GLW_TEXTURE_TYPE_NO_ALPHA;
    break;

  case PIXMAP_IA:
    format     = GL_LUMINANCE_ALPHA;
    tex->type  = GLW_TEXTURE_TYPE_NORMAL;
    break;

  default:
    return;
  }

  tex->width  = pm->pm_width;
  tex->height = pm->pm_height;

  glTexImage2D(m, 0, format, pm->pm_width, pm->pm_height,
	       0, format, GL_UNSIGNED_BYTE, pm->pm_data);
}


/**
 *
 */
void
glw_tex_destroy(glw_root_t *gr, glw_backend_texture_t *tex)
{
  if(tex->tex != 0) {
    glDeleteTextures(1, &tex->tex);
    tex->tex = 0;
  }
}
