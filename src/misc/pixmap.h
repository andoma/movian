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

#ifdef __PPC__
#define PIXMAP_ROW_ALIGN 16
#else
#define PIXMAP_ROW_ALIGN 8
#endif


#include <inttypes.h>
#include "layout.h"


typedef enum {
  PIXMAP_none,
  PIXMAP_PNG,
  PIXMAP_JPEG,
  PIXMAP_GIF,
  PIXMAP_SVG,
  PIXMAP_coded,
  PIXMAP_NULL,
  PIXMAP_BGR32,
  PIXMAP_RGB24,
  PIXMAP_IA,
  PIXMAP_I,
} pixmap_type_t;

#define pixmap_type_is_coded(t) ((t) < PIXMAP_coded)
#define pixmap_is_coded(pm) pixmap_type_is_coded((pm)->pm_type)


/**
 *
 */
typedef struct image_meta {
  int im_want_thumb;
  int im_req_width;
  int im_req_height;
  int im_max_width;
  int im_max_height;
  char im_pot;
  char im_can_mono;
  char im_no_decoding;
  char im_32bit_swizzle; // can do full 32bit swizzle in hardware
  char im_no_rgb24;
  uint8_t im_corner_selection;
  uint16_t im_corner_radius;
  uint16_t im_margin;
} image_meta_t;

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

  float pm_aspect;

  int pm_flags;


#define PIXMAP_THUMBNAIL 0x1       // This is a thumbnail
#define PIXMAP_TEXT_WRAPPED 0x2    // Contains wrapped text
#define PIXMAP_TEXT_TRUNCATED 0x4 // Contains truncated text

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
			int margin);

void pixmap_box_blur(pixmap_t *pm, int boxw, int boxh);

pixmap_t *pixmap_decode(pixmap_t *pm, const image_meta_t *im,
			char *errbuf, size_t errlen);

pixmap_t *svg_decode(pixmap_t *pm, const image_meta_t *im,
		     char *errbuf, size_t errlen);

void svg_init(void);

int color_is_not_gray(uint32_t rgb);

void pixmap_horizontal_gradient(pixmap_t *pm, const int *top, const int *btm);

#define PIXMAP_CORNER_TOPLEFT     0x1
#define PIXMAP_CORNER_TOPRIGHT    0x2
#define PIXMAP_CORNER_BOTTOMLEFT  0x4
#define PIXMAP_CORNER_BOTTOMRIGHT 0x8

pixmap_t *pixmap_rounded_corners(pixmap_t *pm, int r, int which);

#endif
