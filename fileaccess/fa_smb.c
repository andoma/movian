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

#define _GNU_SOURCE

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
static int
smb_scandir(const char *url, fa_scandir_callback_t *cb, void *arg)
{
  SMBCCTX *ctx = smb_get_thread_context();
  SMBCFILE *fd;
  struct smbc_dirent *dirent;
  char buf[256];

  if(ctx == NULL)
    return -1;

  if((fd = ctx->opendir(ctx, url)) == NULL)
    return -1;

  while((dirent = ctx->readdir(ctx, fd)) != NULL) {
    if(dirent->name[0] == 0 || dirent->name[0] == '.')
      continue;

    snprintf(buf, sizeof(buf), "%s/%s", url, dirent->name);

    switch(dirent->smbc_type) {
    case SMBC_FILE_SHARE:
    case SMBC_DIR:
      cb(arg, buf, dirent->name, FA_DIR);
      break;

    case SMBC_FILE:
      cb(arg, buf, dirent->name, FA_FILE);
      break;
    }
  }
  
  ctx->close_fn(ctx, fd);
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
