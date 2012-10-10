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

#include "misc/queue.h"
#include "misc/rstr.h"

#pragma once

struct prop;
struct prop_sub;
struct prop_vec;
struct prop_nf;
typedef struct metadata_lazy_video metadata_lazy_video_t;

#define METAITEM_STATUS_ABSENT     1
#define METAITEM_STATUS_PARTIAL    2
#define METAITEM_STATUS_COMPLETE   3

#define METADATA_QTYPE_FILENAME    1
#define METADATA_QTYPE_IMDB        2
#define METADATA_QTYPE_DIRECTORY   3
#define METADATA_QTYPE_CUSTOM      4
#define METADATA_QTYPE_CUSTOM_IMDB 5
#define METADATA_QTYPE_FILENAME_OR_DIRECTORY 6
#define METADATA_QTYPE_EPISODE     7


#define METADATA_PERMANENT_ERROR -1
#define METADATA_TEMPORARY_ERROR -2
#define METADATA_DEADLOCK        -3

/**
 * Content types.
 * These are stored directly in the sqlite metadata database so they
 * must never be changed
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
  CONTENT_FONT        = 11,
  CONTENT_num
} contenttype_t;


/**
 * Metadata types.
 * These are stored directly in the sqlite metadata database so they
 * must never be changed
 */
typedef enum {
  METADATA_TYPE_VIDEO  = 1,
  METADATA_TYPE_SEASON = 2,
  METADATA_TYPE_SERIES = 3,
  METADATA_TYPE_MUSIC  = 4,
  METADATA_TYPE_num
} metadata_type_t;


/**
 * Image types.
 * These are stored directly in the sqlite metadata database so they
 * must never be changed
 */
typedef enum {
  METADATA_IMAGE_POSTER = 1,
  METADATA_IMAGE_BACKDROP = 2,
  METADATA_IMAGE_PORTRAIT = 3,
  METADATA_IMAGE_BANNER_WIDE = 4,
} metadata_image_type_t;




const char *content2type(contenttype_t ctype);

contenttype_t type2content(const char *str);

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
  int ms_tracknum;
  int ms_disposition;
} metadata_stream_t;


TAILQ_HEAD(metadata_person_queue, metadata_person);

/**
 *
 */
typedef struct metadata_person {
  TAILQ_ENTRY(metadata_person) mp_link;
  rstr_t *mp_name;
  rstr_t *mp_character;
  rstr_t *mp_department;
  rstr_t *mp_job;
  rstr_t *mp_portrait;
} metadata_person_t;


/**
 *
 */
typedef struct metadata {

  char *md_redirect;

  rstr_t *md_manufacturer;
  rstr_t *md_equipment;

  rstr_t *md_backdrop;
  rstr_t *md_icon;
  rstr_t *md_banner_wide;

  rstr_t *md_ext_id;

  rstr_t *md_title;
  rstr_t *md_album;
  rstr_t *md_artist;
  rstr_t *md_format;
  rstr_t *md_genre;

  struct metadata_stream_queue md_streams;
  struct metadata_person_queue md_cast;
  struct metadata_person_queue md_crew;

  struct metadata *md_parent;
  rstr_t *md_description;
  rstr_t *md_tagline;

  rstr_t *md_imdb_id;

  int64_t md_id;
  int64_t md_parent_id;

  time_t md_time;
  contenttype_t md_contenttype;
  float md_duration;

  metadata_type_t md_type;

  int md_rating_count;


  int16_t md_dsid;
  int16_t md_tracks;
  int16_t md_track;

  int16_t md_rating;  // 0 - 100
  int16_t md_year;
  int16_t md_idx;  // -1 == unset (episode, season, etc. Depends on md_type)
  
  char md_metaitem_status;  // METAITEM_STATUS_* -defines
  char md_qtype;

  char md_preferred:1;        // Preferred by the user (set in database)
  char md_cached:1;           // Set if data is from a cached lookup




} metadata_t;


LIST_HEAD(metadata_source_list, metadata_source);

/**
 *
 */
typedef struct metadata_source_funcs {
  int64_t (*query_by_title_and_year)(void *db, const char *item_url, 
				     const char *title, int year,
				     int duration, int qtype);

  int64_t (*query_by_imdb_id)(void *db, const char *item_url, 
			      const char *imdb_id, int qtype);

  int64_t (*query_by_id)(void *db, const char *item_url, const char *id);

  int64_t (*query_by_episode)(void *db, const char *item_url, 
			      const char *title, int season, int episode,
			      int qtype);

} metadata_source_funcs_t;


/**
 * Used to tell what properties a metadata_source can provide
 *
 *
 */

typedef enum {
  METADATA_PROP_TITLE,
  METADATA_PROP_POSTER,
  METADATA_PROP_YEAR,
  METADATA_PROP_VTYPE,
  METADATA_PROP_TAGLINE,
  METADATA_PROP_DESCRIPTION,
  METADATA_PROP_RATING,
  METADATA_PROP_RATING_COUNT,
  METADATA_PROP_BACKDROP,
  METADATA_PROP_GENRE,
  METADATA_PROP_CAST,
  METADATA_PROP_CREW,
  METADATA_PROP_EPISODE_NAME,
  METADATA_PROP_SEASON_NAME,
  METADATA_PROP_ARTIST_PICTURES,
  METADATA_PROP_ALBUM_ART,
} metadata_prop_t;

