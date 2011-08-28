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
#include <limits.h>
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
  int blen = strlen(base);
  if(!strcmp(base, "/"))
    base = "";

  snprintf(buf, bufsize, "%s%s%s%s", prefix, base,
	   blen > 0 && base[blen - 1] == '/' ? "" : "/", fname);
}
	       


static int
fs_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  char buf[URL_MAX];
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
fs_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	int flags, struct prop *stats)
{
  fs_handle_t *fh;

  int fd = open(url, O_RDONLY, 0);
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
static int64_t
fs_seek(fa_handle_t *fh0, int64_t pos, int whence)
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
	char *errbuf, size_t errlen, int non_interactive)
{
  struct stat st;
  if(stat(url, &st)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return FAP_STAT_ERR;
  }

  memset(fs, 0, sizeof(struct fa_stat));
  fs->fs_size = st.st_size;
  fs->fs_mtime = st.st_mtime;
  fs->fs_type = S_ISDIR(st.st_mode) ? CONTENT_DIR : CONTENT_FILE;
  return FAP_STAT_OK;
}


/**
 * FS change notification 
 */
#if ENABLE_INOTIFY
#include <sys/inotify.h>
#include <poll.h>

typedef struct notify_created_file {
  char *name;
  LIST_ENTRY(notify_created_file) link;

} notify_created_file_t;



static void
fs_notify(struct fa_protocol *fap, const char *url,
	  void *opaque,
	  void (*change)(void *opaque,
			 fa_notify_op_t op, 
			 const char *filename,
			 const char *url,
			 int type),
	  int (*breakcheck)(void *opaque))
{
  int fd, n;
  char buf[1024];
  char buf2[URL_MAX];
  struct pollfd fds;
  struct inotify_event *e;
  LIST_HEAD(, notify_created_file) pending_create;
  notify_created_file_t *ncf;

  if((fd = inotify_init()) == -1)
    return;

  fds.fd = fd;
  fds.events = POLLIN;

  if(inotify_add_watch(fd, url, IN_ONLYDIR | IN_CREATE | IN_CLOSE_WRITE | 
		       IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO) == -1) {
    TRACE(TRACE_DEBUG, "FS", "Unable to watch %s -- %s",
	  url, strerror(errno));
    return;
  }

  LIST_INIT(&pending_create);
  
  while(1) {
    n = poll(&fds, 1, 5000); // We'd like to call breakcheck() every 5 secs
    
    if(n < 0)
      break;

    if(breakcheck(opaque))
      break;

    if(n != 1)
      continue;
      
    n = read(fd, buf, sizeof(buf));
    e = (struct inotify_event *)&buf[0];

    if(e->len == 0)
      continue;

    fs_urlsnprintf(buf2, sizeof(buf2), "file://", url, e->name);

    if(e->mask & IN_CREATE) {
      if(e->mask & IN_ISDIR) {
	TRACE(TRACE_DEBUG, "FS", "Directory %s created in %s", e->name, url);
	change(opaque, FA_NOTIFY_ADD, e->name, buf2, CONTENT_DIR);
      } else {
	ncf = malloc(sizeof(notify_created_file_t));
	ncf->name = strdup(e->name);
	LIST_INSERT_HEAD(&pending_create, ncf, link);
	TRACE(TRACE_DEBUG, "FS", "File %s created in %s", e->name, url);
      }
    }

    if(e->mask & IN_CLOSE_WRITE) {

      LIST_FOREACH(ncf, &pending_create, link) 
	if(!strcmp(ncf->name, e->name))
	  break;

      if(ncf != NULL) {
	TRACE(TRACE_DEBUG, "FS", "File %s created and closed", e->name);
	change(opaque, FA_NOTIFY_ADD, e->name, buf2, 
	       e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
	LIST_REMOVE(ncf, link);
	free(ncf->name);
	free(ncf);
      }
    }

    if(e->mask & IN_DELETE) {
      TRACE(TRACE_DEBUG, "FS", "File %s deleted", e->name);
      change(opaque, FA_NOTIFY_DEL, e->name, buf2, 
	     e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
    }

    if(e->mask & IN_MOVED_FROM) {
      TRACE(TRACE_DEBUG, "FS", "File %s moved away from %s", e->name, url);
      change(opaque, FA_NOTIFY_DEL, e->name, buf2, 
	     e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
    }

    if(e->mask & IN_MOVED_TO) {
      TRACE(TRACE_DEBUG, "FS", "File %s moved in to %s", e->name, url);
      change(opaque, FA_NOTIFY_ADD, e->name, buf2, 
	     e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
    }
  }

  while((ncf = LIST_FIRST(&pending_create)) != NULL) {
    LIST_REMOVE(ncf, link);
    free(ncf->name);
    free(ncf);
  }

  close(fd);
}

#endif

#if ENABLE_REALPATH
/**
 *
 */
static int
fs_normalize(struct fa_protocol *fap, const char *url, char *dst, size_t dstlen)
{
  char res[PATH_MAX];
  
  if(realpath(url, res) == NULL)
    return -1;
  snprintf(dst, dstlen, "file://%s", res);
  return 0;
}
#endif


fa_protocol_t fa_protocol_fs = {
  .fap_name = "file",
  .fap_scan = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
#if ENABLE_INOTIFY
  .fap_notify = fs_notify,
#endif
#if ENABLE_REALPATH
  .fap_normalize = fs_normalize,
#endif
};

FAP_REGISTER(fs);
