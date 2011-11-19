/*
 *  metadata management
 *  Copyright (C) 2011 Andreas Öman
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

#include <misc/rstr.h>

#pragma once


/**
 * Content types
 */
typedef enum {
  CONTENT_UNKNOWN     = 0,
  CONTENT_DIR         = 1,
  CONTENT_FILE        = 2,
  CONTENT_ARCHIVE     = 3,
  CONTENT_AUDIO       = 4,
  CONTENT_VIDEO       = 5,
  CONTENT_PLAYLIST    = 6,
  CONTENT_DVD         = 7,
  CONTENT_IMAGE       = 8,
  CONTENT_ALBUM       = 9,
  CONTENT_PLUGIN      = 10,
} contenttype_t;


const char *content2type(contenttype_t ctype);

TAILQ_HEAD(metadata_stream_queue, metadata_stream);



/**
 *
 */
typedef struct metadata_stream {
  TAILQ_ENTRY(metadata_stream) ms_link;

  int ms_streamindex;

  rstr_t *ms_title;
  rstr_t *ms_info;
  rstr_t *ms_isolang;
  rstr_t *ms_codec;

  int ms_type;

  int ms_disposition;

} metadata_stream_t;



/**
 *
 */
typedef struct metadata {
  int md_cached;  // Set if data is from a cached lookup

  char *md_redirect;

  contenttype_t md_contenttype;
  float md_duration;
  int md_tracks;
  time_t md_time;

  rstr_t *md_title;
  rstr_t *md_album;
  rstr_t *md_artist;
  rstr_t *md_format;

  struct metadata_stream_queue md_streams;

} metadata_t;

metadata_t *metadata_create(void);

void metadata_destroy(metadata_t *md);

void metadata_add_stream(metadata_t *md, const char *codec,
			 int type, int streamindex,
			 const char *title,
			 const char *info, const char *isolang,
			 int disposition);

struct prop;
void metadata_to_proptree(const metadata_t *md, struct prop *proproot,
			  int overwrite_title);




void metadb_init(void);

void metadb_fini(void);

void *metadb_get(void);

void metadb_close(void *db);

void metadb_metadata_write(void *db, const char *url, time_t mtime,
			   const metadata_t *md, const char *parent,
			   time_t parent_mtime);

metadata_t *metadb_metadata_get(void *db, const char *url, time_t mtime);

struct fa_dir;
struct fa_dir *metadb_metadata_scandir(void *db, const char *url,
				       time_t *mtimep);

void metadb_unparent_item(void *db, const char *url);

void metadb_register_play(const char *url, int inc, int content_type);

#define METADB_AUDIO_PLAY_THRESHOLD (10 * 1000000)

void metadb_bind_url_to_prop(void *db, const char *url, struct prop *parent);

void metadb_set_video_restartpos(const char *url, int64_t pos);

int64_t video_get_restartpos(const char *url);
