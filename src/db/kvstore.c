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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sqlite3.h>

#include "config.h"
#include "main.h"
#include "prop/prop_i.h"
#include "kvstore.h"
#include "misc/callout.h"
#include "misc/bytestream.h"
#include "fileaccess/fileaccess.h"

#if CONFIG_KVSTORE

#include "db/db_support.h"

LIST_HEAD(kvstore_write_list, kvstore_write);


typedef struct kvstore_write {
  LIST_ENTRY(kvstore_write) kw_link;
  char *kw_url;
  int kw_domain;
  char *kw_key;

  int kw_type;
  int kw_unimportant;
  union {
    char *kw_string;
    int kw_int;
    int64_t kw_int64;
  };

} kvstore_write_t;



static db_pool_t *kvstore_pool;
static struct kvstore_write_list deferred_writes;
static callout_t deferred_callout;
static hts_mutex_t deferred_mutex;


static const char *domain_to_name[] = {
  [KVSTORE_DOMAIN_SYS] = "sys",
  [KVSTORE_DOMAIN_PROP] = "prop",
  [KVSTORE_DOMAIN_PLUGIN] = "plugin",
  [KVSTORE_DOMAIN_SETTING] = "setting"
};

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
static void *
kvstore_get(void)
{
  return db_pool_get(kvstore_pool);
}


/**
 *
 */
static void
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
  fa_makedir(buf);
  snprintf(buf, sizeof(buf), "%s/kvstore/kvstore.db", gconf.persistent_path);

  //  unlink(buf);

  kvstore_pool = db_pool_create(buf, 4);
  db = kvstore_get();
  if(db == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/res/kvstore", app_dataroot());

  int r = db_upgrade_schema(db, buf, "kvstore", NULL, NULL);

  kvstore_close(db);

  if(r)
    kvstore_pool = NULL; // Disable
}



typedef struct kv_prop_bind {
  rstr_t *kpb_url;
  uint64_t kpb_id;
} kv_prop_bind_t;


typedef struct kv_prop_bind_value {
  rstr_t *kpbv_name;
  rstr_t *kpbv_url;
  uint64_t kpbv_id;  // ID of row in URL table
} kv_prop_bind_value_t;


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

      if(gconf.enable_kvstore_debug)
        TRACE(TRACE_DEBUG, "kvstore",
              "Created row %d for URL %s", (int)(*id), url);

    }
  }
  sqlite3_finalize(stmt);
  return rc;
}



/**
 *
 */
static void
kpbv_destroy(kv_prop_bind_value_t *kpbv)
{
  rstr_release(kpbv->kpbv_name);
  rstr_release(kpbv->kpbv_url);
  free(kpbv);
}

/**
 *
 */
