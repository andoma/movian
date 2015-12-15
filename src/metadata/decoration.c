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
#include <stdarg.h>
#include <stdio.h>

#include "main.h"
#include "metadata.h"
#include "metadata_str.h"
#include "prop/prop.h"
#include "playinfo.h"
#include "prop/prop_nodefilter.h"
#include "db/db_support.h"
#include "db/kvstore.h"
#include "fileaccess/fileaccess.h"
#include "settings.h"


#define DECO_MODE_AUTO   0
#define DECO_MODE_MANUAL 1
#define DECO_MODE_OFF    2

LIST_HEAD(deco_browse_list, deco_browse);
TAILQ_HEAD(deco_item_queue, deco_item);
LIST_HEAD(deco_item_list, deco_item);
LIST_HEAD(deco_stem_list, deco_stem);

static prop_courier_t *deco_courier;
static hts_mutex_t deco_mutex;
static struct deco_browse_list deco_browses;
static int deco_pendings;

#define STEM_HASH_SIZE 503

/**
 *
 */
struct deco_browse {
  LIST_ENTRY(deco_browse) db_link;
  char *db_url;

  prop_sub_t *db_sub;
  struct deco_item_queue db_items;

  prop_t *db_prop_contents;
  prop_t *db_prop_model;

  int db_total;
  int db_types[CONTENT_num];

  struct deco_item_list db_items_per_ct[CONTENT_num];

  struct deco_stem_list db_stems[STEM_HASH_SIZE];

  rstr_t *db_imdb_id;

  int db_pending_flags;
#define DB_PENDING_DEFERRED_ALBUM_ANALYSIS 0x1
#define DB_PENDING_DEFERRED_VIDEO_ANALYSIS 0x2

#define DB_PENDING_DEFERRED_FULL_ANALYSIS 0xffffffff

  struct prop_nf *db_pnf;

  int db_audio_filter;

  int db_contents_mask;
#define DB_CONTENTS_IMAGES     0x1
#define DB_CONTENTS_ALBUM      0x2
#define DB_CONTENTS_TV_SEASON  0x4
#define DB_CONTENTS_TV_SERIES  0x8

  int db_current_contents;

  rstr_t *db_title;

  int db_flags;

  int db_lonely_video_item;

  int db_mode;

  struct setting *db_setting_mode;
  struct setting *db_setting_mark_all_as_seen;
  struct setting *db_setting_mark_all_as_unseen;
  struct setting *db_setting_erase_playinfo;

  rstr_t *db_initiator;
};


/**
 *
 */
typedef struct deco_stem {
  LIST_ENTRY(deco_stem) ds_link;
  char *ds_stem;
  struct deco_item_list ds_items;
  rstr_t *ds_imdb_id;

} deco_stem_t;


/**
 *
 */
typedef struct deco_item {
  TAILQ_ENTRY(deco_item) di_link;
  deco_browse_t *di_db;

  LIST_ENTRY(deco_item) di_type_link;

  LIST_ENTRY(deco_item) di_stem_link;
  deco_stem_t *di_ds;

  prop_t *di_root;
  prop_t *di_metadata;
  prop_t *di_options;

  prop_sub_t *di_sub_type;
  contenttype_t di_type;

  prop_sub_t *di_sub_url;
  rstr_t *di_url;
  char *di_postfix;

  prop_sub_t *di_sub_filename;
  rstr_t *di_filename;

  prop_sub_t *di_sub_album;
  rstr_t *di_album;

  prop_sub_t *di_sub_artist;
  rstr_t *di_artist;

  prop_sub_t *di_sub_duration;
  int di_duration;

  prop_sub_t *di_sub_series;
  rstr_t *di_series;

  prop_sub_t *di_sub_season;
  int di_season;

  metadata_lazy_video_t *di_mlv;

} deco_item_t;


static void load_nfo(deco_item_t *di);



/**
 *
 */
static rstr_t *
select_imdb_id(deco_item_t *di)
{
  // Pick IMDB ID from stem
  rstr_t *imdbid = di->di_ds->ds_imdb_id;

  // If nothing on stem and the item is lonely, use a global imdb id
  if(imdbid == NULL && di->di_db->db_lonely_video_item)
    imdbid = di->di_db->db_imdb_id;
  return imdbid;
}

/**
 *
 */
