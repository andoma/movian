/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2011 Andreas Öman
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

#include <stdio.h>
#include <unistd.h>

#include "prop/prop.h"
#include "ext/sqlite/sqlite3.h"

#include "showtime.h"
#include "media.h"

#include "api/lastfm.h"

#include "metadata.h"
#include "fileaccess/fileaccess.h"

#include "db/db_support.h"

#include "video/video_settings.h"

#define METADATA_VERSION_STR "1"

// If not set to true by metadb_init() no metadb actions will occur
static db_pool_t *metadb_pool;
static hts_mutex_t mip_mutex;

static void mip_update_by_url(sqlite3 *db, const char *url);

/**
 *
 */
metadata_t *
metadata_create(void)
{
  metadata_t *md = calloc(1, sizeof(metadata_t));
  TAILQ_INIT(&md->md_streams);
  return md;
}

/**
 *
 */
void
metadata_destroy(metadata_t *md)
{
  metadata_stream_t *ms;
  rstr_release(md->md_title);
  rstr_release(md->md_album);
  rstr_release(md->md_artist);
  rstr_release(md->md_format);

  free(md->md_redirect);

  while((ms = TAILQ_FIRST(&md->md_streams)) != NULL) {
    TAILQ_REMOVE(&md->md_streams, ms, ms_link);
    rstr_release(ms->ms_info);
    rstr_release(ms->ms_isolang);
    free(ms);
  }
  free(md);
}


/**
 *
 */
void
metadata_add_stream(metadata_t *md, const char *codec, enum AVMediaType type,
		    int streamindex, const char *info, const char *isolang,
		    int disposition)
{
  metadata_stream_t *ms = malloc(sizeof(metadata_stream_t));
  ms->ms_info = rstr_alloc(info);
  ms->ms_isolang = rstr_alloc(isolang);
  ms->ms_codec = rstr_alloc(codec);
  ms->ms_type = type;
  ms->ms_disposition = disposition;
  ms->ms_streamindex = streamindex;
  TAILQ_INSERT_TAIL(&md->md_streams, ms, ms_link);
}


/**
 *
 */

const char *
content2type(contenttype_t ctype) {

  static const char *types[] = {
    [CONTENT_UNKNOWN]  = "unknown",
    [CONTENT_DIR]      = "directory",
    [CONTENT_FILE]     = "file",
    [CONTENT_AUDIO]    = "audio",
    [CONTENT_ARCHIVE]  = "archive",
    [CONTENT_VIDEO]    = "video",
    [CONTENT_PLAYLIST] = "playlist",
    [CONTENT_DVD]      = "dvd",
    [CONTENT_IMAGE]    = "image",
    [CONTENT_ALBUM]    = "album",
  };

  if (ctype < 0 || ctype >= sizeof(types) / sizeof(types[0]))
    return NULL;

  return types[ctype];
}



/**
 *
 */
static void
metadata_stream_make_prop(const metadata_stream_t *ms, prop_t *parent)
{
  char url[16];
  int score = 0;
  snprintf(url, sizeof(url), "libav:%d", ms->ms_streamindex);

  if(ms->ms_disposition & AV_DISPOSITION_DEFAULT)
    score += 10;

  mp_add_track(parent,
	       NULL,
	       url,
	       rstr_get(ms->ms_codec),
	       rstr_get(ms->ms_info),
	       rstr_get(ms->ms_isolang),
	       NULL,
	       score);
}


/**
 *
 */
void
metadata_to_proptree(const metadata_t *md, prop_t *proproot,
		     int overwrite_title)
{
  metadata_stream_t *ms;

  if(md->md_title != NULL)
    prop_set_rstring_ex(prop_create(proproot, "title"),
			NULL, md->md_title, !overwrite_title);

  if(md->md_artist) {
    prop_set_rstring(prop_create(proproot, "artist"), md->md_artist);

    lastfm_artistpics_init(prop_create(proproot, "artist_images"),
			   md->md_artist);
  }

  if(md->md_album) {
    prop_set_rstring(prop_create(proproot, "album"),  md->md_album);

    if(md->md_artist != NULL)
      lastfm_albumart_init(prop_create(proproot, "album_art"),
			   md->md_artist, md->md_album);
  }

  TAILQ_FOREACH(ms, &md->md_streams, ms_link) {

    prop_t *p;

    switch(ms->ms_type) {
    case AVMEDIA_TYPE_AUDIO:
      p = prop_create(proproot, "audiostreams");
      break;
    case AVMEDIA_TYPE_VIDEO:
      p = prop_create(proproot, "videostreams");
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      p = prop_create(proproot, "subtitlestreams");
      break;
    default:
      continue;
    }
    metadata_stream_make_prop(ms, p);
  }

  if(md->md_format != NULL)
    prop_set_rstring(prop_create(proproot, "format"), md->md_format);

  if(md->md_duration)
    prop_set_float(prop_create(proproot, "duration"), md->md_duration);

  if(md->md_tracks)
    prop_set_int(prop_create(proproot, "tracks"), md->md_tracks);

  if(md->md_time)
    prop_set_int(prop_create(proproot, "timestamp"), md->md_time);
}


