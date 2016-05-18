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
#include <stdint.h>
#include <stdlib.h>

#include "arch/atomic.h"
#include "compiler.h"

struct buf;
struct pixmap;
struct fa_handle;

/**
 * Control struct for loading images
 */
typedef struct image_meta {
  float im_req_aspect;
  int im_req_width;
  int im_req_height;
  int im_max_width;
  int im_max_height;

  char im_can_mono : 1;
  char im_no_decoding : 1;
  char im_32bit_swizzle : 1; // can do full 32bit swizzle in hardware
  char im_want_thumb : 1;
  char im_intensity_analysis : 1;
  char im_primary_color_analysis : 1;
  char im_force_local_load : 1;

  uint8_t im_corner_selection;
  uint16_t im_corner_radius;
  uint16_t im_shadow;
  uint16_t im_margin;

  void *im_opaque;
  void (*im_incremental)(void *opaque, struct pixmap *pm);

} image_meta_t;


/**
 *
 */
typedef enum image_component_type {
  IMAGE_component_none,
  IMAGE_PIXMAP,
  IMAGE_CODED,
  IMAGE_VECTOR,
  IMAGE_TEXT_INFO,
} image_component_type_t;


/**
 * The values in this enum can be sent over wire in STPP so they
 * must not be changed (or STPP protocol version must be bumped)
 */
typedef enum image_coded_type {
  IMAGE_coded_none,
  IMAGE_PNG = 1,
  IMAGE_JPEG = 2,
  IMAGE_GIF = 3,
  IMAGE_SVG = 4,
  IMAGE_BMP = 5,
} image_coded_type_t;


/**
 *
 */
typedef struct image_component_coded {
  struct buf *icc_buf;
  image_coded_type_t icc_type;
} image_component_coded_t;


/**
 *
 */
typedef struct image_component_vector {
  union {
    int32_t *icv_int;
    float *icv_flt;
    void *icv_data;
  };
  int icv_used;
  int icv_capacity;
  int icv_colorized : 1;
} image_component_vector_t;


/**
 *
 */
typedef struct image_component_text_info {
  int *ti_charpos;
  uint16_t ti_charposlen;
  uint16_t ti_lines;
  uint16_t ti_flags;
#define IMAGE_TEXT_WRAPPED   0x1
#define IMAGE_TEXT_TRUNCATED 0x2
} image_component_text_info_t;


/**
 *
 */
typedef struct image_component {
  image_component_type_t type;

  union {
    struct pixmap *pm;
    image_component_coded_t coded;
    image_component_vector_t vector;
    image_component_text_info_t text_info;
  };

} image_component_t;


/**
 *
 */
typedef struct image {
  atomic_t im_refcount;

  uint16_t im_width;
  uint16_t im_height;
  uint16_t im_margin;
  uint16_t im_num_components;

  /*
   * These flags are sent over the wire in STPP so they
   * must not be changed (or STPP protocol version must be bumped)
   */
  uint16_t im_flags;
#define IMAGE_THUMBNAIL   0x1
#define IMAGE_PROGRESSIVE 0x2
#define IMAGE_ADAPTED     0x4  /* When an image is loaded and this flag
                                  is set the returned image is also dependant
                                  on the image_meta parameters. */
  uint8_t im_color_planes;
  uint8_t im_origin_coded_type;
  uint8_t im_orientation;

  image_component_t im_components[0];

} image_t;


/**
 *
 */
image_t *image_alloc(int num_components) attribute_malloc;

image_t *image_retain(image_t *img) attribute_unused_result;

void image_release(image_t *img);

void image_clear_component(image_component_t *ic);

void image_dump(const image_t *im, const char *prefix);

image_t *image_create_from_pixmap(struct pixmap *pm);

image_t *image_create_vector(int width, int height, int margin);

image_t *image_decode(image_t *img, const image_meta_t *im,
                      char *errbuf, size_t errlen);

image_t *image_rasterize_ft(const image_component_t *ic,
                            int with, int height, int margin);

/***************************************************************************
 * Coded images (JPEG, PNG, etc)
 */

image_t *image_coded_create_from_data(const void *data, size_t size,
                                      image_coded_type_t type);

image_t *image_coded_create_from_buf(struct buf *buf, image_coded_type_t type);

image_t *image_coded_alloc(void **datap, size_t size, image_coded_type_t type);

struct pixmap *image_decode_libav(image_coded_type_t type,
                                  struct buf *buf, const image_meta_t *im,
                                  char *errbuf, size_t errlen);

extern struct pixmap *(*accel_image_decode)(image_coded_type_t type,
					    struct buf *buf,
					    const image_meta_t *im,
					    char *errbuf, size_t errlen,
                                            const image_t *img);

#if ENABLE_LIBJPEG
struct pixmap *libjpeg_decode(struct fa_handle *fh,
                              const image_meta_t *meta,
                              char *errbuf, size_t errlen);
#endif

/***************************************************************************
 * SVG Parser
 */
image_t *svg_decode(struct buf *buf, const image_meta_t *im,
                    char *errbuf, size_t errlen);

image_t * nanosvg_decode(struct buf *buf, const image_meta_t *im,
                         char *errbuf, size_t errlen);



/**
 *
 */
static __inline image_component_t *
image_find_component(image_t *img, image_component_type_t type)
{
  if(img == NULL)
    return NULL;
  for(int i = 0; i < img->im_num_components; i++)
    if(img->im_components[i].type == type)
      return &img->im_components[i];
  return NULL;
}


/**
 * Dominant color computation
 */
void dominant_color(struct pixmap *pm);