static void
insert_video_mlv(deco_item_t *di)
{
  if(di->di_url == NULL || di->di_filename == NULL)
    return;

  deco_browse_t *db = di->di_db;

  if(db->db_mode == DECO_MODE_OFF)
    return;

  int manual = db->db_mode == DECO_MODE_MANUAL;

  rstr_t *fname;

  if(db->db_flags & DECO_FLAGS_RAW_FILENAMES) {
    fname = metadata_remove_postfix_rstr(di->di_filename);
  } else {
    fname = rstr_dup(di->di_filename);
  }

  di->di_mlv = metadata_bind_video_info(di->di_url, fname,
                                        select_imdb_id(di),
					di->di_duration,
					di->di_root, db->db_title,
					db->db_lonely_video_item, 0,
					-1, -1, -1, manual,
                                        db->db_initiator);
  rstr_release(fname);
}


/**
 *
 */
static void
stem_analysis(deco_browse_t *db, deco_stem_t *ds)
{
  deco_item_t *di;
  deco_item_t *video = NULL;
  deco_item_t *image = NULL;

  LIST_FOREACH(di, &ds->ds_items, di_stem_link) {
    if(di->di_type == CONTENT_IMAGE)
      image = di;
    if(di->di_type == CONTENT_VIDEO)
      video = di;
  }


  if(video && image) {
    prop_t *p;

    p = prop_create_r(video->di_metadata, "usericon");
    prop_set_rstring(p, image->di_url);
    prop_ref_dec(p);

    prop_set(video->di_metadata, "usericon", PROP_SET_RSTRING, image->di_url);
    prop_set(image->di_root, "hidden", PROP_SET_INT, 1);
  }
}


/**
 *
 */
static void
update_contents(deco_browse_t *db)
{
  int mask = db->db_mode == DECO_MODE_AUTO ? db->db_contents_mask : 0;

  if(!(mask & DB_CONTENTS_ALBUM)) {
    prop_nf_pred_remove(db->db_pnf, db->db_audio_filter);
    db->db_audio_filter = 0;
  }


  if(mask & DB_CONTENTS_IMAGES) {
    if(db->db_current_contents == DB_CONTENTS_IMAGES)
      return;
    db->db_current_contents = DB_CONTENTS_IMAGES;
    prop_set_string(db->db_prop_contents, "images");
    if(!(db->db_flags & DECO_FLAGS_NO_AUTO_SORTING)) {
      prop_nf_sort(db->db_pnf, "node.metadata.timestamp", 0, 1, NULL, 0);
      prop_nf_sort(db->db_pnf, NULL, 0, 2, NULL, 0);
    }
    return;
  }

  if(mask & DB_CONTENTS_ALBUM) {
    if(db->db_current_contents == DB_CONTENTS_ALBUM)
      return;
    db->db_current_contents = DB_CONTENTS_ALBUM;

    prop_set_string(db->db_prop_contents, "album");

    if(!(db->db_flags & DECO_FLAGS_NO_AUTO_SORTING)) {
      prop_nf_sort(db->db_pnf, "node.metadata.track", 0, 1, NULL, 0);
      prop_nf_sort(db->db_pnf, NULL, 0, 2, NULL, 0);

      if(!db->db_audio_filter)
        db->db_audio_filter = 
          prop_nf_pred_str_add(db->db_pnf, "node.type", 
                               PROP_NF_CMP_NEQ, "audio", NULL,
                               PROP_NF_MODE_EXCLUDE);
    }
    return;
  }

  if(mask & DB_CONTENTS_TV_SEASON) {
    if(db->db_current_contents == DB_CONTENTS_TV_SEASON)
      return;
    db->db_current_contents = DB_CONTENTS_TV_SEASON;

    prop_set_string(db->db_prop_contents, "tvseason");
    if(!(db->db_flags & DECO_FLAGS_NO_AUTO_SORTING)) {
      prop_nf_sort(db->db_pnf, "node.metadata.episode.number", 0, 1, NULL, 0);
      prop_nf_sort(db->db_pnf, NULL, 0, 2, NULL, 0);
    }
    return;
  }

  db->db_current_contents = 0;
  prop_set_void(db->db_prop_contents);
  prop_nf_sort(db->db_pnf, NULL, 0, 1, NULL, 0);
  prop_nf_sort(db->db_pnf, NULL, 0, 2, NULL, 0);
}


/**
 *
 */
static void
type_analysis(deco_browse_t *db)
{
  if(db->db_types[CONTENT_IMAGE] * 4 > db->db_total * 3) {
    db->db_contents_mask |= DB_CONTENTS_IMAGES;
  } else {
    db->db_contents_mask &= ~DB_CONTENTS_IMAGES;
  }

  update_contents(db);
}


