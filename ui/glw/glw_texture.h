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

typedef struct glw_texture {

  LIST_ENTRY(glw_texture) gt_global_link;
  LIST_ENTRY(glw_texture) gt_flush_link;
  TAILQ_ENTRY(glw_texture) gt_work_link;
  int gt_flags;

  enum {
    GT_STATE_INACTIVE,
    GT_STATE_LOADING,
    GT_STATE_VALID,
    GT_STATE_ERROR,
  } gt_state;

  unsigned int gt_refcnt;

  float gt_aspect;

  unsigned int gt_texture;

  const char *gt_filename;

  void *gt_bitmap;
  size_t gt_bitmap_size;
  int gt_xs;
  int gt_ys;
  int gt_bpp;

  int gt_format;
  int gt_ext_format;
  int gt_ext_type;

} glw_texture_t;

glw_texture_t *glw_tex_create(glw_root_t *gr, const char *filename);

void glw_tex_deref(glw_root_t *gr, glw_texture_t *ht);

void glw_tex_layout(glw_root_t *gr, glw_texture_t *gt);

void glw_image_init(glw_root_t *gr);

void glw_texture_purge(glw_root_t *gr);

void glw_tex_is_active(glw_root_t *gr, glw_texture_t *gt);

void glw_tex_autoflush(glw_root_t *gr);

void glw_tex_upload(glw_texture_t *gt);

void glw_tex_flush_all(glw_root_t *gr);

#endif /* GLW_TEXTURE_H */
