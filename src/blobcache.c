/*
 *  Blob cache
 *  Copyright (C) 2010 Andreas Öman
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

#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>

#include "showtime.h"
#include "blobcache.h"
#include "db/db_support.h"
#include "misc/callout.h"
#include "arch/arch.h"
#include "arch/atomic.h"

static callout_t blobcache_callout;
static db_pool_t *cachedb_pool;

static void blobcache_do_prune(struct callout *c, void *opaque);

static int estimated_cache_size;

#define BLOB_CACHE_MINSIZE  (10 * 1000 * 1000)
#define BLOB_CACHE_MAXSIZE (100 * 1000 * 1000)


/**
 *
 */
static uint64_t 
blobcache_compute_maxsize(uint64_t csize)
{
  uint64_t avail = arch_cache_avail_bytes() + csize;
  avail = MAX(BLOB_CACHE_MINSIZE, MIN(avail / 10, BLOB_CACHE_MAXSIZE));
  return avail;
}


/**
 *
 */
static void
blobcache_put0(sqlite3 *db, const char *key, const char *stash,
	       const void *data, size_t size, int maxage,
	       const char *etag, time_t mtime)
{
  int rc;
  time_t now = time(NULL);

  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db,
			  "INSERT OR REPLACE INTO item "
			  "(k, stash, payload, lastaccess, expiry, etag, modtime) " 
			  "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
			  -1, &stmt, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return;
  }

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, stash, -1, SQLITE_STATIC);
  sqlite3_bind_blob(stmt, 3, data, size, SQLITE_STATIC);
  sqlite3_bind_int(stmt,  4, now);
  sqlite3_bind_int64(stmt,  5, (int64_t)now + maxage);

  if(etag != NULL)
    sqlite3_bind_text(stmt, 6, etag, -1, SQLITE_STATIC);

  if(mtime != 0)
    sqlite3_bind_int(stmt,  7, mtime);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  int s = atomic_add(&estimated_cache_size, size) + size;
  if(blobcache_compute_maxsize(s) < s && !callout_isarmed(&blobcache_callout))
    callout_arm(&blobcache_callout, blobcache_do_prune, NULL, 5);
}


/**
 *
 */
void
blobcache_put(const char *key, const char *stash,
	      const void *data, size_t size, int maxage,
	      const char *etag, time_t mtime)
{
  sqlite3 *db = db_pool_get(cachedb_pool);
  if(db == NULL)
    return;
  
  blobcache_put0(db, key, stash, data, size, maxage, etag, mtime);
  
  db_pool_put(cachedb_pool, db);
}


/**
 *
 */
static void *
blobcache_get0(sqlite3 *db, const char *key, const char *stash,
	       size_t *sizep, int pad, int *is_expired,
	       char **etagp, time_t *mtimep)
{
  int rc;
  void *rval = NULL;
  sqlite3_stmt *stmt;
  time_t now;

  if(db_begin(db))
    return NULL;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT payload,expiry,etag,modtime FROM item "
			  "WHERE k=?1 AND stash=?2",
			  -1, &stmt, NULL);
  if(rc) {
    db_rollback(db);
    return NULL;
  }

  time(&now);

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, stash, -1, SQLITE_STATIC);
  rc = sqlite3_step(stmt);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    db_rollback(db);
    return NULL;
  }

  int expired = now > sqlite3_column_int64(stmt, 1);

  if(!expired || is_expired != NULL) {
    sqlite3_column_blob(stmt, 0);
    size_t size = sqlite3_column_bytes(stmt, 0);
    if(size > 0) {
      rval = malloc(size + pad);
      memset(rval + size, 0, pad);
      memcpy(rval, sqlite3_column_blob(stmt, 0), size);
      *sizep = size;
    }
  }

  if(is_expired != NULL)
    *is_expired = expired;

  if(etagp != NULL) {
    const char *str = (const char *)sqlite3_column_text(stmt, 2);
    if(str != NULL)
      *etagp = strdup(str);
  }

  if(mtimep != NULL)
    *mtimep = sqlite3_column_int(stmt, 3);


  sqlite3_finalize(stmt);

  // Update atime

  rc = sqlite3_prepare_v2(db,
			  "UPDATE item SET "
			  "lastaccess = ?3 "
			  "WHERE k = ?1 AND stash = ?2",
			  -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    db_rollback(db);
    return rval;
  } else {
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, stash, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, time(NULL));
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  db_commit(db);
  return rval;
}


/**
 *
 */
void *
blobcache_get(const char *key, const char *stash, size_t *sizep, int pad,
	      int *ignore_expiry, char **etagp, time_t *mtimep)
{
  sqlite3 *db = db_pool_get(cachedb_pool);

  if(etagp != NULL)
    *etagp = NULL;

  if(mtimep != NULL)
    *mtimep = 0;

  if(db == NULL)
    return NULL;
  
  void *r = blobcache_get0(db, key, stash, sizep, pad, ignore_expiry,
			   etagp, mtimep);
  
  db_pool_put(cachedb_pool, db);
  return r;
}



/**
 *
 */