/**
 *
 */
typedef struct deco_artist {
  LIST_ENTRY(deco_artist) da_link;
  rstr_t *da_artist;
  int da_count;
} deco_artist_t;


/**
 *
 */
static int 
artist_compar(const void *A, const void *B)
{
  const deco_artist_t *a = *(const deco_artist_t **)A;
  const deco_artist_t *b = *(const deco_artist_t **)B;
  return b->da_count - a->da_count;
}


/**
 *
 */
static void
album_analysis(deco_browse_t *db)
{
  deco_item_t *di;
  rstr_t *v = NULL;

  LIST_HEAD(, deco_artist) artists;
  deco_artist_t *da;
  int artist_count = 0;
  int item_count = 0;


  LIST_INIT(&artists);
  db->db_contents_mask &= ~DB_CONTENTS_ALBUM;

  if(!(db->db_types[CONTENT_AUDIO] > 1 && 
       db->db_types[CONTENT_VIDEO] == 0 &&
       db->db_types[CONTENT_ARCHIVE] == 0 &&
       db->db_types[CONTENT_DIR] == 0 &&
       db->db_types[CONTENT_ALBUM] == 0 &&
       db->db_types[CONTENT_PLUGIN] == 0))
    return;
  
  LIST_FOREACH(di, &db->db_items_per_ct[CONTENT_AUDIO], di_type_link) {
    if(di->di_album == NULL)
      goto cleanup;


    item_count++;

    if(v == NULL)
      v = di->di_album;
    else if(strcmp(rstr_get(v), rstr_get(di->di_album)))
      goto cleanup;
    
    if(di->di_artist == NULL)
      continue;

    LIST_FOREACH(da, &artists, da_link) {
      if(!strcmp(rstr_get(da->da_artist), rstr_get(di->di_artist)))
	break;
    }
    
    if(da == NULL) {
      da = malloc(sizeof(deco_artist_t));
      da->da_count = 1;
      da->da_artist = di->di_artist;
      artist_count++;
    } else {
      da->da_count++;
      LIST_REMOVE(da, da_link);
    }
    LIST_INSERT_HEAD(&artists, da, da_link);
  }

  db->db_contents_mask |= DB_CONTENTS_ALBUM;

  prop_t *m = prop_create_r(db->db_prop_model, "metadata");

  prop_set(m, "album_name", PROP_SET_RSTRING, v);

  if(artist_count > 0) {
    deco_artist_t **vec = malloc(artist_count * sizeof(deco_artist_t *));
    int i;
    da = LIST_FIRST(&artists);
    for(i = 0; i < artist_count; i++) {
      vec[i] = da;
      da = LIST_NEXT(da, da_link);
    }
    qsort(vec, artist_count, sizeof(deco_artist_t *), artist_compar);
    da = vec[0];
    free(vec);

    if(da->da_count * 2 >= item_count) {
      prop_set(m, "album_name", PROP_SET_RSTRING, da->da_artist);
      
      prop_t *p = prop_create_r(m, "album_art");
      metadata_bind_albumart(p, da->da_artist, v);
      prop_ref_dec(p);
    }
  }

  prop_ref_dec(m);

 cleanup:
  while((da = LIST_FIRST(&artists)) != NULL) {
    LIST_REMOVE(da, da_link);
    free(da);
  }
}


/**
 *
 */
