/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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



#include <sys/stat.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <sqlite3.h>

#include "config.h"
#include "showtime.h"
#include "prop/prop.h"
#include "db/db_support.h"
#include "kvstore.h"
#include "misc/callout.h"

LIST_HEAD(kvstore_deferred_write_list, kvstore_deferred_write);


typedef struct kvstore_deferred_write {
  LIST_ENTRY(kvstore_deferred_write) kdw_link;
  char *kdw_url;
  int kdw_domain;
  char *kdw_key;

  int kdw_type;
  union {
    char *kdw_string;
    int kdw_int;
  };

} kvstore_deferred_write_t;



static db_pool_t *kvstore_pool;
static struct kvstore_deferred_write_list deferred_writes;
static callout_t deferred_callout;
static hts_mutex_t deferred_mutex;


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
  char buf[256];

  hts_mutex_init(&deferred_mutex);

  snprintf(buf, sizeof(buf), "%s/kvstore", gconf.persistent_path);
  mkdir(buf, 0770);
  snprintf(buf, sizeof(buf), "%s/kvstore/kvstore.db", gconf.persistent_path);

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
static int
get_url(void *db, const char *url, uint64_t *id)
{
  int rc;
  sqlite3_stmt *stmt;

  rc = db_prepare(db, &stmt,
		  "SELECT id FROM url WHERE url=?1");
  
  if(rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
      
  rc = sqlite3_step(stmt);
  if(rc == SQLITE_LOCKED) {
    sqlite3_finalize(stmt);
    return SQLITE_LOCKED;
  }
  if(rc == SQLITE_ROW) {
    *id = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return SQLITE_OK;

  } else if(rc == SQLITE_DONE) {
    sqlite3_finalize(stmt);

    rc = db_prepare(db, &stmt,
		    "INSERT INTO url ('url') VALUES (?1)");
		    
    
    if(rc != SQLITE_OK)
      return rc;

    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

    rc = db_step(stmt);
    if(rc == SQLITE_DONE) {
      *id = sqlite3_last_insert_rowid(db);
      rc = SQLITE_OK;
    }
  }
  sqlite3_finalize(stmt);
  return rc;
}


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

      rc = get_url(db, kpb->kpb_url, &kpb->kpb_id);
      if(rc == SQLITE_LOCKED) {
        db_rollback_deadlock(db);
	goto again;
      }

      if(rc != SQLITE_OK) {
	db_rollback(db);
	kvstore_close(db);
	return;
      }
    }
    
    if(event == PROP_SET_VOID) {
      rc = db_prepare(db, &stmt,
		      "DELETE FROM url_kv "
		      "WHERE url_id = ?1 "
		      "AND domain = ?4 "
		      "AND key = ?2");
    } else {

      rc = db_prepare(db, &stmt,
		      "INSERT OR REPLACE INTO url_kv "
		      "(url_id, domain, key, value) "
		      "VALUES "
		      "(?1, ?4, ?2, ?3)");
    }

    if(rc != SQLITE_OK) {
      db_rollback(db);
      kvstore_close(db);
      return;
    }

    sqlite3_bind_int64(stmt, 1, kpb->kpb_id);
    sqlite3_bind_int(stmt, 4, KVSTORE_DOMAIN_PROP);

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
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
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

  rc = db_prepare(db, &stmt, 
		  "SELECT id,key,value "
		  "FROM url "
		  "LEFT OUTER JOIN url_kv ON id = url_id "
		  "WHERE url=?1 "
		  "AND domain=?2");
		  

  if(rc != SQLITE_OK) {
    kvstore_close(db);
    return;
  }
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, KVSTORE_DOMAIN_PROP);

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




/**
 *
 */
static sqlite3_stmt *
kv_url_opt_get(void *db, const char *url, int domain, const char *key)
{
  sqlite3_stmt *stmt;
  int rc;

  if(db == NULL)
    return NULL;

  rc = db_prepare(db, &stmt, 
		  "SELECT value "
		  "FROM url, url_kv "
		  "WHERE url=?1 "
		  "AND key = ?2 "
		  "AND domain = ?3 "
		  "AND url.id = url_id"
		  );

  if(rc != SQLITE_OK) {
    return NULL;
  }
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, key, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt,  3, domain);

  if(db_step(stmt) == SQLITE_ROW)
    return stmt;
  sqlite3_finalize(stmt);
  return NULL;
}

/**
 *
 */
rstr_t *
kv_url_opt_get_rstr(const char *url, int domain, const char *key)
{
  void *db = kvstore_get();
  sqlite3_stmt *stmt = kv_url_opt_get(db, url, domain, key);
  rstr_t *r = NULL;
  if(stmt) {
    r = db_rstr(stmt, 0);
    sqlite3_finalize(stmt);
  }
  kvstore_close(db);
  return r;
}


/**
 *
 */
int
kv_url_opt_get_int(const char *url, int domain, const char *key, int def)
{
  void *db = kvstore_get();
  sqlite3_stmt *stmt = kv_url_opt_get(db, url, domain, key);
  int v = def;
  if(stmt) {
    v = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
  }
  kvstore_close(db);
  return v;
}




/**
 *
 */
