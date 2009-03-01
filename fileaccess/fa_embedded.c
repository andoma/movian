/*
 *  File browser
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "fileaccess.h"

#include "fa_proto.h"


typedef struct fa_embedded_fh {
  fa_handle_t h;
  unsigned char *ptr;
  int size;
  int pos;
} fa_embedded_fh_t;


/**
 * Open file
 */
static fa_handle_t *
emb_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  fa_embedded_fh_t *fh;
  unsigned char *ptr;
  int size;

  if(0) {
#ifdef CONFIG_EMBEDDED_THEME
  } else if(!strcmp(url, "theme")) {
    extern unsigned char embedded_theme[];
    extern int embedded_theme_size;
    ptr  = embedded_theme;
    size = embedded_theme_size;
#endif
  } else {
    snprintf(errbuf, errlen, "File not found");
    return NULL;
  }
  fh = calloc(1, sizeof(fa_embedded_fh_t));
  fh->ptr = ptr;
  fh->size = size;

  fh->h.fh_proto = fap;
  return &fh->h;
}


/**
 * Close file
 */
static void
emb_close(fa_handle_t *fh0)
{
  free(fh0);
}


/**
 * Read from file
 */
static int
emb_read(fa_handle_t *fh0, void *buf, size_t size)
{
  fa_embedded_fh_t *fh = (fa_embedded_fh_t *)fh0;

  if(size < 1)
    return size;

  if(fh->pos + size > fh->size)
    size = fh->size - fh->pos;
  memcpy(buf, fh->ptr + fh->pos, size);
  return size;
}


/**
 * Seek in file
 */
static int64_t
emb_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  fa_embedded_fh_t *fh = (fa_embedded_fh_t *)handle;
  int np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = fh->pos + pos;
    break;

  case SEEK_END:
    np = fh->size + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;

  fh->pos = np;
  return np;
}



/**
 * Return size of file
 */
static int64_t
emb_fsize(fa_handle_t *handle)
{
  fa_embedded_fh_t *fh = (fa_embedded_fh_t *)handle;
  return fh->size;
}



/**
 * Standard unix stat
 */
static int
emb_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	 char *errbuf, size_t errlen)
{
  fa_handle_t *handle;
  fa_embedded_fh_t *fh;

  if((handle = emb_open(fap, url, errbuf, errlen)) == NULL)
    return -1;
 
  fh = (fa_embedded_fh_t *)handle;

  buf->st_mode = S_IFREG;
  buf->st_size = fh->size;
  
  free(fh);
  return 0;
}





fa_protocol_t fa_protocol_embedded = {
  .fap_name  = "embedded",
  .fap_scan  = NULL,
  .fap_open  = emb_open,
  .fap_close = emb_close,
  .fap_read  = emb_read,
  .fap_seek  = emb_seek,
  .fap_fsize = emb_fsize,
  .fap_stat  = emb_stat,
};
