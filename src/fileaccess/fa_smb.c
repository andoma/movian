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
#include "fa_proto.h"
#include "keyring.h"

// libsmbclient is not thread safe.
// Welcome to 1980

static pthread_mutex_t smb_mutex;
static SMBCCTX *smb_ctx;
static int smb_statcode;
static int smb_non_interactive;

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
  smb_ctx = smbc_new_context();
  smb_ctx->callbacks.auth_fn = smb_auth;
  smbc_init_context(smb_ctx);
  pthread_mutex_init(&smb_mutex, NULL);
}


/**
 *
 */
typedef struct smbdirentry {
  int type;
  char name[0];
} smbdirentry_t;


/**
 *
 */
static int
smb_scandir(fa_dir_t *fa, const char *url, char *errbuf, size_t errsize)
{
  SMBCFILE *fd;
  struct smbc_dirent *dirent;
  char buf[URL_MAX];
  int l, i;

  /* Sorting */
  int svec_len, svec_size;
  smbdirentry_t **svec, *sde;

  pthread_mutex_lock(&smb_mutex);

  if((fd = smb_ctx->opendir(smb_ctx, url)) == NULL) {
    snprintf(errbuf, errsize, "%s", strerror(errno));
    pthread_mutex_unlock(&smb_mutex);
    return -1;
  }
  svec_size = 100;
  svec_len = 0;
  svec = malloc(svec_size * sizeof(struct smbdirentry_t *));

  while((dirent = smb_ctx->readdir(smb_ctx, fd)) != NULL) {
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

  smb_ctx->close_fn(smb_ctx, fd);

  for(i = 0; i < svec_len; i++) {
  
    sde = svec[i];

    snprintf(buf, sizeof(buf), "%s/%s", url, sde->name);

    switch(sde->type) {
    case SMBC_FILE_SHARE:
    case SMBC_DIR:
      fa_dir_add(fa, buf, sde->name, CONTENT_DIR);
      break;

    case SMBC_FILE:
      fa_dir_add(fa, buf, sde->name, CONTENT_FILE);
      break;
    }
    free(sde);
  }
  free(svec);
  pthread_mutex_unlock(&smb_mutex);
  return 0;
}


typedef struct smb_handle {
  fa_handle_t h;
  void *fd;
} smb_handle_t;

/**
 * Open file
 */
static fa_handle_t *
smb_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	 int flags, struct prop *stats)
{
  smb_handle_t *fh;
  pthread_mutex_lock(&smb_mutex);
  void *fd = smb_ctx->open(smb_ctx, url, O_RDONLY, 0);
  if(fd == NULL) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    pthread_mutex_unlock(&smb_mutex);
    return NULL;
  }
  fh = malloc(sizeof(smb_handle_t));
  fh->fd = fd;
  fh->h.fh_proto = fap;
  pthread_mutex_unlock(&smb_mutex);
  return &fh->h;
}


/**
 * Close file
 */
static void
smb_close(fa_handle_t *fh_)
{
  smb_handle_t *fh = (smb_handle_t *)fh_;
  pthread_mutex_lock(&smb_mutex);
  smb_ctx->close_fn(smb_ctx, fh->fd);
  free(fh);
  pthread_mutex_unlock(&smb_mutex);
}


/**
 * Read from file
 */
static int
smb_read(fa_handle_t *fh_, void *buf, size_t size)
{
  smb_handle_t *fh = (smb_handle_t *)fh_;
  pthread_mutex_lock(&smb_mutex);
  int r = smb_ctx->read(smb_ctx, fh->fd, buf, size);
  pthread_mutex_unlock(&smb_mutex);
  return r;
}


/**
 * Seek in file
 */
static int64_t
smb_seek(fa_handle_t *fh_, off_t pos, int whence)
{
  smb_handle_t *fh = (smb_handle_t *)fh_;
  pthread_mutex_lock(&smb_mutex);
  int64_t r = smb_ctx->lseek(smb_ctx, fh->fd, pos, whence);
  pthread_mutex_unlock(&smb_mutex);
  return r;
}


/**
 * Return size of file
 */
static off_t
smb_fsize(fa_handle_t *fh_)
{
  smb_handle_t *fh = (smb_handle_t *)fh_;
  struct stat st;

  pthread_mutex_lock(&smb_mutex);
  if(smb_ctx->fstat(smb_ctx, fh->fd, &st) < 0) {
    pthread_mutex_unlock(&smb_mutex);
    return -1;
  }
  pthread_mutex_unlock(&smb_mutex);
  return st.st_size;
}


/**
 * Standard unix stat
 */
static int
smb_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	 char *errbuf, size_t errlen, int non_interactive)
{
  struct stat st;
  pthread_mutex_lock(&smb_mutex);

  smb_statcode = -1;
  smb_non_interactive = non_interactive;

  int r = smb_ctx->stat(smb_ctx, url, &st);
  if(r)
    r = smb_statcode;
  else {
    memset(fs, 0, sizeof(struct fa_stat));
    fs->fs_size = st.st_size;
    fs->fs_mtime = st.st_mtime;
    fs->fs_type = S_ISDIR(st.st_mode) ? CONTENT_DIR : CONTENT_FILE;
  }

  pthread_mutex_unlock(&smb_mutex);
  return r;
}


static fa_protocol_t fa_protocol_smb = {
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
FAP_REGISTER(smb);

/**
 * Authentication callback
 */
static void 
smb_auth(const char *server, const char *share,
	 char *wrkgrp, int wrkgrplen,
	 char *user,   int userlen,
	 char *passwd, int passwdlen)
{
  char buf[256];
  char *username;
  char *password;
  char *domain;
  int query;

  snprintf(buf, sizeof(buf), "\\\\%s\\%s", server, share);
  for(query = 0; query < 2; query++) {

    if(query == 1 && smb_non_interactive) {
      smb_statcode = FAP_STAT_NEED_AUTH;
      return;
    }

    int r = keyring_lookup(buf, &username, &password, &domain, NULL,
			   "SMB Client", "Access denied", 
			   (query ? KEYRING_QUERY_USER : 0) |
			   KEYRING_SHOW_REMEMBER_ME | KEYRING_REMEMBER_ME_SET);

    if(r == KEYRING_OK)
      break;

    if(r == KEYRING_USER_REJECTED)
      return;
  }

  if(query == 2)
    return;

  if(domain)
    snprintf(wrkgrp, wrkgrplen, "%s", domain);

  if(username)
    snprintf(user, userlen, "%s", username);

  if(password)
    snprintf(passwd, passwdlen, "%s", password);
}

