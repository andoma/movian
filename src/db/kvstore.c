/*
 *  Key value store
 *  Copyright (C) 2012 Andreas Öman
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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "config.h"
#include "showtime.h"
#include "ext/sqlite/sqlite3.h"
#include "prop/prop.h"
#include "db/db_support.h"
#include "kvstore.h"

static db_pool_t *kvstore_pool;

/**
 *
 */
void
kvstore_fini(void)
{
  db_pool_close(kvstore_pool);
}


/**
 *
 */
void *
kvstore_get(void)
{
  return db_pool_get(kvstore_pool);
}


/**
 *
 */
void 
kvstore_close(void *db)
{
  db_pool_put(kvstore_pool, db);
}


/**
 *
 */
void
kvstore_init(void)
{
  sqlite3 *db;
  extern char *showtime_persistent_path;
  char buf[256];

  snprintf(buf, sizeof(buf), "%s/kvstore", showtime_persistent_path);
  mkdir(buf, 0770);
  snprintf(buf, sizeof(buf), "%s/kvstore/kvstore.db", showtime_persistent_path);

  //  unlink(buf);

  kvstore_pool = db_pool_create(buf, 2);
  db = kvstore_get();
  if(db == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/resources/kvstore", showtime_dataroot());

  int r = db_upgrade_schema(db, buf, "kvstore");

  kvstore_close(db);

  if(r)
    kvstore_pool = NULL; // Disable
}



typedef struct kv_prop_bind {
  uint64_t kpb_id;  // ID of row in URL table
  prop_sub_t *kpb_sub;
  int kpb_init;
  char *kpb_url;
} kv_prop_bind_t;

/**
 *
 */
static void
kv_value_cb(void *opaque, prop_event_t event, ...)
{
  kv_prop_bind_t *kpb = opaque;
  va_list ap, apx;
  //  prop_t *p;
  //  rstr_t *key;
  void *db;
  sqlite3_stmt *stmt;
  int rc;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    (void)va_arg(ap, prop_t *);
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    break;

  case PROP_SET_VOID:
  case PROP_SET_RSTRING:
  case PROP_SET_CSTRING:
  case PROP_SET_INT:
  case PROP_SET_FLOAT:
    
    db = kvstore_get();
    if(db == NULL)
      break;
    
  again:
    if(db_begin(db))
      break;
    
    if(kpb->kpb_id == -1) {
      rc = db_prepare(db,
		      "SELECT id FROM url WHERE url=?1",
		    -1, &stmt, NULL);

      if(rc != SQLITE_OK) {
	TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	      __FUNCTION__, __LINE__);
	db_rollback(db);
	kvstore_close(db);
	return;
      }

      sqlite3_bind_text(stmt, 1, kpb->kpb_url, -1, SQLITE_STATIC);
      
      rc = sqlite3_step(stmt);
      if(rc == SQLITE_LOCKED) {
	sqlite3_finalize(stmt);
	db_rollback_deadlock(db);
	goto again;
      }
      if(rc == SQLITE_ROW) {
	kpb->kpb_id = sqlite3_column_int64(stmt, 0);
	sqlite3_finalize(stmt);
      } else {
	sqlite3_finalize(stmt);

	rc = db_prepare(db,
			"INSERT INTO url ('url') VALUES (?1)",
			-1, &stmt, NULL);
	
	if(rc != SQLITE_OK) {
	  TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
		__FUNCTION__, __LINE__);
	  db_rollback(db);
	  kvstore_close(db);
	  return;
	}
	
	sqlite3_bind_text(stmt, 1, kpb->kpb_url, -1, SQLITE_STATIC);

	rc = db_step(stmt);
	sqlite3_finalize(stmt);
	if(rc == SQLITE_LOCKED) {
	  db_rollback_deadlock(db);
	  goto again;
	}
	
	if(rc == SQLITE_DONE) {
	  kpb->kpb_id = sqlite3_last_insert_rowid(db);
	} else {
	  db_rollback(db);
	  kvstore_close(db);
	  return;
	}
      }
    }
    
    if(event == PROP_SET_VOID) {
      rc = db_prepare(db,
		      "DELETE FROM page_kv WHERE url_id = ?1 AND key = ?2",
		      -1, &stmt, NULL);
    } else {

      rc = db_prepare(db,
		      "INSERT OR REPLACE INTO page_kv "
		      "(url_id, key, value) "
		      "VALUES "
		      "(?1, ?2, ?3)",
		      -1, &stmt, NULL);
    }

    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      db_rollback(db);
      kvstore_close(db);
      return;
    }

    sqlite3_bind_int64(stmt, 1, kpb->kpb_id);

    va_copy(apx, ap);

    switch(event) {
    default:
      break;

    case PROP_SET_VOID:
      break;
    case PROP_SET_RSTRING:
      db_bind_rstr(stmt, 3, va_arg(apx, rstr_t *));
      break;
    case PROP_SET_CSTRING:
      sqlite3_bind_text(stmt, 3, va_arg(apx, const char *), -1,
			SQLITE_STATIC);
      break;
    case PROP_SET_INT:
      sqlite3_bind_int(stmt, 3, va_arg(apx, int));
      break;
    case PROP_SET_FLOAT:
      sqlite3_bind_double(stmt, 3, va_arg(apx, double));
      break;
    }

    rstr_t *key = prop_get_name(va_arg(apx, prop_t *));
    db_bind_rstr(stmt, 2, key);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    rstr_release(key);
    
    if(rc == SQLITE_LOCKED) {
      db_rollback_deadlock(db);
      goto again;
    }
    db_commit(db);
    kvstore_close(db);
      
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
kpb_destroy(kv_prop_bind_t *kpb)
{
  prop_unsubscribe(kpb->kpb_sub);
  free(kpb->kpb_url);
  free(kpb);
}

