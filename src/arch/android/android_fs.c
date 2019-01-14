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
#include <limits.h>
#include "main.h"
#include "misc/minmax.h"
#include "misc/str.h"

#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_proto.h"

#include "android.h"

char *android_fs_settings_path;
char *android_fs_cache_path;
char *android_fs_sdcard_path;


typedef struct fs_handle {
  fa_handle_t h;
  int fd;
} fs_handle_t;



static fa_err_code_t
android_url_to_path(const fa_protocol_t *fap, const char *url, int flags,
                    char *errbuf, size_t errlen, char **path)
{

  if(!strcmp(fap->fap_name, "persistent")) {
    *path = fmt("%s%s", android_fs_settings_path, url);
  } else if(!strcmp(fap->fap_name, "cache")) {
    *path = fmt("%s%s", android_fs_cache_path, url);
  } else if(!strcmp(fap->fap_name, "file")) {
    *path = strdup(url);
  } else {

    if(!android_get_permission(flags & FA_WRITE ?
                               "android.permission.WRITE_EXTERNAL_STORAGE" :
                               "android.permission.READ_EXTERNAL_STORAGE",
                               !(flags & FA_NON_INTERACTIVE))) {
      snprintf(errbuf, errlen, "Access rejected by user");
      TRACE(TRACE_INFO, "ANDROID_FS", "Access to %s://%s rejected by user",
            fap->fap_name, url);
      return FAP_PERMISSION_DENIED;
    }

    *path = fmt("%s%s", android_fs_sdcard_path, url);
  }
#if 0
  TRACE(TRACE_DEBUG, "ANDROID_FS", "Resolve %s://%s => %s",
        fap->fap_name, url, *path);
#endif
  return 0;
}


static int
fs_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
           char *errbuf, size_t errlen, int flags)
{
  char buf[URL_MAX];
  struct stat st;
  struct dirent *d;
  int type;
  DIR *dir;

  scoped_char *path = NULL;
  if(android_url_to_path(fap, url, 0, errbuf, errlen, &path))
    return -1;

  const char *sep = url[strlen(url) - 1] == '/' ? "" : "/";

  if((dir = opendir(path)) == NULL) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }

  while((d = readdir(dir)) != NULL) {
    if(!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
      continue;

    snprintf(buf, sizeof(buf), "%s/%s", path, d->d_name);

    if(stat(buf, &st))
      continue;

    switch(st.st_mode & S_IFMT) {
    case S_IFDIR:
      type = CONTENT_DIR;
      break;

    case S_IFREG:
      type = CONTENT_FILE;
      break;

    default:
      continue;
    }

    snprintf(buf, sizeof(buf), "%s://%s%s%s",
             fap->fap_name, url, sep, d->d_name);
    fa_dir_add(fd, buf, d->d_name, type);
  }
  closedir(dir);
  return 0;
}


/**
 * Close file
 */
static void
fs_close(fa_handle_t *fh0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  close(fh->fd);
  free(fh);
}


/**
 * Open file
 */
static fa_handle_t *
fs_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	int flags, struct fa_open_extra *foe)
{
  fs_handle_t *fh = NULL;
  int fd;

  scoped_char *path = NULL;
  if(android_url_to_path(fap, url, flags, errbuf, errlen, &path))
    return NULL;

  if(flags & FA_WRITE) {

    int open_flags = O_RDWR | O_CREAT;

    if(!(flags & FA_APPEND))
      open_flags |= O_TRUNC;

    fd = open(path, open_flags, 0666);


    if(fd != -1 && flags & FA_APPEND)
      lseek(fd, 0, SEEK_END);

  } else {
    fd = open(path, O_RDONLY, 0);
  }

  if(fd == -1) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return NULL;
  }


  fh = calloc(1, sizeof(fs_handle_t));
  fh->fd = fd;
  fh->h.fh_proto = fap;
  return &fh->h;
}

/**
 * Read from file
 */
static int
fs_read(fa_handle_t *fh0, void *buf, size_t size)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  return read(fh->fd, buf, size);
}


/**
 * Write to file
 */
static int
fs_write(fa_handle_t *fh0, const void *buf, size_t size)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  return write(fh->fd, buf, size);
}

/**
 * Seek in file
 */
static int64_t
fs_seek(fa_handle_t *fh0, int64_t pos, int whence, int lazy)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  return lseek(fh->fd, pos, whence);
}

/**
 * Return size of file
 */
static int64_t
fs_fsize(fa_handle_t *fh0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  struct stat st;
  if(fstat(fh->fd, &st) < 0)
    return -1;
  return st.st_size;
}


