/*
 *  File access common functions
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

#define _GNU_SOURCE

#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "showtime.h"
#include "fileaccess.h"

#include <libavutil/avstring.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>

struct fa_protocol_list fileaccess_all_protocols;
static URLProtocol fa_lavf_proto;

/**
 * Glue struct for exposing fileaccess protocols into lavf
 */
typedef struct fa_lavf_glue {
  fa_protocol_t *fap;
  void *fh;
} fa_lavf_glue_t;


/**
 *
 */
const char *
fa_resolve_proto(const char *url, fa_protocol_t **p)
{
  fa_protocol_t *fap;

  char proto[30];
  int n = 0;

  while(*url != ':' && *url>31 && n < sizeof(proto) - 1)
    proto[n++] = *url++;

  proto[n] = 0;

  if(url[0] != ':' || url[1] != '/' || url[2] != '/')
    return NULL;

  url += 3;

  LIST_FOREACH(fap, &fileaccess_all_protocols, fap_link)
    if(!strcmp(fap->fap_name, proto)) {
      *p = fap;
      return url;
    }
  return NULL;
}


/**
 *
 */
int
fileaccess_scandir(const char *url, fa_scandir_callback_t *cb, void *arg)
{
  fa_protocol_t *fap;

  if((url = fa_resolve_proto(url, &fap)) == NULL)
    return -1;

  return fap->fap_scan(url, cb, arg);
}

/**
 *
 */
off_t
fileaccess_size(const char *url)
{
  fa_protocol_t *fap;
  off_t size;
  void *handle;

  if((url = fa_resolve_proto(url, &fap)) == NULL)
    return -1;

  if((handle = fap->fap_open(url)) == NULL)
    return -1;

  size = fap->fap_fsize(handle);

  fap->fap_close(handle);
  return size;
}



/**
 *
 */

#define INITPROTO(a)							      \
 {									      \
   extern  fa_protocol_t fa_protocol_ ## a;				      \
   LIST_INSERT_HEAD(&fileaccess_all_protocols, &fa_protocol_ ## a, fap_link); \
 }

void
fileaccess_init(void)
{
  INITPROTO(fs);
  register_protocol(&fa_lavf_proto);
}



/**
 * lavf -> fileaccess open wrapper
 */
static int
fa_lavf_open(URLContext *h, const char *filename, int flags)
{
  fa_protocol_t *fap;
  fa_lavf_glue_t *flg;
  void *fh;

  av_strstart(filename, "showtime:", &filename);  

  if((filename = fa_resolve_proto(filename, &fap)) == NULL)
    return AVERROR_NOENT;
  
  if((fh = fap->fap_open(filename)) == NULL)
    return AVERROR_NOENT;
  
  flg = malloc(sizeof(fa_lavf_glue_t));
  flg->fap = fap;
  flg->fh = fh;
  h->priv_data = flg;
  return 0;
}

/**
 * lavf -> fileaccess read wrapper
 */
static int
fa_lavf_read(URLContext *h, unsigned char *buf, int size)
{
  fa_lavf_glue_t *flg = h->priv_data;
  
  return flg->fap->fap_read(flg->fh, buf, size);
}

/**
 * lavf -> fileaccess seek wrapper
 */
static offset_t
fa_lavf_seek(URLContext *h, offset_t pos, int whence)
{
  fa_lavf_glue_t *flg = h->priv_data;
  
  if(whence == AVSEEK_SIZE)
    return flg->fap->fap_fsize(flg->fh);
  
  return flg->fap->fap_seek(flg->fh, pos, whence);
}

/**
 * lavf -> fileaccess close wrapper
 */
static int
fa_lavf_close(URLContext *h)
{
  fa_lavf_glue_t *flg = h->priv_data;

  flg->fap->fap_close(flg->fh);

  free(flg);
  h->priv_data = NULL;
  return 0;
}





static URLProtocol fa_lavf_proto = {
    "showtime",
    fa_lavf_open,
    fa_lavf_read,
    NULL,            /* Write */
    fa_lavf_seek,
    fa_lavf_close,
};
