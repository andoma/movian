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

#include "config.h"

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "showtime.h"
#include "fileaccess.h"

#include <libavutil/avstring.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>

#include <libhts/svfs.h>

struct fa_protocol_list fileaccess_all_protocols;
static URLProtocol fa_lavf_proto;

/**
 * Glue struct for exposing fileaccess protocols into lavf
 */
typedef struct fa_glue {
  fa_protocol_t *fap;
  void *fh;
} fa_glue_t;


/**
 *
 */
const char *
fa_resolve_proto(const char *url, fa_protocol_t **p)
{
  fa_protocol_t *fap;
  const char *url0 = url;
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
      return fap->fap_flags & FAP_INCLUDE_PROTO_IN_URL ? url0 : url;
    }
  return NULL;
}


/**
 *
 */
fa_dir_t *
fileaccess_scandir(const char *url)
{
  fa_protocol_t *fap;
  fa_dir_t *fd;

  if((url = fa_resolve_proto(url, &fap)) == NULL)
    return NULL;

  fd = fa_dir_alloc();
  if(fap->fap_scan(fd, url)) {
    fa_dir_free(fd);
    return NULL;
  }

  return fd;
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
fa_dir_t *
fa_dir_alloc(void)
{
  fa_dir_t *fd = malloc(sizeof(fa_dir_t));
  TAILQ_INIT(&fd->fd_entries);
  fd->fd_count = 0;
  return fd;
}

/**
 *
 */
void
fa_dir_free(fa_dir_t *fd)
{
  fa_dir_entry_t *fde;

  while((fde = TAILQ_FIRST(&fd->fd_entries)) != NULL) {
    TAILQ_REMOVE(&fd->fd_entries, fde, fde_link);
    free(fde->fde_filename);
    free(fde->fde_url);
    free(fde);
  }
  free(fd);
}

/**
 *
 */
void
fa_dir_add(fa_dir_t *fd, const char *url, const char *filename, int type)
{
  fa_dir_entry_t *fde;

  if(filename[0] == '.')
    return; /* Skip all dot-filenames */

  fde = malloc(sizeof(fa_dir_entry_t));

  fde->fde_url      = strdup(url);
  fde->fde_filename = strdup(filename);
  fde->fde_type = type;

  TAILQ_INSERT_TAIL(&fd->fd_entries, fde, fde_link);
  fd->fd_count++;
}



static int 
fa_dir_sort_compar(const void *A, const void *B)
{
  fa_dir_entry_t *a = *(fa_dir_entry_t **)A;
  fa_dir_entry_t *b = *(fa_dir_entry_t **)B;

  return strcasecmp(a->fde_filename, b->fde_filename);
}

/**
 *
 */
void
fa_dir_sort(fa_dir_t *fd)
{
  fa_dir_entry_t **v;
  fa_dir_entry_t *fde;
  int i = 0;

  if(fd->fd_count == 0)
    return;

  v = malloc(fd->fd_count * sizeof(fa_dir_entry_t *));

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link)
    v[i++] = fde;

  qsort(v, fd->fd_count, sizeof(fa_dir_entry_t *), fa_dir_sort_compar);

  TAILQ_INIT(&fd->fd_entries);
  for(i = 0; i < fd->fd_count; i++)
    TAILQ_INSERT_TAIL(&fd->fd_entries, v[i], fde_link);
  
  free(v);
}

/**
 *
 */
#define INITPROTO(a)							      \
 {									      \
   extern  fa_protocol_t fa_protocol_ ## a;				      \
   LIST_INSERT_HEAD(&fileaccess_all_protocols, &fa_protocol_ ## a, fap_link); \
   if(fa_protocol_ ## a.fap_init != NULL) fa_protocol_ ## a.fap_init();	      \
 }

void
fileaccess_init(void)
{
  INITPROTO(fs);
  INITPROTO(rar);
  INITPROTO(zip);
  INITPROTO(theme);
  INITPROTO(http);
  INITPROTO(webdav);
#ifdef HAVE_LIBSMBCLIENT
  INITPROTO(smb);
#endif
  register_protocol(&fa_lavf_proto);
}



/**
 * lavf -> fileaccess open wrapper
 */
static int
fa_lavf_open(URLContext *h, const char *filename, int flags)
{
  fa_protocol_t *fap;
  fa_glue_t *glue;
  void *fh;

  av_strstart(filename, "showtime:", &filename);  

  if((filename = fa_resolve_proto(filename, &fap)) == NULL)
    return AVERROR_NOENT;
  
  if((fh = fap->fap_open(filename)) == NULL)
    return AVERROR_NOENT;
  
  glue = malloc(sizeof(fa_glue_t));
  glue->fap = fap;
  glue->fh = fh;
  h->priv_data = glue;
  return 0;
}

/**
 * lavf -> fileaccess read wrapper
 */
static int
fa_lavf_read(URLContext *h, unsigned char *buf, int size)
{
  fa_glue_t *glue = h->priv_data;
  
  return glue->fap->fap_read(glue->fh, buf, size);
}

/**
 * lavf -> fileaccess seek wrapper
 */
static int64_t
fa_lavf_seek(URLContext *h, int64_t pos, int whence)
{
  fa_glue_t *glue = h->priv_data;
  
  if(whence == AVSEEK_SIZE)
    return glue->fap->fap_fsize(glue->fh);
  
  return glue->fap->fap_seek(glue->fh, pos, whence);
}

/**
 * lavf -> fileaccess close wrapper
 */
static int
fa_lavf_close(URLContext *h)
{
  fa_glue_t *glue = h->priv_data;

  glue->fap->fap_close(glue->fh);

  free(glue);
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


#if 0

/**
 * Showtime VFS -> fileaccess open wrapper
 */
static void *
fa_svfs_open(const char *filename)
{
  fa_protocol_t *fap;
  fa_glue_t *glue;
  void *fh;

  if((filename = fa_resolve_proto(filename, &fap)) == NULL)
    return NULL;
  
  if((fh = fap->fap_open(filename)) == NULL)
    return NULL;
  
  glue = malloc(sizeof(fa_glue_t));
  glue->fap = fap;
  glue->fh = fh;
  return glue;
}

/**
 * svfs -> fileaccess read wrapper
 */
static int
fa_svfs_read(void *handle, void *buf, size_t size)
{
  fa_glue_t *glue = handle;
  
  return glue->fap->fap_read(glue->fh, buf, size);
}

/**
 * svfs -> fileaccess seek wrapper
 */
static int64_t
fa_svfs_seek(void *handle, int64_t pos, int whence)
{
  fa_glue_t *glue = handle;
  
  return glue->fap->fap_seek(glue->fh, pos, whence);
}

/**
 * svfs -> fileaccess close wrapper
 */
static void
fa_svfs_close(void *handle)
{
  fa_glue_t *glue = handle;

  glue->fap->fap_close(glue->fh);

  free(glue);
}


/**
 * svfs -> fileaccess close wrapper
 */
static int
fa_svfs_stat(const char *filename, struct stat *buf)
{
  fa_protocol_t *fap;

  if((filename = fa_resolve_proto(filename, &fap)) == NULL)
    return -1;
  
  return fap->fap_stat(filename, buf);
}
	



struct svfs_ops showtime_vfs_ops = {
  .open     = fa_svfs_open,
  .close    = fa_svfs_close,
  .read     = fa_svfs_read,
  .seek     = fa_svfs_seek,
  .stat     = fa_svfs_stat,
  .opendir  = fa_opendir,
  .readdir  = fa_readdir,
  .closedir = fa_closedir,
};
#endif
