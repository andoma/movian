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

#include <unistd.h>

#include "showtime.h"
#include "js.h"
#include "db/db_support.h"
#include "fileaccess/fileaccess.h"

/**
 *
 */
typedef struct js_db {
  sqlite3 *jd_db;
  hts_thread_t jd_self;
  char *jd_name;
  sqlite3_stmt *jd_stmt;
  int jd_step_rc;
  int jd_transaction;
  int jd_debug;
} js_db_t;


/**
 *
 */
static void
js_db_finalize(JSContext *cx, JSObject *obj)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(jd->jd_stmt)
    sqlite3_finalize(jd->jd_stmt);

  if(jd->jd_db != NULL)
    sqlite3_close(jd->jd_db);

  if(jd->jd_debug)
    TRACE(TRACE_DEBUG, "JS", "Database %s finalized", jd->jd_name);

  free(jd->jd_name);
  free(jd);
}


/**
 *
 */
static JSClass db_class = {
  "db", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, js_db_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static JSClass db_deadlock_exn = {
  "deadlockexception", 0,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, JS_FinalizeStub,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static int
js_db_check(JSContext *cx, js_db_t *jd)
{
  if(hts_thread_current() != jd->jd_self) {
    JS_ReportError(cx,
                   "Using a db object from multiple threads is not supported");
    return -1;
  }

  if(jd->jd_db == NULL) {
    JS_ReportError(cx, "Database is closed");
    return -1;
  }
  return 0;
}


/**
 *
 */
static JSBool
js_db_close(JSContext *cx, JSObject *obj, uintN argc,
            jsval *argv, jsval *rval)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  if(jd->jd_stmt) {
    sqlite3_finalize(jd->jd_stmt);
    jd->jd_stmt = NULL;
  }

  sqlite3_close(jd->jd_db);
  jd->jd_db = NULL;

  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_db_upgrade(JSContext *cx, JSObject *obj, uintN argc,
              jsval *argv, jsval *rval)
{
  int r = 0;
  const char *path;
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &path))
    return JS_FALSE;

  r = db_upgrade_schema(jd->jd_db, path, jd->jd_name, NULL, NULL);

  *rval = BOOLEAN_TO_JSVAL(!r);
  return JS_TRUE;
}


static void
js_txn_deadlock(JSContext *cx, js_db_t *jd)
{
  if(JS_IsExceptionPending(cx))
    return;

  if(jd->jd_debug)
    TRACE(TRACE_DEBUG, "JS", "Raising deadlock exception");

  JSObject *obj = JS_NewObjectWithGivenProto(cx, &db_deadlock_exn, NULL, NULL);
  JS_SetPendingException(cx, OBJECT_TO_JSVAL(obj));
}


/**
 *
 */
