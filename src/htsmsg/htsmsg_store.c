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

#include "main.h"
#include "htsmsg.h"
#include "htsmsg_json.h"
#include "htsmsg_store.h"
#include "misc/callout.h"
#include "arch/arch.h"
#include "fileaccess/fileaccess.h"

#define SETTINGS_CACHE_DELAY 2000000 // micro seconds

LIST_HEAD(loaded_msg_list, loaded_msg);

typedef struct loaded_msg {
  LIST_ENTRY(loaded_msg) lm_link;
  htsmsg_t *lm_msg;
  char *lm_key;
  callout_t lm_timer;
  atomic_t lm_refcount;
  char lm_dirty;
} loaded_msg_t;

#define SETTINGS_TRACE(fmt, ...) do {            \
  if(gconf.enable_settings_debug) \
    TRACE(TRACE_DEBUG, "Settings", fmt, ##__VA_ARGS__); \
} while(0)


#ifdef PS3
#define RENAME_CANT_OVERWRITE 1
#else
#define RENAME_CANT_OVERWRITE 0
#endif

static struct loaded_msg_list loaded_msgs;
static hts_mutex_t loaded_msg_mutex;

static char *settings_path;

/**
 *
 */
static void
loaded_msg_release(loaded_msg_t *lm)
{
  if(atomic_dec(&lm->lm_refcount))
    return;
  free(lm->lm_key);
  free(lm);
}


/**
 *
 */