/**
 *
 */
void
metadb_init(void)
{
  sqlite3 *db;
  extern char *showtime_persistent_path;
  char buf[256];

  snprintf(buf, sizeof(buf), "%s/metadb", showtime_persistent_path);
  mkdir(buf, 0770);
  snprintf(buf, sizeof(buf), "%s/metadb/meta.db", showtime_persistent_path);

  //  unlink(buf);

  hts_mutex_init(&mip_mutex);

  metadb_pool = db_pool_create(buf, 2);
  db = metadb_get();
  if(db == NULL)
    return;

  int r = db_upgrade_schema(db, "bundle://resources/metadb", "metadb");

  metadb_close(db);

  if(r)
    metadb_pool = NULL; // Disable
}



/**
 *
 */
void *
metadb_get(void)
{
  return db_pool_get(metadb_pool);
}


/**
 *
 */
void 
metadb_close(void *db)
{
  db_pool_put(metadb_pool, db);
}


/**
 *
 */
static int64_t
db_item_get(sqlite3 *db, const char *url, time_t *mtimep)
{
  int rc;
  int64_t rval = -1;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT id,mtime from item where url=?1 ",
			  -1, &stmt, NULL);
  if(rc)
    return -1;
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);

  if(rc == SQLITE_ROW) {
    rval = sqlite3_column_int64(stmt, 0);
    if(mtimep != NULL)
      *mtimep = sqlite3_column_int(stmt, 1);
  }
  sqlite3_finalize(stmt);
  return rval;
}


/**
 *
 */
static int64_t
db_item_create(sqlite3 *db, const char *url, int contenttype, time_t mtime,
	       int64_t parentid)
{
  int rc;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, 
			  "INSERT INTO item "
			  "(url, contenttype, mtime, parent, metadataversion) "
			  "VALUES "
			  "(?1, ?2, ?3, ?4, " METADATA_VERSION_STR ")",
			  -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  if(contenttype)
    sqlite3_bind_int(stmt, 2, contenttype);
  if(mtime)
    sqlite3_bind_int(stmt, 3, mtime);
  if(parentid > 0)
    sqlite3_bind_int64(stmt, 4, parentid);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if(rc == SQLITE_DONE)
    return sqlite3_last_insert_rowid(db);
  else
    return -1;
}


/**
 *
 */
static int64_t
metadb_artist_get_by_title(sqlite3 *db, const char *title)
{
  int rc;
  int64_t rval = -1;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT id from artist where title=?1 ",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_text(sel, 1, title, -1, SQLITE_STATIC);
  rc = sqlite3_step(sel);

  if(rc == SQLITE_ROW) {
    rval = sqlite3_column_int64(sel, 0);
  } else if(rc == SQLITE_DONE) {
    // No entry found, INSERT it

    sqlite3_stmt *ins;

    rc = sqlite3_prepare_v2(db, 
			    "INSERT INTO artist "
			    "(title) "
			    "VALUES "
			    "(?1)",
			    -1, &ins, NULL);

    if(rc == SQLITE_OK) {
      sqlite3_bind_text(ins, 1, title, -1, SQLITE_STATIC);
      rc = sqlite3_step(ins);
      sqlite3_finalize(ins);
      if(rc == SQLITE_DONE)
	rval = sqlite3_last_insert_rowid(db);
    } else {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
    }
  }

  sqlite3_finalize(sel);
  return rval;
}


/**
 *
 */
