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

#include "fa_proto.h"

typedef struct fs_handle {
  fa_handle_t h;
  int fd;
} fs_handle_t;



static void
fs_urlsnprintf(char *buf, size_t bufsize, const char *prefix, const char *base,
	       const char *fname)
{
  if(!strcmp(base, "/"))
    base = "";
  snprintf(buf, bufsize, "%s%s/%s", prefix, base, fname);
}
	       


static int
fs_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  char buf[256];
  struct stat st;
  struct dirent *d;
  int type;
  DIR *dir;

  if((dir = opendir(url)) == NULL) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }
  
  while((d = readdir(dir)) != NULL) {
    fs_urlsnprintf(buf, sizeof(buf), "", url, d->d_name);

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
    
    fs_urlsnprintf(buf, sizeof(buf), "file://", url, d->d_name);

    fa_dir_add(fd, buf, d->d_name, type);
  }
  closedir(dir);
  return 0;
}

/**
 * Open file
 */
static fa_handle_t *
fs_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  fs_handle_t *fh;

  int fd = open(url, O_RDONLY);
  if(fd == -1) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return NULL;
  }
  fh = malloc(sizeof(fs_handle_t));
  fh->fd = fd;

  fh->h.fh_proto = fap;

  return &fh->h;
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
 * Read from file
 */
static int
fs_read(fa_handle_t *fh0, void *buf, size_t size)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;

  return read(fh->fd, buf, size);
}

/**
 * Seek in file
 */
static off_t
fs_seek(fa_handle_t *fh0, off_t pos, int whence)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  return lseek(fh->fd, pos, whence);
}

/**
 * Return size of file
 */
static off_t
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
fs_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	char *errbuf, size_t errlen)
{
  if(stat(url, buf)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }
  return 0;
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
