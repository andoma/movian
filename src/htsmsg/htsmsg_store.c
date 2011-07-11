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
#include "misc/callout.h"

#define SETTINGS_STORE_DELAY 2 // seconds

extern char *showtime_persistent_path;
static char *showtime_settings_path;

LIST_HEAD(pending_store_list, pending_store);


typedef struct pending_store {
  LIST_ENTRY(pending_store) ps_link;
  htsmsg_t *ps_msg;
  char *ps_path;
} pending_store_t;


static int rename_cant_overwrite;
static struct pending_store_list pending_stores;
static callout_t pending_store_callout;
static hts_mutex_t pending_store_mutex;

/**
 *
 */
static void
pending_store_destroy(pending_store_t *ps)
{
  LIST_REMOVE(ps, ps_link);
  htsmsg_destroy(ps->ps_msg);
  free(ps->ps_path);
  free(ps);
}


/**
 *
 */
static void
pending_store_write(pending_store_t *ps)
{
  char *path;
  char fullpath[PATH_MAX];
  char fullpath2[PATH_MAX];
  int x, l, fd;
  struct stat st;
  htsbuf_queue_t hq;
  htsbuf_data_t *hd;
  int ok;

  path = mystrdupa(ps->ps_path);
  l = strlen(ps->ps_path);
  for(x = 0; x < l; x++) {
    if(path[x] == '/') {
      /* It's a directory here */

      path[x] = 0;
      snprintf(fullpath, sizeof(fullpath), "%s/%s", showtime_settings_path, path);

      if(stat(fullpath, &st) && mkdir(fullpath, 0700)) {
	TRACE(TRACE_ERROR, "Settings", "Unable to create dir \"%s\": %s",
	       fullpath, strerror(errno));
	return;
      }
      path[x] = '/';
    }
  }

 retry:

  snprintf(fullpath, sizeof(fullpath), "%s/%s%s",
	   showtime_settings_path, path, 
	   rename_cant_overwrite ? "" : ".tmp");

  if((fd = open(fullpath, O_CREAT | O_TRUNC | O_WRONLY, 0700)) < 0) {
    TRACE(TRACE_ERROR, "Settings", "Unable to create \"%s\" - %s",
	    fullpath, strerror(errno));
    return;
  }

  ok = 1;

  htsbuf_queue_init(&hq, 0);
  htsmsg_json_serialize(ps->ps_msg, &hq, 1);

  int bytes = 0;

  TAILQ_FOREACH(hd, &hq.hq_q, hd_link) {
    if(write(fd, hd->hd_data + hd->hd_data_off, hd->hd_data_len) !=
       hd->hd_data_len) {
      TRACE(TRACE_ERROR, "Settings", "Failed to write file \"%s\" - %s",
	      fullpath, strerror(errno));
      ok = 0;
      break;
    }
    bytes += hd->hd_data_len;
  }
  htsbuf_queue_flush(&hq);
  close(fd);

  if(!ok) {
    unlink(fullpath);
    return;
  }

  snprintf(fullpath2, sizeof(fullpath2), "%s/%s", showtime_settings_path, path);

  if(!rename_cant_overwrite && rename(fullpath, fullpath2)) {

    if(errno == EEXIST) {
      TRACE(TRACE_ERROR, "Settings", 
	    "Seems like rename() can not overwrite, retrying");
      rename_cant_overwrite = 1;
      goto retry;
    }

    TRACE(TRACE_ERROR, "Settings", "Failed to rename \"%s\" -> \"%s\" - %s",
	  fullpath, fullpath2, strerror(errno));
  } else {
    TRACE(TRACE_DEBUG, "Settings", "Wrote %d bytes to \"%s\"",
	  bytes, fullpath2);
  }
}


/**
 *
 */
void
htsmsg_store_flush(void)
{
  pending_store_t *ps;
  hts_mutex_lock(&pending_store_mutex);
  while((ps = LIST_FIRST(&pending_stores)) != NULL) {
    pending_store_write(ps);
    pending_store_destroy(ps);
  }
  hts_mutex_unlock(&pending_store_mutex);
}


