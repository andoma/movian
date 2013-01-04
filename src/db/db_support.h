#pragma once

#include <sqlite3.h>
#include "misc/rstr.h"

int db_one_statement(sqlite3 *db, const char *sql, const char *src);

int db_get_int_from_query(sqlite3 *db, const char *query, int *v);

int db_get_int64_from_query(sqlite3 *db, const char *query, int64_t *v);

int db_begin0(sqlite3 *db, const char *src);

int db_commit0(sqlite3 *db, const char *src);

int db_rollback0(sqlite3 *db, const char *src);

int db_rollback_deadlock0(sqlite3 *db, const char *src);

int db_step(sqlite3_stmt *pStmt);

int db_preparex(sqlite3 *db, sqlite3_stmt **ppStmt, const char *zSql, 
		const char *file, int line);


#define db_prepare(db, stmt, sql) db_preparex(db, stmt, sql, __FILE__, __LINE__)

#define db_begin(db)    db_begin0(db, __FUNCTION__)
#define db_commit(db)   db_commit0(db, __FUNCTION__)
#define db_rollback(db) db_rollback0(db, __FUNCTION__)
#define db_rollback_deadlock(db) db_rollback_deadlock0(db, __FUNCTION__)


int db_upgrade_schema(sqlite3 *db, const char *schemadir, const char *dbname);

typedef struct db_pool db_pool_t;

db_pool_t *db_pool_create(const char *path, int size);

sqlite3 *db_pool_get(db_pool_t *p);

void db_pool_put(db_pool_t *p, sqlite3 *db);

void db_pool_close(db_pool_t *dp);

rstr_t *db_rstr(sqlite3_stmt *stmt, int col);

int db_posint(sqlite3_stmt *stmt, int col);

static inline void db_bind_rstr(sqlite3_stmt *stmt, int col, rstr_t *rstr)
{
  sqlite3_bind_text(stmt, col, (void *)rstr_get(rstr), -1, SQLITE_STATIC);

}

void db_escape_path_query(char *dst, size_t dstlen, const char *src);

void db_init(void);
