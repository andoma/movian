/*
 *  Functions for storing program settings
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

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "showtime.h"
#include "htsmsg.h"
#include "htsmsg_json.h"
#include "htsmsg_store.h"

extern char *showtime_settings_path;

/**
 *
 */
void
htsmsg_store_save(htsmsg_t *record, const char *pathfmt, ...)
{
  char path[PATH_MAX];
  char fullpath[PATH_MAX];
  char fullpath2[PATH_MAX];
  int x, l, fd;
  va_list ap;
  struct stat st;
  htsbuf_queue_t hq;
  htsbuf_data_t *hd;
  char *n;
  int ok;

  if(showtime_settings_path == NULL)
    return;

  va_start(ap, pathfmt);
  vsnprintf(path, sizeof(path), pathfmt, ap);
  va_end(ap);

  n = path;

  while(*n) {
    if(*n == ':' || *n == '?' || *n == '*' || *n > 127 || *n < 32)
      *n = '_';
    n++;
  }

  l = strlen(path);

  for(x = 0; x < l; x++) {
    if(path[x] == '/') {
      /* It's a directory here */

      path[x] = 0;
      snprintf(fullpath, sizeof(fullpath), "%s/%s", showtime_settings_path, path);

      if(stat(fullpath, &st) && mkdir(fullpath, 0700)) {
	TRACE(TRACE_ERROR, "settings", "Unable to create dir \"%s\": %s",
	       fullpath, strerror(errno));
	return;
      }
      path[x] = '/';
    }
  }

  snprintf(fullpath, sizeof(fullpath), "%s/%s.tmp", showtime_settings_path, path);

  if((fd = open(fullpath, O_CREAT | O_TRUNC | O_WRONLY, 0700)) < 0) {
    TRACE(TRACE_ERROR, "settings", "Unable to create \"%s\" - %s",
	    fullpath, strerror(errno));
    return;
  }

  ok = 1;

  htsbuf_queue_init(&hq, 0);
  htsmsg_json_serialize(record, &hq, 1);
 
  TAILQ_FOREACH(hd, &hq.hq_q, hd_link)
    if(write(fd, hd->hd_data + hd->hd_data_off, hd->hd_data_len) !=
       hd->hd_data_len) {
      TRACE(TRACE_ERROR, "settings", "Failed to write file \"%s\" - %s",
	      fullpath, strerror(errno));
      ok = 0;
      break;
    }

  close(fd);

  snprintf(fullpath2, sizeof(fullpath2), "%s/%s", showtime_settings_path, path);

  if(ok)
    rename(fullpath, fullpath2);
  else
    unlink(fullpath);
	   
  htsbuf_queue_flush(&hq);
}

/**
 *
 */
static htsmsg_t *
htsmsg_store_load_one(const char *filename)
{
  struct stat st;
  int fd;
  char *mem;
  htsmsg_t *r;
  int n;

  if(stat(filename, &st) < 0)
    return NULL;

  if((fd = open(filename, O_RDONLY, 0)) < 0)
    return NULL;

  mem = malloc(st.st_size + 1);
  mem[st.st_size] = 0;

  n = read(fd, mem, st.st_size);
  close(fd);
  if(n == st.st_size)
    r = htsmsg_json_deserialize(mem);
  else
    r = NULL;

  free(mem);

  return r;
}

/**
 *
 */
static int
htsmsg_store_buildpath(char *dst, size_t dstsize, const char *fmt, va_list ap)
{
  char *n = dst;

  if(showtime_settings_path == NULL)
     return -1;

  snprintf(dst, dstsize, "%s/", showtime_settings_path);

  vsnprintf(dst + strlen(dst), dstsize - strlen(dst), fmt, ap);

  while(*n) {
    if(*n == ':' || *n == '?' || *n == '*' || *n > 127 || *n < 32)
      *n = '_';
    n++;
  }
  return 0;
}

/**
 *
 */
htsmsg_t *
htsmsg_store_load(const char *pathfmt, ...)
{
  char fullpath[PATH_MAX];
  char child[PATH_MAX];
  va_list ap;
  struct stat st;
  struct dirent *d;
  htsmsg_t *r, *c;
  DIR *dir;

  va_start(ap, pathfmt);
  if(htsmsg_store_buildpath(fullpath, sizeof(fullpath), pathfmt, ap) < 0)
    return NULL;

  if(stat(fullpath, &st) != 0)
    return NULL;

  if(S_ISDIR(st.st_mode)) {

    if((dir = opendir(fullpath)) == NULL)
      return NULL;

    r = htsmsg_create_map();

    while((d = readdir(dir)) != NULL) {
      if(d->d_name[0] == '.')
	continue;
      
      snprintf(child, sizeof(child), "%s/%s", fullpath, d->d_name);
      c = htsmsg_store_load_one(child);
      if(c != NULL)
	htsmsg_add_msg(r, d->d_name, c);

    }
    closedir(dir);

  } else {
    r = htsmsg_store_load_one(fullpath);
  }

  return r;
}

/**
 *
 */
void
htsmsg_store_remove(const char *pathfmt, ...)
{
  char fullpath[PATH_MAX];
  va_list ap;

  va_start(ap, pathfmt);
  if(!htsmsg_store_buildpath(fullpath, sizeof(fullpath), pathfmt, ap))
    unlink(fullpath);
  va_end(ap);
}