static void
video_analysis(deco_browse_t *db)
{
  deco_item_t *di;
  int reasonable_video_items = 0;

  const int real_video_duration_threshold = METADATA_DURATION_LIMIT;

  LIST_FOREACH(di, &db->db_items_per_ct[CONTENT_VIDEO], di_type_link) {
    if(di->di_duration > real_video_duration_threshold) {
      reasonable_video_items++;
    }
  }

  db->db_lonely_video_item = reasonable_video_items <= 1;

  METADATA_TRACE("Analyzed '%s' Found %d video items > %d seconds",
                 db->db_url, reasonable_video_items,
                 real_video_duration_threshold);

  LIST_FOREACH(di, &db->db_items_per_ct[CONTENT_VIDEO], di_type_link) {

    if(di->di_mlv == NULL) {

      insert_video_mlv(di);

    } else {

      // The MLV code is responsible for supressing any assignments that
      // are equal to current values

      mlv_set_duration(di->di_mlv, di->di_duration);
      mlv_set_lonely(di->di_mlv,   db->db_lonely_video_item);
      mlv_set_imdb_id(di->di_mlv,  select_imdb_id(di));

    }
  }

  const char *series = NULL;
  int season = -1;

  LIST_FOREACH(di, &db->db_items_per_ct[CONTENT_VIDEO], di_type_link) {
    if(di->di_series == NULL) {
      series = NULL;
      break;
    }

    if(series == NULL)
      series = rstr_get(di->di_series);
    else if(strcmp(series, rstr_get(di->di_series))) {
      series = NULL;
      break;
    }
  }


  if(series != NULL) {

    LIST_FOREACH(di, &db->db_items_per_ct[CONTENT_VIDEO], di_type_link) {
      if(di->di_season == 0) {
	season = -1;
	break;
      }

      if(season == -1)
	season = di->di_season;
      else if(season != di->di_season) {
	season = -1;
	break;
      }
    }
  }
  
  di = LIST_FIRST(&db->db_items_per_ct[CONTENT_VIDEO]);

  prop_t *x = prop_create_r(db->db_prop_model, "season");
  if(season != -1 && series) {
    assert(di != NULL);
    db->db_contents_mask |= DB_CONTENTS_TV_SEASON;
    prop_t *y = prop_create_r(di->di_metadata, "season");
    prop_link(y, x);
    prop_ref_dec(y);
  } else {
    prop_unlink(x);
    db->db_contents_mask &= ~DB_CONTENTS_TV_SEASON;
  }
  prop_ref_dec(x);


  x = prop_create_r(db->db_prop_model, "series");
  if(series) {
    assert(di != NULL);
    db->db_contents_mask |= DB_CONTENTS_TV_SERIES;
    prop_t *y = prop_create_r(di->di_metadata, "series");
    prop_link(y, x);
    prop_ref_dec(y);
  } else {
    prop_unlink(x);
    db->db_contents_mask &= ~DB_CONTENTS_TV_SERIES;
  }
  prop_ref_dec(x);

}



/**
 *
 */
static deco_stem_t *
stem_get(deco_browse_t *db, const char *str)
{
  const unsigned int hash = mystrhash(str) % STEM_HASH_SIZE;
  deco_stem_t *ds;
  LIST_FOREACH(ds, &db->db_stems[hash], ds_link) {
    if(!strcmp(ds->ds_stem, str))
      return ds;
  }
  
  ds = calloc(1, sizeof(deco_stem_t));
  ds->ds_stem = strdup(str);
  LIST_INSERT_HEAD(&db->db_stems[hash], ds, ds_link);
  return ds;
}


/**
 *
 */
static void
stem_release(deco_stem_t *ds)
{
  if(LIST_FIRST(&ds->ds_items) != NULL)
    return;
  LIST_REMOVE(ds, ds_link);
  free(ds->ds_stem);
  rstr_release(ds->ds_imdb_id);
  free(ds);
}



/**
 *
 */
static void
di_set_url(deco_item_t *di, rstr_t *str)
{
  char *s, *p;
  deco_browse_t *db = di->di_db;

  rstr_set(&di->di_url, str);

  if(di->di_ds != NULL) {
    LIST_REMOVE(di, di_stem_link);
    stem_release(di->di_ds);
    di->di_ds = NULL;
    free(di->di_postfix);
    di->di_postfix = NULL;
  }

  if(str == NULL)
    return;

  s = strdup(rstr_get(str));
  
  p = strrchr(s, '.');
  if(p != NULL) {
    if(strchr(p, '/') == NULL)
      *p++ = 0;
    else
      p = NULL;
  }

  deco_stem_t *ds = stem_get(db, s);
  
  LIST_INSERT_HEAD(&ds->ds_items, di, di_stem_link);
  di->di_ds = ds;
  di->di_postfix = p ? strdup(p) : NULL;
  free(s);

  if(di->di_postfix != NULL) {
    if(!strcasecmp(di->di_postfix, "nfo")) {
      load_nfo(di);
      di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_VIDEO_ANALYSIS;
      deco_pendings = 1;
      return;
    }
  }
  stem_analysis(db, ds);
}


/**
 *
 */
static void
di_set_album(deco_item_t *di, rstr_t *str)
{
  rstr_set(&di->di_album, str);
  di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_ALBUM_ANALYSIS;
  deco_pendings = 1;
}


/**
 *
 */
static void
di_set_artist(deco_item_t *di, rstr_t *str)
{
  rstr_set(&di->di_artist, str);
  di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_ALBUM_ANALYSIS;
  deco_pendings = 1;
}


