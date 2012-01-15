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

static prop_courier_t *deco_courier;
static hts_mutex_t deco_mutex;

TAILQ_HEAD(deco_item_queue, deco_item);
LIST_HEAD(deco_item_list,deco_item);
LIST_HEAD(deco_stem_list, deco_stem);

#define STEM_HASH_SIZE 503

/**
 *
 */
typedef struct deco_browse {
  prop_sub_t *db_sub;
  struct deco_item_queue db_items;

  prop_t *db_prop_contents;

  int db_total;
  int db_types[CONTENT_num];

  struct deco_stem_list db_stems[STEM_HASH_SIZE];

} deco_browse_t;


/**
 *
 */
typedef struct deco_stem {
  LIST_ENTRY(deco_stem) ds_link;
  char *ds_stem;
  struct deco_item_list ds_items;

} deco_stem_t;


/**
 *
 */
typedef struct deco_item {
  TAILQ_ENTRY(deco_item) di_link;
  deco_browse_t *di_db;

  rstr_t *di_url;

  LIST_ENTRY(deco_item) di_stem_link;
  deco_stem_t *di_ds;

  prop_t *di_root;

  prop_sub_t *di_sub_url;
  prop_sub_t *di_sub_type;

  char *di_postfix;
  contenttype_t di_type;

  prop_sub_t *di_sub_album;
  rstr_t *di_album;

} deco_item_t;


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
    if(di->di_type == CONTENT_VIDEO)
      video = di;
    
    if(di->di_type == CONTENT_IMAGE)
      image = di;
  }

  if(video && image) {
    prop_t *icon = prop_create(prop_create(video->di_root, "metadata"), "icon");
    prop_set_rstring(icon, image->di_url);
    prop_set_int(prop_create(image->di_root, "hidden"), 1);
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
    return;
  }
}

static void
album_analysis(deco_browse_t *db)
{
  deco_item_t *di;
  rstr_t *v = NULL;

  if(db->db_types[CONTENT_AUDIO] > 1 && 
     db->db_types[CONTENT_VIDEO] == 0 &&
     db->db_types[CONTENT_ARCHIVE] == 0 &&
     db->db_types[CONTENT_DIR] == 0 &&
     db->db_types[CONTENT_ALBUM] == 0 &&
     db->db_types[CONTENT_PLUGIN] == 0) {

    TAILQ_FOREACH(di, &db->db_items, di_link) {
      if(di->di_type != CONTENT_AUDIO)
	continue;
      if(di->di_album == NULL)
	return;
      
      if(v == NULL)
	v = di->di_album;
      else
	if(strcmp(rstr_get(v), rstr_get(di->di_album)))
	  return;
    }
    prop_set_string(db->db_prop_contents, "album");
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
  
  stem_analysis(db, ds);
}


/**
 *
 */
static void
di_set_album(deco_item_t *di, rstr_t *str)
{
  rstr_set(&di->di_album, str);
  album_analysis(di->di_db);
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

  db->db_types[di->di_type]--;
  di->di_type = str ? type2content(str) : CONTENT_UNKNOWN;
  db->db_types[di->di_type]++;

  if(di->di_type == CONTENT_AUDIO)
    di->di_sub_album = 
      prop_subscribe(0,
		     PROP_TAG_NAME("node", "metadata", "album"),
		     PROP_TAG_CALLBACK_RSTR, di_set_album, di,
		     PROP_TAG_NAMED_ROOT, di->di_root, "node",
		     PROP_TAG_COURIER, deco_courier,
		     NULL);

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
  prop_unsubscribe(di->di_sub_url);
  prop_unsubscribe(di->di_sub_type);
  if(di->di_sub_album != NULL)
    prop_unsubscribe(di->di_sub_album);

  TAILQ_REMOVE(&db->db_items, di, di_link);
  free(di->di_postfix);
  rstr_release(di->di_album);
  rstr_release(di->di_url);
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
  deco_browse_clear(db);
  prop_unsubscribe(db->db_sub);
  prop_ref_dec(db->db_prop_contents);
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
decorated_browse_create(prop_t *model)
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
    free(db);
  } else {
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
  hts_mutex_lock(&deco_mutex);

  while(1) {
    struct prop_notify_queue exp, nor;

    hts_mutex_unlock(&deco_mutex);
    prop_courier_wait(deco_courier, &nor, &exp, 0);
    hts_mutex_lock(&deco_mutex);

    prop_notify_dispatch(&exp);
    prop_notify_dispatch(&nor);

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