/**
 * Standard unix stat
 */
static int
fs_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	int flags, char *errbuf, size_t errlen)
{
  struct stat st;
  scoped_char *path = NULL;

  fa_err_code_t err =
    android_url_to_path(fap, url, flags, errbuf, errlen, &path);
  if(err)
    return err;

  if(stat(path, &st)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return FAP_ERROR;
  }

  memset(fs, 0, sizeof(struct fa_stat));
  fs->fs_size = st.st_size;
  fs->fs_mtime = st.st_mtime;
  fs->fs_type = S_ISDIR(st.st_mode) ? CONTENT_DIR : CONTENT_FILE;
  return FAP_OK;
}

/**
 * Rmdir (remove directory)
 */
static int
fs_rmdir(const fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  scoped_char *path = NULL;

  fa_err_code_t err =
    android_url_to_path(fap, url, FA_WRITE, errbuf, errlen, &path);
  if(err)
    return err;

  if(rmdir(path)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }
  return 0;
}


/**
 * Unlink (remove file)
 */
static int
fs_unlink(const fa_protocol_t *fap, const char *url,
          char *errbuf, size_t errlen)
{
  scoped_char *path = NULL;

  fa_err_code_t err =
    android_url_to_path(fap, url, FA_WRITE, errbuf, errlen, &path);
  if(err)
    return err;

  if(unlink(path)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }
  return 0;
}



/**
 *
 */
static fa_err_code_t
fs_makedir(struct fa_protocol *fap, const char *url)
{
  scoped_char *path = NULL;

  fa_err_code_t err =
    android_url_to_path(fap, url, FA_WRITE, NULL, 0, &path);
  TRACE(TRACE_ERROR, "ANDROID_FS", "makedir %s %s", url, path);
  if(err)
    return err;

  if(mkdir(path, 0770)) {
    switch(errno) {
    case ENOENT:  return FAP_NOENT;
    case EPERM:   return FAP_PERMISSION_DENIED;
    case EEXIST:  return FAP_EXIST;
    default:      return FAP_ERROR;
    }
  }
  return 0;
}


/**
 *
 */
static int
fs_rename(const fa_protocol_t *fap, const char *old_url, const char *new_url,
          char *errbuf, size_t errlen)
{
  scoped_char *old_path = NULL;
  scoped_char *new_path = NULL;
  fa_err_code_t err;

  err = android_url_to_path(fap, old_url, FA_WRITE, errbuf, errlen, &old_path);
  if(err)
    return err;
  err = android_url_to_path(fap, new_url, FA_WRITE, errbuf, errlen, &new_path);
  if(err)
    return err;

  if(rename(old_path, new_path)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }
  return 0;
}



static int
fs_ftruncate(fa_handle_t *fh0, uint64_t newsize)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  if(!ftruncate(fh->fd, newsize))
    return FAP_OK;
  return FAP_ERROR;
}


fa_protocol_t fa_protocol_es = {
  .fap_name = "es",
  .fap_scan = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_write = fs_write,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
  .fap_unlink= fs_unlink,
  .fap_rmdir = fs_rmdir,
  .fap_rename = fs_rename,
  .fap_makedir = fs_makedir,
  .fap_ftruncate = fs_ftruncate,
};
FAP_REGISTER(es);


fa_protocol_t fa_protocol_cache = {
  .fap_name = "cache",
  .fap_scan = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_write = fs_write,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
  .fap_unlink= fs_unlink,
  .fap_rmdir = fs_rmdir,
  .fap_rename = fs_rename,
  .fap_makedir = fs_makedir,
  .fap_ftruncate = fs_ftruncate,
};
FAP_REGISTER(cache);


fa_protocol_t fa_protocol_persistent = {
  .fap_name = "persistent",
  .fap_scan = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_write = fs_write,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
  .fap_unlink= fs_unlink,
  .fap_rmdir = fs_rmdir,
  .fap_rename = fs_rename,
  .fap_makedir = fs_makedir,
  .fap_ftruncate = fs_ftruncate,
};
FAP_REGISTER(persistent);


fa_protocol_t fa_protocol_file = {
  .fap_name = "file",
  .fap_scan = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_write = fs_write,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
  .fap_unlink= fs_unlink,
  .fap_rmdir = fs_rmdir,
  .fap_rename = fs_rename,
  .fap_makedir = fs_makedir,
  .fap_ftruncate = fs_ftruncate,
};

FAP_REGISTER(file);
