/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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
#include "metadata.h"
#include "db/db_support.h"
#include "showtime.h"
#include "playinfo.h"

#define METADATA_VERSION_STR "1"

static HTS_MUTEX_DECL(mip_mutex);

static void update_by_url(sqlite3 *db, const char *url);

/**
 *
 */
void
playinfo_register_play(const char *url, int inc, int content_type)
{
  int rc;
  int i;
  void *db;

  if((db = metadb_get()) == NULL)
    return;

 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    rc = db_prepare(db, &stmt,
		    i == 0 ? 
		    "UPDATE item "
		    "SET playcount = playcount + ?3, "
		    "lastplay = ?2 "
		    "WHERE url=?1"
		    :
		    "INSERT INTO item "
		    "(url, contenttype, playcount, lastplay) "
		    "VALUES "
		    "(?1, ?4, ?3, ?2)"
		    );

    if(rc != SQLITE_OK) {
      db_rollback(db);
      metadb_close(db);
      return;
    }

    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, time(NULL));
    sqlite3_bind_int(stmt, 3, inc);
    sqlite3_bind_int(stmt, 4, content_type);
    rc = db_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_LOCKED) {
      db_rollback_deadlock(db);
      goto again;
    }
    if(i == 0 && rc == SQLITE_DONE && sqlite3_changes(db) > 0)
      break;
  }
  db_commit(db);
  hts_mutex_lock(&mip_mutex);
  update_by_url(db, url);
  hts_mutex_unlock(&mip_mutex);
  metadb_close(db);
}



/**
 *
 */
void
playinfo_set_restartpos(const char *url, int64_t pos_ms)
{
  int rc;
  int i;
  void *db;

  if(pos_ms >= 0 && pos_ms < 60000)
    return;

  if((db = metadb_get()) == NULL)
    return;
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;

    rc = db_prepare(db, &stmt,
		    i == 0 ? 
		    "UPDATE item "
		    "SET restartposition = ?2, contenttype = ?3 "
		    "WHERE url=?1"
		    :
		    "INSERT INTO item "
		    "(url, contenttype, restartposition) "
		    "VALUES "
		    "(?1, ?3, ?2)");

    if(rc != SQLITE_OK) {
      db_rollback(db);
      metadb_close(db);
      return;
    }

    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    if(pos_ms > 0)
      sqlite3_bind_int64(stmt, 2, pos_ms);
    sqlite3_bind_int(stmt, 3, CONTENT_VIDEO);
    rc = db_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_LOCKED) {
      db_rollback_deadlock(db);
      goto again;
    }
    if(i == 0 && rc == SQLITE_DONE && sqlite3_changes(db) > 0)
      break;
  }
  db_commit(db);
  update_by_url(db, url);
  metadb_close(db);
}


/**
 *
 */
int64_t
playinfo_get_restartpos(const char *url)
{
  int rc;
  void *db;
  sqlite3_stmt *stmt;
  int64_t rval = 0;

  if((db = metadb_get()) == NULL)
    return 0;

  rc = db_prepare(db, &stmt,
		  "SELECT restartposition "
		  "FROM item "
		  "WHERE url = ?1"
		  );

  if(rc == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
    rc = db_step(stmt);

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
  prop_sub_t *mip_playcount_sub;

  int mip_refcount;

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
  int rc = METADATA_PERMANENT_ERROR;
  sqlite3_stmt *stmt;

  rc = db_prepare(db, &stmt,
		  "SELECT "
		  "playcount,lastplay,restartposition "
		  "FROM item "
		  "WHERE url=?1"
		  );

  if(rc != SQLITE_OK)
    return METADATA_PERMANENT_ERROR;

  sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);

  rc = db_step(stmt);
  if(rc == SQLITE_ROW) {
    mii->mii_playcount  = sqlite3_column_int(stmt, 0);
    mii->mii_lastplayed = sqlite3_column_int(stmt, 1);
    mii->mii_restartpos = sqlite3_column_int(stmt, 2);
    rc = 0;
  }

  sqlite3_finalize(stmt);
  return rc;
}


/**
 *
 */
static void
mip_set(metadb_item_prop_t *mip, const metadb_item_info_t *mii)
{
  prop_set_int_ex(mip->mip_playcount, mip->mip_playcount_sub,
		  mii->mii_playcount);
  prop_set_int(mip->mip_lastplayed, mii->mii_lastplayed);
  prop_set_float(mip->mip_restartpos, mii->mii_restartpos / 1000.0);
}


/**
 *
 */
static void
update_by_url(sqlite3 *db, const char *url)
{
  metadb_item_prop_t *mip;
  metadb_item_info_t mii;
  int loaded = 0;

  unsigned int hash = mystrhash(url) % MIP_HASHWIDTH;

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
}


/**
 *
 */
