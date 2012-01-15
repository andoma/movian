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

/**
 * EXIF Documentation
 * http://park2.wakwak.com/~tsuruzoh/Computer/Digicams/exif-e.html
 */

#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>

#include "jpeg.h"
#include "pixmap.h"


/**
 *
 */
typedef int (jiparser_t)(jpeginfo_t *ji, const uint8_t *buf, size_t len,
			 int flags);

/**
 *
 */
typedef struct jpegpriv {

  uint8_t *jp_readbuf;
  size_t jp_readbuf_offset;
  size_t jp_readbuf_end;
  void *jp_readhandle;
  jpegreader_t *jp_reader;

} jpegpriv_t;


/**
 *
 */
static int
parse_sof(jpeginfo_t *ji, const uint8_t *buf, size_t len, int flags)
{
  if(len < 5)
    return -1;

  ji->ji_height = buf[1] << 8 | buf[2];
  ji->ji_width  = buf[3] << 8 | buf[4];
  return 0;
}

static const char exifheader[6] = {0x45, 0x78, 0x69, 0x66, 0x00, 0x00};


/**
 *
 */
static time_t
jpeg_time(const char *d)
{
  struct tm tm = {0};
  char dummy;

  if(sscanf(d, "%d%c%d%c%d %d:%d:%d",
	    &tm.tm_year, &dummy, &tm.tm_mon, &dummy, &tm.tm_mday,
	    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 8)
    return 0;

  tm.tm_year -= 1900;
  tm.tm_isdst = -1;
  
#if ENABLE_TIMEGM
  return timegm(&tm);
#else
  return mktime(&tm);
#endif
}


/**
 *
 */
static int
parse_app1(jpeginfo_t *ji, const uint8_t *buf, size_t len, int flags)
{
  int bigendian;
  int ifdbase;
  int ifd = 0;


  int thumbnail_jpeg_offset = -1;
  int thumbnail_jpeg_size   = -1;

#define EXIF8(off) buf[off]

#define EXIF16(off) (bigendian ? \
 ((buf[off] << 8) | buf[off + 1]) : (buf[off + 1] << 8 | buf[off]))

#define EXIF32(off) (bigendian ? \
((buf[off  ] << 24) | (buf[off+1] << 16) | (buf[off+2] << 8) | (buf[off+3])):\
((buf[off+3] << 24) | (buf[off+2] << 16) | (buf[off+1] << 8) | (buf[off  ])))

#define IFDTAG(ifd, tag) (((ifd) << 16) | tag)

  // Exif Header
  if(len < 6 || memcmp(exifheader, buf, 6))
    return 0; // Don't fail here, just skip

  buf += 6;
  len -= 6;

  // TIFF header
  if(len < 8)
    return -1;

  if(buf[0] == 'M' && buf[1] == 'M')
    bigendian = 1;
  else if(buf[0] == 'I' && buf[1] == 'I')
    bigendian = 0;
  else
    return -1;

  //  printf(" EXIF/TIFF %s endian\n", bigendian ? "Big" : "Little");

  if(EXIF16(2) != 0x2a)
    return -1;

  ifdbase = EXIF32(4);

  while(ifdbase) {
    //    printf("  IDF Offset = %d\n", ifdbase);

    if(len < ifdbase + 2)
      return -1;

    int i, entries = EXIF16(ifdbase);
    //    printf("  %d entries\n", entries);

    if(len < ifdbase + 2 + entries * 12 + 4)
      return -1;

    for(i = 0; i < entries; i++) {
      uint16_t tag     = EXIF16(ifdbase + 2 + i * 12 + 0);
      uint16_t type    = EXIF16(ifdbase + 2 + i * 12 + 2);
      //      uint32_t c       = EXIF32(ifdbase + 2 + i * 12 + 4);

      int po = ifdbase + 2 + i * 12 + 8;
      int value = 0;
      const char *str = NULL;
      switch(type) {
      case 1:
	value = (uint8_t)  EXIF8(po);
	break;
      case 2:
	value = (uint32_t) EXIF32(po);
	str = (const char *)buf + value;
	break;
      case 3:
	value = (uint16_t) EXIF16(po);
	break;
      case 4:
	value = (uint32_t) EXIF32(po);
	break;
      case 6:
	value = (int8_t)   EXIF8(po);
	break;
      case 8:
	value = (int16_t)  EXIF8(po);
	break;
      }
      
      //      printf("  IFD%d  %04x (%d) * %d  ==  %d\n",  ifd, tag, type, c, value);

      switch(IFDTAG(ifd, tag)) {
      case IFDTAG(1, 0x201):  // JPEG Thumbnail offset
	thumbnail_jpeg_offset = value;
	break;
      case IFDTAG(1, 0x202):  // JPEG Thumbnail size
	thumbnail_jpeg_size = value;
	break;
      case IFDTAG(0, 0x112):  // Orientation
	ji->ji_orientation = value;
	break;
      case IFDTAG(0, 0x132):  // Datetime
	ji->ji_time = jpeg_time(str);
	break;

      default:
	break;
      }
    }

    ifd++;
    ifdbase = EXIF32(ifdbase + 2 + entries * 12);
  }

  if(flags & JPEG_INFO_THUMBNAIL && 
     thumbnail_jpeg_offset != -1 && thumbnail_jpeg_size != -1 &&
     thumbnail_jpeg_offset + thumbnail_jpeg_size <= len) {

    //    printf("  Thumbnail @ %d, %d bytes\n", thumbnail_jpeg_offset, thumbnail_jpeg_size);
    ji->ji_thumbnail = pixmap_alloc_coded(buf + thumbnail_jpeg_offset,
					  thumbnail_jpeg_size,
					  PIXMAP_JPEG);
    ji->ji_thumbnail->pm_flags |= PIXMAP_THUMBNAIL;
    ji->ji_thumbnail->pm_orientation = ji->ji_orientation;
  }
  return 0;
}


