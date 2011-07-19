#include <stdio.h>

#include "prop/prop.h"
#include "ext/sqlite/sqlite3.h"

#include "showtime.h"
#include "media.h"

#include "api/lastfm.h"

#include "metadata.h"
#include "fileaccess/fileaccess.h"

// If not set to true by metadb_init() no metadb actions will occur
static int metadb_valid;
static char *meta_db_path;

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
		    int streamindex, const char *info, const char *isolang)
{
  metadata_stream_t *ms = malloc(sizeof(metadata_stream_t));
  ms->ms_info = rstr_alloc(info);
  ms->ms_isolang = rstr_alloc(isolang);
  ms->ms_codec = rstr_alloc(codec);
  ms->ms_type = type;
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
metadata_stream_make_prop(metadata_stream_t *ms, prop_t *parent)
{
  char url[16];

  snprintf(url, sizeof(url), "libav:%d", ms->ms_streamindex);

  mp_add_track(parent,
	       NULL,
	       url,
	       rstr_get(ms->ms_codec),
	       rstr_get(ms->ms_info),
	       rstr_get(ms->ms_isolang),
	       NULL, 0);
}


/**
 *
 */
void
metadata_to_proptree(const metadata_t *md, prop_t *proproot,
		     int overwrite_title)
{
  metadata_stream_t *ms;
  prop_t *p;

  if(md->md_title && (p = prop_create_check(proproot, "title")) != NULL) {
    prop_set_rstring_ex(p, NULL, md->md_title, !overwrite_title);
    prop_ref_dec(p);
  }

  if(md->md_artist) {
    if((p = prop_create_check(proproot, "artist")) != NULL) {
      prop_set_rstring(p, md->md_artist);
      prop_ref_dec(p);
    }

    if((p = prop_create_check(proproot, "artist_images")) != NULL) {
      lastfm_artistpics_init(p, md->md_artist);
      prop_ref_dec(p);
    }
  }

  if(md->md_album) {
    if((p = prop_create_check(proproot, "album")) != NULL) {
      prop_set_rstring(p,  md->md_album);
      prop_ref_dec(p);
    }
    
    if(md->md_artist != NULL &&
       (p = prop_create_check(proproot, "album_art")) != NULL) {
      lastfm_albumart_init(p, md->md_artist, md->md_album);
      prop_ref_dec(p);
    }
  }

  TAILQ_FOREACH(ms, &md->md_streams, ms_link) {

    prop_t *p;

    switch(ms->ms_type) {
    case AVMEDIA_TYPE_AUDIO:
      p = prop_create_check(proproot, "audiostreams");
      break;
    case AVMEDIA_TYPE_VIDEO:
      p = prop_create_check(proproot, "videostreams");
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      p = prop_create_check(proproot, "subtitlestreams");
      break;
    default:
      continue;
    }
    if(p != NULL) {
      metadata_stream_make_prop(ms, p);
      prop_ref_dec(p);
    }
  }

  if(md->md_format && (p = prop_create_check(proproot, "format")) != NULL) {
    prop_set_rstring(p,  md->md_format);
    prop_ref_dec(p);
  }

  if(md->md_duration && (p = prop_create_check(proproot, "duration")) != NULL) {
    prop_set_float(p, md->md_duration);
    prop_ref_dec(p);
  }

  if(md->md_tracks && (p = prop_create_check(proproot, "tracks")) != NULL) {
    prop_set_int(p,  md->md_tracks);
    prop_ref_dec(p);
  }

  if(md->md_time && (p = prop_create_check(proproot, "timestamp")) != NULL) {
    prop_set_int(p,  md->md_time);
    prop_ref_dec(p);
  }
}





static int
one_statement(sqlite3 *db, const char *sql)
{
  int rc;
  char *errmsg;
  
  rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
  if(rc) {
    TRACE(TRACE_ERROR, "SQLITE", "%s failed -- %s", sql, errmsg);
    sqlite3_free(&errmsg);
  }
  return rc;
}


/**
 *
 */
static int
db_get_int_from_query(sqlite3 *db, const char *query, int *v)
{
  int rc;
  int64_t rval = -1;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
  if(rc)
    return -1;

  rc = sqlite3_step(stmt);
  
  if(rc == SQLITE_ROW) {
    *v = sqlite3_column_int(stmt, 0);
    rval = 0;
  } else {
    rval = -1;
  }

  sqlite3_finalize(stmt);
  return rval;
}



static int
begin(sqlite3 *db)
{
  return one_statement(db, "BEGIN;");
}


static int
commit(sqlite3 *db)
{
  return one_statement(db, "COMMIT;");
}


static int
rollback(sqlite3 *db)
{
  return one_statement(db, "ROLLBACK;");
}


#define METADB_SCHEMA_DIR "bundle://resources/metadb"

/**
 *
 */
static void
metadb_upgrade(sqlite3 *db)
{
  int ver, tgtver = 0;
  char path[256];
  char buf[256];
  if(db_get_int_from_query(db, "pragma user_version", &ver)) {
    TRACE(TRACE_ERROR, "METADB", "Unable to query db version");
    return;
  }

  fa_dir_t *fd;
  fa_dir_entry_t *fde;
  
  fd = fa_scandir(METADB_SCHEMA_DIR, buf, sizeof(buf));

  if(fd == NULL) {
    TRACE(TRACE_ERROR, "METADB",
	  "Unable to scan schema dir %s -- %s", METADB_SCHEMA_DIR , buf);
    return;
  }

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(fde->fde_type != CONTENT_FILE || strchr(fde->fde_filename, '~'))
      continue;
    tgtver = MAX(tgtver, atoi(fde->fde_filename));
  }

  fa_dir_free(fd);

  while(1) {

    if(ver == tgtver) {
      TRACE(TRACE_DEBUG, "METADB", "At current version %d", ver);
      metadb_valid = 1;
      return;
    }

    ver++;
    snprintf(path, sizeof(path), METADB_SCHEMA_DIR"/%03d.sql", ver);

    struct fa_stat fs;
    char *sql = fa_quickload(path, &fs, NULL, buf, sizeof(buf));
    if(sql == NULL) {
      TRACE(TRACE_ERROR, "METADB",
	    "Unable to upgrade db schema to version %d using %s -- %s",
	    ver, path, buf);
      return;
    }

    begin(db);
    snprintf(buf, sizeof(buf), "PRAGMA user_version=%d", ver);
    if(one_statement(db, buf)) {
      free(sql);
      break;
    }

    const char *s = sql;

    while(strchr(s, ';') != NULL) {
      sqlite3_stmt *stmt;
  
      int rc = sqlite3_prepare_v2(db, s, -1, &stmt, &s);
      if(rc != SQLITE_OK) {
	TRACE(TRACE_ERROR, "METADB",
	      "Unable to prepare statement in upgrade %d\n%s", ver, s);
	goto fail;
      }

      rc = sqlite3_step(stmt);
      if(rc != SQLITE_DONE) {
	TRACE(TRACE_ERROR, "METADB",
	      "Unable to execute statement error %d\n%s", rc, 
	      sqlite3_sql(stmt));
	goto fail;
      }
      sqlite3_finalize(stmt);
    }

    commit(db);
    TRACE(TRACE_INFO, "METADB", "Upgraded to version %d", ver);
    free(sql);
  }
 fail:
  rollback(db);
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
  meta_db_path = strdup(buf);

  db = metadb_get();

  one_statement(db, "pragma journal_mode=wal;");

  metadb_upgrade(db);
  metadb_close(db);
}