static void
mip_release(metadb_item_prop_t *mip)
{
  mip->mip_refcount--;
  if(mip->mip_refcount > 0)
    return;

  LIST_REMOVE(mip, mip_link);

  prop_unsubscribe(mip->mip_destroy_sub);
  prop_unsubscribe(mip->mip_playcount_sub);
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
metadb_item_prop_destroyed(void *opaque, prop_event_t event, ...)
{
  metadb_item_prop_t *mip = opaque;
  if(event == PROP_DESTROYED)
    mip_release(mip);
}

/**
 *
 */
static void
metadb_set_playcount(void *opaque, prop_event_t event, ...)
{
  metadb_item_prop_t *mip = opaque;
  int rc, i;
  void *db;
  va_list ap;

  if(event == PROP_DESTROYED) {
    mip_release(mip);
    return;
  }
  if(event != PROP_SET_INT) 
    return;

  va_start(ap, event);
  int v = va_arg(ap, int);
  va_end(ap);

  if((db = metadb_get()) == NULL)
    return;
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  for(i = 0; i < 2; i++) {
    sqlite3_stmt *stmt;
    rc = db_prepare(db, &stmt, 
		    i == 0 ? 
		    "UPDATE item "
		    "SET playcount = ?2 "
		    "WHERE url=?1"
		    :
		    "INSERT INTO item "
		    "(url, playcount, metadataversion) "
		    "VALUES "
		    "(?1, ?2, " METADATA_VERSION_STR ")"
		    );
    
    if(rc != SQLITE_OK) {
      db_rollback(db);
      metadb_close(db);
      return;
    }

    sqlite3_bind_text(stmt, 1, mip->mip_url, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, v);
    rc = db_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_LOCKED) {
      db_rollback_deadlock(db);
      goto again;
    }

    if(rc == SQLITE_DONE && sqlite3_changes(db) == 0) {
      continue;
    }
    break;
  }

  db_commit(db);
  update_by_url(db, mip->mip_url);
  metadb_close(db);
}



/**
 *
 */
static void
metadb_bind_url_to_prop0(void *db, const char *url, prop_t *parent)
{
  metadb_item_prop_t *mip = malloc(sizeof(metadb_item_prop_t));

  hts_mutex_lock(&mip_mutex);
  mip->mip_refcount = 2;

  mip->mip_destroy_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, metadb_item_prop_destroyed, mip,
		   PROP_TAG_ROOT, parent,
		   PROP_TAG_MUTEX, &mip_mutex,
		   NULL);

  assert(mip->mip_destroy_sub != NULL);


  mip->mip_playcount  = prop_create_r(parent, "playcount");
  mip->mip_lastplayed = prop_create_r(parent, "lastplayed");
  mip->mip_restartpos = prop_create_r(parent, "restartpos");
  
  mip->mip_playcount_sub =
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, metadb_set_playcount, mip,
		   PROP_TAG_ROOT, mip->mip_playcount,
		   PROP_TAG_MUTEX, &mip_mutex,
		   NULL);
  
  assert(mip->mip_playcount_sub != NULL);

  mip->mip_url = strdup(url);
  
  unsigned int hash = mystrhash(url) % MIP_HASHWIDTH;
  
  LIST_INSERT_HEAD(&mip_hash[hash], mip, mip_link);
  
  metadb_item_info_t mii;
  if(!mip_get(db, url, &mii))
    mip_set(mip, &mii);
  hts_mutex_unlock(&mip_mutex);
}


/**
 *
 */
void
playinfo_bind_url_to_prop(void *db, const char *url, prop_t *parent)
{
  if(db != NULL)
    return metadb_bind_url_to_prop0(db, url, parent);

  if((db = metadb_get()) != NULL)
    metadb_bind_url_to_prop0(db, url, parent);
  metadb_close(db);
}


/**
 *
 */
void
playinfo_mark_urls_as(const char **urls, int num_urls, int seen,
                      int content_type)
{
  int i, j;
  void *db;

  if((db = metadb_get()) == NULL)
    return;
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  for(j = 0; j < num_urls; j++) {
    const char *url = urls[j];

    for(i = 0; i < 2; i++) {
      sqlite3_stmt *stmt;
      int rc;
      if(!seen) {
        rc = db_prepare(db, &stmt,
                        "UPDATE item "
                        "SET playcount = 0 "
                        "WHERE url=?1"
                        );

      } else {

        rc = db_prepare(db, &stmt,
                        i == 0 ?
                        "UPDATE item "
                        "SET playcount = 1, "
                        "lastplay = ?2 "
                        "WHERE url=?1 AND playcount = 0"
                        :
                        "INSERT INTO item "
                        "(url, contenttype, playcount, lastplay) "
                        "VALUES "
                        "(?1, ?3, 1, ?2)"
                        );
      }

      if(rc != SQLITE_OK) {
        db_rollback(db);
        metadb_close(db);
        return;
      }

      sqlite3_bind_text(stmt, 1, url, -1, SQLITE_STATIC);
      sqlite3_bind_int(stmt, 2, time(NULL));
      sqlite3_bind_int(stmt, 3, content_type);
      rc = db_step(stmt);
      sqlite3_finalize(stmt);
      if(rc == SQLITE_LOCKED) {
        db_rollback_deadlock(db);
        goto again;
      }
      if(!seen)
        break;
      if(i == 0 && rc == SQLITE_DONE && sqlite3_changes(db) > 0)
        break;
    }
  }
  db_commit(db);
  hts_mutex_lock(&mip_mutex);
  for(j = 0; j < num_urls; j++)
    update_by_url(db, urls[j]);
  hts_mutex_unlock(&mip_mutex);
  metadb_close(db);
}