/**
 *
 */
static void
di_set_filename(deco_item_t *di, rstr_t *str)
{
  rstr_set(&di->di_filename, str);

  di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_VIDEO_ANALYSIS;
  deco_pendings = 1;
}


/**
 *
 */
static void
di_set_duration(deco_item_t *di, int duration)
{
  di->di_duration = duration;
  if(di->di_type == CONTENT_VIDEO) {
    di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_VIDEO_ANALYSIS;
    deco_pendings = 1;
  }
}



/**
 *
 */
static void
di_set_series(deco_item_t *di, rstr_t *str)
{
  rstr_set(&di->di_series, str);
  di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_VIDEO_ANALYSIS;
  deco_pendings = 1;
}



/**
 *
 */
static void
di_set_season(deco_item_t *di, int v)
{
  di->di_season = v;
  di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_VIDEO_ANALYSIS;
  deco_pendings = 1;
}


/**
 *
 */
static void
di_set_type(deco_item_t *di, const char *str)
{
  deco_browse_t *db = di->di_db;

  if(di->di_sub_album != NULL) {
    prop_unsubscribe(di->di_sub_album);
    rstr_set(&di->di_album, NULL);
    di->di_sub_album = NULL;
  }

  if(di->di_sub_artist != NULL) {
    prop_unsubscribe(di->di_sub_artist);
    rstr_set(&di->di_artist, NULL);
    di->di_sub_artist = NULL;
  }

  if(di->di_sub_duration != NULL) {
    prop_unsubscribe(di->di_sub_duration);
    di->di_duration = 0;
    di->di_sub_duration = NULL;
  }

  if(di->di_sub_series != NULL) {
    prop_unsubscribe(di->di_sub_series);
    rstr_set(&di->di_series, NULL);
    di->di_sub_series = NULL;
  }

  if(di->di_sub_season != NULL) {
    prop_unsubscribe(di->di_sub_season);
    di->di_season = 0;
    di->di_sub_season = NULL;
  }

  db->db_types[di->di_type]--;
  LIST_REMOVE(di, di_type_link);

  di->di_type = str ? type2content(str) : CONTENT_UNKNOWN;
  db->db_types[di->di_type]++;
  LIST_INSERT_HEAD(&db->db_items_per_ct[di->di_type], di, di_type_link);


  switch(di->di_type) {

  case CONTENT_AUDIO:
    di->di_sub_album = 
      prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_NAME("node", "metadata", "album"),
		     PROP_TAG_CALLBACK_RSTR, di_set_album, di,
		     PROP_TAG_NAMED_ROOT, di->di_root, "node",
		     PROP_TAG_COURIER, deco_courier,
		     NULL);

    di->di_sub_artist = 
      prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_NAME("node", "metadata", "artist"),
		     PROP_TAG_CALLBACK_RSTR, di_set_artist, di,
		     PROP_TAG_NAMED_ROOT, di->di_root, "node",
		     PROP_TAG_COURIER, deco_courier,
		     NULL);
    di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_ALBUM_ANALYSIS;
    deco_pendings = 1;
    break;

  case CONTENT_VIDEO:
    di->di_sub_duration = 
      prop_subscribe(0,
		     PROP_TAG_NAME("node", "metadata", "duration"),
		     PROP_TAG_CALLBACK_INT, di_set_duration, di,
		     PROP_TAG_NAMED_ROOT, di->di_root, "node",
		     PROP_TAG_COURIER, deco_courier,
		     NULL);

    di->di_sub_series = 
      prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_NAME("node", "metadata", "series", "title"),
		     PROP_TAG_CALLBACK_RSTR, di_set_series, di,
		     PROP_TAG_NAMED_ROOT, di->di_root, "node",
		     PROP_TAG_COURIER, deco_courier,
		     NULL);

    di->di_sub_season = 
      prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_NAME("node", "metadata", "season", "number"),
		     PROP_TAG_CALLBACK_INT, di_set_season, di,
		     PROP_TAG_NAMED_ROOT, di->di_root, "node",
		     PROP_TAG_COURIER, deco_courier,
		     NULL);

    di->di_db->db_pending_flags |= DB_PENDING_DEFERRED_VIDEO_ANALYSIS;
    deco_pendings = 1;
    break;

  default:
    break;
  }


  type_analysis(db);
  if(di->di_ds != NULL)
    stem_analysis(db, di->di_ds);
}


/**
 *
 */
