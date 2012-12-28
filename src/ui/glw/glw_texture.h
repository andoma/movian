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

#define GLW_TEX_CORNER_TOPLEFT        PIXMAP_CORNER_TOPLEFT
#define GLW_TEX_CORNER_TOPRIGHT       PIXMAP_CORNER_TOPRIGHT
#define GLW_TEX_CORNER_BOTTOMLEFT     PIXMAP_CORNER_BOTTOMLEFT
#define GLW_TEX_CORNER_BOTTOMRIGHT    PIXMAP_CORNER_BOTTOMRIGHT
#define GLW_TEX_REPEAT                0x10

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
    GLT_STATE_LOAD_ABORT,
  } glt_state;

  unsigned int glt_refcnt;

  float glt_aspect;

  glw_backend_texture_t glt_texture;

  rstr_t *glt_url;

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
  int16_t glt_radius;
  int16_t glt_margin;

} glw_loadable_texture_t;

void glw_tex_init(glw_root_t *gr);

void glw_tex_fini(glw_root_t *gr);

glw_loadable_texture_t *glw_tex_create(glw_root_t *gr, rstr_t *url,
				       int flags, int xs, int ys,
				       int radius);

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

void glw_tex_upload(glw_root_t *gr, glw_backend_texture_t *tex,
		    const pixmap_t *pm, int flags);

void glw_tex_destroy(glw_root_t *gr, glw_backend_texture_t *tex);

#endif /* GLW_TEXTURE_H */