static JSBool
js_stmt_step(JSContext *cx, js_db_t *jd, jsval *rval)
{
  jd->jd_step_rc = db_step(jd->jd_stmt);

  if(jd->jd_transaction && jd->jd_step_rc == SQLITE_LOCKED) {
    js_txn_deadlock(cx, jd);
    return JS_FALSE;
  }

  if(jd->jd_step_rc == SQLITE_ROW ||
     jd->jd_step_rc == SQLITE_DONE) {

    if(sqlite3_data_count(jd->jd_stmt) == 0) {
      // No data to be returned, close stmt
      sqlite3_finalize(jd->jd_stmt);
      jd->jd_stmt = NULL;
    }
  } else {
    // We don't destroy on failure here cause we wanna retain error state
    *rval = JSVAL_FALSE;
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_db_query(JSContext *cx, JSObject *obj, uintN argc,
            jsval *argv, jsval *rval)
{
  const char *query;
  js_db_t *jd = JS_GetPrivate(cx, obj);
  int rc;

  if(js_db_check(cx, jd))
    return JS_FALSE;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &query))
    return JS_FALSE;

  if(jd->jd_stmt) {
    sqlite3_finalize(jd->jd_stmt);
    jd->jd_stmt = NULL;
  }

  rc = db_prepare(jd->jd_db, &jd->jd_stmt, query);

  if(rc != SQLITE_OK) {
    if(jd->jd_transaction && rc == SQLITE_LOCKED) {
      js_txn_deadlock(cx, jd);
      return JS_FALSE;
    }
    *rval = JSVAL_FALSE;
    return JS_TRUE;
  }

  sqlite3_stmt *stmt = jd->jd_stmt;

  for(int i = 1; i < argc; i++) {
    jsval v = argv[i];
    if(JSVAL_IS_NULL(v) || JSVAL_IS_VOID(v)) {
      sqlite3_bind_null(stmt, i);
    } else if(JSVAL_IS_INT(v)) {
      sqlite3_bind_int(stmt, i, JSVAL_TO_INT(v));
    } else if(JSVAL_IS_BOOLEAN(v)) {
      sqlite3_bind_int(stmt, i, JSVAL_TO_BOOLEAN(v));
    } else if(JSVAL_IS_DOUBLE(v)) {
      double d;
      if(JS_ValueToNumber(cx, v, &d))
        sqlite3_bind_double(stmt, i, d);
    } else if(JSVAL_IS_STRING(v)) {
      JSString *s = JS_ValueToString(cx, v);
      sqlite3_bind_text(stmt, i, JS_GetStringBytes(s), -1, SQLITE_STATIC);
    } else {
      JS_ReportError(cx, "Unable to bind argument %d, invalid type", i);
      sqlite3_finalize(stmt);
      return JS_FALSE;
    }
  }

  *rval = JSVAL_TRUE;
  return js_stmt_step(cx, jd, rval);
}


/**
 *
 */
static JSBool
js_db_step(JSContext *cx, JSObject *obj, uintN argc,
           jsval *argv, jsval *rval)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  if(jd->jd_stmt == NULL) {
    *rval = JSVAL_NULL;
  } else if(jd->jd_step_rc == SQLITE_ROW) {
    int cols = sqlite3_data_count(jd->jd_stmt);
    int i;

    JSObject *r = JS_NewObjectWithGivenProto(cx, NULL, NULL, obj);
    *rval = OBJECT_TO_JSVAL(r);

    if(!JS_EnterLocalRootScope(cx))
      return JS_FALSE;

    for(i = 0; i < cols; i++) {

      const char *cn = sqlite3_column_name(jd->jd_stmt, i);

      switch(sqlite3_column_type(jd->jd_stmt, i)) {
      case SQLITE_INTEGER:
        js_set_prop_int(cx, r, cn, sqlite3_column_int(jd->jd_stmt, i));
        break;
      case SQLITE_TEXT:
        js_set_prop_str(cx, r, cn,
                        (const char *)sqlite3_column_text(jd->jd_stmt, i));
        break;
      case SQLITE_FLOAT:
        js_set_prop_dbl(cx, r, cn, sqlite3_column_double(jd->jd_stmt, i));
        break;
      }
    }
    JS_LeaveLocalRootScope(cx);

    return js_stmt_step(cx, jd, rval);
  }
  *rval = JSVAL_FALSE;
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_db_changes(JSContext *cx, JSObject *obj, uintN argc,
              jsval *argv, jsval *rval)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  *rval = INT_TO_JSVAL(sqlite3_changes(jd->jd_db));
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_db_last_error_code(JSContext *cx, JSObject *obj, uintN argc,
                      jsval *argv, jsval *rval)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  *rval = INT_TO_JSVAL(sqlite3_errcode(jd->jd_db));
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_db_last_error_str(JSContext *cx, JSObject *obj, uintN argc,
                     jsval *argv, jsval *rval)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, sqlite3_errmsg(jd->jd_db)));
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_db_last_insert_row_id(JSContext *cx, JSObject *obj, uintN argc,
                         jsval *argv, jsval *rval)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  *rval = INT_TO_JSVAL(sqlite3_last_insert_rowid(jd->jd_db));
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_db_txn(JSContext *cx, JSObject *obj, uintN argc,
          jsval *argv, jsval *rval)
{
  js_db_t *jd = JS_GetPrivate(cx, obj);

  if(js_db_check(cx, jd))
    return JS_FALSE;

  if(argc != 1) {
    JS_ReportError(cx, "Invalid number of arguments");
    return JS_FALSE;
  }

  if(jd->jd_transaction) {
    JS_ReportError(cx, "Nesting transactions is not allowed");
    return JS_FALSE;
  }

 retry:
  if(db_begin(jd->jd_db)) {
    JS_ReportError(cx, "Failed to start transaction");
    return JS_FALSE;
  }

  jd->jd_transaction = 1;

  int r = JS_CallFunctionValue(cx, NULL, argv[0], 0, NULL, rval);
  jd->jd_transaction = 0;

  if(jd->jd_stmt) {
    sqlite3_finalize(jd->jd_stmt);
    jd->jd_stmt = NULL;
  }

  if(!r) {
    if(jd->jd_debug)
      TRACE(TRACE_DEBUG, "JS", "Transaction rollbacked due to error");
    db_rollback(jd->jd_db);
    if(JS_IsExceptionPending(cx)) {
      jsval exn;
      if(!JS_GetPendingException(cx, &exn)) {
        return JS_FALSE;
      }
      if(JSVAL_IS_OBJECT(exn)) {
        JSClass *c = JS_GetClass(cx, JSVAL_TO_OBJECT(exn));
        if(c == &db_deadlock_exn) {
          if(jd->jd_debug)
            TRACE(TRACE_DEBUG, "JS", "Catched deadlock exception, retrying");
          JS_ClearPendingException(cx);
          JS_BeginRequest(cx);
          usleep(100000);
          JS_EndRequest(cx);
          goto retry;
        }
      }
    }
    return JS_FALSE;
  }

  if(*rval == JSVAL_TRUE) {
    if(jd->jd_debug)
      TRACE(TRACE_DEBUG, "JS", "Transaction committed");

    db_commit(jd->jd_db);
  } else {
    if(jd->jd_debug)
      TRACE(TRACE_DEBUG, "JS", "Transaction rollbacked");
    db_rollback(jd->jd_db);
  }
  return JS_TRUE;
}