/**
 *
 */
typedef struct metadata_source {
  LIST_ENTRY(metadata_source) ms_link;
  char *ms_name;
  char *ms_description;
  int ms_prio;
  int ms_id;
  int ms_enabled;

  const metadata_source_funcs_t *ms_funcs;
  struct prop *ms_settings;

  int ms_mark;
  int ms_qtype;
  int ms_status;
  int64_t ms_cfgid;

  uint64_t ms_partial_props;
  uint64_t ms_complete_props;
} metadata_source_t;


metadata_source_t *metadata_add_source(const char *name,
				       const char *description,
				       int default_prio, metadata_type_t type,
				       const metadata_source_funcs_t *funcs,
				       uint64_t partials,
				       uint64_t complete);

metadata_t *metadata_create(void);

void metadata_destroy(metadata_t *md);

void metadata_add_stream(metadata_t *md, const char *codec,
			 int type, int streamindex,
			 const char *title,
			 const char *info, const char *isolang,
			 int disposition, int tracknum);

void metadata_to_proptree(const metadata_t *md,
			  struct prop *proproot,
			  int cleanup_streams);

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

int metadb_item_set_preferred_ds(void *opaque, const char *url, int ds_id);

int metadb_item_get_preferred_ds(const char *url);

rstr_t *metadb_item_get_user_title(const char *url);

void metadb_item_set_user_title(const char *url, const char *title);

#define METADB_AUDIO_PLAY_THRESHOLD (10 * 1000000)

void metadb_bind_url_to_prop(void *db, const char *url, struct prop *parent);

void metadb_set_video_restartpos(const char *url, int64_t pos);

rstr_t *metadb_get_album_art(void *db, const char *album, const char *artist);

int metadb_get_artist_pics(void *db, const char *artist, 
			   void (*cb)(void *opaque, const char *url,
				      int width, int height),
			   void *opaque);

int64_t video_get_restartpos(const char *url);

int64_t metadb_artist_get_by_title(void *db, const char *title, int ds_id,
				   const char *ext_id);

int64_t metadb_album_get_by_title(void *db, const char *album,
				  int64_t artist_id,
				  int ds_id, const char *ext_id);

void metadb_insert_albumart(void *db, int64_t album_id, const char *url,
			    int width, int height);

void metadb_insert_artistpic(void *db, int64_t artist_id, const char *url,
			     int width, int height);

void metadb_insert_videoart(void *db, int64_t videoitem_id, const char *url,
			    metadata_image_type_t type, int width, int height,
			    int weight, const char *group, int titled);

void metadb_delete_videoart(void *db, int64_t videoitem_id);

void metadb_insert_videocast(void *db, int64_t videoitem_id,
			     const char *name,
			     const char *character,
			     const char *department,
			     const char *job,
			     int order,
			     const char *image,
			     int width,
			     int height,
			     const char *ext_id);

void metadb_delete_videocast(void *db, int64_t videoitem_id);

void metadb_insert_videogenre(void *db, int64_t videoitem_id,
			      const char *title);

int64_t metadb_insert_videoitem(void *db, const char *url, int ds_id,
				const char *ext_id, const metadata_t *md,
				int status, int64_t weight, int qtype,
				int64_t cfgid);

int metadb_get_videoinfo(void *db, const char *url,
			 struct metadata_source_list *sources,
			 int *fixed_ds, metadata_t **mdp);

int64_t metadb_get_videoitem(void *db, const char *url);

void metadb_videoitem_alternatives(struct prop *p, const char *url, int dsid,
				   struct prop_sub *skipme);

int metadb_videoitem_set_preferred(void *db, const char *url, int64_t vid);

int metadb_videoitem_delete_from_ds(void *db, const char *url, int ds);

void decoration_init(void);

#define DECO_FLAGS_RAW_FILENAMES    0x2

void decorated_browse_create(struct prop *model, struct prop_nf *pnf,
			     struct prop *items, rstr_t *title, int flags);

void metadata_init(void);

void metadata_bind_artistpics(struct prop *prop, rstr_t *artist);

void metadata_bind_albumart(struct prop *prop, rstr_t *artist, rstr_t *album);

metadata_lazy_video_t *metadata_bind_video_info(struct prop *prop,
						rstr_t *url, rstr_t *filename,
						rstr_t *imdb_id, int duration,
						struct prop *options,
						struct prop *root,
						rstr_t *parent, int lonely,
						int passive);

void mlv_unbind(metadata_lazy_video_t *mlv);

void mlv_set_imdb_id(metadata_lazy_video_t *mlv, rstr_t *imdb_id);

void mlv_set_duration(metadata_lazy_video_t *mlv, int duration);

void mlv_set_lonely(metadata_lazy_video_t *mlv, int lonely);

rstr_t *metadata_remove_postfix_rstr(rstr_t *in);

rstr_t *metadata_remove_postfix(const char *in);