/**
 *
 */
static void
kv_cb(void *opaque, prop_event_t event, ...)
{
  kv_prop_bind_t *kpb = opaque;
  va_list ap;
  prop_t *p;
  prop_vec_t *pv;
  int i;

  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, kv_value_cb, kpb,
		   PROP_TAG_ROOT, p,
		   NULL);
    break;

  case PROP_ADD_CHILD_VECTOR_DIRECT:
    pv = va_arg(ap, prop_vec_t *);
    for(i = 0; i < prop_vec_len(pv); i++)
      prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE |
		     PROP_SUB_DONTLOCK,
		     PROP_TAG_CALLBACK, kv_value_cb, kpb,
		     PROP_TAG_ROOT, prop_vec_get(pv, i),
		     NULL);
    break;

  case PROP_DEL_CHILD:
  case PROP_MOVE_CHILD:
  case PROP_SET_VOID:
  case PROP_SET_DIR:
  case PROP_REQ_DELETE_VECTOR:
  case PROP_HAVE_MORE_CHILDS:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_DESTROYED:
    kpb_destroy(kpb);
    break;

  default:
    printf("Cant handle event %d\n", event);
    abort();
  }
}


/**
 *
 */
void
kv_prop_bind_create(prop_t *p, const char *url)
{
  void *db;
  int64_t id = -1;
  sqlite3_stmt *stmt;
  int rc;

  db = kvstore_get();
  if(db == NULL)
    return;

  rc = db_prepare(db, 
		  "SELECT id,key,value "
		  "FROM url "
		  "LEFT OUTER JOIN page_kv ON id = url_id "
		  "WHERE url=?1",
		  -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    kvstore_close(db);
    return;
  }
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);


  while(db_step(stmt) == SQLITE_ROW) {
    if(id == -1)
      id = sqlite3_column_int64(stmt, 0);
    if(sqlite3_column_type(stmt, 1) != SQLITE_TEXT)
      continue;

    prop_t *c = prop_create(p, (const char *)sqlite3_column_text(stmt, 1));
    
    switch(sqlite3_column_type(stmt, 2)) {
    case SQLITE_TEXT:
      prop_set_string(c, (const char *)sqlite3_column_text(stmt, 2));
      break;
    case SQLITE_INTEGER:
      prop_set_int(c, sqlite3_column_int(stmt, 2));
      break;
    case SQLITE_FLOAT:
      prop_set_float(c, sqlite3_column_double(stmt, 2));
      break;
    default:
      prop_set_void(c);
      break;
    }
  }

  sqlite3_finalize(stmt);
  kvstore_close(db);

  kv_prop_bind_t *kpb = calloc(1, sizeof(kv_prop_bind_t));
  kpb->kpb_id = id;
  kpb->kpb_url = strdup(url);

  kpb->kpb_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_CALLBACK, kv_cb, kpb,
		   PROP_TAG_ROOT, p,
		   NULL);
}
