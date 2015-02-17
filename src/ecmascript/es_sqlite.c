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

#include "showtime.h"
#include "ecmascript.h"
#include "fileaccess/fileaccess.h"
#include "db/db_support.h"

typedef struct es_sqlite {
  es_resource_t super;
  char *es_name;

  sqlite3 *es_db;
  sqlite3_stmt *es_stmt;
  int es_step_rc;
  int es_transaction;
  int es_debug;

} es_sqlite_t;


/**
 *
 */
static void
es_sqlite_destroy(es_resource_t *eres)
{
  es_sqlite_t *es = (es_sqlite_t *)eres;

  if(es->es_stmt)
    sqlite3_finalize(es->es_stmt);

  if(es->es_db != NULL)
    sqlite3_close(es->es_db);

  if(es->es_debug)
    TRACE(TRACE_DEBUG, "JS", "Database %s finalized", es->es_name);

  free(es->es_name);
  es_resource_unlink(&es->super);
}


/**
 *
 */
static void
es_sqlite_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_sqlite_t *es = (es_sqlite_t *)eres;
  snprintf(dst, dstsize, "%s%s", es->es_name,
           es->es_transaction ? ", in transaction" : "");
}


/**
 *
 */
static const es_resource_class_t es_resource_sqlite = {
  .erc_name = "sqlite",
  .erc_size = sizeof(es_sqlite_t),
  .erc_destroy = es_sqlite_destroy,
  .erc_info = es_sqlite_info,
};


/**
 *
 */
static int
es_sqlite_create(duk_context *ctx)
{
  char path[PATH_MAX];
  char errbuf[512];
  es_context_t *ec = es_get(ctx);
  const char *name = duk_safe_to_string(ctx, 0);

  // Create the db-dir for this plugin

  snprintf(path, sizeof(path), "%s/databases", ec->ec_storage);

  if(fa_makedirs(path, errbuf, sizeof(errbuf)))
    duk_error(ctx, DUK_ERR_ERROR, "Unable to create directory %s -- %s",
              path, errbuf);

  snprintf(path, sizeof(path), "%s/databases/%s", ec->ec_storage, name);

  sqlite3 *db = db_open(path, 0);
  if(db == NULL)
    duk_error(ctx, DUK_ERR_ERROR, "Unable to open database -- check logs");


  es_sqlite_t *es = es_resource_create(ec, &es_resource_sqlite, 0);

  es->es_db = db;
  es->es_name = strdup(name);

  es_resource_push(ctx, &es->super);
  return 1;
}


/**
 *
 */
static void
es_sqlite_stmt_step(duk_context *ctx, es_sqlite_t *es)
{
  es->es_step_rc = db_step(es->es_stmt);

  if(es->es_transaction && es->es_step_rc == SQLITE_LOCKED)
    duk_error(ctx, ST_ERROR_SQLITE_BASE | es->es_step_rc , "Deadlock");

  if(es->es_step_rc == SQLITE_ROW || es->es_step_rc == SQLITE_DONE) {

    if(sqlite3_data_count(es->es_stmt) == 0) {
      // No data to be returned, close stmt
      sqlite3_finalize(es->es_stmt);
      es->es_stmt = NULL;
    }
  } else {
    duk_error(ctx, ST_ERROR_SQLITE_BASE | es->es_step_rc,
              "Sqlite error 0x%x -- %s",
              es->es_step_rc, sqlite3_errmsg(es->es_db));
  }
}


/**
 *
 */
static int
es_sqlite_query(duk_context *ctx)
{
  int argc = duk_get_top(ctx);
  if(argc < 2)
    return DUK_RET_TYPE_ERROR;

  es_sqlite_t *es = es_resource_get(ctx, 0, &es_resource_sqlite);
  const char *query = duk_safe_to_string(ctx, 1);

  if(es->es_stmt) {
    sqlite3_finalize(es->es_stmt);
    es->es_stmt = NULL;
  }

  int rc = db_prepare(es->es_db, &es->es_stmt, query);

  if(rc != SQLITE_OK) {

    if(es->es_transaction && rc == SQLITE_LOCKED)
      duk_error(ctx, ST_ERROR_SQLITE_BASE | rc , "Deadlock");

    duk_error(ctx, ST_ERROR_SQLITE_BASE | rc,
              "Sqlite error 0x%x -- %s",
              rc, sqlite3_errmsg(es->es_db));
  }

  sqlite3_stmt *stmt = es->es_stmt;

  for(int i = 2; i < argc; i++) {
    int sqlite_arg = i - 1;

    if(duk_is_null_or_undefined(ctx, i)) {
      sqlite3_bind_null(stmt, sqlite_arg);
    } else if(duk_is_number(ctx, i)) {
      sqlite3_bind_double(stmt, sqlite_arg, duk_get_number(ctx, i));
    } else if(duk_is_boolean(ctx, i)) {
      sqlite3_bind_int(stmt, sqlite_arg, duk_get_boolean(ctx, i));
    } else {
      sqlite3_bind_text(stmt, sqlite_arg, duk_safe_to_string(ctx, i),
                        -1, SQLITE_STATIC);
    }
  }

  es_sqlite_stmt_step(ctx, es);
  return 0;
}



