/*
 *  Image loader, used by libglw
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "fileaccess.h"
#include "fa_imageloader.h"
#include "misc/pixmap.h"
#include "misc/jpeg.h"

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
static const uint8_t gif89sig[6] = {'G', 'I', 'F', '8', '9', 'a'};
static const uint8_t gif87sig[6] = {'G', 'I', 'F', '8', '7', 'a'};



typedef struct meminfo {
  const uint8_t *data;
  size_t size;
} meminfo_t;

/**
 *
 */
static int
jpeginfo_mem_reader(void *handle, void *buf, off_t offset, size_t size)
{
  meminfo_t *mi = handle;

  if(size + offset > mi->size)
    return -1;

  memcpy(buf, mi->data + offset, size);
  return size;
}

/**
 * Load entire image into memory using fileaccess quickload method.
 * Faster than open+read+close.
 */
static pixmap_t *
fa_imageloader2(const char *url, const char *theme,
		char *errbuf, size_t errlen)
{
  uint8_t *p;
  size_t size;
  meminfo_t mi;
  enum CodecID codec;
  int width = -1, height = -1, orientation = 0;

  if((p = fa_quickload(url, &size, theme, errbuf, errlen)) == NULL) 
    return NULL;

  mi.data = p;
  mi.size = size;

  /* Probe format */

  if((p[6] == 'J' && p[7] == 'F' && p[8] == 'I' && p[9] == 'F') ||
     (p[6] == 'E' && p[7] == 'x' && p[8] == 'i' && p[9] == 'f')) {
      
    jpeginfo_t ji;
    
    if(jpeg_info(&ji, jpeginfo_mem_reader, &mi, 
		 JPEG_INFO_DIMENSIONS | JPEG_INFO_ORIENTATION,
		 p, size, errbuf, errlen)) {
      free(p);
      return NULL;
    }

    codec = CODEC_ID_MJPEG;

    width = ji.ji_width;
    height = ji.ji_height;
    orientation = ji.ji_orientation;

    jpeg_info_clear(&ji);

  } else if(!memcmp(pngsig, p, 8)) {
    codec = CODEC_ID_PNG;
  } else if(!memcmp(gif87sig, p, sizeof(gif87sig)) ||
	    !memcmp(gif89sig, p, sizeof(gif89sig))) {
    codec = CODEC_ID_GIF;
  } else {
    snprintf(errbuf, errlen, "%s: unknown format", url);
    free(p);
    return NULL;
  }

  pixmap_t *pm = pixmap_alloc_coded(p, size, codec);
  pm->pm_width = width;
  pm->pm_height = height;
  pm->pm_orientation = orientation;

  free(p);
  return pm;
}


/**
 *
 */
static int
jpeginfo_reader(void *handle, void *buf, off_t offset, size_t size)
{
  if(fa_seek(handle, offset, SEEK_SET) != offset)
    return -1;
  return fa_read(handle, buf, size);
}


/**
 *
 */
pixmap_t *
fa_imageloader(struct backend *be, const char *url,
	       int want_thumb, const char *theme,
	       char *errbuf, size_t errlen)
{
  fa_handle_t *fh;
  uint8_t p[16];
  int r;
  enum CodecID codec;
  int width = -1, height = -1, orientation = 0;

  if(!want_thumb)
    return fa_imageloader2(url, theme, errbuf, errlen);

  if((fh = fa_open_theme(url, theme)) == NULL) {
    snprintf(errbuf, errlen, "%s: Unable to open file", url);
    return NULL;
  }

  if(fa_read(fh, p, sizeof(p)) != sizeof(p)) {
    snprintf(errbuf, errlen, "%s: file too short", url);
    fa_close(fh);
    return NULL;
  }

  /* Probe format */

  if((p[6] == 'J' && p[7] == 'F' && p[8] == 'I' && p[9] == 'F') ||
     (p[6] == 'E' && p[7] == 'x' && p[8] == 'i' && p[9] == 'f')) {
      
    jpeginfo_t ji;
    
    if(jpeg_info(&ji, jpeginfo_reader, fh, 
		 JPEG_INFO_DIMENSIONS |
		 JPEG_INFO_ORIENTATION |
		 (want_thumb ? JPEG_INFO_THUMBNAIL : 0),
		 p, sizeof(p), errbuf, errlen)) {
      fa_close(fh);
      return NULL;
    }

    if(want_thumb && ji.ji_thumbnail) {
      pixmap_t *pm = pixmap_dup(ji.ji_thumbnail);
      fa_close(fh);
      jpeg_info_clear(&ji);
      return pm;
    }

    codec = CODEC_ID_MJPEG;

    width = ji.ji_width;
    height = ji.ji_height;
    orientation = ji.ji_orientation;

    jpeg_info_clear(&ji);

  } else if(!memcmp(pngsig, p, 8)) {
    codec = CODEC_ID_PNG;
  } else if(!memcmp(gif87sig, p, sizeof(gif87sig)) ||
	    !memcmp(gif89sig, p, sizeof(gif89sig))) {
    codec = CODEC_ID_GIF;
  } else {
    snprintf(errbuf, errlen, "%s: unknown format", url);
    fa_close(fh);
    return NULL;
  }

  pixmap_t *pm = pixmap_alloc_coded(NULL, fa_fsize(fh), codec);

  if(pm == NULL) {
    snprintf(errbuf, errlen, "%s: no memory", url);
    fa_close(fh);
    return NULL;
  }

  pm->pm_width = width;
  pm->pm_height = height;
  pm->pm_orientation = orientation;
  fa_seek(fh, SEEK_SET, 0);
  r = fa_read(fh, pm->pm_data, pm->pm_size);
  fa_close(fh);

  if(r != pm->pm_size) {
    pixmap_release(pm);
    snprintf(errbuf, errlen, "%s: read error", url);
    return NULL;
  }
  return pm;
}
