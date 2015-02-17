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
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "showtime.h"
#include "htsmsg.h"
#include "htsmsg_json.h"
#include "htsmsg_store.h"
#include "misc/callout.h"
#include "arch/arch.h"
#include "fileaccess/fileaccess.h"

#define SETTINGS_STORE_DELAY 2 // seconds

LIST_HEAD(pending_store_list, pending_store);


typedef struct pending_store {
  LIST_ENTRY(pending_store) ps_link;
  htsmsg_t *ps_msg;
  char *ps_path;
} pending_store_t;

#define SETTINGS_TRACE(fmt, ...) do {            \
  if(gconf.enable_settings_debug) \
    TRACE(TRACE_DEBUG, "Settings", fmt, ##__VA_ARGS__); \
} while(0)


#ifdef PS3
#define RENAME_CANT_OVERWRITE 1
#else
#define RENAME_CANT_OVERWRITE 0
#endif

static struct pending_store_list pending_stores;
static callout_t pending_store_callout;
static hts_mutex_t pending_store_mutex;
static char *showtime_settings_path;

/**
 *
 */
static void
pending_store_destroy(pending_store_t *ps)
{
  htsmsg_release(ps->ps_msg);
  free(ps->ps_path);
  free(ps);
}


/**
 *
 */
static void
pending_store_write(pending_store_t *ps)
{
  char fullpath[1024];
  char fullpath2[1024];
  char errbuf[512];
  htsbuf_queue_t hq;
  htsbuf_data_t *hd;
  int ok;

  snprintf(fullpath, sizeof(fullpath), "%s/%s%s",
	   showtime_settings_path, ps->ps_path,
	   RENAME_CANT_OVERWRITE ? "" : ".tmp");

  char *x = strrchr(fullpath, '/');
  if(x != NULL) {
    *x = 0;
    if(fa_makedirs(fullpath, errbuf, sizeof(errbuf))) {
      TRACE(TRACE_ERROR, "Settings", "Unable to create dir %s -- %s",
            fullpath, errbuf);
    }
    *x = '/';
  }

  fa_handle_t *fh =
    fa_open_ex(fullpath, errbuf, sizeof(errbuf), FA_WRITE, NULL);
  if(fh == NULL) {
    TRACE(TRACE_ERROR, "Settings", "Unable to create \"%s\" - %s",
	    fullpath, errbuf);
    return;
  }

  ok = 1;

  htsbuf_queue_init(&hq, 0);
  htsmsg_json_serialize(ps->ps_msg, &hq, 1);

  int bytes = 0;

  TAILQ_FOREACH(hd, &hq.hq_q, hd_link) {
    if(fa_write(fh, hd->hd_data + hd->hd_data_off, hd->hd_data_len) !=
       hd->hd_data_len) {
      TRACE(TRACE_ERROR, "Settings", "Failed to write file %s",
	      fullpath);
      ok = 0;
      break;
    }
    bytes += hd->hd_data_len;
  }
  htsbuf_queue_flush(&hq);
  //  fsync(fd);
  fa_close(fh);

  if(!ok) {
    fa_unlink(fullpath, NULL, 0);
    return;
  }

  snprintf(fullpath2, sizeof(fullpath2), "%s/%s", showtime_settings_path,
           ps->ps_path);

  if(!RENAME_CANT_OVERWRITE && fa_rename(fullpath, fullpath2,
                                         errbuf, sizeof(errbuf))) {

    TRACE(TRACE_ERROR, "Settings", "Failed to rename \"%s\" -> \"%s\" - %s",
	  fullpath, fullpath2, errbuf);
  } else {
    SETTINGS_TRACE("Wrote %d bytes to \"%s\"",
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
    LIST_REMOVE(ps, ps_link);
    hts_mutex_unlock(&pending_store_mutex);
    pending_store_write(ps);
    pending_store_destroy(ps);
    hts_mutex_lock(&pending_store_mutex);
  }
  hts_mutex_unlock(&pending_store_mutex);

#ifdef STOS
  arch_sync_path(showtime_settings_path);
#endif
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
  char p1[1024];

  hts_mutex_init(&pending_store_mutex);

  if(gconf.persistent_path == NULL)
    return;

  snprintf(p1, sizeof(p1), "%s/settings", gconf.persistent_path);

  showtime_settings_path = strdup(p1);
}


/**
 *
 */
void
htsmsg_store_save(htsmsg_t *record, const char *pathfmt, ...)
{
  char path[1024];
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
    htsmsg_release(ps->ps_msg);
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
  char errbuf[512];
  char *mem;
  htsmsg_t *r;
  int n;
  pending_store_t *ps;

  LIST_FOREACH(ps, &pending_stores, ps_link)
    if(!strcmp(ps->ps_path, filename))
      return htsmsg_copy(ps->ps_msg);

  fa_handle_t *fh = fa_open(filename, errbuf, sizeof(errbuf));
  if(fh == NULL) {
    SETTINGS_TRACE("Unable to open %s -- %s", filename, errbuf);
    return NULL;
  }

  int64_t size = fa_fsize(fh);

  mem = malloc(size + 1);
  if(mem == NULL) {
    fa_close(fh);
    return NULL;
  }

  n = fa_read(fh, mem, size);

  fa_close(fh);
  if(n == size)
    r = htsmsg_json_deserialize(mem);
  else
    r = NULL;

  SETTINGS_TRACE(
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
  if(showtime_settings_path == NULL)
     return -1;

  snprintf(dst, dstsize, "%s/", showtime_settings_path);

  char *n = dst + strlen(dst);
  
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
  char fullpath[1024];
  va_list ap;
  struct fa_stat st;
  htsmsg_t *r, *c;

  va_start(ap, pathfmt);
  if(htsmsg_store_buildpath(fullpath, sizeof(fullpath), pathfmt, ap) < 0)
    return NULL;

  hts_mutex_lock(&pending_store_mutex);

  if(fa_stat(fullpath, &st, NULL, 0) == 0 && st.fs_type == CONTENT_DIR) {

    fa_dir_t *fd = fa_scandir(fullpath, NULL, 0);
    fa_dir_entry_t *fde;
    if(fd == NULL) {
      hts_mutex_unlock(&pending_store_mutex);
      return NULL;
    }

    r = htsmsg_create_map();

    RB_FOREACH(fde, &fd->fd_entries, fde_link) {
      const char *filename = rstr_get(fde->fde_filename);
      if(filename[0] == '.')
	continue;

      c = htsmsg_store_load_one(rstr_get(fde->fde_url));
      if(c != NULL)
	htsmsg_add_msg(r, rstr_get(fde->fde_filename), c);

    }
    fa_dir_free(fd);

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
  char fullpath[1024];
  va_list ap;

  va_start(ap, pathfmt);
  if(!htsmsg_store_buildpath(fullpath, sizeof(fullpath), pathfmt, ap)) {

    pending_store_t *ps;

    hts_mutex_lock(&pending_store_mutex);

    LIST_FOREACH(ps, &pending_stores, ps_link)
      if(!strcmp(ps->ps_path, fullpath))
	break;
    
    if(ps != NULL) {
      LIST_REMOVE(ps, ps_link);
      pending_store_destroy(ps);
    }

    fa_unlink(fullpath, NULL, 0);
    hts_mutex_unlock(&pending_store_mutex);
  }
  va_end(ap);
}