/**
 *
 */
static int
es_sqlite_step(duk_context *ctx)
{
  es_sqlite_t *es = es_resource_get(ctx, 0, &es_resource_sqlite);

  if(es->es_stmt == NULL) {
    duk_push_null(ctx);
    return 1;
  }

  const int cols = sqlite3_data_count(es->es_stmt);

  duk_push_object(ctx);

  for(int i = 0; i < cols; i++) {

    int64_t i64;

    switch(sqlite3_column_type(es->es_stmt, i)) {

    case SQLITE_INTEGER:
      i64 = sqlite3_column_int64(es->es_stmt, i);
      if(i64 >= INT32_MIN && i64 <= INT32_MAX)
        duk_push_int(ctx, i64);
      else if(i64 >= 0 && i64 <= UINT32_MAX)
        duk_push_uint(ctx, i64);
      else
        duk_push_number(ctx, i64);
      break;
    case SQLITE_TEXT:
      duk_push_string(ctx, (const char *)sqlite3_column_text(es->es_stmt, i));
      break;
    case SQLITE_FLOAT:
      duk_push_number(ctx, sqlite3_column_double(es->es_stmt, i));
      break;
    default:
      continue;
    }
    duk_put_prop_string(ctx, -2, sqlite3_column_name(es->es_stmt, i));
  }
  es_sqlite_stmt_step(ctx, es);
  return 1;
}



/**
 *
 */
static int
es_db_changes(duk_context *ctx)
{
  es_sqlite_t *es = es_resource_get(ctx, 0, &es_resource_sqlite);
  duk_push_int(ctx, sqlite3_changes(es->es_db));
  return 1;
}


/**
 *
 */
static int
es_db_last_error_code(duk_context *ctx)
{
  es_sqlite_t *es = es_resource_get(ctx, 0, &es_resource_sqlite);
  duk_push_int(ctx, sqlite3_errcode(es->es_db));
  return 1;
}


/**
 *
 */
static int
es_db_last_error_str(duk_context *ctx)
{
  es_sqlite_t *es = es_resource_get(ctx, 0, &es_resource_sqlite);
  duk_push_string(ctx, sqlite3_errmsg(es->es_db));
  return 1;
}


/**
 *
 */
static int
es_db_last_insert_row_id(duk_context *ctx)
{
  es_sqlite_t *es = es_resource_get(ctx, 0, &es_resource_sqlite);
  duk_push_uint(ctx, sqlite3_last_insert_rowid(es->es_db));
  return 1;
}


/**
 *
 */
static int
es_db_upgrade_schema(duk_context *ctx)
{
  es_sqlite_t *es = es_resource_get(ctx, 0, &es_resource_sqlite);

  int r = db_upgrade_schema(es->es_db, duk_safe_to_string(ctx, 1),
                            es->es_name, NULL, NULL);

  if(r)
    duk_error(ctx, DUK_ERR_ERROR, "Unable to upgrade schema");
  return 0;
}



/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_sqlite[] = {
  { "create",          es_sqlite_create,         1 },
  { "query",           es_sqlite_query,          DUK_VARARGS },
  { "changes",         es_db_changes,            1 },
  { "step",            es_sqlite_step,           1 },
  { "lastErrorCode",   es_db_last_error_code,    1 },
  { "lastErrorString", es_db_last_error_str,     1 },
  { "lastRowId",       es_db_last_insert_row_id, 1 },
  { "upgradeSchema",   es_db_upgrade_schema,     2 },
  { NULL, NULL, 0}
};
