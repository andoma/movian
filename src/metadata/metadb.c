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
#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <unistd.h>

#include <sqlite3.h>

#include "prop/prop.h"

#include "main.h"
#include "media/media.h"
#include "metadata.h"
#include "db/db_support.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_json.h"
#include "settings.h"
#include "notifications.h"
#include "metadata_sources.h"

// If not set to true by metadb_init() no metadb actions will occur
static db_pool_t *metadb_pool;

static int
rc2metadatacode(int rc)
{
  if(rc == SQLITE_LOCKED)
    return METADATA_DEADLOCK;
  if(rc == SQLITE_DONE)
    return 0;
  return METADATA_PERMANENT_ERROR;
}



static void
items_clear(void *opaque, prop_event_t event, ...)
{
  rstr_t *r = _("Clearing all metadata means losing <b>all</b> metadata cached from external sources such as themoviedb.org, etc\n\nInformation about resume-points, playcount, etc will be saved.\nAre you sure you proceed?");

  int x = message_popup(rstr_get(r), MESSAGE_POPUP_RICH_TEXT |
			MESSAGE_POPUP_CANCEL | MESSAGE_POPUP_OK, NULL);

  rstr_release(r);

  if(x != MESSAGE_POPUP_OK)
    return;

  void *db = metadb_get();
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  int rc;
  sqlite3_stmt *stmt;
  rc = db_prepare(db, &stmt, "DELETE FROM item");

  if(rc == SQLITE_OK) {
    rc = db_step(stmt);
    sqlite3_finalize(stmt);
  }

  if(rc == SQLITE_LOCKED) {
    db_rollback_deadlock(db);
    goto again;
  }

  int deleted = sqlite3_changes(db);

  db_commit(db);
  metadb_close(db);

  notify_add(NULL, NOTIFY_INFO, NULL, 3, _("%d items deleted"), deleted);
}


/**
 *
 */
void
metadb_init(void)
{
  sqlite3 *db;
  char buf[256];
  char buf2[256];

  snprintf(buf, sizeof(buf), "%s/metadb", gconf.persistent_path);
  fa_makedir(buf);
  snprintf(buf, sizeof(buf), "%s/metadb/meta.db", gconf.persistent_path);

  //  unlink(buf);

  metadb_pool = db_pool_create(buf, 2);
  db = metadb_get();
  if(db == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/res/metadb", app_dataroot());
  snprintf(buf2, sizeof(buf2), "%s/kvstore/kvstore.db", gconf.persistent_path);

  int r = db_upgrade_schema(db, buf, "metadb", "kvstore", buf2);

  metadb_close(db);

  if(r) {
    metadb_pool = NULL; // Disable
  } else {
    prop_t *dir = setting_get_dir("general:resets");
    settings_create_action(dir, _p("Clear all metadata"), NULL,
			   items_clear, NULL, 0, NULL);
  }
}


/**
 *
 */
void
metadb_fini(void)
{
  db_pool_close(metadb_pool);
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
  int64_t rval = METADATA_PERMANENT_ERROR;
  sqlite3_stmt *stmt;

  rc = db_prepare(db, &stmt,
		  "SELECT id,mtime from item where url=?1 ");
  if(rc)
    return METADATA_PERMANENT_ERROR;
  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

  rc = db_step(stmt);

  if(rc == SQLITE_ROW) {
    rval = sqlite3_column_int64(stmt, 0);
    if(mtimep != NULL)
      *mtimep = sqlite3_column_int(stmt, 1);
  } else if(rc == SQLITE_LOCKED)
    rval = METADATA_DEADLOCK;

  sqlite3_finalize(stmt);
  return rval;
}


/**
 *
 */
static int64_t
db_item_create(sqlite3 *db, const char *url, int contenttype, time_t mtime,
	       int64_t parentid, metadata_index_status_t indexstatus)
{
  int rc;
  sqlite3_stmt *stmt;

  rc = db_prepare(db, &stmt,
		  "INSERT INTO item "
		  "(url, contenttype, mtime, parent, indexstatus) "
		  "VALUES "
		  "(?1, ?2, ?3, ?4, ?5)");

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  if(contenttype)
    sqlite3_bind_int(stmt, 2, contenttype);
  if(mtime)
    sqlite3_bind_int(stmt, 3, mtime);
  if(parentid > 0)
    sqlite3_bind_int64(stmt, 4, parentid);
  sqlite3_bind_int(stmt, 5, indexstatus);

  rc = db_step(stmt);
  sqlite3_finalize(stmt);

  if(rc == SQLITE_LOCKED)
    return METADATA_DEADLOCK;

  if(rc == SQLITE_DONE)
    return sqlite3_last_insert_rowid(db);
  else
    return METADATA_PERMANENT_ERROR;
}


/**
 *
 */
int64_t
metadb_artist_get_by_title(void *db, const char *title, int ds_id,
			   const char *ext_id)
{
  int rc;
  int64_t rval = METADATA_PERMANENT_ERROR;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT id "
		  "FROM artist "
		  "WHERE title=?1 "
		  "AND ds_id=?2"
		  "AND (?3 OR ext_id = ?4)");

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(sel, 1, title, -1, SQLITE_STATIC);
  sqlite3_bind_int(sel, 2, ds_id);
  sqlite3_bind_int(sel, 3, !ext_id);
  if(ext_id != NULL)
    sqlite3_bind_text(sel, 4, ext_id, -1, SQLITE_STATIC);
  rc = db_step(sel);

  if(rc == SQLITE_ROW) {
    rval = sqlite3_column_int64(sel, 0);
  } else if(rc == SQLITE_DONE) {
    // No entry found, INSERT it

    sqlite3_stmt *ins;

    rc = db_prepare(db, &ins, 
		    "INSERT INTO artist "
		    "(title, ds_id, ext_id) "
		    "VALUES "
		    "(?1, ?2, ?3)");

    if(rc == SQLITE_OK) {
      sqlite3_bind_text(ins, 1, title, -1, SQLITE_STATIC);
      sqlite3_bind_int(ins, 2, ds_id);
      if(ext_id)
	sqlite3_bind_text(ins, 3, ext_id, -1, SQLITE_STATIC);
      rc = db_step(ins);
      sqlite3_finalize(ins);
      if(rc == SQLITE_LOCKED)
	rval = METADATA_DEADLOCK;
      if(rc == SQLITE_DONE)
	rval = sqlite3_last_insert_rowid(db);
    }
  } else if(rc == SQLITE_LOCKED) {
    rval = METADATA_DEADLOCK;
  }

  sqlite3_finalize(sel);
  return rval;
}


/**
 *
 */