static int64_t
metadb_album_get_by_title(sqlite3 *db, const char *title)
{
  int rc;
  int64_t rval = -1;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT id from album where title=?1 ",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_text(sel, 1, title, -1, SQLITE_STATIC);
  rc = sqlite3_step(sel);

  if(rc == SQLITE_ROW) {
    rval = sqlite3_column_int64(sel, 0);
  } else if(rc == SQLITE_DONE) {
    // No entry found, INSERT it

    sqlite3_stmt *ins;

    rc = sqlite3_prepare_v2(db, 
			    "INSERT INTO album "
			    "(title) "
			    "VALUES "
			    "(?1)",
			    -1, &ins, NULL);

    if(rc == SQLITE_OK) {
      sqlite3_bind_text(ins, 1, title, -1, SQLITE_STATIC);
      rc = sqlite3_step(ins);
      sqlite3_finalize(ins);
      if(rc == SQLITE_DONE)
	rval = sqlite3_last_insert_rowid(db);
    } else {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
    }
  }

  sqlite3_finalize(sel);
  return rval;
}




/**
 *
 */
static int
metadb_insert_audioitem(sqlite3 *db, int64_t item_id, const metadata_t *md)
{
  int64_t artist_id = -1;
  int64_t album_id = -1;

  if(md->md_artist != NULL) {
    artist_id = metadb_artist_get_by_title(db, rstr_get(md->md_artist));
    if(artist_id == -1)
      return -1;
  }

  if(md->md_album != NULL) {
    album_id = metadb_album_get_by_title(db, rstr_get(md->md_album));
    if(album_id == -1)
      return -1;
  }


  int i;
  int rc = 0;
  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, 
			    i == 0 ? 
			    "INSERT OR FAIL INTO audioitem "
			    "(item_id, title, album_id, artist_id, duration) "
			    "VALUES "
			    "(?1, ?2, ?3, ?4, ?5)"
			    :
			    "UPDATE audioitem SET "
			    "title = ?2, "
			    "album_id = ?3, "
			    "artist_id = ?4, "
			    "duration = ?5 "
			    "WHERE item_id = ?1"
			    ,
			    -1, &stmt, NULL);

    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      return -1;
    }

    sqlite3_bind_int64(stmt, 1, item_id);

    if(md->md_title != NULL)
      sqlite3_bind_text(stmt, 2, rstr_get(md->md_title), -1, SQLITE_STATIC);

    if(album_id != -1)
      sqlite3_bind_int64(stmt, 3, album_id);

    if(artist_id != -1)
      sqlite3_bind_int64(stmt, 4, artist_id);

    sqlite3_bind_int(stmt, 5, md->md_duration * 1000);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_CONSTRAINT && i == 0)
      continue;
    break;
  }

  return rc != SQLITE_DONE;
}


/**
 *
 */