static void
kv_value_cb(void *opaque, prop_event_t event, ...)
{
  kv_prop_bind_value_t *kpbv = opaque;
  va_list ap, apx;
  void *db;
  sqlite3_stmt *stmt;
  int rc;

  va_start(ap, event);
  switch(event) {

  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    kpbv_destroy(kpbv);
    break;

  case PROP_SET_VOID:
  case PROP_SET_RSTRING:
  case PROP_SET_CSTRING:
  case PROP_SET_INT:
  case PROP_SET_FLOAT:

    if(kpbv->kpbv_name == NULL)
      break;

    db = kvstore_get();
    if(db == NULL)
      break;

  again:
    if(db_begin(db))
      break;

    if(kpbv->kpbv_id == -1) {

      rc = get_url(db, rstr_get(kpbv->kpbv_url), &kpbv->kpbv_id);
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

    sqlite3_bind_int64(stmt, 1, kpbv->kpbv_id);
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

    db_bind_rstr(stmt, 2, kpbv->kpbv_name);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

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
  rstr_release(kpb->kpb_url);
  free(kpb);
}


/**
 *
 */
static void
kv_cb(void *opaque, prop_event_t event, ...)
{
  kv_prop_bind_t *kpb = opaque;
  kv_prop_bind_value_t *kpbv;
  va_list ap;
  prop_t *p;
  prop_vec_t *pv;
  int i;

  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    kpbv = calloc(1, sizeof(kv_prop_bind_value_t));
    kpbv->kpbv_id = kpb->kpb_id;
    kpbv->kpbv_name = prop_get_name(p);
    kpbv->kpbv_url = rstr_dup(kpb->kpb_url);

    prop_subscribe(PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_CALLBACK, kv_value_cb, kpbv,
                   PROP_TAG_ROOT, p,
                   NULL);
    break;

  case PROP_ADD_CHILD_VECTOR_DIRECT:
    pv = va_arg(ap, prop_vec_t *);
    for(i = 0; i < prop_vec_len(pv); i++) {
      p = prop_vec_get(pv, i);
      kpbv = calloc(1, sizeof(kv_prop_bind_value_t));
      kpbv->kpbv_id = kpb->kpb_id;
      kpbv->kpbv_url = rstr_dup(kpb->kpb_url);
      kpbv->kpbv_name = prop_get_name0(p);

      prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE |
                     PROP_SUB_DONTLOCK,
                     PROP_TAG_CALLBACK, kv_value_cb, kpbv,
                     PROP_TAG_ROOT, p,
                     NULL);
    }
    break;

  case PROP_DEL_CHILD:
  case PROP_SET_VOID:
  case PROP_MOVE_CHILD:
  case PROP_SET_DIR:
  case PROP_REQ_DELETE_VECTOR:
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_DESTROYED:
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
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
    const char *key = (const char *)sqlite3_column_text(stmt, 1);
    prop_t *c = prop_create(p, key);
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
  kpb->kpb_url = rstr_alloc(url);

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
static fa_err_code_t
opt_get_ea(const char *url, int domain, const char *key,
           void **datap, size_t *sizep)
{
  if(!gconf.fa_kvstore_as_xattr)
    return FAP_NOT_SUPPORTED;

  char ea[512];
  snprintf(ea, sizeof(ea), "showtime.default.%s.%s",
           domain_to_name[domain], key);

  return fa_get_xattr(url, ea, datap, sizep);
}


/**
 *
 */
rstr_t *
kv_url_opt_get_rstr(const char *url, int domain, const char *key)
{
  if(url == NULL)
    return NULL;

  void *data;
  size_t size;
  fa_err_code_t err = opt_get_ea(url, domain, key, &data, &size);

  if(err == 0 && size > 0) {

    rstr_t *rval = rstr_allocl(data, size);

    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore",
            "GET XA url=%s key=%s domain=%d value=%s",
            url, key, domain, rstr_get(rval));
    return rval;
  }

  void *db = kvstore_get();
  sqlite3_stmt *stmt = kv_url_opt_get(db, url, domain, key);
  rstr_t *r = NULL;
  if(stmt) {
    r = db_rstr(stmt, 0);
    sqlite3_finalize(stmt);
    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore","GET DB url=%s key=%s domain=%d value=%s",
            url, key, domain, rstr_get(r));
  } else {
    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore","GET DB url=%s key=%s domain=%d value=UNSET",
            url, key, domain);
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
  if(url == NULL)
    return def;

  void *data;
  size_t size;
  fa_err_code_t err = opt_get_ea(url, domain, key, &data, &size);

  if(err == 0 && size == 4) {
    int rval = rd32_be(data);
    free(data);

    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore",
            "GET XA url=%s key=%s domain=%d value=%d",
            url, key, domain, rval);
    return rval;
  }

  void *db = kvstore_get();
  sqlite3_stmt *stmt = kv_url_opt_get(db, url, domain, key);
  int v = def;
  if(stmt) {
    v = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore","GET DB url=%s key=%s domain=%d value=%d",
            url, key, domain, v);
  } else {
    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore","GET DB url=%s key=%s domain=%d value=UNSET",
            url, key, domain);
  }
  kvstore_close(db);
  return v;
}


/**
 *
 */
int64_t
kv_url_opt_get_int64(const char *url, int domain, const char *key, int64_t def)
{
  if(url == NULL)
    return def;

  void *data;
  size_t size;
  fa_err_code_t err = opt_get_ea(url, domain, key, &data, &size);

  if(err == 0 && size == 8) {
    int64_t rval = *(int64_t *)data;
#if !defined(__BIG_ENDIAN__)
    rval = __builtin_bswap64(rval);
#endif
    free(data);

    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore",
            "GET XA url=%s key=%s domain=%d value=%"PRId64,
            url, key, domain, rval);
    return rval;
  }


  void *db = kvstore_get();
  sqlite3_stmt *stmt = kv_url_opt_get(db, url, domain, key);
  int64_t v = def;
  if(stmt) {
    v = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore",
            "GET DB url=%s key=%s domain=%d value=%"PRId64,
            url, key, domain, v);
  } else {
    if(gconf.enable_kvstore_debug)
      TRACE(TRACE_DEBUG, "kvstore","GET DB url=%s key=%s domain=%d value=UNSET",
            url, key, domain);
  }
  kvstore_close(db);
  return v;
}