int64_t
metadb_album_get_by_title(void *db, const char *album, int64_t artist_id,
			  int ds_id, const char *ext_id)
{
  int rc;
  int64_t rval =  METADATA_PERMANENT_ERROR;
  sqlite3_stmt *sel;


  rc = db_prepare(db, &sel,
		  "SELECT id "
		  "FROM album "
		  "WHERE title=?1 "
		  "AND artist_id IS ?2 "
		  "AND ds_id = ?3 "
		  "AND (?4 OR ext_id = ?5)");

  if(rc != SQLITE_OK)
    return  METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(sel, 1, album, -1, SQLITE_STATIC);
  if(artist_id != -1)
    sqlite3_bind_int64(sel, 2, artist_id);
  sqlite3_bind_int(sel, 3, ds_id);
  sqlite3_bind_int(sel, 4, !ext_id);
  if(ext_id != NULL)
    sqlite3_bind_text(sel, 5, ext_id, -1, SQLITE_STATIC);
  rc = db_step(sel);

  if(rc == SQLITE_ROW) {
    rval = sqlite3_column_int64(sel, 0);
  } else if(rc == SQLITE_DONE) {
    // No entry found, INSERT it
    sqlite3_stmt *ins;

    rc = db_prepare(db, &ins,
		    "INSERT INTO album "
		    "(title, ds_id, artist_id, ext_id) "
		    "VALUES "
		    "(?1, ?3, ?2, ?4)");

    if(rc == SQLITE_OK) {
      sqlite3_bind_text(ins, 1, album, -1, SQLITE_STATIC);
      if(artist_id != -1)
	sqlite3_bind_int64(ins, 2, artist_id);
      sqlite3_bind_int(ins, 3, ds_id);
      if(ext_id != NULL)
	sqlite3_bind_text(ins, 4, ext_id, -1, SQLITE_STATIC);

      rc = db_step(ins);
      sqlite3_finalize(ins);
      if(rc == SQLITE_DONE)
	rval = sqlite3_last_insert_rowid(db);
      if(rc == SQLITE_LOCKED)
	rval = METADATA_DEADLOCK;
    }
  } else if(rc == SQLITE_LOCKED)
    rval = METADATA_DEADLOCK;

  sqlite3_finalize(sel);
  return rval;
}



/**
 *
 */
void
metadb_insert_albumart(void *db, int64_t album_id, const char *url,
		       int width, int height)
{
  sqlite3_stmt *ins;
  int rc;

  rc = db_prepare(db, &ins,
		  "INSERT INTO albumart "
		  "(album_id, url, width, height) "
		  "VALUES "
		  "(?1, ?2, ?3, ?4)");

  if(rc != SQLITE_OK)
    return;
  
  sqlite3_bind_int64(ins, 1, album_id);
  sqlite3_bind_text(ins, 2, url, -1, SQLITE_STATIC);
  if(width) sqlite3_bind_int64(ins, 3, width);
  if(height) sqlite3_bind_int64(ins, 4, height);
  db_step(ins);
  sqlite3_finalize(ins);
}


/**
 *
 */
void
metadb_insert_artistpic(void *db, int64_t artist_id, const char *url,
			int width, int height)
{
  sqlite3_stmt *ins;
  int rc;

  rc = db_prepare(db, &ins,
		  "INSERT INTO artistpic "
		  "(artist_id, url, width, height) "
		  "VALUES "
		  "(?1, ?2, ?3, ?4)");

  if(rc != SQLITE_OK)
    return;
  
  sqlite3_bind_int64(ins, 1, artist_id);
  sqlite3_bind_text(ins, 2, url, -1, SQLITE_STATIC);
  if(width) sqlite3_bind_int64(ins, 3, width);
  if(height) sqlite3_bind_int64(ins, 4, height);
  db_step(ins);
  sqlite3_finalize(ins);
}

/**
 *
 */
void
metadb_insert_videoart(void *db, int64_t videoitem_id, const char *url,
		       metadata_image_type_t type, int width, int height,
		       int weight, const char *group, int titled)
{
  sqlite3_stmt *ins;
  int rc;

  rc = db_prepare(db, &ins,
		  "INSERT OR REPLACE INTO videoart "
		  "(videoitem_id, url, width, height, "
		  "type, weight, grp, titled) "
		  "VALUES "
		  "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");

  if(rc != SQLITE_OK)
    return;
  
  sqlite3_bind_int64(ins, 1, videoitem_id);
  sqlite3_bind_text(ins, 2, url, -1, SQLITE_STATIC);
  if(width) sqlite3_bind_int(ins, 3, width);
  if(height) sqlite3_bind_int(ins, 4, height);
  sqlite3_bind_int(ins, 5, type);
  if(weight) sqlite3_bind_int(ins, 6, weight);
  sqlite3_bind_text(ins, 7, group, -1, SQLITE_STATIC);
  sqlite3_bind_int(ins, 8, titled);

  db_step(ins);
  sqlite3_finalize(ins);
}


/**
 *
 */
void
metadb_delete_videoart(void *db, int64_t videoitem_id)
{
  sqlite3_stmt *ins;
  int rc;

  rc = db_prepare(db, &ins,
		  "DELETE FROM videoart WHERE videoitem_id = ?1");

  if(rc != SQLITE_OK)
    return;
  
  sqlite3_bind_int64(ins, 1, videoitem_id);
  db_step(ins);
  sqlite3_finalize(ins);
}



/**
 *
 */
void
metadb_insert_videocast(void *db, int64_t videoitem_id,
			const char *name,
			const char *character,
			const char *department,
			const char *job,
			int order,
			const char *image,
			int width,
			int height,
			const char *ext_id)
{
  sqlite3_stmt *ins;
  int rc;

  rc = db_prepare(db, &ins,
		  "INSERT OR REPLACE INTO videocast "
		  "(videoitem_id, name, character, department, job, "
		  "\"order\", image, width, height, ext_id) "
		  "VALUES "
		  "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)");

  if(rc != SQLITE_OK)
    return;
  
  sqlite3_bind_int64(ins, 1, videoitem_id);
  sqlite3_bind_text(ins, 2, name, -1, SQLITE_STATIC);
  sqlite3_bind_text(ins, 3, character, -1, SQLITE_STATIC);
  sqlite3_bind_text(ins, 4, department, -1, SQLITE_STATIC);
  sqlite3_bind_text(ins, 5, job, -1, SQLITE_STATIC);
  sqlite3_bind_int(ins, 6, order);
  sqlite3_bind_text(ins, 7, image, -1, SQLITE_STATIC);
  if(width) sqlite3_bind_int(ins, 8, width);
  if(height) sqlite3_bind_int(ins, 9, height);
  sqlite3_bind_text(ins, 10, ext_id, -1, SQLITE_STATIC);
  db_step(ins);
  sqlite3_finalize(ins);
}


/**
 *
 */
void
metadb_delete_videocast(void *db, int64_t videoitem_id)
{
  sqlite3_stmt *ins;
  int rc;

  rc = db_prepare(db, &ins,
		  "DELETE FROM videocast WHERE videoitem_id = ?1");

  if(rc != SQLITE_OK)
    return;
  
  sqlite3_bind_int64(ins, 1, videoitem_id);
  db_step(ins);
  sqlite3_finalize(ins);
}


/**
 *
 */
void
metadb_insert_videogenre(void *db, int64_t videoitem_id, const char *title)
{
  sqlite3_stmt *ins;
  int rc;

  rc = db_prepare(db, &ins,
		  "INSERT OR REPLACE INTO videogenre "
		  "(videoitem_id, title) "
		  "VALUES "
		  "(?1, ?2)");

  if(rc != SQLITE_OK)
    return;
  
  sqlite3_bind_int64(ins, 1, videoitem_id);
  sqlite3_bind_text(ins, 2, title, -1, SQLITE_STATIC);
  db_step(ins);
  sqlite3_finalize(ins);
}



