/*
 *  GL Widgets, Bitmap/texture based texts
 *  Copyright (C) 2007 Andreas Öman
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

#ifndef GLW_TEXT_BITMAP_H
#define GLW_TEXT_BITMAP_H

#include "glw_bitmap.h"

typedef struct glw_text_bitmap_data {

  int gtbd_format;
  int gtbd_ext_format;
  int gtbd_ext_type;
  
  uint8_t *gtbd_data;
  int gtbd_siz_x;
  int gtbd_siz_y;
  int gtbd_stride;
  float gtbd_texsize;

  float gtbd_aspect;

  int *gtbd_cursor_pos;
  int gtbd_cursor_scale;
  int gtbd_cursor_pos_size;
} glw_text_bitmap_data_t;


typedef struct glw_text_bitmap {
  struct glw w;

  unsigned int gtb_texture;

  glw_renderer_t gtb_text_renderer;
  int gtb_text_renderer_inited;

  TAILQ_ENTRY(glw_text_bitmap) gtb_workq_link;
  LIST_ENTRY(glw_text_bitmap) gtb_global_link;

  glw_text_bitmap_data_t gtb_data;

  enum {
    GTB_NEED_RERENDER,
    GTB_ON_QUEUE,
    GTB_VALID
  } gtb_status;

  int cursor_flash;

  int *gtb_uc_buffer; /* unicode buffer */
  int gtb_uc_len;
  int gtb_uc_size;

  int gtb_edit_ptr;

  int gtb_int;
  int gtb_int_step;
  int gtb_int_min;
  int gtb_int_max;
  int *gtb_int_ptr;

} glw_text_bitmap_t;



int glw_text_bitmap_init(glw_root_t *gr);

void glw_text_bitmap_ctor(glw_t *w, int init, va_list ap);

void glw_text_flush(glw_root_t *gr);

#endif /* GLW_TEXT_BITMAP_H */