/**
 *
 */
static int
kv_write_db(void *db, const kvstore_write_t *kw, int64_t id)
{
  int rc;
  char vtmp[64];
  const char *value = vtmp;
  sqlite3_stmt *stmt;

  if(kw->kw_type == KVSTORE_SET_VOID) {
    rc = db_prepare(db, &stmt,
                    "DELETE FROM url_kv "
                    "WHERE url_id = ?1 "
                    "AND key = ?2 "
                    "AND domain = ?3");

    if(rc != SQLITE_OK)
      return rc;

    value = "[DELETED]";

  } else {

    rc = db_prepare(db, &stmt,
                    "INSERT OR REPLACE INTO url_kv "
                    "(url_id, key, domain, value) "
                    "VALUES "
                    "(?1, ?2, ?3, ?4)"
                    );

    if(rc != SQLITE_OK)
      return rc;

    switch(kw->kw_type) {
    case KVSTORE_SET_INT:
      sqlite3_bind_int(stmt, 4, kw->kw_int);
      snprintf(vtmp, sizeof(vtmp), "%d", kw->kw_int);
      break;

    case KVSTORE_SET_INT64:
      sqlite3_bind_int(stmt, 4, kw->kw_int64);
      snprintf(vtmp, sizeof(vtmp), "%"PRId64, kw->kw_int64);
      break;

    case KVSTORE_SET_STRING:
      sqlite3_bind_text(stmt, 4, kw->kw_string, -1, SQLITE_STATIC);
      value = kw->kw_string;
      break;

    default:
      break;
    }
  }

  sqlite3_bind_int64(stmt, 1, id);
  sqlite3_bind_text(stmt, 2, kw->kw_key, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, kw->kw_domain);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);


  if(rc == SQLITE_DONE)
    rc = SQLITE_OK;

  if(gconf.enable_kvstore_debug)
    TRACE(TRACE_DEBUG, "kvstore",
          "SET DB url=%s key=%s domain=%d value=%s rc=%d",
          kw->kw_url, kw->kw_key, kw->kw_domain, value, rc);
  return rc;
}


/**
 *
 */
static int
kv_write_xattr(const kvstore_write_t *kw)
{
  char vtmp[64];
  const char *value = vtmp;

  char ea[512];
  uint8_t d4[4];
#if !defined(__BIG_ENDIAN__)
  int64_t i64;
#endif
  const void *data;
  size_t size;
  snprintf(ea, sizeof(ea), "showtime.default.%s.%s",
           domain_to_name[kw->kw_domain], kw->kw_key);

  switch(kw->kw_type) {
  case KVSTORE_SET_INT:
    wr32_be(d4, kw->kw_int);
    data = d4;
    size = 4;
    snprintf(vtmp, sizeof(vtmp), "%d", kw->kw_int);
    break;

  case KVSTORE_SET_INT64:
    size = 8;
#if defined(__BIG_ENDIAN__)
    data = &kw->kw_int64;
#else
    i64 = __builtin_bswap64(kw->kw_int64);
    data = &i64;
#endif
    snprintf(vtmp, sizeof(vtmp), "%"PRId64, kw->kw_int64);
    break;

  case KVSTORE_SET_STRING:
    data = kw->kw_string;
    size = strlen(kw->kw_string);
    value = kw->kw_string;
    break;

  case KVSTORE_SET_VOID:
    data = NULL;
    size = 0;
    value = "[DELETED]";
    break;
  default:
    abort();
  }
  int rc = fa_set_xattr(kw->kw_url, ea, data, size);

  if(gconf.enable_kvstore_debug)
    TRACE(TRACE_DEBUG, "kvstore",
          "SET XA url=%s key=%s domain=%d value=%s rc=%d",
          kw->kw_url, kw->kw_key, kw->kw_domain, value, rc);

  return !!rc;
}


/**
 *
 */
