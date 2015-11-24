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
#include <stdio.h>
#include <unistd.h>

#include "main.h"
#include "fileaccess/fileaccess.h"
#include "misc/minmax.h"

#include "db_support.h"


typedef struct unlock_notify {
  int fired;
  hts_cond_t cond;
  hts_mutex_t mutex;
} unlock_notify_t;

static void
unlock_notify_cb(void **apArg, int nArg){
  int i;
  for(i=0; i<nArg; i++){
    unlock_notify_t *p = apArg[i];
    hts_mutex_lock(&p->mutex);
    p->fired = 1;
    hts_cond_signal(&p->cond);
    hts_mutex_unlock(&p->mutex);
  }
}

static int
wait_for_unlock_notify(sqlite3 *db)
{
  int rc;
  unlock_notify_t un;

  /* Initialize the UnlockNotification structure. */
  un.fired = 0;
  hts_mutex_init(&un.mutex);
  hts_cond_init(&un.cond, &un.mutex);

  /* Register for an unlock-notify callback. */
  rc = sqlite3_unlock_notify(db, unlock_notify_cb, (void *)&un);

  if(rc==SQLITE_OK ) {
    hts_mutex_lock(&un.mutex);
    while(!un.fired)
      hts_cond_wait(&un.cond, &un.mutex);
    hts_mutex_unlock(&un.mutex);
  }

  hts_cond_destroy(&un.cond);
  hts_mutex_destroy(&un.mutex);
  return rc;
}

int
db_step(sqlite3_stmt *pStmt)
{
  int rc;
  while( SQLITE_LOCKED==(rc = sqlite3_step(pStmt)) ){
    rc = wait_for_unlock_notify(sqlite3_db_handle(pStmt));
    if( rc!=SQLITE_OK ) break;
    sqlite3_reset(pStmt);
  }
  if(rc == SQLITE_LOCKED)
    TRACE(TRACE_DEBUG, "DB", "Deadlock detected");
  return rc;
}

int
db_preparex(sqlite3 *db, sqlite3_stmt **ppStmt, const char *zSql, 
	    const char *file, int line)
{
  int rc;

  while(SQLITE_LOCKED==(rc = sqlite3_prepare_v2(db, zSql, -1, ppStmt, NULL))) {
    rc = wait_for_unlock_notify(db);
    if( rc!=SQLITE_OK ) break;
  }

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error %d at %s:%d",
	  rc, file, line);
  }
  return rc;
}

/**
 *
 */
int
db_one_statement(sqlite3 *db, const char *sql, const char *src)
{
  int rc;
  char *errmsg;

  rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
  if(rc) {
    TRACE(TRACE_ERROR, "SQLITE", "%s: %s failed -- %s",
	  src ?: sql, sql, errmsg);
    sqlite3_free(errmsg);
  }
  return rc;
}


/**
 *
 */
int
db_get_int64_from_query(sqlite3 *db, const char *query, int64_t *v)
{
  int rc;
  int64_t rval = -1;
  sqlite3_stmt *stmt;

 restart:
  rc = db_prepare(db, &stmt, query);
  if(rc) {
    if(rc == SQLITE_LOCKED)
      goto restart;
    return -1;
  }

  rc = sqlite3_step(stmt);
  if(rc == SQLITE_LOCKED) {
    sqlite3_finalize(stmt);
    goto restart;
  }

  if(rc == SQLITE_ROW) {
    *v = sqlite3_column_int64(stmt, 0);
    rval = 0;
  } else {
    rval = -1;
  }

  sqlite3_finalize(stmt);
  return rval;
}


/**
 *
 */
int
db_get_int_from_query(sqlite3 *db, const char *query, int *v)
{
  int64_t i64;
  int r = db_get_int64_from_query(db, query, &i64);
  if(r == 0)
    *v = i64;
  return r;
}



int
db_begin0(sqlite3 *db, const char *src)
{
  return db == NULL || db_one_statement(db, "BEGIN;", src);
}


int
db_commit0(sqlite3 *db, const char *src)
{
  return  db == NULL || db_one_statement(db, "COMMIT;", src);
}


int
db_rollback0(sqlite3 *db, const char *src)
{
  return  db == NULL || db_one_statement(db, "ROLLBACK;", src);
}

int
db_rollback_deadlock0(sqlite3 *db, const char *src)
{
  int r = db == NULL || db_one_statement(db, "ROLLBACK;", src);
  TRACE(TRACE_DEBUG, "DB", "Rollback due to deadlock, and retrying");
  usleep(100000);
  return r;
}



