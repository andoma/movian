/*
 *  GL Widgets, Texture
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

#ifndef GLW_TEXTURE_H
#define GLW_TEXTURE_H

#include "misc/pixmap.h"

#define GLW_TEX_REPEAT 0x1

typedef struct glw_loadable_texture {

  LIST_ENTRY(glw_loadable_texture) glt_global_link;
  LIST_ENTRY(glw_loadable_texture) glt_flush_link;
  TAILQ_ENTRY(glw_loadable_texture) glt_work_link;
  struct glw_loadable_texture_queue *glt_q;

  int glt_flags;

  enum {
    GLT_STATE_INACTIVE,
    GLT_STATE_QUEUED,
    GLT_STATE_LOADING,
    GLT_STATE_VALID,
    GLT_STATE_ERROR,
  } glt_state;

  unsigned int glt_refcnt;

  float glt_aspect;

  glw_backend_texture_t glt_texture;

  char *glt_filename;

  pixmap_t *glt_pixmap;

  int16_t glt_req_xs;
  int16_t glt_req_ys;

  int16_t glt_xs;
  int16_t glt_ys;

  int glt_format;
  int glt_ext_format;
  int glt_ext_type;

  unsigned int glt_orientation;

  float glt_s, glt_t;
  int16_t glt_tex_width;
  int16_t glt_tex_height;

} glw_loadable_texture_t;

void glw_tex_init(glw_root_t *gr);

glw_loadable_texture_t *glw_tex_create(glw_root_t *gr, const char *filename,
				       int flags, int xs, int ys);

void glw_tex_deref(glw_root_t *gr, glw_loadable_texture_t *ht);

void glw_tex_layout(glw_root_t *gr, glw_loadable_texture_t *glt);

void glw_tex_purge(glw_root_t *gr);

void glw_tex_autoflush(glw_root_t *gr);

void glw_tex_flush_all(glw_root_t *gr);


/**
 * Backend interface
 */
int glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
			 pixmap_t *pm);

void glw_tex_backend_free_render_resources(glw_root_t *gr,
					   glw_loadable_texture_t *glt);

void glw_tex_backend_free_loader_resources(glw_loadable_texture_t *glt);

void glw_tex_backend_layout(glw_root_t *gr, glw_loadable_texture_t *glt);

#define GLW_TEXTURE_FORMAT_I8A8   1  // Intensity + Alpha
#define GLW_TEXTURE_FORMAT_RGB    2
#define GLW_TEXTURE_FORMAT_BGR32  3

void glw_tex_upload(glw_root_t *gr, glw_backend_texture_t *tex,
		    const void *src, int format, int width, int height,
		    int flags);

void glw_tex_destroy(glw_root_t *gr, glw_backend_texture_t *tex);

#endif /* GLW_TEXTURE_H */