/**
 *
 */
static JSFunctionSpec db_functions[] = {
    JS_FS("close",           js_db_close,   0, 0, 0),
    JS_FS("upgradeSchema",   js_db_upgrade, 1, 0, 0),
    JS_FS("query",           js_db_query,   1, 0, 0),
    JS_FS("changes",         js_db_changes, 0, 0, 0),
    JS_FS("step",            js_db_step, 0, 0, 0),
    JS_FS("lastErrorCode",   js_db_last_error_code, 0, 0, 0),
    JS_FS("lastErrorString", js_db_last_error_str, 0, 0, 0),
    JS_FS("lastRowId",       js_db_last_insert_row_id, 0, 0, 0),
    JS_FS("txn",             js_db_txn, 1, 0, 0),
    JS_FS_END
};


/**
 *
 */
JSBool
js_db_open(JSContext *cx, JSObject *obj, uintN argc,
           jsval *argv, jsval *rval)
{
  const char *name;
  char path[URL_MAX];
  char errbuf[512];
  if(!JS_ConvertArguments(cx, argc, argv, "s", &name))
    return JS_FALSE;

  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  snprintf(path, sizeof(path), "file://%s/plugins/%s/databases",
           gconf.persistent_path, jsp->jsp_id);

  if(fa_makedirs(path, errbuf, sizeof(errbuf))) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  snprintf(path, sizeof(path), "%s/plugins/%s/databases/%s",
           gconf.persistent_path, jsp->jsp_id, name);

  sqlite3 *db = db_open(path, 0);
  if(db == NULL) {
    JS_ReportError(cx, "Unable to open database -- check logs");
    return JS_FALSE;
  }

  JSObject *robj = JS_NewObjectWithGivenProto(cx, &db_class, NULL, obj);
  *rval = OBJECT_TO_JSVAL(robj);

  js_db_t *jd = calloc(1, sizeof(js_db_t));
  jd->jd_db = db;

  snprintf(path, sizeof(path), "%s:%s", jsp->jsp_id, name);
  jd->jd_name = strdup(path);

  jd->jd_self = hts_thread_current();

  JS_SetPrivate(cx, robj, jd);
  JS_DefineFunctions(cx, robj, db_functions);
  return JS_TRUE;
}
