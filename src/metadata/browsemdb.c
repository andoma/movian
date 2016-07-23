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
#include <limits.h>
#include <unistd.h>
#include <stdio.h>

#include "main.h"
#include "backend/backend.h"
#include "db/db_support.h"
#include "metadata.h"
#include "navigator.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_indexer.h"
#include "metadata_str.h"
#include "misc/minmax.h"

/**
 *
 */
typedef struct bmdb {
  char *b_query;
  library_query_t b_type;
  prop_t *b_nodes;
  prop_t *b_metadata;

} bmdb_t;




/**
 *
 */
static int
count_items(void *db, const char *query, const char *url)
{
  int rval = 0;
  sqlite3_stmt *stmt;
  char pfx[PATH_MAX];

  int rc = db_prepare(db, &stmt, query);
  if(rc != SQLITE_OK)
    return 0;

  db_escape_path_query(pfx, sizeof(pfx), url);
  sqlite3_bind_text(stmt, 1, pfx, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  if(rc == SQLITE_ROW)
    rval = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  return rval;
}


/**
 *
 */
static int
get_percentage(const char *url)
{
  void *db = metadb_get();
  int remain, done;
  int rval;
  if(db == NULL)
    return 0;

  remain = count_items(db, 
                      "SELECT count(*) "
                      "FROM item "
                      "WHERE url LIKE ?1 "
                      "AND contenttype = 1 "
                      "AND indexstatus == 0", 
                      url);
  if(remain) {
    done = count_items(db, 
                       "SELECT count(*) "
                       "FROM item "
                       "WHERE url LIKE ?1 "
                       "AND contenttype = 1 "
                       "AND indexstatus > 1", 
                       url);
    rval = MIN(done * 100 / (done+remain), 100);
  } else {
    rval = 100;
  }
  metadb_close(db);
  return rval;
}


/**
 *
 */
static void
bmdb_destroy(bmdb_t *b)
{
  prop_ref_dec(b->b_nodes);
  prop_ref_dec(b->b_metadata);
  free(b->b_query);
  free(b);
}


/**
 *
 */
static void
add_item(bmdb_t *b, const char *url, const char *parent, rstr_t *contenttype,
         const char *title, int track, const char *artist, int duration)
{
  prop_t *c = prop_create_r(b->b_nodes, url);

  prop_unmark(c);
  
  prop_set(c, "type", PROP_SET_RSTRING, contenttype);
  prop_set(c, "url", PROP_SET_STRING, url);

  prop_t *metadata = prop_create_r(c, "metadata");

  if(track)
    prop_set(metadata, "track", PROP_SET_INT, track);

  if(artist)
    prop_set(metadata, "artist", PROP_SET_STRING, artist);

  if(duration > 0)
    prop_set(metadata, "duration", PROP_SET_INT, duration / 1000);

  if(title == NULL) {
    char fname[512];
    fa_url_get_last_component(fname, sizeof(fname), url);

    rstr_t *ft = metadata_remove_postfix(fname);
    prop_set(metadata, "title", PROP_SET_RSTRING, ft);
    rstr_release(ft);
  } else {
    prop_set(metadata, "title", PROP_SET_STRING, title);
  }


#if 0
  prop_t *options = prop_create(bi->bi_prop, "options");
  metadata_bind_video_info(metadata, bi->bi_url, title, NULL, 0, options,
                           bi->bi_prop, NULL, 0, 1);
#endif
  prop_ref_dec(metadata);
  prop_ref_dec(c);
}


/**
 *
 */
static void
video_query(bmdb_t *b, void *db)
{
  sqlite3_stmt *stmt;

  int rc = db_prepare(db, &stmt, 
                      "SELECT i.url, p.url, i.contenttype "
                      "FROM item AS i, item AS p "
                      "WHERE i.url LIKE ?1 "
                      "AND i.parent IS NOT NULL "
                      "AND (i.contenttype == 5 OR i.contenttype == 7) "
                      "AND i.parent = p.id"
                      );
  
  if(rc != SQLITE_OK)
    return;

  char q[PATH_MAX];
  db_escape_path_query(q, sizeof(q), b->b_query);
  sqlite3_bind_text(stmt, 1, q, -1, SQLITE_STATIC);

  while((rc = db_step(stmt)) == SQLITE_ROW) {
    const char *url = (const char *)sqlite3_column_text(stmt, 0);
    const char *parent = (const char *)sqlite3_column_text(stmt, 1);
    rstr_t *ct = rstr_alloc(content2type(sqlite3_column_int(stmt, 2)));
    add_item(b, url, parent, ct, NULL, 0, NULL, 0);
    rstr_release(ct);
  }
  sqlite3_finalize(stmt);
}


/**
 *
 */
static void
albums_query(bmdb_t *b, void *db)
{
  sqlite3_stmt *stmt;

  int rc = db_prepare(db, &stmt, 
                      "SELECT album.id, album.title, artist.title "
                      "FROM album, item, audioitem, artist "
                      "WHERE audioitem.item_id = item.id "
                      "AND audioitem.album_id = album.id "
                      "AND item.url LIKE ?1 "
                      "AND audioitem.ds_id = 1 "
                      "AND item.parent IS NOT NULL "
                      "AND audioitem.artist_id = artist.id "
                      "GROUP BY album_id");

  if(rc != SQLITE_OK)
    return;

  char q[PATH_MAX];
  db_escape_path_query(q, sizeof(q), b->b_query);
  sqlite3_bind_text(stmt, 1, q, -1, SQLITE_STATIC);

  rstr_t *ct = rstr_alloc("album");

  while((rc = db_step(stmt)) == SQLITE_ROW) {
    char url[PATH_MAX];
    snprintf(url, sizeof(url), "library:album:%d", 
             sqlite3_column_int(stmt, 0));
    add_item(b, url, NULL, ct, 
             (const char *)sqlite3_column_text(stmt, 1), 0,
             (const char *)sqlite3_column_text(stmt, 2), 0);
  }
  rstr_release(ct);
  sqlite3_finalize(stmt);
}


/**
 *
 */
static void
album_query(bmdb_t *b, void *db)
{
  sqlite3_stmt *stmt;
  int rc;
  int album_id = atoi(b->b_query);

  rc = db_prepare(db, &stmt, 
                  "SELECT album.title, artist.title "
                  "FROM album, artist "
                  "WHERE album.id = ?1 "
                  "AND artist.id = album.artist_id "
                  "AND album.ds_id = 1");

  if(rc != SQLITE_OK)
    return;

  sqlite3_bind_int(stmt, 1, album_id);

  if(db_step(stmt) == SQLITE_ROW) {
    rstr_t *album  = db_rstr(stmt, 0);
    rstr_t *artist = db_rstr(stmt, 1);

    prop_set(b->b_metadata, "title",       PROP_SET_RSTRING, album);
    prop_set(b->b_metadata, "artist_name", PROP_SET_RSTRING, artist);

    prop_t *p = prop_create_r(b->b_metadata, "album_art");
    metadata_bind_albumart(p, artist, album);
    prop_ref_dec(p);

    rstr_release(album);
    rstr_release(artist);
  }

  sqlite3_finalize(stmt);

  rc = db_prepare(db, &stmt, 
                  "SELECT url, audioitem.title, track, duration, "
                  "artist.title "
                  "FROM audioitem,item,artist "
                  "WHERE audioitem.item_id = item.id "
                  "AND audioitem.artist_id = artist.id "
                  "AND album_id = ?1 "
                  "AND parent IS NOT NULL "
                  "AND audioitem.ds_id = 1");

  if(rc != SQLITE_OK)
    return;

  sqlite3_bind_int(stmt, 1, album_id);

  rstr_t *ct = rstr_alloc("audio");

  while((rc = db_step(stmt)) == SQLITE_ROW) {
    add_item(b, (const char *)sqlite3_column_text(stmt, 0),
             NULL, ct, 
             (const char *)sqlite3_column_text(stmt, 1),
             sqlite3_column_int(stmt, 2),
             (const char *)sqlite3_column_text(stmt, 4),
             sqlite3_column_int(stmt, 3));
             
  }
  rstr_release(ct);
  sqlite3_finalize(stmt);
}


/**
 *
 */
static void
artist_query(bmdb_t *b, void *db)
{
  sqlite3_stmt *stmt;
  int rc;
  int artist_id = atoi(b->b_query);

  rc = db_prepare(db, &stmt, 
                  "SELECT title "
                  "FROM artist "
                  "WHERE ds_id = 1 "
                  "AND id = ?1");

  if(rc != SQLITE_OK)
    return;

  sqlite3_bind_int(stmt, 1, artist_id);

  if(db_step(stmt) == SQLITE_ROW) {
    rstr_t *artist = db_rstr(stmt, 0);
    prop_set(b->b_metadata, "title",       PROP_SET_RSTRING, artist);
    
    prop_t *p = prop_create_r(b->b_metadata, "artist_images");
    metadata_bind_artistpics(p, artist);
    prop_ref_dec(p);

    rstr_release(artist);
  }

  sqlite3_finalize(stmt);

  rc = db_prepare(db, &stmt, 
                  "SELECT id,title "
                  "FROM album "
                  "WHERE ds_id = 1 "
                  "AND artist_id = ?1");

  if(rc != SQLITE_OK)
    return;

  sqlite3_bind_int(stmt, 1, artist_id);

  rstr_t *ct = rstr_alloc("album");

  while((rc = db_step(stmt)) == SQLITE_ROW) {
    char url[PATH_MAX];
    snprintf(url, sizeof(url), "library:album:%d", 
             sqlite3_column_int(stmt, 0));

    add_item(b, url, NULL, ct, 
             (const char *)sqlite3_column_text(stmt, 1), 0, NULL, 0);
  }
  rstr_release(ct);
  sqlite3_finalize(stmt);
}


/**
 *
 */
static void
artists_query(bmdb_t *b, void *db)
{
  sqlite3_stmt *stmt;

  int rc = db_prepare(db, &stmt, 
                      "SELECT artist.id, artist.title "
                      "FROM artist,item,audioitem "
                      "WHERE audioitem.item_id = item.id "
                      "AND audioitem.artist_id = artist.id "
                      "AND item.url like ?1 "
                      "AND parent IS NOT NULL "
                      "AND audioitem.ds_id = 1 "
                      "GROUP by artist_id");

  if(rc != SQLITE_OK)
    return;

  char q[PATH_MAX];
  db_escape_path_query(q, sizeof(q), b->b_query);
  sqlite3_bind_text(stmt, 1, q, -1, SQLITE_STATIC);

  rstr_t *ct = rstr_alloc("artist");

  while((rc = db_step(stmt)) == SQLITE_ROW) {
    char url[PATH_MAX];
    snprintf(url, sizeof(url), "library:artist:%d", 
             sqlite3_column_int(stmt, 0));
    add_item(b, url, NULL, ct,
             (const char *)sqlite3_column_text(stmt, 1), 0, NULL, 0);
  }
  rstr_release(ct);
  sqlite3_finalize(stmt);
}


/**
 *
 */
static int
bmdb_query_exec(void *db, bmdb_t *b)
{
  prop_mark_childs(b->b_nodes);

  switch(b->b_type) {
  case LIBRARY_QUERY_ALBUMS:
    albums_query(b, db);
    break;
  case LIBRARY_QUERY_ALBUM:
    album_query(b, db);
    break;
  case LIBRARY_QUERY_VIDEOS:
    video_query(b, db);
    break;
  case LIBRARY_QUERY_ARTISTS:
    artists_query(b, db);
    break;
  case LIBRARY_QUERY_ARTIST:
    artist_query(b, db);
    break;
  }

  prop_destroy_marked_childs(b->b_nodes);
  return 0;
}




/**
 *
 */
void
metadata_browse(void *db, const char *url, prop_t *nodes,
                prop_t *model,
                library_query_t type,
                int (*checkstop)(void *opaque), void *opaque)
{
  prop_t *status = prop_create_r(model, "status");
  bmdb_t b = {0};
  b.b_query = strdup(url);
  b.b_type = type;
  b.b_nodes = nodes;
  b.b_metadata = prop_create_r(model, "metadata");

  while(!checkstop(opaque)) {
    int p = get_percentage(url);

    if(p != 100) {
      prop_set(model, "percentage", PROP_SET_INT, p);
      prop_set(model, "progressmeter", PROP_SET_INT, 1);
      prop_link(_p("Indexing"), status);
    } else {
      prop_set(model, "progressmeter", PROP_SET_INT, 0);
      prop_unlink(status);
    }

    //    int64_t ts = arch_get_ts();
    bmdb_query_exec(db, &b);
    //    printf("Query took %lld\n", arch_get_ts() - ts);
    prop_set(model, "loading", PROP_SET_INT, 0);
    sleep(1);
  }

  prop_set(model, "progressmeter", PROP_SET_INT, 0);
  prop_unlink(status);

  free(b.b_query);
  prop_ref_dec(status);
  prop_ref_dec(b.b_metadata);
}




/**
 *
 */
static void *
bmdb_thread(void *aux)
{
  bmdb_t *b = aux;
  void *db = metadb_get();
  bmdb_query_exec(db, b);
  metadb_close(db);
  bmdb_destroy(b);
  return NULL;
}


/**
 *
 */
static bmdb_t *
bmdb_query_create(const char *query, int type, prop_t *model)
{
  bmdb_t *b = calloc(1, sizeof(bmdb_t));

  prop_set(model, "type", PROP_SET_STRING, "directory");
  b->b_nodes = prop_create_r(model, "nodes");
  b->b_metadata = prop_create_r(model, "metadata");
  b->b_type = type;
  b->b_query = strdup(query);
  return b;
}


/**
 *
 */
static int
library_open(prop_t *page, const char *url, int sync)
{
  const char *q;
  bmdb_t *b;
  prop_t *model = prop_create(page, "model");

  url += strlen("library:");

  if((q = mystrbegins(url, "albums:")) != NULL) {
    b = bmdb_query_create(q, LIBRARY_QUERY_ALBUMS, model);
  } else if((q = mystrbegins(url, "album:")) != NULL) {
    b = bmdb_query_create(q, LIBRARY_QUERY_ALBUM, model);
    prop_set(model, "contents", PROP_SET_STRING, "album");
  } else if((q = mystrbegins(url, "artists:")) != NULL) {
    b = bmdb_query_create(q, LIBRARY_QUERY_ARTISTS, model);
  } else if((q = mystrbegins(url, "artist:")) != NULL) {
    b = bmdb_query_create(q, LIBRARY_QUERY_ARTIST, model);
    prop_set(model, "contents", PROP_SET_STRING, "artist");
  } else if((q = mystrbegins(url, "videos:")) != NULL) {
    b = bmdb_query_create(q, LIBRARY_QUERY_VIDEOS, model);

  } else {
    nav_open_error(page, "Invalid browse URL");
    return 0;
  }

  hts_thread_create_detached("bmdbquery", bmdb_thread, b,
			     THREAD_PRIO_METADATA);
  return 0;
}


/**
 *
 */
static int
library_canhandle(const char *url)
{
  return !strncmp(url, "library:", strlen("library:"));
}


/**
 *
 */
static backend_t be_library = {
  .be_canhandle = library_canhandle,
  .be_open = library_open,
};

BE_REGISTER(library);
