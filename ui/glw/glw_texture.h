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

typedef struct glw_loadable_texture {

  LIST_ENTRY(glw_loadable_texture) glt_global_link;
  LIST_ENTRY(glw_loadable_texture) glt_flush_link;
  TAILQ_ENTRY(glw_loadable_texture) glt_work_link;
  int glt_flags;

  enum {
    GLT_STATE_INACTIVE,
    GLT_STATE_LOADING,
    GLT_STATE_VALID,
    GLT_STATE_ERROR,
  } glt_state;

  unsigned int glt_refcnt;

  float glt_aspect;

  glw_backend_texture_t glt_texture;

  const char *glt_filename;

  void *glt_bitmap;
  size_t glt_bitmap_size;
  int glt_xs;
  int glt_ys;
  int glt_bpp;

  int glt_format;
  int glt_ext_format;
  int glt_ext_type;

} glw_loadable_texture_t;

void glw_tex_init(glw_root_t *gr);

glw_loadable_texture_t *glw_tex_create(glw_root_t *gr, const char *filename);

void glw_tex_deref(glw_root_t *gr, glw_loadable_texture_t *ht);

void glw_tex_layout(glw_root_t *gr, glw_loadable_texture_t *glt);

void glw_tex_purge(glw_root_t *gr);

void glw_tex_is_active(glw_root_t *gr, glw_loadable_texture_t *glt);

void glw_tex_autoflush(glw_root_t *gr);

void glw_tex_upload(glw_loadable_texture_t *glt);

void glw_tex_flush_all(glw_root_t *gr);


/**
 * Backend interface
 */
int glw_tex_backend_decode(glw_root_t *gr, glw_loadable_texture_t *glt);

void glw_tex_backend_free_render_resources(glw_loadable_texture_t *glt);

void glw_tex_backend_free_loader_resources(glw_loadable_texture_t *glt);

void glw_tex_backend_layout(glw_loadable_texture_t *glt);

#endif /* GLW_TEXTURE_H */