/**
 *
 */
static int
metadb_insert_audioitem(sqlite3 *db, int64_t item_id, const metadata_t *md,
			int ds_id)
{
  int64_t artist_id = -1;
  int64_t album_id = -1;

  if(md->md_artist != NULL) {
    artist_id = metadb_artist_get_by_title(db, rstr_get(md->md_artist),
					   ds_id, NULL);
    if(artist_id < 0)
      return artist_id;
  }

  if(md->md_album != NULL) {
    album_id = metadb_album_get_by_title(db, rstr_get(md->md_album),
					 artist_id, ds_id, NULL);
    if(album_id < 0)
      return album_id;
  }


  int i;
  int rc = 0;
  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    rc = db_prepare(db, &stmt,
		    i == 0 ? 
		    "INSERT OR FAIL INTO audioitem "
		    "(item_id, title, album_id, artist_id, duration, ds_id, track) "
		    "VALUES "
		    "(?1, ?2, ?3, ?4, ?5, 1, ?6)"
		    :
		    "UPDATE audioitem SET "
		    "title = ?2, "
		    "album_id = ?3, "
		    "artist_id = ?4, "
		    "duration = ?5 "
		    "WHERE item_id = ?1 AND ds_id = 1"
		    );

    if(rc != SQLITE_OK)
      return METADATA_PERMANENT_ERROR;

    sqlite3_bind_int64(stmt, 1, item_id);

    if(md->md_title != NULL)
      sqlite3_bind_text(stmt, 2, rstr_get(md->md_title), -1, SQLITE_STATIC);

    if(album_id != -1)
      sqlite3_bind_int64(stmt, 3, album_id);

    if(artist_id != -1)
      sqlite3_bind_int64(stmt, 4, artist_id);

    sqlite3_bind_int(stmt, 5, md->md_duration * 1000);

    sqlite3_bind_int(stmt, 6, md->md_track);

    rc = db_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_CONSTRAINT && i == 0)
      continue;
    break;
  }

  return rc2metadatacode(rc);
}


/**
 *
 */
static rstr_t *
metadb_construct_imageset(sqlite3_stmt *sel, int urlcol, int wcol, int hcol)
{
  int rc, n;

  htsmsg_t *m = htsmsg_create_list();

  while((rc = db_step(sel)) == SQLITE_ROW) {
    htsmsg_t *img = htsmsg_create_map();
    htsmsg_add_str(img, "url", (const char *)sqlite3_column_text(sel, urlcol));
    n = sqlite3_column_int(sel, wcol);
    if(n > 0)
      htsmsg_add_u32(img, "width", n);

    n = sqlite3_column_int(sel, hcol);
    if(n > 0)
      htsmsg_add_u32(img, "height", n);
    htsmsg_add_msg(m, NULL, img);
  }

  rstr_t *rstr = htsmsg_json_serialize_to_rstr(m, "imageset:");
  htsmsg_release(m);
  return rstr;
}



/**
 *
 */
rstr_t *
metadb_get_album_art(void *db, const char *album, const char *artist)
{
  int rc;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT aa.url, aa.width, aa.height "
		  "FROM artist,album,albumart AS aa "
		  "WHERE artist.title=?1 "
		  "AND album.title=?2 "
		  "AND album.artist_id = artist.id "
		  "AND aa.album_id = album.id"
		  );
  if(rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_text(sel, 1, artist, -1, SQLITE_STATIC);
  sqlite3_bind_text(sel, 2, album, -1, SQLITE_STATIC);
  rstr_t *r = metadb_construct_imageset(sel, 0, 1, 2);
  sqlite3_finalize(sel);
  return r;
}


/**
 *
 */
static rstr_vec_t *
metadb_get_video_art(void *db, int64_t videoitem_id, int type)
{
  int rc;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT url "
		  "FROM videoart "
		  "WHERE videoitem_id=?1 "
		  "AND type=?2 "
		  "ORDER BY weight DESC"
		  );

  if(rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_int64(sel, 1, videoitem_id);
  sqlite3_bind_int(sel, 2, type);
  rstr_vec_t *rv = NULL;
  while((rc = db_step(sel)) == SQLITE_ROW) {
    rstr_t *r = db_rstr(sel, 0);
    rstr_vec_append(&rv, r);
    rstr_release(r);
  }

  sqlite3_finalize(sel);
  return rv;
}


/**
 *
 */
static rstr_t *
metadb_construct_list(sqlite3_stmt *sel, int col)
{
  char buf[512];
  int rc;
  int cnt = 0;

  buf[0] = 0;
  while((rc = db_step(sel)) == SQLITE_ROW) {
    const char *str = (const char *)sqlite3_column_text(sel, col);
    if(str == NULL)
      continue;
    cnt += snprintf(buf + cnt, sizeof(buf) - cnt, "%s%s", cnt ? ", ": "", str);
  }
  return rstr_alloc(buf);
}


/**
 *
 */
static rstr_t *
metadb_get_video_genre(sqlite3 *db, int64_t videoitem_id)
{
  int rc;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT title "
		  "FROM videogenre "
		  "WHERE videoitem_id = ?1");

  if(rc != SQLITE_OK)
    return NULL;

  sqlite3_bind_int64(sel, 1, videoitem_id);
  rstr_t *r = metadb_construct_list(sel, 0);
  sqlite3_finalize(sel);
  return r;
}


/**
 *
 */
static int
metadb_get_video_cast(sqlite3 *db, int64_t videoitem_id, metadata_t *md)
{
  int rc;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT name,character,department,job,image "
		  "FROM videocast "
		  "WHERE videoitem_id = ?1 "
		  "ORDER BY \"order\"");

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, videoitem_id);

  while((rc = db_step(sel)) == SQLITE_ROW) {
    metadata_person_t *mp = calloc(1, sizeof(metadata_person_t));
    mp->mp_name       = db_rstr(sel, 0);
    mp->mp_character  = db_rstr(sel, 1);
    mp->mp_department = db_rstr(sel, 2);
    mp->mp_job        = db_rstr(sel, 3);
    mp->mp_portrait   = db_rstr(sel, 4);
    
    if(mp->mp_department && !strcmp(rstr_get(mp->mp_department), "Cast"))
      TAILQ_INSERT_TAIL(&md->md_cast, mp, mp_link);
    else
      TAILQ_INSERT_TAIL(&md->md_crew, mp, mp_link);
  }
  sqlite3_finalize(sel);
  return 0;
}


		     

/**
 *
 */
int
metadb_get_artist_pics(void *db, const char *artist, 
		       void (*cb)(void *opaque, const char *url,
				  int width, int height),
		       void *opaque)
{
  int rc;
  sqlite3_stmt *sel;
  int rval = METADATA_PERMANENT_ERROR;
  rc = db_prepare(db, &sel,
		  "SELECT ap.url, ap.width, ap.height "
		  "FROM artist,artistpic AS ap "
		  "WHERE artist.title=?1 "
		  "AND ap.artist_id = artist.id");

  if(rc != SQLITE_OK)
    return  METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(sel, 1, artist, -1, SQLITE_STATIC);

  while((rc = db_step(sel)) == SQLITE_ROW) {
    cb(opaque, (const char *)sqlite3_column_text(sel, 0),
       sqlite3_column_int(sel, 1),
       sqlite3_column_int(sel, 2));
    rval = 0;
  }
  sqlite3_finalize(sel);
  return rval;
}