/**
 * Buffered reader
 */
static int
jpeg_read(jpegpriv_t *jp, void *buf, off_t offset, int size)
{
  int r;

  if(jp->jp_readbuf != NULL &&  offset >= jp->jp_readbuf_offset && 
     offset + size <= jp->jp_readbuf_end) {
    memcpy(buf, &jp->jp_readbuf[offset - jp->jp_readbuf_offset], size);
  } else {
    r = size + 1024;
    jp->jp_readbuf = realloc(jp->jp_readbuf, r);
    r = jp->jp_reader(jp->jp_readhandle, jp->jp_readbuf, offset, r);

    if(r < size)
      return -1;
    memcpy(buf, jp->jp_readbuf, size);
    jp->jp_readbuf_offset = offset;
    jp->jp_readbuf_end = offset + r;
  }

  return size;
}

            
/**
 *
 */
int
jpeg_info(jpeginfo_t *ji, jpegreader_t *reader, void *handle, int flags,
	  const uint8_t *buf, size_t len, char *errbuf, size_t errlen)
{
  uint16_t marker;
  uint16_t mlen;
  void *loadbuf = NULL;
  int offset = 0;
  jiparser_t *jip;

  memset(ji, 0, sizeof(jpeginfo_t));

  ji->ji_width = -1;
  ji->ji_height = -1;


  jpegpriv_t jp = {0};
  jp.jp_readhandle = handle;
  jp.jp_reader = reader;

  if(len < 2 || buf[0] != 0xff || buf[1] != 0xd8) {
    snprintf(errbuf, errlen, "Invalid JPEG header");
    return -1;
  }

  len -= 2;
  buf += 2;
  offset += 2;

  while(len >= 4) {
    marker = (buf[0] << 8) | buf[1];
    mlen   = (buf[2] << 8) | buf[3];
    len -= 4;
    buf += 4;
    offset += 4;
    mlen -= 2;

    jip = NULL;

    switch(marker) {
    case 0xffda: // SOS
      free(loadbuf);
      free(jp.jp_readbuf);
      return 0;

    case 0xffc0: // SOF0
    case 0xffc1: // SOF1
    case 0xffc2: // SOF2
    case 0xffc3: // SOF3
      if(flags & JPEG_INFO_DIMENSIONS)
	jip = parse_sof;
      break;

    case 0xffe1: // APP1
      if(flags & (JPEG_INFO_THUMBNAIL | JPEG_INFO_ORIENTATION |
		  JPEG_INFO_METADATA))
	jip = parse_app1;
      break;
    }


    if(jip) {
      int ll = mlen + 4;
      free(loadbuf);
      buf = loadbuf = malloc(ll);

      if(jpeg_read(&jp, loadbuf, offset, ll) != ll) {
	snprintf(errbuf, errlen, "Read error");
	break;
      }

      if(jip(ji, loadbuf, ll - 4, flags)) {
	snprintf(errbuf, errlen, "Error while  processing section 0x%04x",
		 marker);
	break;
      }
      // Continue with bytes after section
      buf = loadbuf + mlen;
      len = 4;
      offset += mlen;

    } else {

      free(loadbuf);
      buf = loadbuf = malloc(4);
      offset += mlen;
      if(jpeg_read(&jp, loadbuf, offset, 4) != 4) {
	snprintf(errbuf, errlen, "Read error");
	break;
      }
      len = 4;
    }
  }

  free(loadbuf);
  free(jp.jp_readbuf);
  return -1;
}


/**
 *
 */
void
jpeg_info_clear(jpeginfo_t *ji)
{
  if(ji->ji_thumbnail != NULL)
    pixmap_release(ji->ji_thumbnail);
}








#if 0

/**
 *
 */
static int
reader(void *handle, void *buf, off_t offset, size_t size)
{
  int fd = (int)handle;

  if(lseek(fd, offset, SEEK_SET) != offset)
    return -1;
  return read(fd, buf, size);
}

int
main(int argc, char **argv)
{
  struct stat st;
  int fd, r, i;
  void *b;
  char buf[16];
  jpeginfo_t ji = {0};

  for(i = 1; i < argc; i++) {

    fd = open(argv[i], O_RDONLY);

    if(read(fd, buf, sizeof(buf)) != sizeof(buf))
      exit(1);

    printf("%s\n", argv[i]);
    r = jpeghdrinfo(&ji, reader, (void *)fd, buf, sizeof(buf), NULL, 0);
    if(r) {
      printf("  Not an JPEG file\n");
      continue;
    }
    printf("  width = %d\n", ji.ji_width);
    printf(" height = %d\n", ji.ji_height);
    printf("\n");
  }
  return r;
}
#endif
