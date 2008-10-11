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

#define _GNU_SOURCE /* for versionsort() */

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
#include "showtime.h"
#include "fileaccess.h"

static int 
scan_filter(const struct dirent *d)
{
  if(d->d_name[0] == '.')
    return 0;
  return 1;
}

static void
fs_urlsnprintf(char *buf, size_t bufsize, const char *prefix, const char *base,
	       const char *fname)
{
  if(!strcmp(base, "/"))
    base = "";
  snprintf(buf, bufsize, "%s%s/%s", prefix, base, fname);
}
	       


static int
fs_scandir(const char *url, fa_scandir_callback_t *cb, void *arg)
{
  char buf[1000];
  struct stat st;
  struct dirent **namelist, *d;
  int n, type, i;

  n = scandir(url, &namelist, scan_filter, versionsort);

  if(n < 0) {
    return errno;
  } else {
    for(i = 0; i < n; i++) {
      d = namelist[i];

      fs_urlsnprintf(buf, sizeof(buf), "", url, d->d_name);

      if(stat(buf, &st))
	continue;

      switch(st.st_mode & S_IFMT) {
      case S_IFDIR:
	type = FA_DIR;
	break;
      case S_IFREG:
	type = FA_FILE;
	break;
      default:
	continue;
      }


      fs_urlsnprintf(buf, sizeof(buf), "file://", url, d->d_name);

      cb(arg, buf, d->d_name, type);
    }
    free(namelist);
  }
  return 0;
}

/**
 * Open file
 */
static void *
fs_open(const char *url)
{
  int *h;
  int fd = open(url, O_RDONLY);
  if(fd == -1)
    return NULL;
  h = malloc(sizeof(int));
  *h = fd;
  return h;
}

/**
 * Close file
 */
static void
fs_close(void *handle)
{
  int fd = *(int *)handle;
  close(fd);
  free(handle);
}

/**
 * Read from file
 */
static int
fs_read(void *handle, void *buf, size_t size)
{
  int fd = *(int *)handle;

  return read(fd, buf, size);
}

/**
 * Seek in file
 */
static off_t
fs_seek(void *handle, off_t pos, int whence)
{
  int fd = *(int *)handle;
  return lseek(fd, pos, whence);
}

/**
 * Return size of file
 */
static off_t
fs_fsize(void *handle)
{
  int fd = *(int *)handle;
  struct stat st;
  
  if(fstat(fd, &st) < 0)
    return -1;

  return st.st_size;
}


/**
 * Standard unix stat
 */
static int
fs_stat(const char *url, struct stat *buf)
{
  return stat(url, buf);
}


fa_protocol_t fa_protocol_fs = {
  .fap_name = "file",
  .fap_scan = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
};

/**
 *
 */
static const char *
theme_get(void)
{
  return HTS_CONTENT_PATH "/showtime/themes/new";
}
/**
 *
 */
static int
theme_scandir(const char *url, fa_scandir_callback_t *cb, void *arg)
{
  char buf[200];

  snprintf(buf, sizeof(buf), "%s/%s", theme_get(), url);
  printf("Scanning %s\n", buf);
  return fs_scandir(buf, cb, arg);
}


/**
 *
 */
static void *
theme_open(const char *url)
{
  char buf[200];

  snprintf(buf, sizeof(buf), "%s/%s", theme_get(), url);
  return fs_open(buf);
}

/**
 *
 */
fa_protocol_t fa_protocol_theme = {
  .fap_name = "theme",
  .fap_scan = theme_scandir,
  .fap_open  = theme_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = NULL,
};
