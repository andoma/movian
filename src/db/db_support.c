/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2011 Andreas Öman
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

#include <stdio.h>
#include <unistd.h>

#include "showtime.h"
#include "fileaccess/fileaccess.h"

#include "db_support.h"

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

  rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
  if(rc)
    return -1;

  rc = sqlite3_step(stmt);

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



/**
 *
 */
int
db_upgrade_schema(sqlite3 *db, const char *schemadir, const char *dbname)
{
  int ver, tgtver = 0;
  char path[256];
  char buf[256];

  db_one_statement(db, "pragma journal_mode=wal;", NULL);

  if(db_get_int_from_query(db, "pragma user_version", &ver)) {
    TRACE(TRACE_ERROR, "DB", "%s: Unable to query db version", dbname);
    return -1;
  }

  fa_dir_t *fd;
  fa_dir_entry_t *fde;

  fd = fa_scandir(schemadir, buf, sizeof(buf));

  if(fd == NULL) {
    TRACE(TRACE_ERROR, "DB",
	  "%s: Unable to scan schema dir %s -- %s", dbname, schemadir , buf);
    return -1;
  }

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(fde->fde_type != CONTENT_FILE || strchr(fde->fde_filename, '~'))
      continue;
    tgtver = MAX(tgtver, atoi(fde->fde_filename));
  }

  fa_dir_free(fd);

  if(ver > tgtver) {
    TRACE(TRACE_ERROR, "DB", "%s: Installed version %d is too high for "
	  "this version of Showtime", dbname, ver);
    return -1;
  }

  while(1) {

    if(ver == tgtver) {
      TRACE(TRACE_DEBUG, "DB", "%s: At current version %d", dbname, ver);
      return 0;
    }

    ver++;
    snprintf(path, sizeof(path), "%s/%03d.sql", schemadir, ver);

    size_t size;
    char *sql = fa_load(path, &size, NULL, buf, sizeof(buf), NULL);
    if(sql == NULL) {
      TRACE(TRACE_ERROR, "DB",
	    "%s: Unable to upgrade db schema to version %d using %s -- %s",
	    dbname, ver, path, buf);
      return -1;
    }

    db_begin(db);
    snprintf(buf, sizeof(buf), "PRAGMA user_version=%d", ver);
    if(db_one_statement(db, buf, NULL)) {
      free(sql);
      break;
    }

    const char *s = sql;

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
    TRACE(TRACE_INFO, "DB", "%s: Upgraded to version %d", dbname, ver);
    free(sql);
  }
 fail:
  db_rollback(db);
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
db_pool_get(db_pool_t *dp)
{
  int rc;
  int i;
  char *errmsg;
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

  rc = sqlite3_open_v2(dp->dp_path, &db,
		       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | 
		       SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_SHAREDCACHE,
		       NULL);

  if(rc) {
    TRACE(TRACE_ERROR, "DB", "%s: Unable to open database: %s",
	  dp->dp_path, sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  rc = sqlite3_exec(db, "PRAGMA synchronous = normal;", NULL, NULL, &errmsg);
  if(rc) {
    TRACE(TRACE_ERROR, 
	  "DB", "%s: Unable to set synchronous mode to NORMAL: %s",
	  dp->dp_path, errmsg);
    sqlite3_free(errmsg);
    sqlite3_close(db);
    return NULL;
  }
  return db;
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