static int
metadb_insert_stream(sqlite3 *db, int64_t item_id, const metadata_stream_t *ms)
{
  int rc;
  sqlite3_stmt *stmt;
  const char *media;

  switch(ms->ms_type) {
  case AVMEDIA_TYPE_VIDEO:    media = "video"; break;
  case AVMEDIA_TYPE_AUDIO:    media = "audio"; break;
  case AVMEDIA_TYPE_SUBTITLE: media = "subtitle"; break;
  default:
    return 0;
  }

  rc = sqlite3_prepare_v2(db, 
			  "INSERT INTO stream "
			  "(item_id, streamindex, info, isolang, codec, mediatype, disposition) "
			  "VALUES "
			  "(?1, ?2, ?3, ?4, ?5, ?6, ?7)"
			  ,
			  -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(stmt, 1, item_id);
  sqlite3_bind_int(stmt, 2, ms->ms_streamindex);
  if(ms->ms_info != NULL)
    sqlite3_bind_text(stmt, 3, rstr_get(ms->ms_info), -1, SQLITE_STATIC);
  if(ms->ms_isolang != NULL)
    sqlite3_bind_text(stmt, 4, rstr_get(ms->ms_isolang), -1, SQLITE_STATIC);
  if(ms->ms_codec != NULL)
    sqlite3_bind_text(stmt, 5, rstr_get(ms->ms_codec), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, media, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 7, ms->ms_disposition);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc != SQLITE_DONE;
}


/**
 *
 */
static int
metadb_set_streams(sqlite3 *db, int64_t item_id, const metadata_t *md)
{
  metadata_stream_t *ms;
  sqlite3_stmt *stmt;
  int rc;

  rc = sqlite3_prepare_v2(db, 
			  "DELETE FROM stream WHERE item_id = ?1",
			  -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(stmt, 1, item_id);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if(rc != SQLITE_DONE)
    return 1;

  TAILQ_FOREACH(ms, &md->md_streams, ms_link) {
    if(metadb_insert_stream(db, item_id, ms))
      return 1;
  }
  return 0;
}



/**
 *
 */
static int
metadb_insert_videoitem(sqlite3 *db, int64_t item_id, const metadata_t *md)
{
  int i;
  int rc = 0;
  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, 
			    i == 0 ? 
			    "INSERT OR FAIL INTO videoitem "
			    "(item_id, title, duration, format) "
			    "VALUES "
			    "(?1, ?2, ?3, ?4)"
			    :
			    "UPDATE videoitem SET "
			    "title = ?2, "
			    "duration = ?3, "
			    "format = ?4 "
			    "WHERE item_id = ?1"
			    ,
			    -1, &stmt, NULL);

    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      return -1;
    }
    sqlite3_bind_int64(stmt, 1, item_id);

    if(md->md_title != NULL)
      sqlite3_bind_text(stmt, 2, rstr_get(md->md_title), -1, SQLITE_STATIC);

    if(md->md_format != NULL)
      sqlite3_bind_text(stmt, 4, rstr_get(md->md_format), -1, SQLITE_STATIC);

    sqlite3_bind_int(stmt, 3, md->md_duration * 1000);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_CONSTRAINT && i == 0)
      continue;
    break;
  }

  if(rc != SQLITE_DONE)
    return 1;
  return metadb_set_streams(db, item_id, md);
}



/**
 *
 */
static int
metadb_insert_imageitem(sqlite3 *db, int64_t item_id, const metadata_t *md)
{
  int i;
  int rc = 0;
  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, 
			    i == 0 ? 
			    "INSERT OR FAIL INTO imageitem "
			    "(item_id, original_time) "
			    "VALUES "
			    "(?1, ?2)"
			    :
			    "UPDATE imageitem SET "
			    "original_time = ?2 "
			    "WHERE item_id = ?1"
			    ,
			    -1, &stmt, NULL);

    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      return -1;
    }
    sqlite3_bind_int64(stmt, 1, item_id);

    if(md->md_time)
      sqlite3_bind_int(stmt, 2, md->md_time);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_CONSTRAINT && i == 0)
      continue;
    break;
  }

  if(rc != SQLITE_DONE)
    return 1;
  return 0;
}






/**
 *
 */
