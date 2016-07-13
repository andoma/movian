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
#pragma once
#include "misc/queue.h"
#include "misc/rstr.h"

#define METADATA_TRACE(x, ...) do {                                     \
    if(gconf.enable_metadata_debug)                                     \
      TRACE(TRACE_DEBUG, "METADATA", x, ##__VA_ARGS__);                 \
  } while(0)

// Don't do lookups for anything shorter than 15 minutes
#define METADATA_DURATION_LIMIT (60 * 15)

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
#define METADATA_QTYPE_MOVIE       8
#define METADATA_QTYPE_TVSHOW      9


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
  CONTENT_SHARE       = 12,
  CONTENT_DOCUMENT    = 13,  // PDF etc
  CONTENT_num
} contenttype_t;


static inline int content_dirish(contenttype_t ct)
{
  return ct == CONTENT_DIR || ct == CONTENT_SHARE || ct == CONTENT_ARCHIVE ||
    ct == CONTENT_PLAYLIST;
}

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
  METADATA_IMAGE_THUMB = 5,
} metadata_image_type_t;


/**
 * Index status
 * These are stored directly in the sqlite metadata database so they
 * must never be changed
 */
typedef enum {
  INDEX_STATUS_NIL = 0,
  INDEX_STATUS_ERROR = 1,
  INDEX_STATUS_STATED = 2,
  INDEX_STATUS_FILE_ANALYZED = 3,
  INDEX_STATUS_METADATA_BOUND = 4,
} metadata_index_status_t;

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
  int ms_channels;   // -1 == unknown
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

  rstr_vec_t *md_backdrops;
  rstr_vec_t *md_icons;
  rstr_vec_t *md_wide_banners;
  rstr_vec_t *md_thumbs;

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
  char md_cache_status:2;     // Set if data is from a cached lookup
#define METADATA_CACHE_STATUS_NO         0
#define METADATA_CACHE_STATUS_FULL       1
#define METADATA_CACHE_STATUS_UNPARENTED 2



} metadata_t;


TAILQ_HEAD(metadata_source_queue, metadata_source);

/**
 *
 */
typedef struct metadata_source_funcs {
  int64_t (*query_by_title_and_year)(void *db, const char *item_url,
				     const char *title, int year,
				     int duration, int qtype,
                                     const char *initiator);

  int64_t (*query_by_imdb_id)(void *db, const char *item_url,
			      const char *imdb_id, int qtype,
                              const char *initiator);

  int64_t (*query_by_id)(void *db, const char *item_url, const char *id,
                         const char *initiator);

  int64_t (*query_by_episode)(void *db, const char *item_url,
			      const char *title, int season, int episode,
			      int qtype, const char *initiator);

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


metadata_t *metadata_create(void);

void metadata_destroy(metadata_t *md);

void metadata_add_stream(metadata_t *md, const char *codec,
			 int type, int streamindex,
			 const char *title,
			 const char *info, const char *isolang,
			 int disposition, int tracknum, int channels);

void metadata_to_proptree(const metadata_t *md,
			  struct prop *proproot,
			  int cleanup_streams);

metadata_t *metadata_get_video_data(const char *url);

const char *metadata_qtypestr(int qtype);

//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------


typedef struct metadata_source_query_info {
  const struct metadata_source *msqi_ms;
  int64_t msqi_id;
  int msqi_mark;
  int msqi_qtype;
  int msqi_status;
} metadata_source_query_info_t;


void metadb_init(void);

void metadb_fini(void);

void *metadb_get(void);

void metadb_close(void *db);

void metadb_metadata_write(void *db, const char *url, time_t mtime,
			   const metadata_t *md, const char *parent,
			   time_t parent_mtime,
                           metadata_index_status_t indexstatus);

int metadb_metadata_writex(void *db, const char *url, time_t mtime,
                           const metadata_t *md, const char *parent,
                           time_t parent_mtime,
                           metadata_index_status_t indexstatus);


metadata_t *metadb_metadata_get(void *db, const char *url, time_t mtime);

struct fa_dir;
struct fa_dir *metadb_metadata_scandir(void *db, const char *url,
				       time_t *mtimep);

void metadb_parent_item(void *db, const char *url, const char *parent_url);

void metadb_unparent_item(void *db, const char *url);

int metadb_item_set_preferred_ds(void *opaque, const char *url, int ds_id);

int metadb_item_get_preferred_ds(const char *url);

rstr_t *metadb_item_get_user_title(const char *url);

void metadb_item_set_user_title(const char *url, const char *title);

rstr_t *metadb_get_album_art(void *db, const char *album, const char *artist);

int metadb_get_artist_pics(void *db, const char *artist, 
			   void (*cb)(void *opaque, const char *url,
				      int width, int height),
			   void *opaque);

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
                         metadata_source_query_info_t *msqi, int num_msqi,
			 int *fixed_ds, metadata_t **mdp,
                         int only_preferred);

int64_t metadb_get_videoitem(void *db, const char *url);

void metadb_videoitem_alternatives(struct prop *p, const char *url, int dsid,
				   struct prop_sub *skipme);

int metadb_videoitem_set_preferred(void *db, const char *url, int64_t vid);

int metadb_videoitem_delete_from_ds(void *db, const char *url, int ds);

void decoration_init(void);

#define DECO_FLAGS_NO_AUTO_DESTROY  0x1
#define DECO_FLAGS_RAW_FILENAMES    0x2
#define DECO_FLAGS_NO_AUTO_SORTING  0x4

typedef struct deco_browse deco_browse_t;

deco_browse_t *decorated_browse_create(struct prop *model, struct prop_nf *pnf,
                                       struct prop *items, rstr_t *title,
                                       int flags, const char *url,
                                       const char *initiator);

// Use if DECO_FLAGS_NO_AUTO_DESTROY
void decorated_browse_destroy(deco_browse_t *db);

void mlp_init(void);

void metadata_init(void);

void metadata_bind_artistpics(struct prop *prop, rstr_t *artist);

void metadata_bind_albumart(struct prop *prop, rstr_t *artist, rstr_t *album);

metadata_lazy_video_t *metadata_bind_video_info(rstr_t *url, rstr_t *filename,
						rstr_t *imdb_id,
                                                int duration,
						struct prop *root,
						rstr_t *parent, int lonely,
						int passive,
						int year, int season,
						int episode,
                                                int manual,
                                                rstr_t *initiator);

void mlv_unbind(metadata_lazy_video_t *mlv, int cleanup);

void mlv_set_imdb_id(metadata_lazy_video_t *mlv, rstr_t *imdb_id);

void mlv_set_duration(metadata_lazy_video_t *mlv, int duration);

void mlv_set_lonely(metadata_lazy_video_t *mlv, int lonely);

int mlv_direct_query(void *db, rstr_t *url, rstr_t *filename,
                     const char *imdb_id, int duration, const char *folder,
                     int lonely);

/**
 * Browse library
 */ 

typedef enum {
  LIBRARY_QUERY_ALBUMS,
  LIBRARY_QUERY_ALBUM,
  LIBRARY_QUERY_ARTISTS,
  LIBRARY_QUERY_ARTIST,
  LIBRARY_QUERY_VIDEOS,
} library_query_t;

void metadata_browse(void *db, const char *url,
                     struct prop *nodes, struct prop *model,
                     library_query_t qtype,
                     int (*checkstop)(void *opaque), void *opaque);
