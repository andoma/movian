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
#include <libavutil/base64.h>

#include "fileaccess.h"
#include "fa_proto.h"
#include "misc/str.h"
#include "misc/minmax.h"

typedef struct data_handle {
  fa_handle_t fh;
  size_t size;
  int fpos;
  void *data;
} data_handle_t;

/**
 *
 */
static int64_t
data_seek(fa_handle_t *fh, int64_t pos, int whence, int lazy)
{
  data_handle_t *dh = (data_handle_t *)fh;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = dh->fpos + pos;
    break;

  case SEEK_END:
    np = dh->size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;
  dh->fpos = np;
  return np;
}


/**
 *
 */
static void
data_close(fa_handle_t *fh)
{
  data_handle_t *dh = (data_handle_t *)fh;
  free(dh->data);
}


/**
 *
 */
static int
data_read(fa_handle_t *fh, void *buf, size_t size)
{
  data_handle_t *dh = (data_handle_t *)fh;

  size = MIN(size, dh->size - dh->fpos);
  memcpy(buf, dh->data + dh->fpos, size);
  dh->fpos += size;
  return size;
}


/**
 *
 */
static int64_t
data_fsize(fa_handle_t *fh)
{
  data_handle_t *dh = (data_handle_t *)fh;
  return dh->size;
}



/**
 *
 */
static fa_handle_t *
data_open(struct fa_protocol *fap, const char *uri,
         char *errbuf, size_t errlen, int flags,
         struct fa_open_extra *foe)
{
  const char *sep = strchr(uri, ',');
  if(sep == NULL) {
    snprintf(errbuf, errlen, "No comma separator before payload");
    return NULL;
  }
  int hdrlen = sep - uri;
  if(find_str(uri, hdrlen, ";base64") == NULL) {
    snprintf(errbuf, errlen, "Data not base64 encoded");
    return NULL;
  }
  sep++;
  int len = strlen(sep);


  void *data = malloc(len);
  len = av_base64_decode(data, sep, len);
  if(len == -1) {
    snprintf(errbuf, errlen, "Invalid base64");
    free(data);
    return NULL;
  }

  data_handle_t *dh = calloc(1, sizeof(data_handle_t));
  dh->size = len;
  dh->data = data;

  dh->fh.fh_proto = fap;
  return &dh->fh;
}


fa_protocol_t fa_protocol_data = {
  .fap_name  = "data",
  .fap_open  = data_open,
  .fap_close = data_close,
  .fap_read  = data_read,
  .fap_seek  = data_seek,
  .fap_fsize = data_fsize,
};

FAP_REGISTER(data);
