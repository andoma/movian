/*
 *  JPEG / Exif parser
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

#ifndef JPEG_H__
#define JPEG_H__

typedef int (jpegreader_t)(void *handle, void *buf, off_t offset, size_t size);

typedef struct jpeginfo {
  int ji_width;
  int ji_height;
  int ji_orientation;  // See orientation in pixmap.h
  struct pixmap *ji_thumbnail;
  time_t ji_time;
  struct rstr *ji_manufacturer;
  struct rstr *ji_equipment;
} jpeginfo_t;


#define JPEG_INFO_DIMENSIONS  0x1
#define JPEG_INFO_THUMBNAIL   0x2
#define JPEG_INFO_ORIENTATION 0x4
#define JPEG_INFO_METADATA    0x8

int jpeg_info(jpeginfo_t *ji, jpegreader_t *reader, void *handle, int flags,
	      const uint8_t *buf, size_t len, char *errbuf, size_t errlen);

void jpeg_info_clear(jpeginfo_t *ji);

/**
 *
 */
typedef struct jpeg_meminfo {
  const uint8_t *data;
  size_t size;
} jpeg_meminfo_t;


int jpeginfo_mem_reader(void *handle, void *buf, off_t offset, size_t size);

#endif /* JPEG_H__ */
