/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#pragma once
#ifdef __PPC__
#define PIXMAP_ROW_ALIGN 16
#else
#define PIXMAP_ROW_ALIGN 8
#endif


#include <inttypes.h>
#include "misc/layout.h"
#include "image.h"

typedef enum {
  PIXMAP_none,
  PIXMAP_NULL,
  PIXMAP_BGR32,   // 32 bit, changes depending on endian
  PIXMAP_RGB24,   // R G B 24 bit packed
  PIXMAP_RGBA,    // R G B A  -- Always same byte order in RAM
  PIXMAP_BGRA,    // B G R A  -- Always same byte order in RAM
  PIXMAP_IA,
  PIXMAP_I,
} pixmap_type_t;


/**
 * Internal struct for passing images
 */
typedef struct pixmap {
  uint8_t *pm_data;

  atomic_t pm_refcount;

  float pm_aspect;

  pixmap_type_t pm_type;
  int pm_linesize;

  uint16_t pm_width;
  uint16_t pm_height;
  uint16_t pm_margin;

  float pm_intensity;

} pixmap_t;



#if 0
pixmap_t *pixmap_alloc_coded(const void *data, size_t size,
			     pixmap_type_t type);
#endif

pixmap_t *pixmap_dup(pixmap_t *pm);

void pixmap_release(pixmap_t *pm);

void pixmap_composite(pixmap_t *dst, const pixmap_t *src,
		      int xdisp, int ydisp, int rgba);

pixmap_t *pixmap_create(int width, int height, pixmap_type_t type,
			int margin);

void pixmap_box_blur(pixmap_t *pm, int boxw, int boxh);

pixmap_t *pixmap_decode(pixmap_t *pm, const image_meta_t *im,
			char *errbuf, size_t errlen);

int color_is_not_gray(uint32_t rgb);

void pixmap_horizontal_gradient(pixmap_t *pm, const int *top, const int *btm);

/**
 * PIXMAP_CORNER_ selects which corners to actually carve out
 * in pixmap_rounded_corners
 */
#define PIXMAP_CORNER_TOPLEFT     0x1
#define PIXMAP_CORNER_TOPRIGHT    0x2
#define PIXMAP_CORNER_BOTTOMLEFT  0x4
#define PIXMAP_CORNER_BOTTOMRIGHT 0x8

/**
 *
 */
#define PIXMAP_ALLOW_SMALL_ASPECT_DISTORTION  0x10

pixmap_t *pixmap_rounded_corners(pixmap_t *pm, int r, int which);

void pixmap_drop_shadow(pixmap_t *pm, int boxw, int boxh);

void pixmap_compute_rescale_dim(const image_meta_t *im,
				int src_width, int src_height,
				int *dst_width, int *dst_height);

void pixmap_intensity_analysis(pixmap_t *pm);

/**
 *
 */
static __inline int 
bytes_per_pixel(pixmap_type_t fmt)
{
  switch(fmt) {
  case PIXMAP_BGR32:
  case PIXMAP_RGBA:
  case PIXMAP_BGRA:
    return 4;

  case PIXMAP_RGB24:
    return 3;

  case PIXMAP_IA:
    return 2;
    
  case PIXMAP_I:
    return 1;

  default:
    return 0;
  }
}

static __inline void *
pm_pixel(pixmap_t *pm, unsigned int x, unsigned int y)
{
  return pm->pm_data + (y + pm->pm_margin) * pm->pm_linesize +
    (x + pm->pm_margin) * bytes_per_pixel(pm->pm_type);
}
