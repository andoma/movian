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

#include <libavcodec/avcodec.h>

/**
 * Based on JPEG/EXIF orientations
 *
 * http://sylvana.net/jpegcrop/exif_orientation.html
 */
#define PIXMAP_ORIENTATION_NONE       0
#define PIXMAP_ORIENTATION_NORMAL     1
#define PIXMAP_ORIENTATION_MIRROR_X   2
#define PIXMAP_ORIENTATION_ROT_180    3
#define PIXMAP_ORIENTATION_MIRROR_Y   4
#define PIXMAP_ORIENTATION_TRANSPOSE  5
#define PIXMAP_ORIENTATION_ROT_90     6
#define PIXMAP_ORIENTATION_TRANSVERSE 7
#define PIXMAP_ORIENTATION_ROT_270    8


/**
 * Internal struct for passing immages
 */
typedef struct pixmap {
  int pm_refcount;

  int pm_orientation;

  int pm_width;
  int pm_height;

  int pm_flags;

#define PIXMAP_THUMBNAIL 0x1 // This is a thumbnail

  enum CodecID pm_codec;

  // if pm_codec == CODEC_ID_NONE
  enum PixelFormat pm_pixfmt;
  AVPicture pm_pict;

  // if pm_codec != CODEC_ID_NONE
  void *pm_data;
  size_t pm_size;

} pixmap_t;

pixmap_t *pixmap_alloc_coded(const void *data, size_t size, 
			     enum CodecID codec);

pixmap_t *pixmap_create_rgb24(int width, int height, const void *pixels,
			      int pitch);

pixmap_t *pixmap_dup(pixmap_t *pm);

void pixmap_release(pixmap_t *pm);

#endif