/**
 *
 */
int
db_upgrade_schema(sqlite3 *db, const char *schemadir, const char *dbname,
                  const char *extra_db, const char *extra_db_path)
{
  int ver, tgtver = 0;
  char path[256];
  char buf[256];
  char detach[256];

  if(extra_db != NULL) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp),
             "ATTACH DATABASE '%s' AS %s", extra_db_path, extra_db);
    snprintf(detach, sizeof(detach), "DETACH DATABASE %s", extra_db);
    if(db_one_statement(db, tmp, NULL)) {
      TRACE(TRACE_ERROR, "DB", "%s: Unable to %s", tmp);
      return -1;
    }
  } else {
    detach[0] = 0;
  }

  db_one_statement(db, "pragma journal_mode=wal;", NULL);

  if(db_get_int_from_query(db, "pragma user_version", &ver)) {
    TRACE(TRACE_ERROR, "DB", "%s: Unable to query db version", dbname);
    if(detach[0]) db_one_statement(db, detach, NULL);
    return -1;
  }

  fa_dir_t *fd;
  fa_dir_entry_t *fde;

  fd = fa_scandir(schemadir, buf, sizeof(buf));

  if(fd == NULL) {
    TRACE(TRACE_ERROR, "DB",
	  "%s: Unable to scan schema dir %s -- %s", dbname, schemadir , buf);
    if(detach[0]) db_one_statement(db, detach, NULL);
    return -1;
  }

  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(fde->fde_type != CONTENT_FILE || strchr(rstr_get(fde->fde_filename), '~'))
      continue;
    tgtver = MAX(tgtver, atoi(rstr_get(fde->fde_filename)));
  }

  fa_dir_free(fd);

  if(ver > tgtver) {
    TRACE(TRACE_ERROR, "DB", "%s: Installed version %d is too high for "
	  "this version of "APPNAMEUSER, dbname, ver);
    if(detach[0]) db_one_statement(db, detach, NULL);
    return -1;
  }

  int enable_fk = 0;

  while(1) {

    if(ver == tgtver) {
      TRACE(TRACE_DEBUG, "DB", "%s: At current version %d", dbname, ver);
      if(detach[0]) db_one_statement(db, detach, NULL);
      return 0;
    }

    ver++;
    snprintf(path, sizeof(path), "%s/%03d.sql", schemadir, ver);

    buf_t *sql = fa_load(path,
                          FA_LOAD_ERRBUF(buf, sizeof(buf)),
                          NULL);
    if(sql == NULL) {
      TRACE(TRACE_ERROR, "DB",
	    "%s: Unable to upgrade db schema to version %d using %s -- %s",
	    dbname, ver, path, buf);
      if(detach[0]) db_one_statement(db, detach, NULL);
      return -1;
    }


    if(strstr(buf_cstr(sql), "-- schema-upgrade:disable-fk")) {
      db_one_statement(db, "PRAGMA foreign_keys=OFF;", NULL);
      enable_fk = 1;
    }

    db_begin(db);
    snprintf(buf, sizeof(buf), "PRAGMA user_version=%d", ver);
    if(db_one_statement(db, buf, NULL)) {
      free(sql);
      break;
    }

    const char *s = buf_cstr(sql);

    while(strchr(s, ';') != NULL) {
      sqlite3_stmt *stmt;

      int rc = sqlite3_prepare_v2(db, s, -1, &stmt, &s);
      if(rc != SQLITE_OK) {
	TRACE(TRACE_ERROR, "DB",
	      "%s: Unable to prepare statement in upgrade %d\n%s", dbname, ver, s);
	goto fail;
      }

      rc = sqlite3_step(stmt);
      if(rc != SQLITE_DONE) {
	TRACE(TRACE_ERROR, "DB",
	      "%s: Unable to execute statement error %d\n%s", dbname, rc, 
	      sqlite3_sql(stmt));
	goto fail;
      }
      sqlite3_finalize(stmt);
    }

    db_commit(db);
    if(enable_fk) {
      db_one_statement(db, "PRAGMA foreign_keys=ON;", NULL);
      enable_fk = 0;
    }
    TRACE(TRACE_INFO, "DB", "%s: Upgraded to version %d", dbname, ver);
    buf_release(sql);
  }
 fail:
  if(detach[0]) db_one_statement(db, detach, NULL);
  db_rollback(db);

  if(enable_fk)
    db_one_statement(db, "PRAGMA foreign_keys=ON;", NULL);
  return -1;
}


/**
 *
 */