void
metadb_metadata_write(void *db, const char *url, time_t mtime,
		      const metadata_t *md, const char *parent,
		      time_t parent_mtime)
{
  int64_t item_id;
  int64_t parent_id = 0;
  int rc;
  sqlite3_stmt *stmt;

  if(db_begin(db))
    return;

  if(parent != NULL) {
    parent_id = db_item_get(db, parent, NULL);
    if(parent_id == -1)
      parent_id = db_item_create(db, parent, CONTENT_DIR, parent_mtime, 0);
  }

  item_id = db_item_get(db, url, NULL);
  if(item_id == -1) {

    item_id = db_item_create(db, url, md->md_contenttype, mtime, parent_id);

    if(item_id == -1) {
      db_rollback(db);
      return;
    }

  } else {

    rc = sqlite3_prepare_v2(db, 
			    parent_id > 0 ? 
			    "UPDATE item "
			    "SET contenttype=?1, "
			    "mtime=?2, "
			    "metadataversion=" METADATA_VERSION_STR ", "
			    "parent=?4 "
			    "WHERE id=?3"
			    :
			    "UPDATE item "
			    "SET contenttype=?1, "
			    "mtime=?2, "
			    "metadataversion=" METADATA_VERSION_STR " "
			    "WHERE id=?3",
			    -1, &stmt, NULL);

    if(rc != SQLITE_OK) {
      db_rollback(db);
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      return;
    }

    if(md->md_contenttype)
      sqlite3_bind_int(stmt, 1, md->md_contenttype);
    if(mtime)
      sqlite3_bind_int(stmt, 2, mtime);
    sqlite3_bind_int64(stmt, 3, item_id);
    sqlite3_bind_int64(stmt, 4, parent_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  int r;

  switch(md->md_contenttype) {
  case CONTENT_AUDIO:
    r = metadb_insert_audioitem(db, item_id, md);
    break;

  case CONTENT_VIDEO:
    r = metadb_insert_videoitem(db, item_id, md);
    break;

  case CONTENT_IMAGE:
    r = metadb_insert_imageitem(db, item_id, md);
    break;

  case CONTENT_DIR:
  case CONTENT_DVD:
    r = 0;
    break;

  default:
    r = 1;
    break;
  }

  if(r)
    db_rollback(db);
  else
    db_commit(db);
}


typedef struct get_cache {
  int64_t gc_album_id;
  rstr_t *gc_album_title;

  int64_t gc_artist_id;
  rstr_t *gc_artist_title;

} get_cache_t;


/**
 *
 */
static void
get_cache_release(get_cache_t *gc)
{
  rstr_release(gc->gc_album_title);
  rstr_release(gc->gc_artist_title);
}


/**
 *
 */
static int
metadb_metadata_get_artist(sqlite3 *db, get_cache_t *gc, int64_t id)
{
  if(id < 1)
    return -1;

  if(id == gc->gc_artist_id)
    return 0;

  int rc;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db,
			  "SELECT title "
			  "FROM artist "
			  "WHERE id = ?1",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(sel, 1, id);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return -1;
  }

  gc->gc_artist_id = id;

  rstr_release(gc->gc_artist_title);
  gc->gc_artist_title = rstr_alloc((void *)sqlite3_column_text(sel, 0));
  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static int
metadb_metadata_get_album(sqlite3 *db, get_cache_t *gc, int64_t id)
{
  if(id < 1)
    return -1;

  if(id == gc->gc_album_id)
    return 0;

  int rc;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db,
			  "SELECT title "
			  "FROM album "
			  "WHERE id = ?1",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(sel, 1, id);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return -1;
  }

  gc->gc_album_id = id;
  rstr_release(gc->gc_album_title);
  gc->gc_album_title = rstr_alloc((void *)sqlite3_column_text(sel, 0));
  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static int
metadb_metadata_get_audio(sqlite3 *db, metadata_t *md, int64_t item_id,
			  get_cache_t *gc)
{
  int rc;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db,
			  "SELECT title, album_id, artist_id, duration "
			  "FROM audioitem "
			  "WHERE item_id = ?1",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(sel, 1, item_id);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return -1;
  }

  md->md_title = rstr_alloc((void *)sqlite3_column_text(sel, 0));

  if(!metadb_metadata_get_album(db, gc, sqlite3_column_int64(sel, 1)))
    md->md_album = rstr_dup(gc->gc_album_title);

  if(!metadb_metadata_get_artist(db, gc, sqlite3_column_int64(sel, 2)))
    md->md_artist = rstr_dup(gc->gc_artist_title);

  md->md_duration = sqlite3_column_int(sel, 3) / 1000.0f;

  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static int
metadb_metadata_get_video(sqlite3 *db, metadata_t *md, int64_t item_id)
{
  int rc;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db,
			  "SELECT title, duration, format "
			  "FROM videoitem "
			  "WHERE item_id = ?1",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(sel, 1, item_id);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return -1;
  }

  md->md_title = rstr_alloc((void *)sqlite3_column_text(sel, 0));
  md->md_duration = sqlite3_column_int(sel, 1) / 1000.0f;
  md->md_format = rstr_alloc((void *)sqlite3_column_text(sel, 2));

  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static int
metadb_metadata_get_streams(sqlite3 *db, metadata_t *md, int64_t item_id)
{
  int rc;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db,
			  "SELECT streamindex, info, isolang, codec, mediatype, disposition "
			  "FROM stream "
			  "WHERE item_id = ?1 ORDER BY streamindex",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(sel, 1, item_id);

  while((rc = sqlite3_step(sel)) == SQLITE_ROW) {
    enum AVMediaType type;

    const char *str = (const char *)sqlite3_column_text(sel, 4);
    if(!strcmp(str, "audio"))
      type = AVMEDIA_TYPE_AUDIO;
    else if(!strcmp(str, "video"))
      type = AVMEDIA_TYPE_VIDEO;
    else if(!strcmp(str, "subtitle"))
      type = AVMEDIA_TYPE_SUBTITLE;
    else
      continue;

    metadata_add_stream(md, 
			(const char *)sqlite3_column_text(sel, 3),
			type,
			sqlite3_column_int(sel, 0),
			(const char *)sqlite3_column_text(sel, 1),
			(const char *)sqlite3_column_text(sel, 2),
			sqlite3_column_int(sel, 5));
  }
  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static int
metadb_metadata_get_image(sqlite3 *db, metadata_t *md, int64_t item_id)
{
  int rc;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db,
			  "SELECT original_time "
			  "FROM imageitem "
			  "WHERE item_id = ?1",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }

  sqlite3_bind_int64(sel, 1, item_id);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return -1;
  }

  md->md_time = sqlite3_column_int(sel, 0);

  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static metadata_t *
metadata_get(void *db, int item_id, int contenttype, get_cache_t *gc)
{
  metadata_t *md = metadata_create();
  md->md_cached = 1;
  md->md_contenttype = contenttype; 

  int r;
  switch(md->md_contenttype) {
  case CONTENT_AUDIO:
    r = metadb_metadata_get_audio(db, md, item_id, gc);
    break;

  case CONTENT_VIDEO:
    if((r = metadb_metadata_get_video(db, md, item_id)) != 0)
      break;
    r = metadb_metadata_get_streams(db, md, item_id);
    break;

  case CONTENT_IMAGE:
    r = metadb_metadata_get_image(db, md, item_id);
    break;

  case CONTENT_DIR:
  case CONTENT_DVD:
    r = 0;
    break;

  default:
    r = 1;
    break;
  }

  if(r) {
    metadata_destroy(md);
    return NULL;
  }
  return md;
}


/**
 *
 */
metadata_t *
metadb_metadata_get(void *db, const char *url, time_t mtime)
{
  int rc;
  sqlite3_stmt *sel;

  if(db_begin(db))
    return NULL;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT id,contenttype from item "
			  "where url=?1 AND "
			  "mtime=?2 AND "
			  "metadataversion=" METADATA_VERSION_STR,
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    db_rollback(db);
    return NULL;
  }

  sqlite3_bind_text(sel, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int(sel, 2, mtime);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    db_rollback(db);
    return NULL;
  }

  get_cache_t gc = {0};

  metadata_t *md = metadata_get(db, 
				sqlite3_column_int64(sel, 0),
				sqlite3_column_int(sel, 1),
				&gc);
  get_cache_release(&gc);

  sqlite3_finalize(sel);
  db_rollback(db);
  return md;
}


/**
 *
 */
fa_dir_t *
metadb_metadata_scandir(void *db, const char *url, time_t *mtime)
{
  if(db_begin(db))
    return NULL;

  int64_t parent_id = db_item_get(db, url, mtime);

  if(parent_id == -1) {
    db_rollback(db);
    return NULL;
  }

  sqlite3_stmt *sel;
  int rc;

  rc = sqlite3_prepare_v2(db,
			  "SELECT id,url,contenttype,mtime,playcount,lastplay,metadataversion "
			  "FROM item "
			  "WHERE parent = ?1",
			  -1, &sel, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    db_rollback(db);
    return NULL;
  }

  sqlite3_bind_int64(sel, 1, parent_id);

  fa_dir_t *fd = fa_dir_alloc();

  get_cache_t gc = {0};

  while((rc = sqlite3_step(sel)) == SQLITE_ROW) {
    if(sqlite3_column_type(sel, 2) != SQLITE_INTEGER)
      continue;

    int64_t item_id = sqlite3_column_int64(sel, 0);
    const char *url = (const char *)sqlite3_column_text(sel, 1);
    int contenttype = sqlite3_column_int(sel, 2);
    char fname[256];
    fa_dir_entry_t *fde;

    fa_url_get_last_component(fname, sizeof(fname), url);

    fde = fa_dir_add(fd, url, fname, contenttype);
    if(fde != NULL) {
      if(sqlite3_column_type(sel, 3) == SQLITE_INTEGER) {
	fde->fde_statdone = 1;
	fde->fde_stat.fs_mtime = sqlite3_column_int(sel, 3);
      }

      fde->fde_md = metadata_get(db, item_id, contenttype, &gc);
    }
  }

  sqlite3_finalize(sel);

  get_cache_release(&gc);

  db_rollback(db);

  if(fd->fd_count == 0) {
    fa_dir_free(fd);
    fd = NULL;
  }

  return fd;
}





/**
 *
 */
void
metadb_unparent_item(void *db, const char *url)
{
  int rc;

  if(db_begin(db))
    return;

  sqlite3_stmt *stmt;
    
  rc = sqlite3_prepare_v2(db, "UPDATE item SET parent = NULL WHERE url=?1",
			  -1, &stmt, NULL);
  
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    db_rollback(db);
    return;
  }

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  db_commit(db);
}




