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
#include <stdio.h>
#include <unistd.h>

#include <sqlite3.h>

#include "prop/prop.h"
#include "prop/prop_concat.h"

#include "main.h"
#include "media/media.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/str.h"

#include "metadata.h"
#include "metadata_sources.h"

#include "db/db_support.h"
#include "db/kvstore.h"

#include "settings.h"
#include "subtitles/subtitles.h"



hts_mutex_t metadata_sources_mutex;

struct metadata_source_queue metadata_sources[METADATA_TYPE_num];
static prop_t *metadata_sources_settings[METADATA_TYPE_num];
static int tagkey;

/**
 *
 */
static int
ms_prio_cmp(const metadata_source_t *a, const metadata_source_t *b)
{
  return a->ms_prio - b->ms_prio;
}


/**
 *
 */
static void
ms_set_enable(void *opaque, int value)
{
  metadata_source_t *ms = opaque;
  sqlite3_stmt *stmt;
  ms->ms_enabled = value;

  prop_setv(ms->ms_settings, "metadata", "enabled", NULL, PROP_SET_INT,
	    ms->ms_enabled);

  void *db = metadb_get();

  int rc = db_prepare(db, &stmt,
		      "UPDATE datasource "
		      "SET enabled = ?2 "
		      "WHERE id = ?1");

  if(rc != SQLITE_OK) {
    metadb_close(db);
    return;
  }

  sqlite3_bind_int(stmt, 1, ms->ms_id);
  sqlite3_bind_int(stmt, 2, ms->ms_enabled);

  rc = db_step(stmt);
  sqlite3_finalize(stmt);
  metadb_close(db);
}

/**
 *
 */
metadata_source_t *
metadata_add_source(const char *name, const char *description,
		    int prio,  metadata_type_t type,
		    const metadata_source_funcs_t *funcs,
		    uint64_t partials, uint64_t complete)
{
  assert(type < METADATA_TYPE_num);

  void *db = metadb_get();
  int rc;
  int id = METADATA_PERMANENT_ERROR;
  int enabled = 1;
  sqlite3_stmt *stmt;

 again:
  if(db_begin(db))
    goto err;

  rc = db_prepare(db, &stmt,
		  "SELECT id,prio,enabled FROM datasource WHERE name=?1");

  if(rc != SQLITE_OK) {
    goto err;
  }

  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

  rc = db_step(stmt);
  if(rc == SQLITE_LOCKED) {
    sqlite3_finalize(stmt);
    db_rollback_deadlock(db);
    goto again;
  }

  if(rc == SQLITE_ROW) {
    id = sqlite3_column_int(stmt, 0);
    if(sqlite3_column_type(stmt, 1) == SQLITE_INTEGER)
      prio = sqlite3_column_int(stmt, 1);
    if(sqlite3_column_type(stmt, 1) == SQLITE_INTEGER)
      enabled = sqlite3_column_int(stmt, 2);

    sqlite3_finalize(stmt);

  } else {

    sqlite3_finalize(stmt);

    rc = db_prepare(db, &stmt,
		    "INSERT INTO datasource "
		    "(name, prio, type, enabled) "
		    "VALUES "
		    "(?1, ?2, ?3, ?4)");

    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
    }

    sqlite3_bind_text(stmt,1, name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, prio);
    sqlite3_bind_int(stmt, 3, type);
    sqlite3_bind_int(stmt, 4, enabled);

    rc = db_step(stmt);
    sqlite3_finalize(stmt);
    if(rc == SQLITE_LOCKED) {
      db_rollback_deadlock(db);
      goto again;
    }
    id = sqlite3_last_insert_rowid(db);
  }
  db_commit(db);
  metadb_close(db);

  metadata_source_t *ms = calloc(1, sizeof(metadata_source_t));
  ms->ms_type = type;
  ms->ms_prio = prio;
  ms->ms_name = strdup(name);
  ms->ms_description = strdup(description);
  ms->ms_id = id;
  ms->ms_funcs = funcs;
  ms->ms_enabled = enabled;
  ms->ms_partial_props = partials;
  ms->ms_complete_props = complete;


  ms->ms_settings =
    settings_add_dir_cstr(metadata_sources_settings[type],
			  ms->ms_description, NULL, NULL, NULL, NULL);

  prop_tag_set(ms->ms_settings, &tagkey, ms);

  hts_mutex_lock(&metadata_sources_mutex);

  setting_create(SETTING_BOOL, ms->ms_settings, 0,
                 SETTING_TITLE(_p("Enabled")),
                 SETTING_MUTEX(&metadata_sources_mutex),
                 SETTING_CALLBACK(ms_set_enable, ms),
                 SETTING_VALUE(ms->ms_enabled),
                 NULL);

  ms_set_enable(ms, enabled);

  TAILQ_INSERT_SORTED(&metadata_sources[type], ms, ms_link, ms_prio_cmp,
                      metadata_source_t);

  metadata_source_t *n = TAILQ_NEXT(ms, ms_link);
  prop_move(ms->ms_settings, n ? n->ms_settings : NULL);

  hts_mutex_unlock(&metadata_sources_mutex);

  return ms;

 err:
  metadb_close(db);
  return NULL;
}