static int
htsmsg_store_buildpath(char *dst, size_t dstsize, const char *path)
{
  if(settings_path == NULL)
     return -1;

  snprintf(dst, dstsize, "%s/", settings_path);

  char *n = dst + strlen(dst);

  snprintf(n, dstsize - strlen(dst), "%s", path);

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
static void
loaded_msg_write(loaded_msg_t *lm)
{
  char fullpath[1024];
  char fullpath2[1024];
  char errbuf[512];
  htsbuf_queue_t hq;
  htsbuf_data_t *hd;
  int ok;

  snprintf(fullpath, sizeof(fullpath), "%s/%s%s",
	   settings_path, lm->lm_key,
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
  htsmsg_json_serialize(lm->lm_msg, &hq, 1);

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

  snprintf(fullpath2, sizeof(fullpath2), "%s/%s", settings_path,
           lm->lm_key);

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
static void
lm_destroy(loaded_msg_t *lm)
{
  if(lm->lm_dirty)
    loaded_msg_write(lm);
  htsmsg_release(lm->lm_msg);
  lm->lm_msg = NULL;
  LIST_REMOVE(lm, lm_link);
  callout_disarm(&lm->lm_timer);
  loaded_msg_release(lm);
}


/**
 *
 */
void
htsmsg_store_flush(void)
{
  loaded_msg_t *lm;
  hts_mutex_lock(&loaded_msg_mutex);
  while((lm = LIST_FIRST(&loaded_msgs)) != NULL)
    lm_destroy(lm);

  hts_mutex_unlock(&loaded_msg_mutex);

#ifdef STOS
  arch_sync_path(settings_path);
#endif
}




/**
 *
 */
void
htsmsg_store_init(void)
{
  char p1[1024];

  hts_mutex_init(&loaded_msg_mutex);

  if(gconf.persistent_path == NULL)
    return;

  snprintf(p1, sizeof(p1), "%s/settings", gconf.persistent_path);

  settings_path = strdup(p1);
}


/**
 *
 */
static void
htsmsg_store_timer_cb(struct callout *c, void *ptr)
{
  loaded_msg_t *lm = ptr;
  lm_destroy(lm);
}


/**
 *
 */
static int
htsmsg_store_lockmgr(void *ptr, lockmgr_op_t op)
{
  loaded_msg_t *lm = ptr;

  switch(op) {
  case LOCKMGR_UNLOCK:
    hts_mutex_unlock(&loaded_msg_mutex);
    return 0;
  case LOCKMGR_LOCK:
    hts_mutex_lock(&loaded_msg_mutex);
    return 0;
  case LOCKMGR_TRY:
    return hts_mutex_trylock(&loaded_msg_mutex);
  case LOCKMGR_RETAIN:
    atomic_inc(&lm->lm_refcount);
    return 0;
  case LOCKMGR_RELEASE:
    loaded_msg_release(lm);
    return 0;
  }
  abort();

}



/**
 *
 */
void
htsmsg_store_save(htsmsg_t *record, const char *key)
{
  char path[1024];
  loaded_msg_t *lm;

  if(htsmsg_store_buildpath(path, sizeof(path), key))
    return;

  hts_mutex_lock(&loaded_msg_mutex);

  LIST_FOREACH(lm, &loaded_msgs, lm_link)
    if(!strcmp(lm->lm_key, key))
      break;

  if(lm == NULL) {
    lm = calloc(1, sizeof(loaded_msg_t));
    atomic_set(&lm->lm_refcount, 1);
    lm->lm_key = strdup(key);
    LIST_INSERT_HEAD(&loaded_msgs, lm, lm_link);
  } else {
    htsmsg_release(lm->lm_msg);
  }

  lm->lm_msg = htsmsg_copy(record);

  lm->lm_dirty = 1;
  callout_arm_managed(&lm->lm_timer, htsmsg_store_timer_cb, lm,
                      SETTINGS_CACHE_DELAY, htsmsg_store_lockmgr);

  hts_mutex_unlock(&loaded_msg_mutex);
}


/**
 *
 */
static loaded_msg_t *
htsmsg_store_obtain(const char *path, int create)
{
  char fullpath[1024];
  char errbuf[512];
  htsmsg_t *r = NULL;
  loaded_msg_t *lm;

  if(htsmsg_store_buildpath(fullpath, sizeof(fullpath), path) < 0)
    return NULL;

  LIST_FOREACH(lm, &loaded_msgs, lm_link)
    if(!strcmp(lm->lm_key, path))
      return lm;

  buf_t *b = fa_load(fullpath,
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     NULL);
  if(b == NULL) {
    if(!create) {
      SETTINGS_TRACE("Unable to open %s -- %s", fullpath, errbuf);
      return NULL;
    }
  } else {
    r = htsmsg_json_deserialize(buf_cstr(b));
    buf_release(b);

    SETTINGS_TRACE("Read %s -- File %s", fullpath, r ? "OK" : "corrupted");

    if(r == NULL && !create)
      return NULL;
  }
  if(r == NULL)
    r = htsmsg_create_map();

  lm = calloc(1, sizeof(loaded_msg_t));
  atomic_set(&lm->lm_refcount, 1);
  lm->lm_key = strdup(path);
  LIST_INSERT_HEAD(&loaded_msgs, lm, lm_link);
  lm->lm_msg = r;

  callout_arm_managed(&lm->lm_timer, htsmsg_store_timer_cb, lm,
                      SETTINGS_CACHE_DELAY, htsmsg_store_lockmgr);

  return lm;
}


/**
 *
 */
htsmsg_t *
htsmsg_store_load(const char *path)
{
  htsmsg_t *r;

  hts_mutex_lock(&loaded_msg_mutex);

  loaded_msg_t *lm = htsmsg_store_obtain(path, 0);
  r = lm != NULL ? htsmsg_copy(lm->lm_msg) : NULL;
  hts_mutex_unlock(&loaded_msg_mutex);

  return r;
}

/**
 *
 */
void
htsmsg_store_remove(const char *path)
{
  char fullpath[1024];
  loaded_msg_t *lm;

  if(htsmsg_store_buildpath(fullpath, sizeof(fullpath), path))
    return;

  hts_mutex_lock(&loaded_msg_mutex);

  LIST_FOREACH(lm, &loaded_msgs, lm_link)
    if(!strcmp(lm->lm_key, path))
      break;

  if(lm != NULL) {
    lm->lm_dirty = 0;
    lm_destroy(lm);
  }

  fa_unlink(fullpath, NULL, 0);
  hts_mutex_unlock(&loaded_msg_mutex);
}


/**
 *
 */
void
htsmsg_store_set(const char *store, const char *key, int value_type, ...)
{
  va_list ap;
  va_start(ap, value_type);

  hts_mutex_lock(&loaded_msg_mutex);
  loaded_msg_t *lm = htsmsg_store_obtain(store, 1);

  htsmsg_delete_field(lm->lm_msg, key);

  switch(value_type) {
  case -1:
    break;
  case HMF_MAP:
  case HMF_LIST:
    htsmsg_add_msg(lm->lm_msg, key, va_arg(ap, htsmsg_t *));
    break;
  case HMF_S64:
    htsmsg_add_s64(lm->lm_msg, key, va_arg(ap, int64_t));
    break;
  case HMF_STR:
    htsmsg_add_str(lm->lm_msg, key, va_arg(ap, const char *));
    break;
  default:
    abort();
  }
  lm->lm_dirty = 1;
  callout_arm_managed(&lm->lm_timer, htsmsg_store_timer_cb, lm,
                      SETTINGS_CACHE_DELAY, htsmsg_store_lockmgr);
  hts_mutex_unlock(&loaded_msg_mutex);
  va_end(ap);
}


/**
 *
 */
int
htsmsg_store_get_int(const char *store, const char *key, int def)
{
  hts_mutex_lock(&loaded_msg_mutex);
  loaded_msg_t *lm = htsmsg_store_obtain(store, 1);
  int r = htsmsg_get_s32_or_default(lm->lm_msg, key, def);
  hts_mutex_unlock(&loaded_msg_mutex);
  return r;
}


/**
 *
 */
rstr_t *
htsmsg_store_get_str(const char *store, const char *key)
{
  hts_mutex_lock(&loaded_msg_mutex);
  loaded_msg_t *lm = htsmsg_store_obtain(store, 1);
  rstr_t *r = rstr_alloc(htsmsg_get_str(lm->lm_msg, key));
  hts_mutex_unlock(&loaded_msg_mutex);
  return r;
}

