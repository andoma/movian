/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2012 Andreas Öman
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

#include <limits.h>

#include "showtime.h"
#include "backend/backend.h"
#include "db/db_support.h"
#include "metadata.h"
#include "navigator.h"
#include "fileaccess/fileaccess.h"

#define BMDB_HASH_SIZE 511

LIST_HEAD(bmdb_item_list, bmdb_item);

/**
 *
 */
typedef struct bmdb {
  struct bmdb_item_list b_hash[BMDB_HASH_SIZE];
  char *b_query;
  prop_t *b_model;
  prop_t *b_nodes;

} bmdb_t;


/**
 *
 */
typedef struct bmdb_item {
  LIST_ENTRY(bmdb_item) bi_link;
  rstr_t *bi_url;

  prop_t *bi_prop;

} bmdb_item_t;


/**
 *
 */
static int
browsemdb_canhandle(const char *url)
{
  return !strncmp(url, "browsemdb:", strlen("browsemdb:"));
}


/**
 *
 */
static void
escape_like_query(char *dst, size_t dstlen, const char *src)
{
  for(; *src && dstlen > 3; dstlen--) {
    if(*src == '%' || *src == '_') {
      *dst++ = '\\';
      *dst++ = *src++;
      dstlen--;
    } else {
      *dst++ = *src++;
    }
  }
  *dst++ = '%';
  *dst = 0;
}


/**
 *
 */
static void
bmdb_destroy(bmdb_t *b)
{
  prop_ref_dec(b->b_nodes);
  prop_ref_dec(b->b_model);
  free(b->b_query);
  free(b);
}

/**
 *
 */
static void
add_item(bmdb_t *b, const char *url, const char *parent, int contenttype)
{
  char fname[512];
  char directory[512];

  fa_url_get_last_component(fname, sizeof(fname), url);
  fa_url_get_last_component(directory, sizeof(directory), parent);
  
  rstr_t *title = metadata_remove_postfix(fname);

  prop_t *item = prop_create_root(NULL);
  prop_set_string(prop_create(item, "type"), "video");
  prop_t *metadata = prop_create(item, "metadata");
  prop_t *options = prop_create(item, "options");

  prop_set_rstring(prop_create(metadata, "title"), title);


  rstr_t *rurl = rstr_alloc(url);
  rstr_t *rdir = rstr_alloc(directory);

  metadata_bind_video_info(metadata,
			   rurl, title,
			   NULL, 0,
			   options, item,
			   rdir, 0, 1);

  if(prop_set_parent(item, b->b_nodes))
    prop_destroy(item);

  rstr_release(title);
}





/**
 *
 */
static int
bmdb_files(prop_t *page, const char *url)
{
  char q[PATH_MAX];
  int rc;
  sqlite3_stmt *stmt;

  bmdb_t *b = calloc(1, sizeof(bmdb_t));
  
  b->b_model = prop_create_r(page, "model");
  b->b_nodes = prop_create_r(b->b_model, "nodes");

  prop_set_string(prop_create(b->b_model, "type"), "directory");

  escape_like_query(q, sizeof(q), url);

  void *db = metadb_get();

  rc = db_prepare(db, &stmt, 
		  "SELECT i.url, p.url, i.contenttype "
		  "FROM item AS i, item AS p "
		  "WHERE i.url LIKE ?1 "
		  "AND (i.contenttype == 5 OR i.contenttype == 7) "
		  "AND i.parent = p.id"
		  );

  if(rc != SQLITE_OK) {
    goto err;
  }

  sqlite3_bind_text(stmt, 1, q, -1, SQLITE_STATIC);

  while((rc = db_step(stmt)) == SQLITE_ROW) {
    const char *url = (const char *)sqlite3_column_text(stmt, 0);
    const char *parent = (const char *)sqlite3_column_text(stmt, 1);
    int contenttype = sqlite3_column_int(stmt, 2);
    add_item(b, url, parent, contenttype);
  }

  sqlite3_finalize(stmt);

  bmdb_destroy(b);

  metadb_close(db);
  return 0;

 err:
  nav_open_error(page, "some Error");
  metadb_close(db);
  return 0;
}


/**
 *
 */
static int
browsemdb_open(prop_t *page, const char *url)
{
  const char *q;

  url += strlen("browsemdb:");

  if((q = mystrbegins(url, "files:")) != NULL)
    return bmdb_files(page, q);

  nav_open_error(page, "Invalid browse URL");
  return 0;
}


/**
 *
 */
static backend_t be_browsemdb = {
  .be_canhandle = browsemdb_canhandle,
  .be_open = browsemdb_open,
};

BE_REGISTER(browsemdb);
