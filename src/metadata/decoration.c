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

#include <stdarg.h>
#include <stdio.h>

#include "showtime.h"
#include "metadata.h"
#include "prop/prop.h"
#include "prop/prop_nodefilter.h"
#include "db/db_support.h"
#include "fileaccess/fileaccess.h"

LIST_HEAD(deco_browse_list, deco_browse);
TAILQ_HEAD(deco_item_queue, deco_item);
LIST_HEAD(deco_item_list,deco_item);
LIST_HEAD(deco_stem_list, deco_stem);

static prop_courier_t *deco_courier;
static hts_mutex_t deco_mutex;
static struct deco_browse_list deco_browses;
static int deco_pendings;

#define STEM_HASH_SIZE 503

/**
 *
 */
typedef struct deco_browse {
  LIST_ENTRY(deco_browse) db_link;
  prop_sub_t *db_sub;
  struct deco_item_queue db_items;

  prop_t *db_prop_contents;
  prop_t *db_prop_model;

  int db_total;
  int db_types[CONTENT_num];

  struct deco_stem_list db_stems[STEM_HASH_SIZE];

  rstr_t *db_imdb_id;

  int db_pending_flags;
#define DB_PENDING_ALBUM_ANALYSIS 0x1

  struct prop_nf *db_pnf;

} deco_browse_t;


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

  metadata_lazy_prop_t *di_mlp;

} deco_item_t;


static void load_nfo(deco_item_t *di);


/**
 *
 */
static void
analyze_video(deco_item_t *di)
{
  if(di->di_url == NULL || di->di_duration == 0)
    return;
  
  deco_browse_t *db = di->di_db;
  rstr_t *title = NULL;
  int year = 0;

  if(di->di_filename != NULL)
    title = metadata_filename_to_title(rstr_get(di->di_filename), &year);

  metadata_bind_movie_info(&di->di_mlp, di->di_metadata,
			   di->di_url, title, year,
			   di->di_ds->ds_imdb_id ?: db->db_imdb_id,
			   di->di_duration,
			   di->di_options);
  
  rstr_release(title);
}



/**
 *
 */
static void
analyze_item(deco_item_t *di)
{
  switch(di->di_type) {
  case CONTENT_VIDEO:
    analyze_video(di);
    break;
  default:
    break;
  }
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

    p = prop_create_r(video->di_metadata, "fallbackicon");
    prop_set_rstring(p, image->di_url);
    prop_ref_dec(p);

    p = prop_create_r(image->di_root, "hidden");
    prop_set_int(p, 1);
    prop_ref_dec(p);
  }
}


/**
 *
 */
static void
type_analysis(deco_browse_t *db)
{
  if(db->db_types[CONTENT_IMAGE] * 4 > db->db_total * 3) {
    prop_set_string(db->db_prop_contents, "images");
    prop_nf_sort(db->db_pnf, "node.metadata.timestamp", 0, 1, NULL, 0);
    return;
  }
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

  db->db_pending_flags &= ~DB_PENDING_ALBUM_ANALYSIS;

  LIST_INIT(&artists);

  if(!(db->db_types[CONTENT_AUDIO] > 1 && 
       db->db_types[CONTENT_VIDEO] == 0 &&
       db->db_types[CONTENT_ARCHIVE] == 0 &&
       db->db_types[CONTENT_DIR] == 0 &&
       db->db_types[CONTENT_ALBUM] == 0 &&
       db->db_types[CONTENT_PLUGIN] == 0)) 
    return;

  TAILQ_FOREACH(di, &db->db_items, di_link) {
    if(di->di_type != CONTENT_AUDIO)
      continue;
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

  prop_set_string(db->db_prop_contents, "album");

  prop_nf_sort(db->db_pnf, "node.metadata.track", 0, 1, NULL, 1);

  prop_t *m = prop_create_r(db->db_prop_model, "metadata");
  prop_t *p;

  p = prop_create_r(m, "album_name");
  prop_set_rstring(p, v);
  prop_ref_dec(p);

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
      p = prop_create_r(m, "artist_name");
      prop_set_rstring(p, da->da_artist);
      prop_ref_dec(p);
      
      p = prop_create_r(m, "album_art");
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

      TAILQ_FOREACH(di, &db->db_items, di_link) {
	if(di->di_type == CONTENT_VIDEO)
	  analyze_video(di);
      }
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
  di->di_db->db_pending_flags |= DB_PENDING_ALBUM_ANALYSIS;
  deco_pendings = 1;
}


/**
 *
 */
static void
di_set_artist(deco_item_t *di, rstr_t *str)
{
  rstr_set(&di->di_artist, str);
  di->di_db->db_pending_flags |= DB_PENDING_ALBUM_ANALYSIS;
  deco_pendings = 1;
}


/**
 *
 */
static void
di_set_filename(deco_item_t *di, rstr_t *str)
{
  rstr_set(&di->di_filename, str);
  analyze_item(di);
}


/**
 *
 */
static void
di_set_duration(deco_item_t *di, int duration)
{
  di->di_duration = duration;
  analyze_item(di);
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

  db->db_types[di->di_type]--;
  di->di_type = str ? type2content(str) : CONTENT_UNKNOWN;
  db->db_types[di->di_type]++;


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
    break;

  case CONTENT_VIDEO:
    di->di_sub_duration = 
      prop_subscribe(0,
		     PROP_TAG_NAME("node", "metadata", "duration"),
		     PROP_TAG_CALLBACK_INT, di_set_duration, di,
		     PROP_TAG_NAMED_ROOT, di->di_root, "node",
		     PROP_TAG_COURIER, deco_courier,
		     NULL);

  default:
    break;
  }


  type_analysis(db);
  if(di->di_ds != NULL)
    stem_analysis(db, di->di_ds);
  analyze_item(di);
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
  db->db_types[CONTENT_UNKNOWN]++;

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

  TAILQ_REMOVE(&db->db_items, di, di_link);
  free(di->di_postfix);
  rstr_release(di->di_album);
  rstr_release(di->di_artist);
  rstr_release(di->di_url);
  rstr_release(di->di_filename);
  
  if(di->di_mlp != NULL)
    metadata_unbind(di->di_mlp);

  free(di);
}



