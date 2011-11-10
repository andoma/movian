/*
 *  Pixmaps - Helpers for transfer of images between modules
 *  Copyright (C) 2009 Andreas Öman
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

#ifndef PIXMAP_H__
#define PIXMAP_H__

#include <inttypes.h>
#include "layout.h"


typedef enum {
  PIXMAP_PNG,
  PIXMAP_JPEG,
  PIXMAP_GIF,
  PIXMAP_coded,
  PIXMAP_BGR32,
  PIXMAP_RGB24,
  PIXMAP_IA,
  PIXMAP_I,
} pixmap_type_t;

#define pixmap_type_is_coded(t) ((t) < PIXMAP_coded)
#define pixmap_is_coded(pm) pixmap_type_is_coded((pm)->pm_type)

/**
 * Internal struct for passing images
 */
typedef struct pixmap {
  int pm_refcount;

  uint8_t pm_orientation;   // LAYOUT_ORIENTATION_ from layout.h

  uint16_t pm_width;
  uint16_t pm_height;
  uint16_t pm_lines;   // Lines of text
  uint16_t pm_margin;

  int pm_flags;


#define PIXMAP_THUMBNAIL 0x1       // This is a thumbnail
#define PIXMAP_TEXT_WRAPPED 0x2    // Contains wrapped text
#define PIXMAP_TEXT_ELLIPSIZED 0x4 // Contains ellipsized text

  pixmap_type_t pm_type;

  union {
    struct {
      uint8_t *pixels;
      int *charpos;
      int linesize;
      int charposlen;
    } raw;

    struct {
      void *data;
      size_t size;
    } codec;
  };

} pixmap_t;

#define pm_data codec.data
#define pm_size codec.size

#define pm_pixels     raw.pixels
#define pm_linesize   raw.linesize
#define pm_charpos    raw.charpos
#define pm_charposlen raw.charposlen

pixmap_t *pixmap_alloc_coded(const void *data, size_t size,
			     pixmap_type_t type);

pixmap_t *pixmap_dup(pixmap_t *pm);

void pixmap_release(pixmap_t *pm);

#define PIXMAP_BLUR        0
#define PIXMAP_EDGE_DETECT 1
#define PIXMAP_EMBOSS      2

pixmap_t *pixmap_convolution_filter(const pixmap_t *src, int kernel);

pixmap_t *pixmap_multiply_alpha(const pixmap_t *src);

pixmap_t *pixmap_extract_channel(const pixmap_t *src, unsigned int channel);

void pixmap_composite(pixmap_t *dst, const pixmap_t *src,
		      int xdisp, int ydisp, int rgba);

pixmap_t *pixmap_create(int width, int height, pixmap_type_t type,
			int rowalign);

#endif
