/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

/**
 *
 */
typedef enum image_component_type {
  IMAGE_none,
  IMAGE_PIXMAP,
  IMAGE_CODED,
  IMAGE_VECTOR,
  IMAGE_TEXT_INFO,
} image_component_type_t;


/**
 *
 */
typedef struct image_component_coded {
  void *data;
  size_t size;
} image_component_coded_t;


/**
 *
 */
typedef struct image_component_vector {
  int32_t *commands;
  int count;
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
  int im_refcount;

  uint16_t im_width;
  uint16_t im_height;
  uint16_t im_margin;
  uint16_t im_num_components;

  image_component_t im_components[0];

} image_t;


/**
 *
 */
image_t *image_alloc(int num_components) __attribute__ ((malloc));

image_t *image_retain(image_t *img)  __attribute__ ((warn_unused_result));

void image_release(image_t *img);

void image_clear_component(image_component_t *ic);

void image_dump(const image_t *im, const char *prefix);

/**
 *
 */
static inline image_component_t *
image_find_component(image_t *img, image_component_type_t type)
{
  if(img == NULL)
    return NULL;
  for(int i = 0; i < img->im_num_components; i++)
    if(img->im_components[i].type == type)
      return &img->im_components[i];
  return NULL;
}
