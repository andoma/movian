/*
 *  File access using libsmbclient
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
#ifdef HAVE_LIBSMBCLIENT

#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <libsmbclient.h>

#include "showtime.h"
#include "fileaccess.h"


/**
 * We use pthread_key's to setup one smb client context per
 * thread. The contexts are destroyed when the threads exit.
 * Perhaps not perfect but without it we would have to pass
 * around a lot of pointers or have a global lock (ueck!)
 */

static pthread_key_t smb_thread_key;

static void smb_thread_destroyed(void *aux);

static void smb_auth(const char *server, const char *share,
		     char *wrkgrp, int wrkgrplen,
		     char *user,   int userlen,
		     char *passwd, int passwdlen);

/**
 *
 */
static void
smb_init(void)
{
  pthread_key_create(&smb_thread_key, smb_thread_destroyed);
}


/**
 *
 */
static void 
smb_thread_destroyed(void *aux)
{
  SMBCCTX *ctx = aux;
  smbc_free_context(ctx, 1);
}


/**
 *
 */
static SMBCCTX *
smb_get_thread_context(void)
{
  SMBCCTX *ctx = pthread_getspecific(smb_thread_key);
  if(ctx != NULL)
    return ctx;

  if((ctx = smbc_new_context()) == NULL)
    return NULL;
  
  ctx->callbacks.auth_fn = smb_auth;

  if(smbc_init_context(ctx) == NULL) {
    smbc_free_context(ctx, 1);
    return NULL;
  }

  pthread_setspecific(smb_thread_key, ctx);
  return ctx;
}
  

/**
 *
 */

typedef struct smbdirentry {
  int type;
  char name[0];
} smbdirentry_t;

static int
smb_scandir_sort(const void *A, const void *B)
{
  const smbdirentry_t *a = *(smbdirentry_t * const *)A;
  const smbdirentry_t *b = *(smbdirentry_t * const *)B;

  return strcmp(a->name, b->name);

}

static int
smb_scandir(const char *url, fa_scandir_callback_t *cb, void *arg)
{
  SMBCCTX *ctx = smb_get_thread_context();
  SMBCFILE *fd;
  struct smbc_dirent *dirent;
  char buf[URL_MAX];
  int l, i;

  /* Sorting */
  int svec_len, svec_size;
  smbdirentry_t **svec, *sde;

  if(ctx == NULL)
    return -1;

  if((fd = ctx->opendir(ctx, url)) == NULL)
    return -1;

  svec_size = 100;
  svec_len = 0;
  svec = malloc(svec_size * sizeof(struct smbdirentry_t *));

  while((dirent = ctx->readdir(ctx, fd)) != NULL) {
    if(dirent->name[0] == 0 || dirent->name[0] == '.')
      continue;

    l = strlen(dirent->name);
    sde = malloc(sizeof(smbdirentry_t) + l + 1);
    sde->type = dirent->smbc_type;
    memcpy(sde->name, dirent->name, l);
    sde->name[l] = 0;

    svec[svec_len++] = sde;

    if(svec_len == svec_size) {
      svec_size *= 2;
      svec = realloc(svec, svec_size * sizeof(struct smbdirentry_t *));
    }
  }

  ctx->close_fn(ctx, fd);

  qsort(svec, svec_len, sizeof(struct smbdirentry_t *), smb_scandir_sort);

  for(i = 0; i < svec_len; i++) {
  
    sde = svec[i];

    snprintf(buf, sizeof(buf), "%s/%s", url, sde->name);

    switch(sde->type) {
    case SMBC_FILE_SHARE:
    case SMBC_DIR:
      cb(arg, buf, sde->name, FA_DIR);
      break;

    case SMBC_FILE:
      cb(arg, buf, sde->name, FA_FILE);
      break;
    }
    free(sde);
  }
  free(svec);
  return 0;
}


/**
 * Open file
 */
static void *
smb_open(const char *url)
{
  SMBCCTX *ctx = smb_get_thread_context();
  return ctx->open(ctx, url, O_RDONLY, 0);
}


/**
 * Close file
 */
static void
smb_close(void *handle)
{
  SMBCCTX *ctx = smb_get_thread_context();
  ctx->close_fn(ctx, handle);
}


/**
 * Read from file
 */
static int
smb_read(void *handle, void *buf, size_t size)
{
  SMBCCTX *ctx = smb_get_thread_context();
  return ctx->read(ctx, handle, buf, size);
}


/**
 * Seek in file
 */
static off_t
smb_seek(void *handle, off_t pos, int whence)
{
  SMBCCTX *ctx = smb_get_thread_context();
  return ctx->lseek(ctx, handle, pos, whence);
}


/**
 * Return size of file
 */
static off_t
smb_fsize(void *handle)
{
  SMBCCTX *ctx = smb_get_thread_context();
  struct stat st;
  if(ctx->fstat(ctx, handle, &st) < 0)
    return -1;
  return st.st_size;
}


/**
 * Standard unix stat
 */
static int
smb_stat(const char *url, struct stat *buf)
{
  SMBCCTX *ctx = smb_get_thread_context();
  return ctx->stat(ctx, url, buf);
}


fa_protocol_t fa_protocol_smb = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL,
  .fap_init  = smb_init,
  .fap_name  = "smb",
  .fap_scan  = smb_scandir,
  .fap_open  = smb_open,
  .fap_close = smb_close,
  .fap_read  = smb_read,
  .fap_seek  = smb_seek,
  .fap_fsize = smb_fsize,
  .fap_stat  = smb_stat,
};

/**
 * Authentication callback
 */
static void 
smb_auth(const char *server, const char *share,
	 char *wrkgrp, int wrkgrplen,
	 char *user,   int userlen,
	 char *passwd, int passwdlen)
{
  
  printf("libsmbclient: Auth required for %s %s\n", server, share);
}

#endif /* HAVE_LIBSMBCLIENT */
