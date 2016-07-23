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
#include "fa_probe.h"
#include "fileaccess.h"
#include "htsmsg/htsmsg_store.h"

#define INDEXER_TRACE(x, ...) do {                                   \
    if(gconf.enable_indexer_debug)                                   \
      TRACE(TRACE_DEBUG, "Indexer", x, ##__VA_ARGS__);               \
  } while(0)

extern int media_buffer_hungry;

static void
update_item(void *db, const fa_dir_entry_t *fsentry, const char *parent,
            time_t parent_mtime)
{
  metadata_t *md;
  metadata_index_status_t index_status = INDEX_STATUS_ANALYZED;

  if(content_dirish(fsentry->fde_type)) {
    md = fa_probe_dir(rstr_get(fsentry->fde_url));

    if(md->md_contenttype == CONTENT_DIR) {
      // Regular dirs need further scanning
      index_status = INDEX_STATUS_UNSET;
    }

  } else {
    md = fa_probe_metadata(rstr_get(fsentry->fde_url), NULL, 0,
                           rstr_get(fsentry->fde_filename), NULL);
  }
  if(md == NULL)
    return;

  metadb_metadata_write(db, rstr_get(fsentry->fde_url),
                        fsentry->fde_stat.fs_mtime,
                        md, parent, parent_mtime,
                        index_status);
  metadata_destroy(md);
}



static int
rescan_directory(const char *url, char *errbuf, size_t errlen, void *db,
                 time_t fs_mtime)
{
  fa_dir_entry_t *fsentry, *dbentry, *n;

  fa_dir_t *fsdir = fa_scandir(url, errbuf, errlen);
  if(fsdir == NULL)
    return -1;

  RB_FOREACH(fsentry, &fsdir->fd_entries, fde_link) {
    fa_dir_entry_stat(fsentry);
    if(fsentry->fde_type == CONTENT_FILE) {
      fsentry->fde_type =
        contenttype_from_filename(rstr_get(fsentry->fde_filename));
    }
  }

  fa_dir_t *dbdir = metadb_metadata_scandir(db, url, NULL);
  if(dbdir == NULL)
    dbdir = fa_dir_alloc();

  for(dbentry = RB_FIRST(&dbdir->fd_entries); dbentry != NULL; dbentry = n) {
    n = RB_NEXT(dbentry, fde_link);

    fsentry = fa_dir_find(fsdir, dbentry->fde_url);
    if(fsentry->fde_type == CONTENT_UNKNOWN)
      fsentry = NULL;

    if(fsentry != NULL) {

      // Exist in fs and in db, check modification time and index status

      if(fsentry->fde_stat.fs_mtime == dbentry->fde_stat.fs_mtime &&
         dbentry->fde_md != NULL &&
         dbentry->fde_md->md_index_status >= INDEX_STATUS_ANALYZED) {
        // Ok, don't do anything
      } else {
        INDEXER_TRACE("Updating item %s", rstr_get(fsentry->fde_url));
        update_item(db, fsentry, url, fs_mtime);
      }
      fa_dir_entry_free(fsdir, fsentry);
    } else {
      // Exist in DB but not in filesystem
      INDEXER_TRACE("Removing item %s", rstr_get(dbentry->fde_url));
      metadb_unparent_item(db, rstr_get(dbentry->fde_url));
    }
  }

  RB_FOREACH(fsentry, &fsdir->fd_entries, fde_link) {
    if(fsentry->fde_type == CONTENT_UNKNOWN)
      continue;
    INDEXER_TRACE("New item %s", rstr_get(fsentry->fde_url));
    update_item(db, fsentry, url, fs_mtime);
  }

  fa_dir_free(fsdir);
  fa_dir_free(dbdir);
  return 0;
}


/**
 *
 */
static void
index_directory(const char *url)
{
  fa_stat_t fs;
  int err;
  char errbuf[512];
  void *db = metadb_get();

  if(!fa_stat_ex(url, &fs, errbuf, sizeof(errbuf), FA_NON_INTERACTIVE)) {
    INDEXER_TRACE("Scanning path %s", url);
    err = rescan_directory(url, errbuf, sizeof(errbuf), db, fs.fs_mtime);
  } else {
    INDEXER_TRACE("Scanning %s failed -- %s", url, errbuf);
    err = 1;
  }
  sqlite3_stmt *stmt;

  // Update the index status for the scanned directory
  int rc = db_prepare(db, &stmt,
                      "UPDATE item "
                      "SET indexstatus = ?2 "
                      "WHERE url = ?1");
  if(!rc) {
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, err ? INDEX_STATUS_ERROR : INDEX_STATUS_ANALYZED);
    db_step(stmt);
    sqlite3_finalize(stmt);
  }
  metadb_close(db);
}



TAILQ_HEAD(item_queue, item);

typedef struct item {
  TAILQ_ENTRY(item) link;
  char *url;
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
  }
  sqlite3_finalize(stmt);
  return 0;
}


/**
 *
 */
static int
find_unprocessed_directory(const char *prefix)
{
  char pfx[PATH_MAX];
  void *db = metadb_get();

  struct item_queue q;
  db_escape_path_query(pfx, sizeof(pfx), prefix);

  TAILQ_INIT(&q);

  int r = get_items(db, &q, pfx,
                    "SELECT url "
                    "FROM item "
                    "WHERE url LIKE ?1 "
                    "AND contenttype = 1 "
                    "AND indexstatus = 0 "
                    "LIMIT 1");

  metadb_close(db);
  if(r)
    return 0;

  item_t *i;
  r = 0;
  TAILQ_FOREACH(i, &q, link) {
    index_directory(i->url);
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
clear_index_status(const char *url)
{
  char pfx[PATH_MAX];
  db_escape_path_query(pfx, sizeof(pfx), url);
  void *db = metadb_get();
  sqlite3_stmt *stmt;
  int rc;

  rc = db_prepare(db, &stmt,
                  "UPDATE item "
                  "SET indexstatus = 0 "
                  "WHERE url LIKE ?1 OR url = ?2");
  if(!rc) {
    sqlite3_bind_text(stmt, 1, pfx, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, url, -1, SQLITE_STATIC);
    db_step(stmt);
    sqlite3_finalize(stmt);
  }
  metadb_close(db);
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
int
fa_indexer_enabled(const char *url)
{
  indexer_root_t *ir;
  int rval = 0;
  hts_mutex_lock(&indexer_mutex);
  TAILQ_FOREACH(ir, &roots, ir_link) {
    if(!strcmp(ir->ir_url, url))
      rval = 1;
  }
  hts_mutex_unlock(&indexer_mutex);
  return rval;
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
      TRACE(TRACE_INFO, "Indexer", "Creating indexed root at %s", url);
      hts_cond_signal(&indexer_cond);
      save_state();
    }
  } else {
    if(ir != NULL) {
      TAILQ_REMOVE(&roots, ir, ir_link);
      ir_release(ir);
      TRACE(TRACE_INFO, "Indexer", "Removing indexed root at %s", url);
      clear_index_status(url);
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
        index_directory(ir->ir_url);
        did_something = 1;
      } else {
        did_something |= find_unprocessed_directory(ir->ir_url);
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