static void
deco_browse_add_node(deco_browse_t *db, prop_t *p, deco_item_t *before)
{
  deco_item_t *di = calloc(1, sizeof(deco_item_t));
  di->di_db = db;
  di->di_root = prop_ref_inc(p);
  di->di_metadata = prop_create_r(p, "metadata");
  di->di_options = prop_create_r(p, "options");

  db->db_total++;
  di->di_type = CONTENT_UNKNOWN;
  db->db_types[di->di_type]++;
  LIST_INSERT_HEAD(&db->db_items_per_ct[di->di_type], di, di_type_link);

  prop_tag_set(p, db, di);

  if(before) {
    TAILQ_INSERT_BEFORE(before, di, di_link);
  } else {
    TAILQ_INSERT_TAIL(&db->db_items, di, di_link);
  }

  di->di_sub_url = 
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "url"),
		   PROP_TAG_CALLBACK_RSTR, di_set_url, di,
		   PROP_TAG_NAMED_ROOT, p, "node",
		   PROP_TAG_COURIER, deco_courier,
		   NULL);

  di->di_sub_filename = 
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "filename"),
		   PROP_TAG_CALLBACK_RSTR, di_set_filename, di,
		   PROP_TAG_NAMED_ROOT, p, "node",
		   PROP_TAG_COURIER, deco_courier,
		   NULL);

  di->di_sub_type = 
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "type"),
		   PROP_TAG_CALLBACK_STRING, di_set_type, di,
		   PROP_TAG_NAMED_ROOT, p, "node",
		   PROP_TAG_COURIER, deco_courier,
		   NULL);
}


/**
 *
 */
static void
deco_browse_add_nodes(deco_browse_t *db, prop_vec_t *pv, deco_item_t *before)
{
  int i;

  for(i = 0; i < prop_vec_len(pv); i++) {
    prop_t *p = prop_vec_get(pv, i);
    deco_browse_add_node(db, p, before);
  }
}


/**
 *
 */
static void
deco_item_destroy(deco_browse_t *db, deco_item_t *di)
{
  if(di->di_ds != NULL) {
    LIST_REMOVE(di, di_stem_link);
    stem_release(di->di_ds);
  }

  LIST_REMOVE(di, di_type_link);
  db->db_types[di->di_type]--;
  db->db_total--;
  prop_ref_dec(di->di_root);
  prop_ref_dec(di->di_metadata);
  prop_ref_dec(di->di_options);
  prop_unsubscribe(di->di_sub_url);
  prop_unsubscribe(di->di_sub_filename);
  prop_unsubscribe(di->di_sub_type);
  prop_unsubscribe(di->di_sub_album);
  prop_unsubscribe(di->di_sub_artist);
  prop_unsubscribe(di->di_sub_duration);
  prop_unsubscribe(di->di_sub_series);
  prop_unsubscribe(di->di_sub_season);

  TAILQ_REMOVE(&db->db_items, di, di_link);
  free(di->di_postfix);
  rstr_release(di->di_album);
  rstr_release(di->di_artist);
  rstr_release(di->di_url);
  rstr_release(di->di_filename);

  if(di->di_mlv != NULL)
    mlv_unbind(di->di_mlv, 0);

  free(di);
}



static void
deco_browse_del_node(deco_browse_t *db, deco_item_t *di)
{
  db->db_pending_flags |= DB_PENDING_DEFERRED_FULL_ANALYSIS;
  deco_item_destroy(db, di);
  deco_pendings = 1;
}


/**
 *
 */
static void
deco_browse_clear(deco_browse_t *db)
{
  deco_item_t *di;

  while((di = TAILQ_FIRST(&db->db_items)) != NULL) {
    prop_tag_clear(di->di_root, db);
    deco_item_destroy(db, di);
  }
}


/**
 *
 */
static void
deco_browse_destroy(deco_browse_t *db)
{
  prop_nf_release(db->db_pnf);
  deco_browse_clear(db);
  setting_destroy(db->db_setting_mode);
  setting_destroy(db->db_setting_mark_all_as_seen);
  setting_destroy(db->db_setting_mark_all_as_unseen);
  setting_destroy(db->db_setting_erase_playinfo);
  prop_unsubscribe(db->db_sub);
  prop_ref_dec(db->db_prop_contents);
  prop_ref_dec(db->db_prop_model);
  rstr_release(db->db_imdb_id);
  LIST_REMOVE(db, db_link);
  rstr_release(db->db_title);
  free(db->db_url);
  rstr_release(db->db_initiator);
  free(db);
}