static void
deco_browse_del_node(deco_browse_t *db, deco_item_t *di)
{
  deco_item_destroy(db, di);
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
  prop_unsubscribe(db->db_sub);
  prop_ref_dec(db->db_prop_contents);
  prop_ref_dec(db->db_prop_model);
  rstr_release(db->db_imdb_id);
  LIST_REMOVE(db, db_link);
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
    break;

  case PROP_SET_VOID:
    deco_browse_clear(db);
    break;

  case PROP_DESTROYED:
    deco_browse_destroy(db);
    break;

  default:
    printf("Cant handle event %d\n", event);
    abort();
  }
  va_end(ap);
}


/**
 *
 */
void
decorated_browse_create(prop_t *model, struct prop_nf *pnf)
{
  prop_t *src = prop_create(model, "source");

  hts_mutex_lock(&deco_mutex);

  deco_browse_t *db = calloc(1, sizeof(deco_browse_t));
  TAILQ_INIT(&db->db_items);
  db->db_sub = prop_subscribe(PROP_SUB_TRACK_DESTROY,
			      PROP_TAG_CALLBACK, deco_browse_node_cb, db,
			      PROP_TAG_ROOT, src,
			      PROP_TAG_COURIER, deco_courier,
			      NULL);

  if(db->db_sub == NULL) {
    prop_nf_release(pnf);
    free(db);
  } else {
    db->db_pnf = pnf;
    LIST_INSERT_HEAD(&deco_browses, db, db_link);
    db->db_prop_model = prop_ref_inc(model);
    db->db_prop_contents = prop_ref_inc(prop_create(model, "contents"));
  }

  hts_mutex_unlock(&deco_mutex);
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
    struct prop_notify_queue exp, nor;

    int do_timo = 0;
    if(deco_pendings)
      do_timo = 50;

    hts_mutex_unlock(&deco_mutex);
    r = prop_courier_wait(deco_courier, &nor, &exp, do_timo);
    hts_mutex_lock(&deco_mutex);

    prop_notify_dispatch(&exp);
    prop_notify_dispatch(&nor);

    if(r && deco_pendings) {
      deco_pendings = 0;
      deco_browse_t *db;

      LIST_FOREACH(db, &deco_browses, db_link) {
	if(db->db_pending_flags & DB_PENDING_ALBUM_ANALYSIS)
	  album_analysis(db);
	db->db_pending_flags = 0;
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

  hts_thread_create_detached("deco", deco_thread, NULL, THREAD_PRIO_LOW);
}


/**
 *
 */
static void
load_nfo(deco_item_t *di)
{
  char *buf = fa_load(rstr_get(di->di_url), NULL, NULL, NULL, 0, NULL, 0,
		      NULL, NULL);
  if(buf == NULL)
    return;

  const char *tt = strstr(buf, "http://www.imdb.com/title/tt");
  if(tt != NULL) {
    tt += strlen("http://www.imdb.com/title/");

    rstr_t *r = rstr_allocl(tt, strspn(tt, "t0123456789"));

    rstr_set(&di->di_ds->ds_imdb_id, r);
    rstr_set(&di->di_db->db_imdb_id, r);
    
    rstr_release(r);
  }
  free(buf);
}
