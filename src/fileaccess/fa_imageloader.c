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

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <libavformat/avformat.h>
#include <libavutil/avstring.h>


#ifdef CONFIG_LIBEXIF
#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-loader.h>
#endif

#include "showtime.h"
#include "fileaccess.h"
#include "fa_imageloader.h"

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
static const uint8_t gif89sig[6] = {'G', 'I', 'F', '8', '9', 'a'};
static const uint8_t gif87sig[6] = {'G', 'I', 'F', '8', '7', 'a'};

/**
 *
 */
int 
fa_imageloader(const char *url, char *errbuf, size_t errlen,
	       int *thumb, void **data, size_t *datasize,
	       int *codecid, const char *theme, prop_pixmap_t **pixmap)
{
  fa_handle_t *fh;
  char p[16];
  int is_exif = 0;
  int r;

  *pixmap = NULL;

  if((fh = fa_open_theme(url, theme)) == NULL)
    return -1;

  if(fa_read(fh, p, sizeof(p)) != sizeof(p)) {
    TRACE(TRACE_INFO, "imageloader", "%s: file too short", url);
    fa_close(fh);
    return -1;
  }

  /* figure format */

  if(p[6] == 'J' && p[7] == 'F' && p[8] == 'I' && p[9] == 'F') {
    *codecid = CODEC_ID_MJPEG;
  } else if(p[6] == 'E' && p[7] == 'x' && p[8] == 'i' && p[9] == 'f') {
    *codecid = CODEC_ID_MJPEG;
    is_exif = 1;
  } else if(!memcmp(pngsig, p, 8)) {
    *codecid = CODEC_ID_PNG;
  } else if(!memcmp(gif87sig, p, sizeof(gif87sig)) ||
	    !memcmp(gif89sig, p, sizeof(gif89sig))) {
    *codecid = CODEC_ID_GIF;
  } else {
    TRACE(TRACE_INFO, "imageloader", "%s: unknown format", url);
    fa_close(fh);
    return -1;
  }
  

#ifdef CONFIG_LIBEXIF
  if(is_exif && *thumb) {
    unsigned char exifbuf[1024];
    int v, x;
    ExifLoader *l;
    ExifData *ed;

    l = exif_loader_new();

    v = exif_loader_write(l, (unsigned char *)p, sizeof(p));
    while(v) {
      if((x = fa_read(fh, exifbuf, sizeof(exifbuf))) < 1)
	break;
      v = exif_loader_write(l, exifbuf, x);
    }

    ed = exif_loader_get_data(l);
    exif_loader_unref (l);

    if(ed != NULL && ed->data != NULL) {
      fa_close(fh);
      *data = malloc(ed->size);
      memcpy(*data, ed->data, ed->size);
      *datasize = ed->size;
      exif_data_unref(ed);
      return 0;
    }
  }
#endif
  *thumb = 0;
  fa_seek(fh, SEEK_SET, 0);

  *datasize = fa_fsize(fh);
  *data = malloc(*datasize + FF_INPUT_BUFFER_PADDING_SIZE);
  
  r = fa_read(fh, *data, *datasize);

  memset(*data + *datasize, 0, FF_INPUT_BUFFER_PADDING_SIZE);

  fa_close(fh);

  if(r != *datasize) {
    free(*data);
    return -1;
  }
  return 0;
}