/**
 *
 */
static int
metadb_insert_stream(sqlite3 *db, int64_t videoitem_id,
		     const metadata_stream_t *ms)
{
  int rc;
  sqlite3_stmt *stmt;
  const char *media;

  switch(ms->ms_type) {
  case MEDIA_TYPE_VIDEO:    media = "video"; break;
  case MEDIA_TYPE_AUDIO:    media = "audio"; break;
  case MEDIA_TYPE_SUBTITLE: media = "subtitle"; break;
  default:
    return 0;
  }

  rc = db_prepare(db, &stmt,
		  "INSERT INTO videostream "
		  "(videoitem_id, streamindex, info, isolang, "
		  "codec, mediatype, disposition, title) "
		  "VALUES "
		  "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(stmt, 1, videoitem_id);
  sqlite3_bind_int(stmt, 2, ms->ms_streamindex);
  if(ms->ms_info != NULL)
    sqlite3_bind_text(stmt, 3, rstr_get(ms->ms_info), -1, SQLITE_STATIC);
  if(ms->ms_isolang != NULL)
    sqlite3_bind_text(stmt, 4, rstr_get(ms->ms_isolang), -1, SQLITE_STATIC);
  if(ms->ms_codec != NULL)
    sqlite3_bind_text(stmt, 5, rstr_get(ms->ms_codec), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, media, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 7, ms->ms_disposition);
  if(ms->ms_title != NULL)
    sqlite3_bind_text(stmt, 8, rstr_get(ms->ms_title), -1, SQLITE_STATIC);

  rc = db_step(stmt);
  sqlite3_finalize(stmt);
  return rc2metadatacode(rc);
}


/**
 *
 */
static int
metadb_set_streams(sqlite3 *db, int64_t videoitem_id, const metadata_t *md)
{
  metadata_stream_t *ms;
  sqlite3_stmt *stmt;
  int rc, r;

  rc = db_prepare(db, &stmt,
		  "DELETE FROM videostream WHERE videoitem_id = ?1");

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(stmt, 1, videoitem_id);

  rc = db_step(stmt);
  sqlite3_finalize(stmt);
  if(rc == SQLITE_LOCKED)
    return METADATA_DEADLOCK;
  if(rc != SQLITE_DONE)
    return METADATA_PERMANENT_ERROR;

  TAILQ_FOREACH(ms, &md->md_streams, ms_link) {
    if((r = metadb_insert_stream(db, videoitem_id, ms)) < 0)
      return r;
  }
  return 0;
}



/**
 *
 */
static int64_t
metadb_insert_videoitem0(sqlite3 *db, int64_t item_id, int ds_id,
			 const char *ext_id, const metadata_t *md,
			 int status, int64_t weight, int qtype,
			 int64_t cfgid)
{
  int i;
  int rc = 0;
  int64_t id = METADATA_PERMANENT_ERROR;

  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    if(i == 1) {
      rc = db_prepare(db, &stmt,
		      "SELECT id "
		      "FROM videoitem "
		      "WHERE (?5 OR item_id = ?1) "
		      "AND ds_id = ?2 "
		      "AND (?3 OR ext_id = ?4)");

      if(rc != SQLITE_OK)
	return METADATA_PERMANENT_ERROR;

      sqlite3_bind_int(stmt, 5, !item_id);
      sqlite3_bind_int64(stmt, 1, item_id);
      sqlite3_bind_int64(stmt, 2, ds_id);

      sqlite3_bind_int(stmt, 3, !ext_id);
      sqlite3_bind_text(stmt, 4, ext_id, -1, SQLITE_STATIC);

      rc = db_step(stmt);
      if(rc != SQLITE_ROW) {
	sqlite3_finalize(stmt);
	if(rc == SQLITE_LOCKED)
	  return METADATA_DEADLOCK;
	TRACE(TRACE_ERROR, "SQLITE", "SQL Error 0x%x at %s:%d",
	      rc, __FUNCTION__, __LINE__);
	return METADATA_PERMANENT_ERROR;
      }
      id = sqlite3_column_int64(stmt, 0);
      sqlite3_finalize(stmt);
    }


    rc = db_prepare(db, &stmt,
		    i == 0 ? 
		    "INSERT OR FAIL INTO videoitem "
		    "(item_id, ds_id, ext_id, "
		    "title, duration, format, type, tagline, description, "
		    "year, rating, rate_count, imdb_id, status, weight, "
		    "querytype, cfgid, parent_id, idx) "
		    "VALUES "
		    "(?1, ?2, ?4, "
		    "?5, ?6, ?7, ?8, ?9, ?10, "
		    "?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20)"
		    :
		    "UPDATE videoitem SET "
		    "title = ?5, "
		    "duration = ?6, "
		    "format = ?7, "
		    "type = ?8, "
		    "tagline = ?9, "
		    "description = ?10, "
		    "year = ?11, "
		    "rating = ?12, "
		    "rate_count = ?13, "
		    "imdb_id = ?14, "
		    "status = ?15, "
		    "cfgid = ?18, "
		    "parent_id = ?19, "
		    "idx = ?20 "
		    "WHERE id = ?3 "
		    );

    if(rc != SQLITE_OK)
      return METADATA_PERMANENT_ERROR;

    // Keys
    if(item_id)
      sqlite3_bind_int64(stmt, 1, item_id);
    sqlite3_bind_int(stmt, 2, ds_id);
    sqlite3_bind_int64(stmt, 3, id);
    sqlite3_bind_text(stmt, 4, ext_id, -1, SQLITE_STATIC);

    // Data

    if(md != NULL) {
      sqlite3_bind_text(stmt, 5, rstr_get(md->md_title), -1, SQLITE_STATIC);

      if(md->md_duration)
	sqlite3_bind_int(stmt, 6, md->md_duration * 1000);

      sqlite3_bind_text(stmt, 7, rstr_get(md->md_format), -1, SQLITE_STATIC);

      sqlite3_bind_int(stmt, 8, md->md_type);

      sqlite3_bind_text(stmt, 9, rstr_get(md->md_tagline), -1, SQLITE_STATIC);
      sqlite3_bind_text(stmt, 10, rstr_get(md->md_description),-1, SQLITE_STATIC);
      if(md->md_year > 1900)
	sqlite3_bind_int(stmt, 11, md->md_year);
      if(md->md_rating >= 0)
	sqlite3_bind_int(stmt, 12, md->md_rating);
      if(md->md_rating_count >= 0)
	sqlite3_bind_int(stmt, 13, md->md_rating_count);

      if(md->md_idx >= 0)
	sqlite3_bind_int(stmt, 20, md->md_idx);

      sqlite3_bind_text(stmt, 14, rstr_get(md->md_imdb_id), -1, SQLITE_STATIC);

      if(md->md_parent_id)
	sqlite3_bind_int64(stmt, 19, md->md_parent_id);
    } else {
      sqlite3_bind_int(stmt, 8, 0);
    }
    
    sqlite3_bind_int(stmt, 15, status);
    sqlite3_bind_int64(stmt, 16, weight);
    sqlite3_bind_int(stmt, 17, qtype);
    sqlite3_bind_int64(stmt, 18, cfgid);

    rc = db_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_CONSTRAINT && i == 0)
      continue;
    if(i == 0)
      id = sqlite3_last_insert_rowid(db);
    break;
  }

  if(rc == SQLITE_LOCKED)
    return METADATA_DEADLOCK;

  if(rc != SQLITE_DONE) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d error:%d",
	  __FUNCTION__, __LINE__, rc);
    return METADATA_PERMANENT_ERROR;
  }

  if(md != NULL) {
    if(metadb_set_streams(db, id, md)) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
      return METADATA_PERMANENT_ERROR;
    }
  }

  return id;
}


