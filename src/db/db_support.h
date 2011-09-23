#pragma once

#include "ext/sqlite/sqlite3.h"

int db_one_statement(sqlite3 *db, const char *sql);

int db_get_int_from_query(sqlite3 *db, const char *query, int *v);

int db_get_int64_from_query(sqlite3 *db, const char *query, int64_t *v);

int db_begin(sqlite3 *db);

int db_commit(sqlite3 *db);

int db_rollback(sqlite3 *db);

int db_upgrade_schema(sqlite3 *db, const char *schemadir, const char *dbname);

typedef struct db_pool db_pool_t;

db_pool_t *db_pool_create(const char *path, int size);

sqlite3 *db_pool_get(db_pool_t *p);

void db_pool_put(db_pool_t *p, sqlite3 *db);