/**
 *
 */
static void
class_handle_move(metadata_source_t *ms, metadata_source_t *before)
{
  int type = ms->ms_type;

  TAILQ_REMOVE(&metadata_sources[type], ms, ms_link);

  if(before) {
    TAILQ_INSERT_BEFORE(before, ms, ms_link);
  } else {
    TAILQ_INSERT_TAIL(&metadata_sources[type], ms, ms_link);
  }

  void *db = metadb_get();

  int prio = 1;
  TAILQ_FOREACH(ms, &metadata_sources[type], ms_link)  {
    sqlite3_stmt *stmt;
    int rc;

    ms->ms_prio = prio++;

    rc = db_prepare(db, &stmt,
		    "UPDATE datasource "
                    "SET prio = ?1 "
                    "WHERE "
                    "id = ?2");

    if(rc != SQLITE_OK) {
      TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	    __FUNCTION__, __LINE__);
    } else {
      sqlite3_bind_int(stmt, 1, ms->ms_prio);
      sqlite3_bind_int(stmt, 2, ms->ms_id);

      db_step(stmt);
      sqlite3_finalize(stmt);
    }
  }
  metadb_close(db);
}


/**
 *
 */
static void
provider_class_node_sub(void *opaque, prop_event_t event, ...)
{
  prop_t *p1, *p2;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_REQ_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    class_handle_move(prop_tag_get(p1, &tagkey),
                      p2 ? prop_tag_get(p2, &tagkey) : NULL);
    prop_move(p1, p2);
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
add_provider_class(prop_concat_t *pc,
		   metadata_type_t type,
		   prop_t *title)
{
  prop_t *c = prop_create_root(NULL);

  metadata_sources_settings[type] = c;

  prop_t *d = prop_create_root(NULL);

  prop_link(title, prop_create(prop_create(d, "metadata"), "title"));
  prop_set_string(prop_create(d, "type"), "separator");

  prop_t *n = prop_create(c, "nodes");

  prop_concat_add_source(pc, n, d);

  prop_subscribe(0,
                 PROP_TAG_CALLBACK, provider_class_node_sub, NULL,
                 PROP_TAG_MUTEX, &metadata_sources_mutex,
                 PROP_TAG_ROOT, n,
                 NULL);
}


/**
 *
 */
const metadata_source_t *
metadata_source_get(metadata_type_t type, int id)
{
  const metadata_source_t *ms;
  hts_mutex_lock(&metadata_sources_mutex);
  TAILQ_FOREACH(ms, &metadata_sources[type], ms_link)
    if(ms->ms_enabled && ms->ms_id == id)
      break;
  hts_mutex_unlock(&metadata_sources_mutex);

  // This must be fixed when we can delete metadata_sources
  // We need to retain a reference or something like that

  return ms;
}


/**
 *
 */
void
metadata_sources_init(void)
{
  prop_t *s;
  prop_concat_t *pc;

  hts_mutex_init(&metadata_sources_mutex);

  s = settings_add_dir(NULL, _p("Metadata"), "metadata", NULL,
		       _p("Metadata configuration and provider settings"),
		       "settings:metadata");

  pc = prop_concat_create(prop_create(s, "nodes"));

  add_provider_class(pc, METADATA_TYPE_VIDEO, _p("Providers for Video"));
  add_provider_class(pc, METADATA_TYPE_MUSIC, _p("Providers for Music"));
}