/**
 *
 */
void
metadb_register_play(const char *url, int inc)
{
  int rc;
  int i;
  void *db;

  if((db = metadb_get()) == NULL)
    return;

  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, 
			    i == 0 ? 
			    "UPDATE item "
			    "SET playcount = playcount + ?3, "
			    "lastplay = ?2 "
			    "WHERE url=?1"
			    :
			    "INSERT INTO item "
			    "(url, playcount, lastplay) "
			    "VALUES "
			    "(?1, ?3, ?2)",
			    -1, &stmt, NULL);

    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      db_rollback(db);
      metadb_close(db);
      return;
    }

    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, time(NULL));
    sqlite3_bind_int(stmt, 3, inc);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if(i == 0 && rc == SQLITE_DONE && sqlite3_changes(db) > 0)
      break;
  }
  db_commit(db);
  mip_update_by_url(db, url);
  metadb_close(db);
}



/**
 *
 */
void
metadb_set_video_restartpos(const char *url, int64_t pos)
{
  int rc;
  void *db;
  sqlite3_stmt *stmt;

  if((db = metadb_get()) == NULL)
    return;

  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  rc = sqlite3_prepare_v2(db, 
			  "UPDATE videoitem "
			  "SET restartposition = ?2 "
			  "WHERE item_id = "
			  "(SELECT id FROM item where url=?1)",
			  -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    db_rollback(db);
    metadb_close(db);
    return;
  }

  
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  if(pos >= 0)
    sqlite3_bind_int64(stmt, 2, pos);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if((rc != SQLITE_DONE || sqlite3_changes(db) == 0) && pos >= 0) {

    int64_t item_id;
    item_id = db_item_get(db, url, NULL);
    if(item_id == -1)
      item_id = db_item_create(db, url, 0, 0, 0);

    if(item_id != -1) {
      rc = sqlite3_prepare_v2(db, 
			      "INSERT INTO videoitem "
			      "(item_id, restartposition) "
			      "VALUES "
			      "(?1, ?2)",
			      -1, &stmt, NULL);
      
      if(rc != SQLITE_OK) {
	TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	      __FUNCTION__, __LINE__);
	db_rollback(db);
	metadb_close(db);
	return;
      }
      sqlite3_bind_int64(stmt, 1, item_id);
      sqlite3_bind_int64(stmt, 2, pos);
      rc = sqlite3_step(stmt);
      sqlite3_finalize(stmt);
    }
  }
  db_commit(db);
  mip_update_by_url(db, url);
  metadb_close(db);
}