void
kv_url_opt_set(const char *url, int domain, const char *key,
	       int type, ...)
{
  void *db;
  int rc;
  uint64_t id;
  va_list ap;

  kvstore_write_t kw;
  kw.kw_url    = (char *)url;
  kw.kw_domain = domain;
  kw.kw_key    = (char *)key;
  kw.kw_type   = type & 0xff;
  kw.kw_unimportant = type & KVSTORE_UNIMPORTANT;

  va_start(ap, type);

  switch(kw.kw_type) {
  case KVSTORE_SET_INT:
    kw.kw_int = va_arg(ap, int);
    break;

  case KVSTORE_SET_INT64:
    kw.kw_int64 = va_arg(ap, int64_t);
    break;

  case KVSTORE_SET_STRING:
    kw.kw_string = va_arg(ap, char *);
    if(kw.kw_string == NULL)
      kw.kw_type = KVSTORE_SET_VOID;
    break;

  default:
    break;
  }
  va_end(ap);

  if(gconf.fa_kvstore_as_xattr) {
    if(!kv_write_xattr(&kw))
      return;
  }

#ifdef STOS
  if(kw.kw_unimportant)
    return;
#endif

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

  rc = kv_write_db(db, &kw, id);
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
  kvstore_write_t *kw;
  void *db;
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

  LIST_FOREACH(kw, &deferred_writes, kw_link) {

    if(gconf.fa_kvstore_as_xattr) {
      if(!kv_write_xattr(kw))
        continue;
    }

#ifdef STOS
    if(kw->kw_unimportant)
      continue;
#endif

    if(current_url == NULL || strcmp(kw->kw_url, current_url)) {

      rc = get_url(db, kw->kw_url, &id);
      if(rc == SQLITE_LOCKED) {
        db_rollback_deadlock(db);
        goto again;
      }

      if(rc != SQLITE_OK) {
        db_rollback(db);
        goto err;
      }
      current_url = kw->kw_url;
    }

    rc = kv_write_db(db, kw, id);
    if(rc == SQLITE_LOCKED) {
      db_rollback_deadlock(db);
      goto again;
    }
    if(rc != SQLITE_OK) {
      db_rollback(db);
      goto err;
    }
  }

  db_commit(db);

 err:

  while((kw = LIST_FIRST(&deferred_writes)) != NULL) {
    LIST_REMOVE(kw, kw_link);
    free(kw->kw_url);
    free(kw->kw_key);
    if(kw->kw_type == KVSTORE_SET_STRING)
      free(kw->kw_string);
    free(kw);
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
  kvstore_write_t *kw;
  va_list ap;
  const char *str;
  va_start(ap, type);

  hts_mutex_lock(&deferred_mutex);

  LIST_FOREACH(kw, &deferred_writes, kw_link) {
    if(!strcmp(kw->kw_url, url) &&
       !strcmp(kw->kw_key, key) &&
       kw->kw_domain == domain)
      break;
  }

  if(kw == NULL) {
    kw = malloc(sizeof(kvstore_write_t));
    kw->kw_url    = strdup(url);
    kw->kw_key    = strdup(key);
    kw->kw_domain = domain;
    LIST_INSERT_HEAD(&deferred_writes, kw, kw_link);
  } else {
    if(kw->kw_type == KVSTORE_SET_STRING)
      free(kw->kw_string);
  }


  kw->kw_type = type & 0xff;
  kw->kw_unimportant = type & KVSTORE_UNIMPORTANT;

  switch(kw->kw_type) {
  case KVSTORE_SET_INT:
    kw->kw_int = va_arg(ap, int);
    break;

  case KVSTORE_SET_INT64:
    kw->kw_int64 = va_arg(ap, int64_t);
    break;

  case KVSTORE_SET_STRING:
    str = va_arg(ap, const char *);
    if(str != NULL) {
      kw->kw_string = strdup(str);
    } else {
      kw->kw_type = KVSTORE_SET_VOID;
    }
    break;

  default:
    break;
  }

  hts_mutex_unlock(&deferred_mutex);

  callout_arm(&deferred_callout, deferred_callout_fire, NULL, 5);
  va_end(ap);
}

#else


void
kvstore_init(void)
{
}

void
kvstore_fini(void)
{
}

void
kv_prop_bind_create(prop_t *p, const char *url)
{
}

rstr_t *
kv_url_opt_get_rstr(const char *url, int domain, const char *key)
{
  return NULL;
}

int
kv_url_opt_get_int(const char *url, int domain, const char *key, int def)
{
  return def;
}

int64_t
kv_url_opt_get_int64(const char *url, int domain,
                     const char *key, int64_t def)
{
  return def;
}

void
kv_url_opt_set(const char *url, int domain, const char *key,
               int type, ...)
{
}


void
kv_url_opt_set_deferred(const char *url, int domain, const char *key,
                        int type, ...)
{
}

void
kvstore_deferred_flush(void)
{
}

#endif