/**
 *
 */
void *
metadb_get(void)
{
  int rc;
  char *errmsg;
  sqlite3 *db;
 
  rc = sqlite3_open_v2(meta_db_path, &db,
		       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | 
		       SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_SHAREDCACHE,
		       NULL);

  if(rc) {
    TRACE(TRACE_ERROR, "metadata", "Unable to open database: %s",
	  sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  rc = sqlite3_exec(db, "PRAGMA synchronous = normal;", NULL, NULL, &errmsg);
  if(rc) {
    TRACE(TRACE_ERROR, 
	  "metadata", "Unable to set synchronous mode to NORMAL: %s",
	  errmsg);
    sqlite3_free(&errmsg);
    sqlite3_close(db);
    return NULL;
  }
  return db;
}


/**
 *
 */
void 
metadb_close(void *db)
{
  sqlite3_close(db);
}


void
metadb_playcount_incr(void *db, const char *url)
{
  int rc;
  sqlite3_stmt *stmt;

  if(!metadb_valid)
    return;

  rc = sqlite3_prepare_v2(db, 
			  "UPDATE item SET playcount=playcount+1 WHERE URL=?1;",
			  -1, &stmt, NULL);
  
  if(rc != SQLITE_OK)
    return;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}






/**
 *
 */
static int64_t
db_item_get(sqlite3 *db, const char *url)
{
  int rc;
  int64_t rval = -1;
  sqlite3_stmt *stmt;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT id from item where url=?1 ",
			  -1, &stmt, NULL);
  if(rc)
    return -1;
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

  rc = sqlite3_step(stmt);
  
  if(rc == SQLITE_ROW)
    rval = sqlite3_column_int(stmt, 0);

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
			  "(url, contenttype, mtime, parent) "
			  "VALUES "
			  "(?1, ?2, ?3, ?4)",
			  -1, &stmt, NULL);
  
  if(rc != SQLITE_OK)
    return -1;

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
  if(rc != SQLITE_OK)
    return -1;

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
  if(rc != SQLITE_OK)
    return -1;

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
    
    if(rc != SQLITE_OK)
      return -1;

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
			  "(item_id, streamindex, info, isolang, codec, mediatype) "
			  "VALUES "
			  "(?1, ?2, ?3, ?4, ?5, ?6)"
			  ,
			  -1, &stmt, NULL);
  
  if(rc != SQLITE_OK) {
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
  
  if(rc != SQLITE_OK)
    return -1;

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
			    "(item_id, title, duration) "
			    "VALUES "
			    "(?1, ?2, ?3)"
			    :
			    "UPDATE videoitem SET "
			    "title = ?2, "
			    "duration = ?3 "
			    "WHERE item_id = ?1"
			    ,
			    -1, &stmt, NULL);
    
    if(rc != SQLITE_OK)
      return -1;

    sqlite3_bind_int64(stmt, 1, item_id);

    if(md->md_title != NULL)
      sqlite3_bind_text(stmt, 2, rstr_get(md->md_title), -1, SQLITE_STATIC);
    
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
void
metadb_metadata_write(void *db, const char *url, time_t mtime,
		      const metadata_t *md, const char *parent)
{
  int64_t item_id;
  int rc;
  sqlite3_stmt *stmt;

  if(!metadb_valid)
    return;

  if(begin(db))
    return;

  item_id = db_item_get(db, url);
  if(item_id == -1) {
    item_id = db_item_create(db, url, md->md_contenttype, mtime, 0);

    if(item_id == -1) {
      rollback(db);
      return;
    }

  } else {
    
    rc = sqlite3_prepare_v2(db, 
			    "UPDATE item "
			    "SET contenttype=?1, mtime=?2 "
			    "WHERE id=?3",
			    -1, &stmt, NULL);
  
    if(rc != SQLITE_OK) {
      rollback(db);
      return;
    }

    if(md->md_contenttype)
      sqlite3_bind_int(stmt, 1, md->md_contenttype);
    if(mtime)
      sqlite3_bind_int(stmt, 2, mtime);
    sqlite3_bind_int64(stmt, 3, item_id);

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

  case CONTENT_DIR:
  case CONTENT_DVD:
    r = 0;
    break;

  default:
    r = 1;
    break;
  }
  
  if(r)
    rollback(db);
  else
    commit(db);
}


