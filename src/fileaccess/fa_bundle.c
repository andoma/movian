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
#include "filebundle.h"

#include "fa_proto.h"

struct filebundle *filebundles;


 typedef struct fa_bundle_fh {
  fa_handle_t h;
  const unsigned char *ptr;
  int size;
  int pos;
} fa_bundle_fh_t;


static const struct filebundle_entry *
resolve_entry(const char *url)
{
  struct filebundle *fb;
  const struct filebundle_entry *fbe;
  const char *u;

  for(fb = filebundles; fb != NULL; fb = fb->next) {
    if(strncmp(url, fb->prefix, strlen(fb->prefix)))
      continue;

    u = url + strlen(fb->prefix);
    if(*u != '/')
      continue;
    u++;

    for(fbe = fb->entries; fbe->filename != NULL; fbe++) {
      if(!strcmp(fbe->filename, u))
	return fbe;
    }
  }
  return NULL;
}


/**
 * Open file
 */
static fa_handle_t *
b_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  const struct filebundle_entry *fbe;
  fa_bundle_fh_t *fh;

  if((fbe = resolve_entry(url)) == NULL) {
    snprintf(errbuf, errlen, "File not found");
    return NULL;
  }
  fh = calloc(1, sizeof(fa_bundle_fh_t));
  fh->ptr = fbe->data;
  fh->size = fbe->size;

  fh->h.fh_proto = fap;
  return &fh->h;
}


/**
 * Close file
 */
static void
b_close(fa_handle_t *fh0)
{
  free(fh0);
}


/**
 * Read from file
 */
static int
b_read(fa_handle_t *fh0, void *buf, size_t size)
{
  fa_bundle_fh_t *fh = (fa_bundle_fh_t *)fh0;

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
b_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  fa_bundle_fh_t *fh = (fa_bundle_fh_t *)handle;
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
b_fsize(fa_handle_t *handle)
{
  fa_bundle_fh_t *fh = (fa_bundle_fh_t *)handle;
  return fh->size;
}



/**
 * Standard unix stat
 */
static int
b_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	 char *errbuf, size_t errlen)
{
  fa_handle_t *handle;
  fa_bundle_fh_t *fh;

  if((handle = b_open(fap, url, errbuf, errlen)) == NULL)
    return -1;
 
  fh = (fa_bundle_fh_t *)handle;

  buf->st_mode = S_IFREG;
  buf->st_size = fh->size;
  
  free(fh);
  return 0;
}


static fa_protocol_t fa_protocol_bundle = {
  .fap_name  = "bundle",
  .fap_scan  = NULL,
  .fap_open  = b_open,
  .fap_close = b_close,
  .fap_read  = b_read,
  .fap_seek  = b_seek,
  .fap_fsize = b_fsize,
  .fap_stat  = b_stat,
};
FAP_REGISTER(bundle);