static int
blobcache_get_meta0(sqlite3 *db, const char *key, const char *stash,
		    char **etagp, time_t *mtimep)
{
  int rc;
  sqlite3_stmt *stmt;
  time_t now;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT etag,mtime FROM item "
			  "WHERE k=?1 AND stash=?2",
			  -1, &stmt, NULL);
  if(rc)
    return -1;

  time(&now);

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, stash, -1, SQLITE_STATIC);
  rc = sqlite3_step(stmt);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return -1;
  }

  const char *str = (const char *)sqlite3_column_text(stmt, 0);
  if(str != NULL)
    *etagp = strdup(str);

  if(mtimep != NULL)
    *mtimep = sqlite3_column_int(stmt, 1);

  sqlite3_finalize(stmt);
  return 0;
}


/**
 *
 */
int
blobcache_get_meta(const char *key, const char *stash, 
		   char **etagp, time_t *mtimep)
{
  sqlite3 *db = db_pool_get(cachedb_pool);

  *etagp = NULL;
  *mtimep = 0;

  if(db == NULL)
    return -1;
  
  int r = blobcache_get_meta0(db, key, stash, etagp, mtimep);

  db_pool_put(cachedb_pool, db);
  return r;
}


/**
 *
 */
static void
blobcache_prune_old(void)
{
  DIR *d1, *d2;
  struct dirent *de1, *de2;
  char path[PATH_MAX];
  char path2[PATH_MAX];
  char path3[PATH_MAX];

  snprintf(path, sizeof(path), "%s/blobcache", showtime_cache_path);

  if((d1 = opendir(path)) == NULL)
    return;

  while((de1 = readdir(d1)) != NULL) {
    if(de1->d_name[0] != '.') {
      snprintf(path2, sizeof(path2), "%s/blobcache/%s",
	       showtime_cache_path, de1->d_name);

      if((d2 = opendir(path2)) != NULL) {
	while((de2 = readdir(d2)) != NULL) {
          if(de2->d_name[0] != '.') {

	    snprintf(path3, sizeof(path3), "%s/blobcache/%s/%s",
		     showtime_cache_path, de1->d_name,
		     de2->d_name);
	    unlink(path3);
	  }
	}
	closedir(d2);
      }
      rmdir(path2);
    }
  }
  closedir(d1);
  rmdir(path);
}  


/**
 *
 */
void
blobcache_init(void)
{
  sqlite3 *db;
  extern char *showtime_cache_path;
  char buf[256];

  blobcache_prune_old();

  snprintf(buf, sizeof(buf), "%s/cachedb", showtime_cache_path);
  mkdir(buf, 0770);
  snprintf(buf, sizeof(buf), "%s/cachedb/cache.db", showtime_cache_path);

  //  unlink(buf);

  cachedb_pool = db_pool_create(buf, 4);
  db = db_pool_get(cachedb_pool);
  if(db == NULL)
    return;

  int r = db_upgrade_schema(db, "bundle://resources/cachedb", "cachedb");
  
  db_pool_put(cachedb_pool, db);

  if(r)
    cachedb_pool = NULL;
  else
    callout_arm(&blobcache_callout, blobcache_do_prune, NULL, 1);
}


void
blobcache_fini(void)
{
  db_pool_close(cachedb_pool);
}


/**
 *
 */
static void
blobcache_prune(sqlite3 *db)
{
  int rc;
  sqlite3_stmt *sel, *del;
  int64_t currentsize, pruned_bytes = 0;
  int pruned_items = 0;
  if(db_begin(db))
    return;

  if(db_get_int64_from_query(db, "SELECT sum(length(payload)) FROM item",
			     &currentsize)) {
    db_rollback(db);
    return;
  }
  estimated_cache_size = currentsize;

  uint64_t limit = blobcache_compute_maxsize(currentsize) * 9 / 10;
  if(currentsize <= limit) {
    db_rollback(db);
    return;
  }

  rc = sqlite3_prepare_v2(db, 
			  "SELECT _rowid_,length(payload) "
			  "FROM item "
			  "ORDER BY lastaccess",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error %d at %s:%d",
	  rc, __FUNCTION__, __LINE__);
    db_rollback(db);
    return;
  }

  while((rc = sqlite3_step(sel)) == SQLITE_ROW) {
    int itemsize = sqlite3_column_int(sel, 1);
    int64_t id = sqlite3_column_int64(sel, 0);

    rc = sqlite3_prepare_v2(db, "DELETE FROM item WHERE _rowid_ = ?1",
			    -1, &del, NULL);
    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      sqlite3_finalize(sel);
      db_rollback(db);
      return;
    }
    sqlite3_bind_int(del, 1, id);
    rc = sqlite3_step(del);
    sqlite3_finalize(del);

    if(rc != SQLITE_DONE) {
      sqlite3_finalize(sel);
      db_rollback(db);
      return;
    }

    currentsize -= itemsize;
    pruned_bytes += itemsize;
    pruned_items++;
    if(currentsize <= limit)
      break;
  }
  TRACE(TRACE_DEBUG, "CACHE", 
	"Pruned %d items, %"PRId64" bytes from cache",
	pruned_items, pruned_bytes);
  estimated_cache_size = currentsize;
  sqlite3_finalize(sel);
  db_commit(db);
  
}


/**
 *
 */
static void
blobcache_do_prune(struct callout *c, void *opaque)
{
  sqlite3 *db = db_pool_get(cachedb_pool);
  if(db != NULL)
    blobcache_prune(db);
  db_pool_put(cachedb_pool, db);
}