struct db_pool {
  int dp_size;
  int dp_closed;
  char *dp_path;
  hts_mutex_t dp_mutex;
  sqlite3 *dp_pool[0];
};

/**
 *
 */
db_pool_t *
db_pool_create(const char *path, int size)
{
  db_pool_t *dp;
  
  dp = calloc(1, sizeof(db_pool_t) + sizeof(sqlite3 *) * size);
  dp->dp_size = size;
  dp->dp_path = strdup(path);
  hts_mutex_init(&dp->dp_mutex);
  return dp;
}


/**
 *
 */
sqlite3 *
db_open(const char *path, int flags)
{
  int rc;
  sqlite3 *db;

  rc = sqlite3_open_v2(path, &db,
		       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
		       SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_SHAREDCACHE,
		       NULL);

  if(rc) {
    TRACE(TRACE_ERROR, "DB", "%s: Unable to open database: %s",
	  path, sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  db_one_statement(db, "PRAGMA synchronous = normal", path);
  if(flags & DB_OPEN_CASE_SENSITIVE_LIKE)
    db_one_statement(db, "PRAGMA case_sensitive_like=1", path);
  db_one_statement(db, "PRAGMA foreign_keys=1", path);

  int freelist_count = 0;
  int page_count = 0;

  db_get_int_from_query(db, "PRAGMA freelist_count", &freelist_count);
  db_get_int_from_query(db, "PRAGMA page_count",     &page_count);

  TRACE(TRACE_DEBUG, "DB", "Opened database %s pages: free=%d total=%d",
        path, freelist_count, page_count);

  return db;
}


/**
 *
 */
sqlite3 *
db_pool_get(db_pool_t *dp)
{
  int i;
  sqlite3 *db;

  if(dp == NULL)
    return NULL;

  hts_mutex_lock(&dp->dp_mutex);

  if(dp->dp_closed) {
    hts_mutex_unlock(&dp->dp_mutex);
    return NULL;
  }

  for(i = 0; i < dp->dp_size; i++) {
    if(dp->dp_pool[i] != NULL) {
      db = dp->dp_pool[i];
      dp->dp_pool[i] = NULL;
      hts_mutex_unlock(&dp->dp_mutex);
      return db;
    }
  }

  hts_mutex_unlock(&dp->dp_mutex);

  return db_open(dp->dp_path, DB_OPEN_CASE_SENSITIVE_LIKE);
}

/**
 *
 */
void
db_pool_put(db_pool_t *dp, sqlite3 *db)
{
  int i;
  if(db == NULL)
    return;

  if(!sqlite3_get_autocommit(db)) {
    TRACE(TRACE_ERROR, "DB",
	  "%s: db handle returned to pool while in transaction, closing handle",
	  dp->dp_path);
    sqlite3_close(db);
    return;
  }

  hts_mutex_lock(&dp->dp_mutex);
  for(i = 0; i < dp->dp_size; i++) {
    if(dp->dp_pool[i] == NULL) {
      dp->dp_pool[i] = db;
      hts_mutex_unlock(&dp->dp_mutex);
      return;
    }
  }

  hts_mutex_unlock(&dp->dp_mutex);
  sqlite3_close(db);
}


/**
 *
 */
void
db_pool_close(db_pool_t *dp)
{
  int i;
  if(dp == NULL)
    return;

  hts_mutex_lock(&dp->dp_mutex);
  dp->dp_closed = 1;
  for(i = 0; i < dp->dp_size; i++)
    if(dp->dp_pool[i] != NULL)
      sqlite3_close(dp->dp_pool[i]);
  hts_mutex_unlock(&dp->dp_mutex);
}


/**
 *
 */
rstr_t *
db_rstr(sqlite3_stmt *stmt, int col)
{
  return rstr_alloc((const char *)sqlite3_column_text(stmt, col));
}


/**
 *
 */
int
db_posint(sqlite3_stmt *stmt, int col)
{
  if(sqlite3_column_type(stmt, col) == SQLITE_INTEGER)
    return sqlite3_column_int(stmt, col);
  return -1;
}


/**
 *
 */
void
db_escape_path_query(char *dst, size_t dstlen, const char *src)
{
  for(; *src && dstlen > 3; dstlen--) {
    if(*src == '%' || *src == '_') {
      *dst++ = '\\';
      *dst++ = *src++;
      dstlen--;
    } else {
      *dst++ = *src++;
    }
  }
  *dst++ = '%';
  *dst = 0;
}




#if ENABLE_SQLITE_LOCKING

/**
 * Sqlite mutex helpers
 */
static hts_lwmutex_t static_mutexes[6];

static int
sqlite_mutex_init(void)
{
  int i;
  for(i = 0; i < 6; i++)
    hts_lwmutex_init(&static_mutexes[i]);
  return SQLITE_OK;
}

static int
sqlite_mutex_end(void)
{
  return SQLITE_OK;
}

static sqlite3_mutex *
sqlite_mutex_alloc(int id)
{
  hts_lwmutex_t *m;

  switch(id) {
  case SQLITE_MUTEX_FAST:
    m = malloc(sizeof(hts_lwmutex_t));
    hts_lwmutex_init(m);
    break;

  case SQLITE_MUTEX_RECURSIVE:
    m = malloc(sizeof(hts_lwmutex_t));
    hts_lwmutex_init_recursive(m);
    break;
    
  case SQLITE_MUTEX_STATIC_MASTER: m=&static_mutexes[0]; break;
  case SQLITE_MUTEX_STATIC_MEM:    m=&static_mutexes[1]; break;
  case SQLITE_MUTEX_STATIC_MEM2:   m=&static_mutexes[2]; break;
  case SQLITE_MUTEX_STATIC_PRNG:   m=&static_mutexes[3]; break;
  case SQLITE_MUTEX_STATIC_LRU:    m=&static_mutexes[4]; break;
  case SQLITE_MUTEX_STATIC_LRU2:   m=&static_mutexes[5]; break;
  default:
    return NULL;
  }
  return (sqlite3_mutex *)m;
}

static void
sqlite_mutex_free(sqlite3_mutex *M)
{
  hts_lwmutex_t *m = (hts_lwmutex_t *)M;
  hts_lwmutex_destroy(m);
  free(m);
}

static void
sqlite_mutex_enter(sqlite3_mutex *M)
{
  hts_lwmutex_t *m = (hts_lwmutex_t *)M;
  hts_lwmutex_lock(m);
}

static void
sqlite_mutex_leave(sqlite3_mutex *M)
{
  hts_lwmutex_t *m = (hts_lwmutex_t *)M;
  hts_lwmutex_unlock(m);
}

static int
sqlite_mutex_try(sqlite3_mutex *m)
{
  return SQLITE_BUSY;
}

static struct sqlite3_mutex_methods sqlite_mutexes = {
  sqlite_mutex_init,
  sqlite_mutex_end,
  sqlite_mutex_alloc,
  sqlite_mutex_free,
  sqlite_mutex_enter,
  sqlite_mutex_try,
  sqlite_mutex_leave,
};
#endif



static void
db_log(void *aux, int code, const char *str)
{
  int non_extended_code = code & 0xff;
  // Some codes are nothing to worry about as we or sqlite retries internally
  if(non_extended_code == SQLITE_CONSTRAINT ||
     code == SQLITE_LOCKED_SHAREDCACHE ||
     non_extended_code == SQLITE_SCHEMA)
    return;

  TRACE(code == 0 ? TRACE_INFO : TRACE_ERROR,
        "SQLITE", "%s (code: 0x%x)", str, code);
}

#include "misc/callout.h"

static callout_t memlogger;

static void
memlogger_fn(callout_t *co, void *aux)
{
  callout_arm(&memlogger, memlogger_fn, NULL, 1);

  int dyn_current, dyn_highwater;
  int pgc_current, pgc_highwater;
  int scr_current, scr_highwater;

  sqlite3_status(SQLITE_STATUS_MEMORY_USED,
                 &dyn_current, &dyn_highwater, 0);
  sqlite3_status(SQLITE_STATUS_PAGECACHE_USED,
                 &pgc_current, &pgc_highwater, 0);
  sqlite3_status(SQLITE_STATUS_SCRATCH_USED,
                 &scr_current, &scr_highwater, 0);

  TRACE(TRACE_DEBUG, "SQLITE", "Mem %d (%d)  PGC: %d (%d) Scratch: %d (%d)",
        dyn_current, dyn_highwater,
        pgc_current, pgc_highwater,
        scr_current, scr_highwater);

}

void
db_init(void)
{
  sqlite3_temp_directory = gconf.cache_path;
#if ENABLE_SQLITE_LOCKING
  sqlite3_config(SQLITE_CONFIG_MUTEX, &sqlite_mutexes);
#endif
  sqlite3_config(SQLITE_CONFIG_LOG, &db_log, NULL);

  sqlite3_initialize();
#ifdef PS3
  sqlite3_soft_heap_limit(10000000);
#endif
  if(0)
    callout_arm(&memlogger, memlogger_fn, NULL, 1);
}