int64_t
metadb_insert_videoitem(void *db, const char *url, int ds_id,
			const char *ext_id, const metadata_t *md,
			int status, int64_t weight, int qtype,
			int64_t cfgid)
{
  int64_t item_id = db_item_get(db, url, NULL);

  if(item_id == METADATA_DEADLOCK)
    return item_id;

  if(item_id == METADATA_PERMANENT_ERROR) {
    item_id = db_item_create(db, url, CONTENT_VIDEO, 0, 0, 0);
    if(item_id < 0)
      return item_id;
  }
  
  return metadb_insert_videoitem0(db, item_id, ds_id, ext_id, md, status,
				  weight, qtype, cfgid);
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

    rc = db_prepare(db, &stmt,
		    i == 0 ? 
		    "INSERT OR FAIL INTO imageitem "
		    "(item_id, original_time, manufacturer, equipment) "
		    "VALUES "
		    "(?1, ?2, ?3, ?4)"
		    :
		    "UPDATE imageitem SET "
		    "original_time = ?2, "
		    "manufacturer = ?3, "
		    "equipment = ?4 "
		    "WHERE item_id = ?1"
		    );

    if(rc != SQLITE_OK)
      return METADATA_PERMANENT_ERROR;

    sqlite3_bind_int64(stmt, 1, item_id);

    if(md->md_time)
      sqlite3_bind_int(stmt, 2, md->md_time);

    sqlite3_bind_text(stmt, 3, rstr_get(md->md_manufacturer),
		      -1, SQLITE_STATIC);

    sqlite3_bind_text(stmt, 4, rstr_get(md->md_equipment),
		      -1, SQLITE_STATIC);
    
    rc = db_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_CONSTRAINT && i == 0)
      continue;
    break;
  }
  return rc2metadatacode(rc);
}


/**
 *
 */
static int
metadb_metadata_writex(void *db, const char *url, time_t mtime,
                       const metadata_t *md, const char *parent,
                       time_t parent_mtime,
                       metadata_index_status_t indexstatus)
{
  int64_t item_id;
  int64_t parent_id = 0;
  int rc;
  sqlite3_stmt *stmt;

  if(parent != NULL) {
    parent_id = db_item_get(db, parent, NULL);
    if(parent_id == METADATA_DEADLOCK)
      return METADATA_DEADLOCK;

    if(parent_id == METADATA_PERMANENT_ERROR)
      parent_id = db_item_create(db, parent, CONTENT_DIR, parent_mtime, 0, 0);

    if(parent_id == METADATA_DEADLOCK)
      return METADATA_DEADLOCK;
  }

  item_id = db_item_get(db, url, NULL);
  if(item_id == METADATA_DEADLOCK)
    return METADATA_DEADLOCK;

  if(item_id < 0) {

    item_id = db_item_create(db, url, md->md_contenttype, mtime, parent_id,
                             indexstatus);

    if(item_id == METADATA_DEADLOCK)
      return METADATA_DEADLOCK;

    if(item_id == -1)
      return METADATA_TEMPORARY_ERROR;

  } else {

    char sql[512];

    snprintf(sql, sizeof(sql),
             "UPDATE item SET "
             "%s"
             "%s"
             "%s"
             "%s"
             " WHERE id=?1",
             md->md_contenttype ? "contenttype=?2," : "",
             mtime              ? "mtime=?3," : "",
             parent_id          ? "parent=?4," : "",
             indexstatus != INDEX_STATUS_NOCHANGE ? "indexstatus=?5," : "");

    char *x = strrchr(sql, ',');
    if(x != NULL) {
      *x = ' ';
      rc = db_prepare(db, &stmt, sql);

      if(rc != SQLITE_OK)
        return METADATA_PERMANENT_ERROR;

      sqlite3_bind_int64(stmt, 1, item_id);

      sqlite3_bind_int(stmt,   2, md->md_contenttype);
      sqlite3_bind_int(stmt,   3, mtime);
      sqlite3_bind_int64(stmt, 4, parent_id);
      sqlite3_bind_int(stmt,   5, indexstatus);

      rc = db_step(stmt);
      sqlite3_finalize(stmt);
      if(rc == METADATA_DEADLOCK)
        return METADATA_DEADLOCK;
    }
  }

  int r;

  switch(md->md_contenttype) {
  case CONTENT_AUDIO:
    r = metadb_insert_audioitem(db, item_id, md, 1);
    break;

  case CONTENT_VIDEO:
    r = metadb_insert_videoitem0(db, item_id, 1, NULL, md, 3, 0, 0, 0) < 0;
    break;

  case CONTENT_IMAGE:
    r = metadb_insert_imageitem(db, item_id, md);
    break;


  default:
    return 0;
  }
  return r;
}



/**
 *
 */
void
metadb_metadata_write(void *db, const char *url, time_t mtime,
		      const metadata_t *md, const char *parent,
		      time_t parent_mtime,
                      metadata_index_status_t indexstatus)
{
  switch(md->md_contenttype) {
  case CONTENT_AUDIO:
  case CONTENT_VIDEO:
  case CONTENT_IMAGE:
  case CONTENT_DIR:
  case CONTENT_DVD:
  case CONTENT_SHARE:
    break;
  default:
    return;
  }

  while(1) {
    if(db_begin(db))
      return;
    
    int r = metadb_metadata_writex(db, url, mtime, md, parent, parent_mtime,
                                   indexstatus);
    
    if(r == METADATA_DEADLOCK) {
      db_rollback_deadlock(db);
      continue;
    }
    if(r)
      db_rollback(db);
    else
      db_commit(db);
    return;
  }
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
    return METADATA_PERMANENT_ERROR;

  if(id == gc->gc_artist_id)
    return 0;

  int rc;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT title "
		  "FROM artist "
		  "WHERE id = ?1 AND ds_id=1"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, id);

  rc = db_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return METADATA_PERMANENT_ERROR;
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
    return METADATA_PERMANENT_ERROR;

  if(id == gc->gc_album_id)
    return 0;

  int rc;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT title "
		  "FROM album "
		  "WHERE id = ?1 AND ds_id=1");

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, id);

  rc = db_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return METADATA_PERMANENT_ERROR;
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

  rc = db_prepare(db, &sel,
		  "SELECT title, album_id, artist_id, duration, track "
		  "FROM audioitem "
		  "WHERE item_id = ?1 AND ds_id = 1"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, item_id);

  rc = db_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return METADATA_PERMANENT_ERROR;
  }

  md->md_title = rstr_alloc((void *)sqlite3_column_text(sel, 0));

  if(!metadb_metadata_get_album(db, gc, sqlite3_column_int64(sel, 1)))
    md->md_album = rstr_dup(gc->gc_album_title);

  if(!metadb_metadata_get_artist(db, gc, sqlite3_column_int64(sel, 2)))
    md->md_artist = rstr_dup(gc->gc_artist_title);

  md->md_duration = sqlite3_column_int(sel, 3) / 1000.0f;
  md->md_track = sqlite3_column_int(sel, 4);

  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static int64_t