/**
 *
 */
static int
metadb_metadata_get_audio(sqlite3 *db, metadata_t *md, int64_t item_id)
{
  int rc;
  sqlite3_stmt *sel;

  rc = sqlite3_prepare_v2(db,
			  "SELECT audioitem.title, album.title, "
			  "artist.title, duration "
			  "FROM audioitem, album, artist "
			  "WHERE album.id = album_id AND artist.id = artist_id "
			  "AND item_id = ?1",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK)
    return -1;

  sqlite3_bind_int64(sel, 1, item_id);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return -1;
  }

  md->md_title = rstr_alloc((void *)sqlite3_column_text(sel, 0));
  md->md_album = rstr_alloc((void *)sqlite3_column_text(sel, 1));
  md->md_artist = rstr_alloc((void *)sqlite3_column_text(sel, 2));
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
			  "SELECT title, duration "
			  "FROM videoitem "
			  "WHERE item_id = ?1",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK)
    return -1;

  sqlite3_bind_int64(sel, 1, item_id);

  rc = sqlite3_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return -1;
  }

  md->md_title = rstr_alloc((void *)sqlite3_column_text(sel, 0));
  md->md_duration = sqlite3_column_int(sel, 1) / 1000.0f;

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
			  "SELECT streamindex, info, isolang, codec, mediatype "
			  "FROM stream "
			  "WHERE item_id = ?1 ORDER BY streamindex",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK)
    return -1;

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
			(const char *)sqlite3_column_text(sel, 2));
			
  }
  sqlite3_finalize(sel);
  return 0;
}



/**
 *
 */
metadata_t *
metadb_metadata_get(void *db, const char *url, time_t mtime)
{
  int rc;
  sqlite3_stmt *sel;

  if(!metadb_valid)
    return NULL;

  if(begin(db))
    return NULL;

  rc = sqlite3_prepare_v2(db, 
			  "SELECT id,contenttype from item "
			  "where url=?1 and mtime=?2",
			  -1, &sel, NULL);
  if(rc != SQLITE_OK) {
    rollback(db);
    return NULL;
  }

  sqlite3_bind_text(sel, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int(sel, 2, mtime);

  rc = sqlite3_step(sel);
  
  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    rollback(db);
    return NULL;
  }

  int64_t item_id = sqlite3_column_int64(sel, 0);
  metadata_t *md = metadata_create();
  md->md_cached = 1;
  md->md_contenttype = sqlite3_column_int(sel, 1);
  sqlite3_finalize(sel);

  int r;
  switch(md->md_contenttype) {
  case CONTENT_AUDIO:
    r = metadb_metadata_get_audio(db, md, item_id);
    break;

  case CONTENT_VIDEO:
    if((r = metadb_metadata_get_video(db, md, item_id)) != 0)
      break;
    r = metadb_metadata_get_streams(db, md, item_id);
    break;

  default:
    r = 0;
    break;
  }

  rollback(db);

  if(r) {
    metadata_destroy(md);
    return NULL;
  }
  return md;
}