void
kv_url_opt_set(const char *url, int domain, const char *key,
	       int type, ...)
{
  void *db;
  sqlite3_stmt *stmt;
  int rc;
  uint64_t id;
  const char *str;
  va_list ap, apx;
  va_start(ap, type);

  db = kvstore_get();
  if(db == NULL)
    return;
  
 again:
  if(db_begin(db)) {
    kvstore_close(db);
    return;
  }

  rc = get_url(db, url, &id);
  if(rc == SQLITE_LOCKED) {
    db_rollback_deadlock(db);
    goto again;
  }

  if(rc != SQLITE_OK) {
    db_rollback(db);
    kvstore_close(db);
    return;
  }

  rc = db_prepare(db, &stmt,
		  "INSERT OR REPLACE INTO url_kv "
		  "(url_id, key, value, domain) "
		  "VALUES "
		  "(?1, ?2, ?3, ?4)"
		  );


  if(rc != SQLITE_OK) {
    db_rollback(db);
    kvstore_close(db);
    return;
  }

  sqlite3_bind_int64(stmt, 1, id);
  sqlite3_bind_text(stmt, 2, key, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, domain);

  va_copy(apx, ap);

  switch(type) {
  case KVSTORE_SET_INT:
    sqlite3_bind_int(stmt, 3, va_arg(apx, int));
    break;

  case KVSTORE_SET_STRING:
    str = va_arg(apx, const char *);
    if(str != NULL) {
      sqlite3_bind_text(stmt, 3, str, -1, SQLITE_STATIC);
      break;
    }
    // FALLTHRU
  case KVSTORE_SET_VOID:
    sqlite3_bind_null(stmt, 3);
    break;

  default:
    break;
  }
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if(rc == SQLITE_LOCKED) {
    db_rollback_deadlock(db);
    goto again;
  }
  db_commit(db);
  kvstore_close(db);
}


/**
 *
 */
void
kvstore_deferred_flush(void)
{
  kvstore_deferred_write_t *kdw;
  void *db;
  sqlite3_stmt *stmt;
  int rc;
  uint64_t id = 0;
  const char *current_url;

  db = kvstore_get();
  if(db == NULL)
    return;

  hts_mutex_lock(&deferred_mutex);

 again:
  if(db_begin(db))
    goto err;

  current_url = NULL;

  LIST_FOREACH(kdw, &deferred_writes, kdw_link) {

    if(current_url == NULL || strcmp(kdw->kdw_url, current_url)) {

      rc = get_url(db, kdw->kdw_url, &id);
      if(rc == SQLITE_LOCKED) {
        db_rollback_deadlock(db);
        goto again;
      }

      if(rc != SQLITE_OK) {
        db_rollback(db);
        goto err;
      }
      current_url = kdw->kdw_url;
    }

    rc = db_prepare(db, &stmt,
                    "INSERT OR REPLACE INTO url_kv "
                    "(url_id, key, value, domain) "
                    "VALUES "
                    "(?1, ?2, ?3, ?4)"
                    );

    if(rc != SQLITE_OK) {
      db_rollback(db);
      goto err;
    }

    sqlite3_bind_int64(stmt, 1, id);
    sqlite3_bind_text(stmt, 2, kdw->kdw_key, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, kdw->kdw_domain);

    switch(kdw->kdw_type) {
    case KVSTORE_SET_INT:
      sqlite3_bind_int(stmt, 3, kdw->kdw_int);
      break;

    case KVSTORE_SET_STRING:
      sqlite3_bind_text(stmt, 3, kdw->kdw_string, -1, SQLITE_STATIC);
      break;

    case KVSTORE_SET_VOID:
      sqlite3_bind_null(stmt, 3);
      break;

    default:
      break;
    }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_LOCKED) {
      db_rollback_deadlock(db);
      goto again;
    }
  }

  db_commit(db);

 err:


  while((kdw = LIST_FIRST(&deferred_writes)) != NULL) {
    LIST_REMOVE(kdw, kdw_link);
    free(kdw->kdw_url);
    free(kdw->kdw_key);
    if(kdw->kdw_type == KVSTORE_SET_STRING)
      free(kdw->kdw_string);
    free(kdw);
  }


  hts_mutex_unlock(&deferred_mutex);
  kvstore_close(db);

}


/**
 *
 */
static void
deferred_callout_fire(struct callout *c, void *opaque)
{
  kvstore_deferred_flush();
}


/**
 *
 */
void
kv_url_opt_set_deferred(const char *url, int domain, const char *key,
                        int type, ...)
{

  kvstore_deferred_write_t *kdw;
  va_list ap;
  const char *str;
  va_start(ap, type);

  hts_mutex_lock(&deferred_mutex);

  LIST_FOREACH(kdw, &deferred_writes, kdw_link) {
    if(!strcmp(kdw->kdw_url, url) &&
       !strcmp(kdw->kdw_key, key) &&
       kdw->kdw_domain == domain)
      break;
  }

  if(kdw == NULL) {
    kdw = malloc(sizeof(kvstore_deferred_write_t));
    kdw->kdw_url    = strdup(url);
    kdw->kdw_key    = strdup(key);
    kdw->kdw_domain = domain;
    LIST_INSERT_HEAD(&deferred_writes, kdw, kdw_link);
  } else {
    if(kdw->kdw_type == KVSTORE_SET_STRING)
      free(kdw->kdw_string);
  }


  kdw->kdw_type = type;

  switch(type) {
  case KVSTORE_SET_INT:
    kdw->kdw_int = va_arg(ap, int);
    break;

  case KVSTORE_SET_STRING:
    str = va_arg(ap, const char *);
    if(str != NULL) {
      kdw->kdw_string = strdup(str);
    } else {
      kdw->kdw_type = KVSTORE_SET_VOID;
    }
    break;

  default:
    break;
  }

  hts_mutex_unlock(&deferred_mutex);

  callout_arm(&deferred_callout, deferred_callout_fire, NULL, 5);
  va_end(ap);
}
