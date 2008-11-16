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
    GT_STATE_WANT_PBO,
    GT_STATE_GOT_PBO,
    GT_STATE_VALID,
    GT_STATE_ERROR,
  } gt_state;

  hts_cond_t gt_cond;

  unsigned int gt_refcnt;

  float gt_aspect;

  unsigned int gt_texture;
  unsigned int gt_pbo;

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

glw_texture_t *glw_tex_create(const char *filename);

void glw_tex_deref(glw_texture_t *ht);

void glw_tex_layout(glw_texture_t *gt);

void glw_image_init(int concurrency);

void glw_texture_purge(void);

void glw_tex_is_active(glw_texture_t *gt);

void glw_tex_autoflush(void);

void glw_tex_upload(glw_texture_t *gt);

void glw_tex_flush_all(void);

#endif /* GLW_TEXTURE_H */
