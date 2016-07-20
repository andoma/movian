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
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "main.h"
#include "metadata/metadata.h"
#include "db/db_support.h"
#include "fa_indexer.h"
#include "fileaccess.h"
#include "htsmsg/htsmsg_store.h"

/**
 *
 */
static void
index_path(const char *url)
{
  fa_stat_t fs;
  int err;
  TRACE(TRACE_DEBUG, "Indexer", "Indexing %s", url);
  if(!fa_stat(url, &fs, NULL, 0)) {
    err = fa_scanner_scan(url, fs.fs_mtime);
  } else {
    err = 1;
  }

  void *db = metadb_get();
  sqlite3_stmt *stmt;
  int rc = db_prepare(db, &stmt,
                      "UPDATE item "
                      "SET indexstatus = ?2 "
                      "WHERE URL = ?1");
  if(!rc) {
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, err ? INDEX_STATUS_ERROR : INDEX_STATUS_STATED);
    db_step(stmt);
    sqlite3_finalize(stmt);
  }
  metadb_close(db);
  TRACE(TRACE_DEBUG, "Indexer", "Indexing %s done err=%d", url, err);
}



TAILQ_HEAD(item_queue, item);

typedef struct item {
  TAILQ_ENTRY(item) link;
  char *url;
  int contenttype;
  time_t mtime;
} item_t;



/**
 *
 */
static void
free_items(struct item_queue *q)
{
  item_t *i;
  while((i = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, i, link);
    free(i->url);
    free(i);
  }
}


/**
 *
 */
static int
get_items(void *db, struct item_queue *q, const char *pfx, const char *query)
{
  sqlite3_stmt *stmt;
  int rc = db_prepare(db, &stmt, query);

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(stmt, 1, pfx, -1, SQLITE_STATIC);

  while((rc = db_step(stmt)) == SQLITE_ROW) {
    item_t *i = malloc(sizeof(item_t));
    TAILQ_INSERT_TAIL(q, i, link);
    const char *url = (const char *)sqlite3_column_text(stmt, 0);
    i->url         = strdup(url);
    i->contenttype = sqlite3_column_int(stmt, 1);
    i->mtime =       sqlite3_column_int(stmt, 2);
  }
  sqlite3_finalize(stmt);
  return 0;
}


/**
 *
 */
static int
do_round(const char *prefix)
{
  char pfx[PATH_MAX];
  void *db = metadb_get();

  struct item_queue q;

  db_escape_path_query(pfx, sizeof(pfx), prefix);

  TAILQ_INIT(&q);

  int r = get_items(db, &q, pfx,
                    "SELECT url, contenttype, mtime "
                    "FROM item "
                    "WHERE url LIKE ?1 "
                    "AND contenttype=1 "
                    "AND indexstatus == 0 "
                    "LIMIT 1");

  metadb_close(db);
  if(r)
    return 0;

  item_t *i;
  r = 0;
  TAILQ_FOREACH(i, &q, link) {
    index_path(i->url);
    r = 1;
  }
  free_items(&q);
  return r;
}


static hts_mutex_t indexer_mutex;
static hts_cond_t indexer_cond;
TAILQ_HEAD(indexer_root_queue, indexer_root);

static struct indexer_root_queue roots;

typedef struct indexer_root {
  TAILQ_ENTRY(indexer_root) ir_link;
  char *ir_url;
  int ir_refcount;
  int ir_root_scanned;
} indexer_root_t;


/**
 *
 */
static void
ir_release(indexer_root_t *ir)
{
  ir->ir_refcount--;
  if(ir->ir_refcount > 0)
    return;
  free(ir->ir_url);
  free(ir);
}


/**
 *
 */
static void
addroot(const char *url)
{
  indexer_root_t *ir;
  ir = calloc(1, sizeof(indexer_root_t));
  ir->ir_url = strdup(url);
  ir->ir_refcount = 1;
  TAILQ_INSERT_TAIL(&roots, ir, ir_link);
}


/**
 *
 */
static void
save_state(void)
{
  indexer_root_t *ir;
  htsmsg_t *m = htsmsg_create_map();
  htsmsg_t *r = htsmsg_create_list();

  TAILQ_FOREACH(ir, &roots, ir_link) {
    htsmsg_t *u = htsmsg_create_map();
    htsmsg_add_str(u, "url", ir->ir_url);
    htsmsg_add_msg(r, NULL, u);
  }

  htsmsg_add_msg(m, "roots", r);
  htsmsg_store_save(m, "indexer");
  htsmsg_release(m);
}



/**
 *
 */
void
fa_indexer_enable(const char *url, int on)
{
  indexer_root_t *ir;

  hts_mutex_lock(&indexer_mutex);
  TAILQ_FOREACH(ir, &roots, ir_link) {
    if(!strcmp(ir->ir_url, url))
      break;
  }

  if(on) {
    if(ir == NULL) {
      addroot(url);
      TRACE(TRACE_DEBUG, "Indexer", "Creating indexed root at %s", url);
      hts_cond_signal(&indexer_cond);
      save_state();
    }
  } else {
    if(ir != NULL) {
      TAILQ_REMOVE(&roots, ir, ir_link);
      ir_release(ir);
      TRACE(TRACE_DEBUG, "Indexer", "Removing indexed root at %s", url);
      save_state();
    }
  }
  hts_mutex_unlock(&indexer_mutex);
}


/**
 *
 */
static void *
indexer_thread(void *aux)
{
  indexer_root_t *ir;
  int did_something;

  hts_mutex_lock(&indexer_mutex);
  while(1) {
  restart:
    did_something = 0;
    TAILQ_FOREACH(ir, &roots, ir_link) {

      ir->ir_refcount++;

      int doroot = 0;
      if(!ir->ir_root_scanned) {
        ir->ir_root_scanned = 1;
        doroot = 1;
      }

      hts_mutex_unlock(&indexer_mutex);

      if(doroot) {
        index_path(ir->ir_url);
        did_something = 1;
      } else {
        did_something |= do_round(ir->ir_url);
      }

      hts_mutex_lock(&indexer_mutex);
      int rf = ir->ir_refcount;
      ir_release(ir);
      if(rf == 1)
        break;
    }

    TAILQ_FOREACH(ir, &roots, ir_link) {
      if(!ir->ir_root_scanned)
        goto restart;
    }

    if(!did_something)
      hts_cond_wait(&indexer_cond, &indexer_mutex);
  }
  return NULL;
}


/**
 *
 */
void
fa_indexer_init(void)
{
  TAILQ_INIT(&roots);
  hts_mutex_init(&indexer_mutex);
  hts_cond_init(&indexer_cond, &indexer_mutex);

  htsmsg_t *m = htsmsg_store_load("indexer");
  if(m != NULL) {
    htsmsg_t *r = htsmsg_get_list(m, "roots");
    if(r != NULL) {
      htsmsg_field_t *f;
      HTSMSG_FOREACH(f, r) {
        htsmsg_t *o;
        if((o = htsmsg_get_map_by_field(f)) == NULL)
          continue;
        const char *url = htsmsg_get_str(o, "url");
        if(url)
          addroot(url);
      }
    }
    htsmsg_release(m);
  }

  hts_thread_create_detached("indexer", indexer_thread, NULL,
			     THREAD_PRIO_METADATA_BG);
}