/**
 *
 */
static void
deco_browse_node_cb(void *opaque, prop_event_t event, ...)
{
  deco_browse_t *db = opaque;
  prop_t *p1, *p2;
  prop_vec_t *pv;

  va_list ap;
  va_start(ap, event);

  switch(event) {

  case PROP_ADD_CHILD:
    deco_browse_add_node(db, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    deco_browse_add_node(db, p1, prop_tag_get(p2, db));
    break;

  case PROP_ADD_CHILD_VECTOR:
    deco_browse_add_nodes(db, va_arg(ap, prop_vec_t *), NULL);
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    deco_browse_add_nodes(db, pv, prop_tag_get(va_arg(ap, prop_t *), db));
    break;

  case PROP_DEL_CHILD:
    p1 = va_arg(ap, prop_t *);
    deco_browse_del_node(db, prop_tag_clear(p1, db));
    break;
    
  case PROP_SET_DIR:
  case PROP_WANT_MORE_CHILDS:
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
    break;

  case PROP_SET_VOID:
    deco_browse_clear(db);
    break;

  case PROP_DESTROYED:
    deco_browse_destroy(db);
    break;

  default:
    break;
  }
  va_end(ap);
}




/**
 *
 */
void
decorated_browse_destroy(deco_browse_t *db)
{
  hts_mutex_lock(&deco_mutex);
  deco_browse_destroy(db);
  hts_mutex_unlock(&deco_mutex);
}

/**
 *
 */
static void
erase_playinfo(void *opaque)
{
  deco_browse_t *db = opaque;
  deco_item_t *di;
  const char **urls = malloc(sizeof(const char *) * db->db_total);

  int i = 0;
  TAILQ_FOREACH(di, &db->db_items, di_link)
    urls[i++] = rstr_get(di->di_url);

  playinfo_erase_urls(urls, db->db_total);
  free(urls);
}

/**
 *
 */
static void
mark_content_as(deco_browse_t *db, int content_type, int seen)
{
  deco_item_t *di;
  int num_videos = db->db_types[content_type];
  const char **urls = malloc(sizeof(const char *) * num_videos);

  int i = 0;
  LIST_FOREACH(di, &db->db_items_per_ct[content_type], di_type_link)
    urls[i++] = rstr_get(di->di_url);

  playinfo_mark_urls_as(urls, num_videos, seen);
  free(urls);
}


/**
 *
 */
static void
mark_all_as_seen(void *opaque)
{
  deco_browse_t *db = opaque;
  mark_content_as(db, CONTENT_VIDEO, 1);
}


/**
 *
 */
static void
mark_all_as_unseen(void *opaque)
{
  deco_browse_t *db = opaque;
  mark_content_as(db, CONTENT_VIDEO, 0);
}


/**
 *
 */
static void
set_mode(void *opaque, const char *str)
{
  deco_browse_t *db = opaque;
  deco_item_t *di;

  int v = atoi(str);

  if(v == db->db_mode)
    return;

  db->db_mode = v;

  LIST_FOREACH(di, &db->db_items_per_ct[CONTENT_VIDEO], di_type_link) {
    if(di->di_mlv != NULL) {
      mlv_unbind(di->di_mlv, 1);
      di->di_mlv = NULL;
    }
  }

  switch(v) {

  case DECO_MODE_AUTO:
  case DECO_MODE_MANUAL:
    deco_pendings = 1;
    db->db_pending_flags |= DB_PENDING_DEFERRED_FULL_ANALYSIS;
    break;

  case DECO_MODE_OFF:
    break;
  }
  update_contents(db);
}


/**
 *
 */
deco_browse_t *
decorated_browse_create(prop_t *model, struct prop_nf *pnf, prop_t *items,
			rstr_t *title, int flags, const char *url,
                        const char *initiator)
{

  deco_browse_t *db = calloc(1, sizeof(deco_browse_t));
  db->db_initiator = rstr_alloc(initiator);
  db->db_url = strdup(url);
  TAILQ_INIT(&db->db_items);

  hts_mutex_lock(&deco_mutex);

  db->db_sub =
    prop_subscribe(flags & DECO_FLAGS_NO_AUTO_DESTROY ?
                   0 : PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_CALLBACK, deco_browse_node_cb, db,
                   PROP_TAG_ROOT, items,
                   PROP_TAG_COURIER, deco_courier,
                   NULL);

  db->db_pnf = prop_nf_retain(pnf);
  LIST_INSERT_HEAD(&deco_browses, db, db_link);
  db->db_prop_model = prop_ref_inc(model);
  db->db_prop_contents = prop_create_r(model, "contents");
  db->db_title = rstr_dup(title);
  db->db_flags = flags;

  prop_t *options = prop_create_r(model, "options");

  db->db_setting_mode =
    setting_create(SETTING_MULTIOPT, options,
                   SETTINGS_INITIAL_UPDATE | SETTINGS_RAW_NODES,
                   SETTING_TITLE(_p("Metadata mode")),
                   SETTING_KVSTORE(db->db_url, "metadatamode"),
                   SETTING_COURIER(deco_courier),
                   SETTING_CALLBACK(set_mode, db),
                   SETTING_OPTION("0", _p("Automatic")),
                   SETTING_OPTION("1", _p("Manual")),
                   SETTING_OPTION("2", _p("Off")),
                   NULL);

  db->db_setting_mark_all_as_seen =
    setting_create(SETTING_ACTION, options,
                   SETTINGS_INITIAL_UPDATE | SETTINGS_RAW_NODES,
                   SETTING_TITLE(_p("Mark all as seen")),
                   SETTING_COURIER(deco_courier),
                   SETTING_CALLBACK(mark_all_as_seen, db),
                   NULL);

  db->db_setting_mark_all_as_unseen =
    setting_create(SETTING_ACTION, options,
                   SETTINGS_INITIAL_UPDATE | SETTINGS_RAW_NODES,
                   SETTING_TITLE(_p("Mark all as unseen")),
                   SETTING_COURIER(deco_courier),
                   SETTING_CALLBACK(mark_all_as_unseen, db),
                   NULL);


  db->db_setting_erase_playinfo =
    setting_create(SETTING_ACTION, options,
                   SETTINGS_INITIAL_UPDATE | SETTINGS_RAW_NODES,
                   SETTING_TITLE(_p("Erase all playback info")),
                   SETTING_COURIER(deco_courier),
                   SETTING_CALLBACK(erase_playinfo, db),
                   NULL);
  prop_ref_dec(options);
  hts_mutex_unlock(&deco_mutex);
  return db;
}


/**
 *
 */
static void *
deco_thread(void *aux)
{
  int r;
  hts_mutex_lock(&deco_mutex);

  while(1) {
    struct prop_notify_queue q;

    int do_timo = 0;
    if(deco_pendings)
      do_timo = 150;

    hts_mutex_unlock(&deco_mutex);
    r = prop_courier_wait(deco_courier, &q, do_timo);
    hts_mutex_lock(&deco_mutex);

    prop_notify_dispatch(&q, 0);

    if(r && deco_pendings) {
      deco_pendings = 0;
      deco_browse_t *db;

      LIST_FOREACH(db, &deco_browses, db_link) {
	if(db->db_pending_flags && db->db_mode == DECO_MODE_AUTO) {

	  if(db->db_pending_flags & DB_PENDING_DEFERRED_ALBUM_ANALYSIS)
	    album_analysis(db);

	  if(db->db_pending_flags & DB_PENDING_DEFERRED_VIDEO_ANALYSIS)
	    video_analysis(db);

	  db->db_pending_flags = 0;
	  update_contents(db);
	}

      }
    }
  }
  return NULL;
}


/**
 *
 */
void
decoration_init(void)
{
  hts_mutex_init(&deco_mutex);
  deco_courier = prop_courier_create_waitable();

  hts_thread_create_detached("deco", deco_thread, NULL, THREAD_PRIO_METADATA);
}


/**
 *
 */
static void
load_nfo(deco_item_t *di)
{
  buf_t *b = fa_load(rstr_get(di->di_url), NULL);
  if(b == NULL)
    return;

  const char *tt = strstr(buf_cstr(b), "http://www.imdb.com/title/tt");
  if(tt != NULL) {
    tt += strlen("http://www.imdb.com/title/");

    rstr_t *r = rstr_allocl(tt, strspn(tt, "t0123456789"));

    METADATA_TRACE("Found IMDB id %s in .nfo file '%s' applying for stem '%s' "
                   "and directory '%s'",
                   rstr_get(r),
                   rstr_get(di->di_url),
                   di->di_ds->ds_stem,
                   di->di_db->db_url);

    rstr_set(&di->di_ds->ds_imdb_id, r);
    rstr_set(&di->di_db->db_imdb_id, r);
    
    rstr_release(r);
  }
  buf_release(b);
}