/**
 *
 */
int64_t
video_get_restartpos(const char *url)
{
  int rc;
  void *db;
  sqlite3_stmt *stmt;
  int64_t rval = 0;

  if(video_settings.resume_mode == VIDEO_RESUME_NO)
    return 0;


  if((db = metadb_get()) == NULL)
    return 0;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT restartposition "
			  "FROM videoitem, item "
			  "WHERE item_id = id AND url = ?1",
			  -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
  } else {
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if(rc == SQLITE_ROW)
      rval = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
  }
  metadb_close(db);
  return rval;
}

/**
 *
 */
typedef struct metadb_item_prop {
  LIST_ENTRY(metadb_item_prop) mip_link;
  prop_t *mip_playcount;
  prop_t *mip_lastplayed;
  prop_t *mip_restartpos;

  char *mip_url;
  
  prop_sub_t *mip_destroy_sub;

} metadb_item_prop_t;

#define MIP_HASHWIDTH 311

static LIST_HEAD(, metadb_item_prop) mip_hash[MIP_HASHWIDTH];



typedef struct metadb_item_info {
  int mii_playcount;
  int mii_lastplayed;
  int mii_restartpos;
} metadb_item_info_t;

/**
 *
 */
static int
mip_get(sqlite3 *db, const char *url, metadb_item_info_t *mii)
{
  int rc, rval = -1;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT "
			  "id,contenttype,playcount,lastplay "
			  "FROM item "
			  "WHERE url=?1 ",
			  -1, &stmt, NULL);
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
    return -1;
  }
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);

  if(rc == SQLITE_ROW) {
    mii->mii_playcount  = sqlite3_column_int(stmt, 2);
    mii->mii_lastplayed = sqlite3_column_int(stmt, 3);
    mii->mii_restartpos = 0;
    rval = 0;
    int64_t id = sqlite3_column_int64(stmt, 0);
    if(sqlite3_column_int(stmt, 1) == CONTENT_VIDEO) {
      sqlite3_finalize(stmt);
      
      rc = sqlite3_prepare_v2(db, 
			      "SELECT "
			      "restartposition "
			      "FROM videoitem "
			      "WHERE item_id=?1 ",
			      -1, &stmt, NULL);
      if(rc != SQLITE_OK) {
	TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	      __FUNCTION__, __LINE__);
	return -1;
      }
      sqlite3_bind_int64(stmt, 1, id);

      rc = sqlite3_step(stmt);

      if(rc == SQLITE_ROW)
	mii->mii_restartpos = sqlite3_column_int(stmt, 0);
    }
  }

  sqlite3_finalize(stmt);
  return rval;
}