/**
 *
 */
static void
pending_store_fire(struct callout *c, void *opaque)
{
  htsmsg_store_flush();
}


/**
 *
 */
void
htsmsg_store_init(void)
{
  char p1[PATH_MAX], p2[PATH_MAX];

  hts_mutex_init(&pending_store_mutex);

  if(showtime_persistent_path == NULL)
    return;

  snprintf(p1, sizeof(p1), "%s/settings",
	   showtime_persistent_path);

  showtime_settings_path = strdup(p1);

  if(!mkdir(p1, 0700)) {
    DIR *dir;
    struct dirent *d;

    if((dir = opendir(showtime_persistent_path)) != NULL) {
      while((d = readdir(dir)) != NULL) {
	if(d->d_name[0] == '.')
	  continue;

	snprintf(p1, sizeof(p1), "%s/%s",
		 showtime_persistent_path, d->d_name);

	snprintf(p2, sizeof(p2), "%s/settings/%s",
		 showtime_persistent_path, d->d_name);

	rename(p1, p2);
      }
    }
  }
}


/**
 *
 */
void
htsmsg_store_save(htsmsg_t *record, const char *pathfmt, ...)
{
  char path[PATH_MAX];
  va_list ap;
  char *n;
  pending_store_t *ps;

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

  hts_mutex_lock(&pending_store_mutex);



  LIST_FOREACH(ps, &pending_stores, ps_link)
    if(!strcmp(ps->ps_path, path))
      break;
  
  if(!callout_isarmed(&pending_store_callout))
    callout_arm(&pending_store_callout, pending_store_fire, NULL,
		SETTINGS_STORE_DELAY);

  if(ps == NULL) {
    ps = malloc(sizeof(pending_store_t));
    ps->ps_path = strdup(path);
    LIST_INSERT_HEAD(&pending_stores, ps, ps_link);
  } else {
    htsmsg_destroy(ps->ps_msg);
  }

  ps->ps_msg = htsmsg_copy(record);

  hts_mutex_unlock(&pending_store_mutex);
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
  pending_store_t *ps;

  LIST_FOREACH(ps, &pending_stores, ps_link)
    if(!strcmp(ps->ps_path, filename))
      return htsmsg_copy(ps->ps_msg);

  if(stat(filename, &st) < 0) {
    TRACE(TRACE_DEBUG, "Settings", 
	  "Trying to load %s -- %s", filename, strerror(errno));
    return NULL;
  }
  if((fd = open(filename, O_RDONLY, 0)) < 0) {
    TRACE(TRACE_ERROR, "Settings", 
	  "Unable to open %s -- %s", filename, strerror(errno));
    return NULL;
  }

  mem = malloc(st.st_size + 1);
  mem[st.st_size] = 0;

  n = read(fd, mem, st.st_size);

  close(fd);
  if(n == st.st_size)
    r = htsmsg_json_deserialize(mem);
  else
    r = NULL;

  TRACE(TRACE_DEBUG, "Settings", 
	"Read %s -- %d bytes. File %s", filename, n, r ? "OK" : "corrupted");

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

  hts_mutex_lock(&pending_store_mutex);

  if(stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {

    if((dir = opendir(fullpath)) == NULL) {
      hts_mutex_unlock(&pending_store_mutex);
      return NULL;
    }

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

  hts_mutex_unlock(&pending_store_mutex);

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
  if(!htsmsg_store_buildpath(fullpath, sizeof(fullpath), pathfmt, ap)) {

    pending_store_t *ps;

    hts_mutex_lock(&pending_store_mutex);

    LIST_FOREACH(ps, &pending_stores, ps_link)
      if(!strcmp(ps->ps_path, fullpath))
	break;
    
    if(ps != NULL)
      pending_store_destroy(ps);

    unlink(fullpath);
    hts_mutex_unlock(&pending_store_mutex);
  }
  va_end(ap);
}
