#pragma once

#include "ext/sqlite/sqlite3.h"

int db_one_statement(sqlite3 *db, const char *sql, const char *src);

int db_get_int_from_query(sqlite3 *db, const char *query, int *v);

int db_get_int64_from_query(sqlite3 *db, const char *query, int64_t *v);

int db_begin0(sqlite3 *db, const char *src);

int db_commit0(sqlite3 *db, const char *src);

int db_rollback0(sqlite3 *db, const char *src);

int db_step(sqlite3_stmt *pStmt);

int db_prepare(sqlite3 *db, const char *zSql, int nSql,
	       sqlite3_stmt **ppStmt, const char **pz);

#define db_begin(db)    db_begin0(db, __FUNCTION__)
#define db_commit(db)   db_commit0(db, __FUNCTION__)
#define db_rollback(db) db_rollback0(db, __FUNCTION__)


int db_upgrade_schema(sqlite3 *db, const char *schemadir, const char *dbname);

typedef struct db_pool db_pool_t;

db_pool_t *db_pool_create(const char *path, int size);

sqlite3 *db_pool_get(db_pool_t *p);

void db_pool_put(db_pool_t *p, sqlite3 *db);

void db_pool_close(db_pool_t *dp);