/**
 *
 */
static void
mip_set(metadb_item_prop_t *mip, const metadb_item_info_t *mii)
{
  prop_set_int(mip->mip_playcount,  mii->mii_playcount);
  prop_set_int(mip->mip_lastplayed, mii->mii_lastplayed);
  prop_set_int(mip->mip_restartpos, mii->mii_restartpos);
}


/**
 *
 */
static void
mip_update_by_url(sqlite3 *db, const char *url)
{
  metadb_item_prop_t *mip;
  metadb_item_info_t mii;
  int loaded = 0;

  unsigned int hash = mystrhash(url) % MIP_HASHWIDTH;

  hts_mutex_lock(&mip_mutex);

  LIST_FOREACH(mip, &mip_hash[hash], mip_link) {
    if(strcmp(mip->mip_url, url))
      continue;

    if(loaded == 0) {
      if(mip_get(db, url, &mii))
	break;
      loaded = 1;
    }
    mip_set(mip, &mii);
  }
  hts_mutex_unlock(&mip_mutex);
}


/**
 *
 */
static void
metadb_item_prop_destroyed(void *opaque, prop_event_t event, ...)
{
  metadb_item_prop_t *mip = opaque;
  if(event != PROP_DESTROYED)
    return;
  hts_mutex_lock(&mip_mutex);
  LIST_REMOVE(mip, mip_link);
  hts_mutex_unlock(&mip_mutex);

  prop_unsubscribe(mip->mip_destroy_sub);
  prop_ref_dec(mip->mip_playcount);
  prop_ref_dec(mip->mip_lastplayed);
  prop_ref_dec(mip->mip_restartpos);
  free(mip->mip_url);
  free(mip);
}


/**
 *
 */
static void
metadb_bind_url_to_prop0(void *db, const char *url, prop_t *parent)
{
  metadb_item_prop_t *mip = malloc(sizeof(metadb_item_prop_t));

  mip->mip_destroy_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, metadb_item_prop_destroyed, mip,
		   PROP_TAG_ROOT, parent,
		   NULL);

  if(mip->mip_destroy_sub == NULL) {
    free(mip);

  } else {

    mip->mip_playcount  = prop_ref_inc(prop_create(parent, "playcount"));
    mip->mip_lastplayed = prop_ref_inc(prop_create(parent, "lastplayed"));
    mip->mip_restartpos = prop_ref_inc(prop_create(parent, "restartpos"));
  
    mip->mip_url = strdup(url);

    unsigned int hash = mystrhash(url) % MIP_HASHWIDTH;

    hts_mutex_lock(&mip_mutex);
    LIST_INSERT_HEAD(&mip_hash[hash], mip, mip_link);
    hts_mutex_unlock(&mip_mutex);

    metadb_item_info_t mii;
    if(!mip_get(db, url, &mii))
      mip_set(mip, &mii);
  }
}


void
metadb_bind_url_to_prop(void *db, const char *url, prop_t *parent)
{
  if(db != NULL)
    return metadb_bind_url_to_prop0(db, url, parent);

  if((db = metadb_get()) != NULL)
    metadb_bind_url_to_prop0(db, url, parent);
  metadb_close(db);
}
