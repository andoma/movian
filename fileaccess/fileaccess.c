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

#include "fa_proto.h"

struct fa_protocol_list fileaccess_all_protocols;
static URLProtocol fa_lavf_proto;


/**
 *
 */
static char *
fa_resolve_proto(const char *url, fa_protocol_t **p,
		 const char *x_proto, const char *x_url)
{
  extern fa_protocol_t fa_protocol_fs;
  struct stat st;
  fa_protocol_t *fap;
  const char *url0 = url;
  char buf[100];
  int n = 0;

  while(*url != ':' && *url>31 && n < sizeof(buf) - 1)
    buf[n++] = *url++;

  buf[n] = 0;

  if(url[0] != ':' || url[1] != '/' || url[2] != '/') {
    /* No protocol specified, assume a plain file */
    fap = &fa_protocol_fs;
    if(fap->fap_stat(fap, url0, &st))
      return NULL;
    
    *p = fap;
    return strdup(url0);
  }

  url += 3;

  if(x_proto != NULL && !strcmp(x_proto, buf)) {
    snprintf(buf, sizeof(buf), "%s%s%s", 
	     x_url, x_url[strlen(x_url) - 1] == '/' ? "" : "/", url);
    return fa_resolve_proto(buf, p, NULL, NULL);
  }

  LIST_FOREACH(fap, &fileaccess_all_protocols, fap_link) {
    if(strcmp(fap->fap_name, buf))
      continue;
    *p = fap;
    return strdup(fap->fap_flags & FAP_INCLUDE_PROTO_IN_URL ? url0 : url);
  }
  return NULL;
}

/**
 *
 */
int
fa_can_handle(const char *url)
{
  fa_protocol_t *fap;
  char *filename;

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL)) == NULL)
    return 0;
  free(filename);
  return 1;
}

/**
 *
 */
void *
fa_open(const char *url)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL)) == NULL)
    return NULL;
  
  fh = fap->fap_open(fap, filename);
  free(filename);

  return fh;
}


/**
 *
 */
void *
fa_open_theme(const char *url, const char *themepath)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, "theme", themepath)) == NULL)
    return NULL;
  
  fh = fap->fap_open(fap, filename);
  free(filename);

  return fh;
}

/**
 *
 */
void
fa_close(void *fh_)
{
  fa_handle_t *fh = fh_;
  fh->fh_proto->fap_close(fh);
}

/**
 *
 */
int
fa_read(void *fh_, void *buf, size_t size)
{
  fa_handle_t *fh = fh_;
  return fh->fh_proto->fap_read(fh, buf, size);
}

/**
 *
 */
int64_t
fa_seek(void *fh_, int64_t pos, int whence)
{
  fa_handle_t *fh = fh_;
  return fh->fh_proto->fap_seek(fh, pos, whence);
}

/**
 *
 */
int64_t
fa_fsize(void *fh_)
{
  fa_handle_t *fh = fh_;
  return fh->fh_proto->fap_fsize(fh);
}

/**
 *
 */
int
fa_stat(const char *url, struct stat *buf)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL)) == NULL)
    return AVERROR_NOENT;
  
  r = fap->fap_stat(fap, filename, buf);
  free(filename);

  return r;
}



/**
 *
 */
fa_dir_t *
fa_scandir(const char *url)
{
  fa_protocol_t *fap;
  fa_dir_t *fd;
  char *filename;

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL)) == NULL)
    return NULL;

  if(fap->fap_scan != NULL) {
    fd = fa_dir_alloc();
    if(fap->fap_scan(fd, filename)) {
      fa_dir_free(fd);
      fd = NULL;
    }
  } else {
    fd = NULL;
  }
  free(filename);
  return fd;
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
  const fa_dir_entry_t *a = *(fa_dir_entry_t * const *)A;
  const fa_dir_entry_t *b = *(fa_dir_entry_t * const *)B;

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
int
fa_findfile(const char *path, const char *file, 
	    char *fullpath, size_t fullpathlen)
{
  fa_dir_t *fd = fa_scandir(path);
  fa_dir_entry_t *fde;

  if(fd == NULL)
    return -2;

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link)
    if(!strcasecmp(fde->fde_filename, file)) {
      snprintf(fullpath, fullpathlen, "%s%s%s", path, 
	       path[strlen(path)-1] == '/' ? "" : "/",
	       fde->fde_filename);
      fa_dir_free(fd);
      return 0;
    }

  fa_dir_free(fd);
  return -1;
}


/**
 *
 */
void *
fa_reference(const char *url)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL)) == NULL)
    return NULL;

  fh = fap->fap_reference != NULL ? fap->fap_reference(fap, filename) : NULL;
  free(filename);
  return fh;
}


/**
 *
 */
void
fa_unreference(void *fh_)
{
  fa_handle_t *fh = fh_;
  if(fh_ == NULL)
    return;

  fh->fh_proto->fap_unreference(fh);
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
  INITPROTO(http);
  INITPROTO(webdav);
  INITPROTO(embedded);
#ifdef HAVE_LIBSMBCLIENT
  INITPROTO(smb);
#endif
  register_protocol(&fa_lavf_proto);
}



/**
 * lavf -> fileaccess open wrapper
 */
static int
fa_lavf_open(URLContext *h, const char *url, int flags)
{
  fa_protocol_t *fap;
  void *fh;
  char *filename;

  av_strstart(url, "showtime:", &url);  

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL)) == NULL)
    return AVERROR_NOENT;
  
  fh = fap->fap_open(fap, filename);
  free(filename);
  
  if(fh == NULL) 
    return AVERROR_NOENT;
  
  h->priv_data = fh;
  return 0;
}

/**
 * lavf -> fileaccess read wrapper
 */
static int
fa_lavf_read(URLContext *h, unsigned char *buf, int size)
{
  return fa_read(h->priv_data, buf, size);
}

/**
 * lavf -> fileaccess seek wrapper
 */
static int64_t
fa_lavf_seek(URLContext *h, int64_t pos, int whence)
{
  if(whence == AVSEEK_SIZE)
    return fa_fsize(h->priv_data);
  
  return fa_seek(h->priv_data, pos, whence);
}

/**
 * lavf -> fileaccess close wrapper
 */
static int
fa_lavf_close(URLContext *h)
{
  fa_close(h->priv_data);
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
