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
int
fileaccess_scandir(const char *url, fa_scandir_callback_t *cb, void *arg)
{
  fa_protocol_t *fap;

  if((url = fa_resolve_proto(url, &fap)) == NULL)
    return EPROTONOSUPPORT;

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
typedef struct fa_dir {
  int entries;

  int curentry;

  struct dirent **vec;

} fa_dir_t;


/**
 *
 */
static void
fa_filldir(void *arg, const char *url, const char *filename, int type)
{
  fa_dir_t *fd = arg;
  struct dirent *d;
  int s, l = strlen(filename);

  fd->vec = realloc(fd->vec, sizeof(void *) * (fd->entries + 1));

  s = sizeof(struct dirent) - sizeof(d->d_name) + l + 1;

  d = malloc(s);
  d->d_ino = 0;
  d->d_off = 0;
  d->d_reclen = 0;
  d->d_type = type == FA_FILE ? DT_REG : DT_DIR;
  memcpy(d->d_name, filename, l);
  d->d_name[l] = 0;

  fd->vec[fd->entries++] = d;
}




/**
 *
 */
static void *
fa_opendir(const char *url)
{
  fa_protocol_t *fap;
  fa_dir_t *fd;

  if((url = fa_resolve_proto(url, &fap)) == NULL)
    return NULL;

  fd = calloc(1, sizeof(fa_dir_t));

  if(fap->fap_scan(url, fa_filldir, fd) < 0) {
    free(fd);
    return NULL;
  }

  return fd;
}


/**
 *
 */
static struct dirent *
fa_readdir(void *handle)
{
  fa_dir_t *fd = handle;
  if(fd->curentry == fd->entries)
    return NULL;

  return fd->vec[fd->curentry++];
}

/**
 *
 */
static void
fa_closedir(void *handle)
{
  fa_dir_t *fd = handle;
  int i;

  for(i = 0; i < fd->entries; i++)
    free(fd->vec[i]);
  free(fd);
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
  INITPROTO(theme);
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
static offset_t
fa_lavf_seek(URLContext *h, offset_t pos, int whence)
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
static offset_t
fa_svfs_seek(void *handle, offset_t pos, int whence)
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






/**
 * POSIX -> fileaccess open wrapper
 */
int
fa_posix_open(const char *filename, int flags, ...)
{
  fa_protocol_t *fap;
  fa_glue_t *glue;
  void *fh;

  if((filename = fa_resolve_proto(filename, &fap)) == NULL)
    return -1;
  
  if((fh = fap->fap_open(filename)) == NULL)
    return -1;
  
  glue = malloc(sizeof(fa_glue_t));
  glue->fap = fap;
  glue->fh = fh;
  return (int)glue;
}

/**
 * svfs -> fileaccess read wrapper
 */
ssize_t
fa_posix_read(int fd, void *buf, size_t size)
{
  fa_glue_t *glue = (void *)fd;
  
  return glue->fap->fap_read(glue->fh, buf, size);
}

/**
 * svfs -> fileaccess seek wrapper
 */
offset_t
fa_posix_seek(int fd, offset_t pos, int whence)
{
  fa_glue_t *glue = (void *)fd;
  
  return glue->fap->fap_seek(glue->fh, pos, whence);
}

/**
 * svfs -> fileaccess close wrapper
 */
int
fa_posix_close(int fd)
{
  fa_glue_t *glue = (void *)fd;

  glue->fap->fap_close(glue->fh);

  free(glue);
  return 0;
}


/**
 * svfs -> fileaccess close wrapper
 */
int
fa_posix_stat(const char *filename, struct stat *buf)
{
  fa_protocol_t *fap;

  if((filename = fa_resolve_proto(filename, &fap)) == NULL)
    return -1;
  
  return fap->fap_stat(filename, buf);
}
	
/**
 * svfs -> fileaccess close wrapper
 */
offset_t
fa_posix_filesize(int fd)
{
  fa_glue_t *glue = (void *)fd;

  return glue->fap->fap_fsize(glue->fh);
}
	