metadb_metadata_get_video(sqlite3 *db, metadata_t *md, int64_t item_id,
			  int ds_id)
{
  int rc;
  sqlite3_stmt *sel;

  rc = db_prepare(db, &sel,
		  "SELECT id, title, duration, format, year "
		  "FROM videoitem "
		  "WHERE item_id = ?1 "
		  "AND ds_id = ?2"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, item_id);
  sqlite3_bind_int(sel, 2, ds_id);

  rc = db_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return METADATA_PERMANENT_ERROR;
  }

  int64_t id = sqlite3_column_int64(sel, 0);

  md->md_title = rstr_alloc((void *)sqlite3_column_text(sel, 1));
  md->md_duration = sqlite3_column_int(sel, 2) / 1000.0f;
  md->md_format = rstr_alloc((void *)sqlite3_column_text(sel, 3));
  md->md_year = sqlite3_column_int(sel, 4);

  sqlite3_finalize(sel);
  return id;
}



/**
 *
 */
int
metadb_videoitem_set_preferred(void *db, const char *url, int64_t vid)
{
  sqlite3_stmt *stmt;
  int rc;

  rc = db_prepare(db, &stmt,
		  "UPDATE videoitem "
		  "SET preferred = (CASE WHEN id=?2 THEN 1 ELSE 0 END) "
		  "WHERE item_id = (SELECT id FROM item WHERE url = ?1)"
		  );
  
  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, vid);

  rc = db_step(stmt);
  sqlite3_finalize(stmt);
  if(rc == SQLITE_LOCKED)
    return METADATA_DEADLOCK;
  return 0;
}


/**
 *
 */
int
metadb_videoitem_delete_from_ds(void *db, const char *url, int ds)
{
  sqlite3_stmt *stmt;
  int rc;

  rc = db_prepare(db, &stmt,
		  "DELETE FROM videoitem "
		  "WHERE item_id = (SELECT id FROM item WHERE url = ?1) AND "
		  "ds_id = ?2"
		  );
  
  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, ds);

  rc = db_step(stmt);
  sqlite3_finalize(stmt);
  if(rc == SQLITE_LOCKED)
    return METADATA_DEADLOCK;
  return 0;
}


/**
 *
 */
static int
metadb_videoitem_alternatives0(void *db, prop_t *p, const char *url, int dsid,
			       struct prop_sub *skipme)
{
  int rc;
  prop_t *active = NULL;
  sqlite3_stmt *sel;
  prop_vec_t *pv = prop_vec_create(10);

  rc = db_prepare(db, &sel,
		  "SELECT v.id, v.title, v.year, v.preferred, v.status "
		  "FROM videoitem as v, item "
		  "WHERE item.url = ?1 "
		  "AND v.item_id = item.id "
		  "AND v.ds_id = ?2 "
		  "ORDER BY v.weight DESC"
		  );
  
  if(rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text(sel, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int(sel, 2, dsid);
  
  while(db_step(sel) == SQLITE_ROW) {
    char str[128];

    int status = sqlite3_column_int(sel, 4);
    if(status == METAITEM_STATUS_ABSENT)
      continue;

    int id = sqlite3_column_int(sel, 0);
    snprintf(str, sizeof(str), "%d", id);
    prop_t *c = prop_create_root(str);


    if(sqlite3_column_int(sel, 3) && active == NULL) 
      active = prop_ref_inc(c);

    const char *title = (const char *)sqlite3_column_text(sel, 1);
    int year = sqlite3_column_int(sel, 2);

    if(year) {
      snprintf(str, sizeof(str), "%s (%d)", title, year);
    } else {
      snprintf(str, sizeof(str), "%s", title);
    }
    prop_set_string(prop_create(c, "title"), str);
    pv = prop_vec_append(pv, c);
  }
  
  prop_destroy_childs(p);
  prop_set_parent_vector(pv, p, NULL, NULL);

  if(active != NULL)
    prop_select_ex(active, NULL, skipme);
  else if(prop_vec_len(pv) > 0)
    prop_select_ex(prop_vec_get(pv, 0), NULL, skipme);

  prop_ref_dec(active);

  prop_vec_release(pv);
  sqlite3_finalize(sel);
  return 0;
}

/**
 *
 */
void
metadb_videoitem_alternatives(prop_t *p, const char *url, int dsid,
			      struct prop_sub *skipme)
{
  void *db;

  if((db = metadb_get()) == NULL)
    return;

 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  if(metadb_videoitem_alternatives0(db, p, url, dsid, skipme))
    goto again;
  
  db_rollback(db);
  metadb_close(db);
}


/**
 *
 */
int
metadb_item_set_preferred_ds(void *db, const char *url, int ds_id)
{
  int rc;
  sqlite3_stmt *stmt;
  rc = db_prepare(db, &stmt,
		  "UPDATE item "
		  "SET ds_id = ?2 "
		  "WHERE url=?1"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  if(ds_id)
    sqlite3_bind_int(stmt, 2, ds_id);
  else
    sqlite3_bind_null(stmt, 2);

  rc = db_step(stmt);
  sqlite3_finalize(stmt);
  if(rc == SQLITE_LOCKED)
    return METADATA_DEADLOCK;
  return 0;
}


/**
 *
 */
int
metadb_item_get_preferred_ds(const char *url)
{
  void *db;
  int rc, id = 0;
  sqlite3_stmt *stmt;

  if((db = metadb_get()) == NULL)
    return METADATA_PERMANENT_ERROR;

  rc = db_prepare(db, &stmt,
		  "SELECT ds_id "
		  "FROM item "
		  "WHERE url=?1"
		  );

  if(rc != SQLITE_OK) {
    metadb_close(db);
    return METADATA_PERMANENT_ERROR;
  }

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

  rc = db_step(stmt);
  if(rc == SQLITE_ROW)
    id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);
  metadb_close(db);
  return id;
}


/**
 *
 */
rstr_t *
metadb_item_get_user_title(const char *url)
{
  void *db;
  int rc;
  sqlite3_stmt *stmt;
  rstr_t *ret = NULL;

  if((db = metadb_get()) == NULL)
    return NULL;

  rc = db_prepare(db, &stmt, 
		  "SELECT usertitle "
		  "FROM item "
		  "WHERE url=?1"
		  );

  if(rc != SQLITE_OK) {
    metadb_close(db);
    return NULL;
  }

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

  rc = db_step(stmt);
  if(rc == SQLITE_ROW)
    ret = db_rstr(stmt, 0);

  sqlite3_finalize(stmt);
  metadb_close(db);
  return ret;
}



/**
 *
 */
void
metadb_item_set_user_title(const char *url, const char *str)
{
  void *db;
  int rc;
  sqlite3_stmt *stmt;

  if((db = metadb_get()) == NULL)
    return;

  rc = db_prepare(db, &stmt, 
		  "UPDATE item "
		  "SET usertitle=?2 "
		  "WHERE url=?1"
		  );

  if(rc != SQLITE_OK) {
    metadb_close(db);
    return;
  }

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  if(str && !*str)
    str = NULL;
  sqlite3_bind_text(stmt, 2, str, -1, SQLITE_STATIC);

  db_step(stmt);
  sqlite3_finalize(stmt);
  metadb_close(db);
}


/**
 *
 */
static int
metadb_get_videoinfo2(void *db, int64_t id, metadata_t **mdp)
{
  sqlite3_stmt *sel;
  int rc;

  rc = db_prepare(db, &sel,
		  "SELECT v.parent_id, v.title, v.tagline, v.description, "
		  "v.year, v.rating, v.rate_count, v.imdb_id, v.idx, v.type, "
		  "v.id "
		  "FROM videoitem AS v "
		  "WHERE v.id = ?1 "
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, id);

  rc = db_step(sel);

  if(rc == SQLITE_LOCKED) {
    sqlite3_finalize(sel);
    return METADATA_DEADLOCK;
  }

  if(rc == SQLITE_ROW) {
    metadata_t *md = metadata_create();

    md->md_parent_id   = sqlite3_column_int64(sel, 0);
    md->md_title       = db_rstr(sel, 1);
    md->md_tagline     = db_rstr(sel, 2);
    md->md_description = db_rstr(sel, 3);
    md->md_year        = sqlite3_column_int(sel, 4);
    md->md_rating      = db_posint(sel, 5);
    md->md_rating_count= db_posint(sel, 6);
    md->md_imdb_id     = db_rstr(sel, 7);
    md->md_idx         = db_posint(sel, 8);
    md->md_type        = sqlite3_column_int(sel, 9);
    md->md_id          = sqlite3_column_int64(sel, 10);

    md->md_icons =
      metadb_get_video_art(db, id, METADATA_IMAGE_POSTER);
    md->md_backdrops =
      metadb_get_video_art(db, id, METADATA_IMAGE_BACKDROP);
    md->md_thumbs =
      metadb_get_video_art(db, id, METADATA_IMAGE_THUMB);
    md->md_wide_banners =
      metadb_get_video_art(db, id, METADATA_IMAGE_BANNER_WIDE);

    md->md_genre = metadb_get_video_genre(db, id);

    metadb_get_video_cast(db, id, md);

    if(md->md_parent_id)
      metadb_get_videoinfo2(db, md->md_parent_id, &md->md_parent);
    *mdp = md;
  }
  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
int64_t
metadb_get_videoitem(void *db, const char *url)
{
  int rc;
  int64_t rval = METADATA_PERMANENT_ERROR;
  sqlite3_stmt *stmt;

  rc = db_prepare(db, &stmt, 
		  "SELECT videoitem.id "
		  "FROM videoitem,item "
		  "WHERE videoitem.item_id = item.id "
		  "AND item.url = ?1"
		  );

  if(rc)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  rc = db_step(stmt);
  if(rc == SQLITE_ROW) {
    rval = sqlite3_column_int64(stmt, 0);
  } else if(rc == SQLITE_LOCKED)
    rval = METADATA_DEADLOCK;
  sqlite3_finalize(stmt);
  return rval;
}


/**
 *
 */
int
metadb_get_videoinfo(void *db, const char *url,
                     metadata_source_query_info_t *msqi, int num_msqi,
		     int *fixed_ds, metadata_t **mdp,
                     int only_preferred)
{
  int rc;
  sqlite3_stmt *sel;

  if(fixed_ds)
    *fixed_ds = 0;
  *mdp = NULL;

  rc = db_prepare(db, &sel,
		  "SELECT id, ds_id FROM item WHERE url = ?1"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(sel, 1, url, -1, SQLITE_STATIC);

  rc = db_step(sel);
  if(rc == SQLITE_LOCKED) {
    sqlite3_finalize(sel);
    return METADATA_DEADLOCK;
  }

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return 0;
  }

  int64_t item_id = sqlite3_column_int64(sel, 0);
  int ds_id = sqlite3_column_int(sel, 1);

  sqlite3_finalize(sel);

  if(fixed_ds)
    *fixed_ds = ds_id;

  if(only_preferred && ds_id == 0) {
    *mdp = NULL;
    return 0;
  }

  rc = db_prepare(db, &sel,
		  "SELECT v.id, v.title, v.tagline, v.description, v.year, "
		  "v.rating, v.rate_count, v.imdb_id, v.ds_id, v.status, "
		  "v.preferred, v.ext_id, ds.id, ds.enabled, v.querytype, "
		  "v.cfgid, v.idx, v.type, v.parent_id "
		  "FROM datasource AS ds, videoitem AS v "
		  "WHERE v.item_id = ?1 "
		  "AND ds.id = v.ds_id "
		  "AND (?2 == 0 OR ?2 = v.ds_id) "
		  "ORDER BY ds.prio ASC, v.weight DESC"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, item_id);
  sqlite3_bind_int(sel, 2, ds_id);

  metadata_t *md = NULL;

  while(db_step(sel) == SQLITE_ROW) {
    int status = sqlite3_column_int(sel, 9);
    int dsid = sqlite3_column_int(sel, 12);
    int dsenabled = sqlite3_column_int(sel, 13);
    int preferred = sqlite3_column_int(sel, 10);
    int qtype = sqlite3_column_int(sel, 14);
    int64_t cfgid = sqlite3_column_int64(sel, 15);
    if(!dsenabled)
      continue;

    if(num_msqi) {
      int i;
      for(i = 0; i < num_msqi; i++) {
        const struct metadata_source *ms = msqi[i].msqi_ms;
        if(ms->ms_id == dsid && ms->ms_cfgid == cfgid)
          break;
      }

      if(i != num_msqi) {
        msqi[i].msqi_mark = 1;
        msqi[i].msqi_qtype = qtype;
        msqi[i].msqi_status = status;
      } else {
        continue;
      }
    }

    if(status == METAITEM_STATUS_ABSENT)
      continue;

    if(preferred && md != NULL) {
      metadata_destroy(md);
      md = NULL;
    }

    if(md != NULL)
      continue;

    md = metadata_create();

    int64_t vid = sqlite3_column_int64(sel, 0);

    md->md_preferred = preferred;
    md->md_title = db_rstr(sel, 1);
    md->md_tagline = db_rstr(sel, 2);
    md->md_description = db_rstr(sel, 3);
    md->md_year = sqlite3_column_int(sel, 4);
    md->md_rating = db_posint(sel, 5);

    md->md_rating_count = db_posint(sel, 6);

    md->md_type = sqlite3_column_int(sel, 17);

    md->md_imdb_id = db_rstr(sel, 7);
    md->md_dsid = sqlite3_column_int(sel, 8);
    md->md_metaitem_status = status;
    md->md_ext_id = db_rstr(sel, 11);

    md->md_icons = metadb_get_video_art(db, vid, METADATA_IMAGE_POSTER);
    md->md_backdrops = metadb_get_video_art(db, vid, METADATA_IMAGE_BACKDROP);
    md->md_wide_banners =
      metadb_get_video_art(db, vid, METADATA_IMAGE_BANNER_WIDE);
    md->md_thumbs = metadb_get_video_art(db, vid, METADATA_IMAGE_THUMB);
    metadb_get_video_cast(db, vid, md);

    md->md_genre = metadb_get_video_genre(db, vid);
    md->md_qtype = qtype;
    
    md->md_idx = db_posint(sel, 16);

    md->md_parent_id = sqlite3_column_int64(sel, 18);
    md->md_id = vid;
    
    if(md->md_parent_id)
      metadb_get_videoinfo2(db, md->md_parent_id, &md->md_parent);
  }

  sqlite3_finalize(sel);
  *mdp = md;
  return 0;
}


/**
 *
 */
static int
metadb_metadata_get_streams(sqlite3 *db, metadata_t *md, int64_t videoitem_id)
{
  int rc;
  sqlite3_stmt *sel;
  int atrack = 0;
  int strack = 0;
  int vtrack = 0;

  rc = db_prepare(db, &sel,
		  "SELECT streamindex, info, isolang, codec, "
		  "mediatype, disposition, title "
		  "FROM videostream "
		  "WHERE videoitem_id = ?1 "
		  "ORDER BY streamindex"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, videoitem_id);

  while((rc = db_step(sel)) == SQLITE_ROW) {
    int type;
    int tn;
    const char *str = (const char *)sqlite3_column_text(sel, 4);
    if(!strcmp(str, "audio")) {
      type = MEDIA_TYPE_AUDIO;
      tn = ++atrack;
    } else if(!strcmp(str, "video")) {
      type = MEDIA_TYPE_VIDEO;
      tn = ++vtrack;
    } else if(!strcmp(str, "subtitle")) {
      type = MEDIA_TYPE_SUBTITLE;
      tn = ++strack;
    } else {
      continue;
    }
    metadata_add_stream(md, 
			(const char *)sqlite3_column_text(sel, 3),
			type,
			sqlite3_column_int(sel, 0),
			(const char *)sqlite3_column_text(sel, 6),
			(const char *)sqlite3_column_text(sel, 1),
			(const char *)sqlite3_column_text(sel, 2),
			sqlite3_column_int(sel, 5),
			tn, -1);
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

  rc = db_prepare(db, &sel,
		  "SELECT original_time, manufacturer, equipment "
		  "FROM imageitem "
		  "WHERE item_id = ?1"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_int64(sel, 1, item_id);

  rc = db_step(sel);

  if(rc != SQLITE_ROW) {
    sqlite3_finalize(sel);
    return METADATA_PERMANENT_ERROR;
  }

  md->md_time = sqlite3_column_int(sel, 0);
  md->md_manufacturer = rstr_alloc((void *)sqlite3_column_text(sel, 1));
  md->md_equipment = rstr_alloc((void *)sqlite3_column_text(sel, 2));
  sqlite3_finalize(sel);
  return 0;
}


/**
 *
 */
static metadata_t *
metadata_get(void *db, int item_id, int contenttype, get_cache_t *gc)
{
  int64_t vi_id;
  metadata_t *md = metadata_create();
  md->md_contenttype = contenttype;
  
  int r;
  switch(md->md_contenttype) {
  case CONTENT_AUDIO:
    r = metadb_metadata_get_audio(db, md, item_id, gc);
    break;

  case CONTENT_VIDEO:
    vi_id = metadb_metadata_get_video(db, md, item_id, 1);
    if(vi_id == -1) {
      r = 1;
      break;
    }
    r = metadb_metadata_get_streams(db, md, vi_id);
    break;

  case CONTENT_IMAGE:
    r = metadb_metadata_get_image(db, md, item_id);
    break;

  case CONTENT_DIR:
  case CONTENT_SHARE:
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

  rc = db_prepare(db, &sel,
		  "SELECT id,contenttype,parent from item "
		  "where url=?1 AND "
		  "mtime=?2");

  if(rc != SQLITE_OK) {
    db_rollback(db);
    return NULL;
  }

  sqlite3_bind_text(sel, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int(sel, 2, mtime);

  rc = db_step(sel);

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

  if(md != NULL)
    md->md_cache_status = 
      sqlite3_column_int(sel, 2) ? 
      METADATA_CACHE_STATUS_FULL :
      METADATA_CACHE_STATUS_UNPARENTED;

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
 again:
  if(db_begin(db))
    return NULL;

  int64_t parent_id = db_item_get(db, url, mtime);

  if(parent_id == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }
  if(parent_id < 0) {
    db_rollback(db);
    return NULL;
  }

  sqlite3_stmt *sel;
  int rc;

  rc = db_prepare(db, &sel,
		  "SELECT id, url, contenttype, mtime, indexstatus "
		  "FROM item "
		  "WHERE parent = ?1"
		  );

  if(rc != SQLITE_OK) {
    db_rollback(db);
    return NULL;
  }

  sqlite3_bind_int64(sel, 1, parent_id);

  fa_dir_t *fd = fa_dir_alloc();

  get_cache_t gc = {0};

  while((rc = db_step(sel)) == SQLITE_ROW) {
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
      if(fde->fde_md != NULL) {
	fde->fde_md->md_cache_status = METADATA_CACHE_STATUS_FULL;
        fde->fde_md->md_index_status = sqlite3_column_int(sel, 4);
      }
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
 again:
  if(db_begin(db))
    return;

  sqlite3_stmt *stmt;
    
  rc = db_prepare(db, &stmt,
		  "UPDATE item SET parent = NULL WHERE url=?1"
		  );
  
  if(rc != SQLITE_OK) {
    db_rollback(db);
    return;
  }

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  rc = db_step(stmt);
  if(rc == SQLITE_LOCKED) {
    db_rollback_deadlock(db);
    goto again;
  }

  sqlite3_finalize(stmt);
  db_commit(db);
}


/**
 *
 */
void
metadb_parent_item(void *db, const char *url, const char *parent_url)
{
  int rc;
  int64_t parent_id;
 again:
  if(db_begin(db))
    return;

  parent_id = db_item_get(db, parent_url, NULL);
  if(parent_id == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }
  sqlite3_stmt *stmt;
    
  rc = db_prepare(db, &stmt,
		  "UPDATE item SET parent = ?2 WHERE url=?1");
  
  if(rc != SQLITE_OK) {
    db_rollback(db);
    return;
  }

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 2, parent_id);
  rc = db_step(stmt);
  if(rc == SQLITE_LOCKED) {
    db_rollback_deadlock(db);
    goto again;
  }

  sqlite3_finalize(stmt);
  db_commit(db);
}



/**
 *
 */
metadata_t *
metadata_get_video_data(const char *url)
{
  void *db = metadb_get();
  metadata_t *md;

  int r = metadb_get_videoinfo(db, url, NULL, 0, NULL, &md, 0);
  metadb_close(db);
  if(r)
    return NULL;
  return md;
}



