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

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <libavformat/avformat.h>

#include "prop/prop.h"
#include "prop/prop_concat.h"
#include "ext/sqlite/sqlite3.h"

#include "showtime.h"
#include "media.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/string.h"

#include "api/lastfm.h"

#include "metadata.h"
#include "fileaccess/fileaccess.h"

#include "db/db_support.h"

#include "video/video_settings.h"

#include "settings.h"

static hts_mutex_t metadata_mutex;
static prop_courier_t *metadata_courier;
static struct metadata_source_list metadata_sources[METADATA_TYPE_num];
prop_t *metadata_sources_settings[METADATA_TYPE_num];

/**
 *
 */
metadata_t *
metadata_create(void)
{
  metadata_t *md = calloc(1, sizeof(metadata_t));
  TAILQ_INIT(&md->md_streams);
  md->md_rating = -1;
  md->md_rate_count = -1;
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
  rstr_release(md->md_genre);
  rstr_release(md->md_director);
  rstr_release(md->md_producer);
  rstr_release(md->md_description);
  rstr_release(md->md_tagline);
  rstr_release(md->md_imdb_id);
  rstr_release(md->md_icon);
  rstr_release(md->md_backdrop);
  rstr_release(md->md_manufacturer);
  rstr_release(md->md_equipment);
  rstr_release(md->md_ext_id);

  free(md->md_redirect);

  while((ms = TAILQ_FIRST(&md->md_streams)) != NULL) {
    TAILQ_REMOVE(&md->md_streams, ms, ms_link);
    rstr_release(ms->ms_title);
    rstr_release(ms->ms_info);
    rstr_release(ms->ms_isolang);
    rstr_release(ms->ms_codec);
    free(ms);
  }
  free(md);
}


/**
 *
 */
void
metadata_add_stream(metadata_t *md, const char *codec, int type,
		    int streamindex,
		    const char *title, const char *info, const char *isolang,
		    int disposition, int tracknum)
{
  metadata_stream_t *ms = malloc(sizeof(metadata_stream_t));
  ms->ms_title = rstr_alloc(title);
  ms->ms_info = rstr_alloc(info);
  ms->ms_isolang = rstr_alloc(isolang);
  ms->ms_codec = rstr_alloc(codec);
  ms->ms_type = type;
  ms->ms_disposition = disposition;
  ms->ms_streamindex = streamindex;
  ms->ms_tracknum = tracknum;
  TAILQ_INSERT_TAIL(&md->md_streams, ms, ms_link);
}



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
  [CONTENT_PLUGIN]   = "plugin",
};


/**
 *
 */
const char *
content2type(contenttype_t ctype)
{
  if(ctype >= sizeof(types) / sizeof(types[0]))
    return NULL;

  return types[ctype];
}


contenttype_t
type2content(const char *str)
{
  int i;
  for(i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
    if(!strcmp(str, types[i]))
      return i;
  }
  return CONTENT_UNKNOWN;
}


/**
 *
 */
static void
metadata_stream_make_prop(const metadata_stream_t *ms, prop_t *parent)
{
  char url[16];
  int score = 0;
  rstr_t *title;

  snprintf(url, sizeof(url), "libav:%d", ms->ms_streamindex);

  if(ms->ms_disposition & AV_DISPOSITION_DEFAULT)
    score += 10;
  
  if(ms->ms_title != NULL) {
    title = rstr_dup(ms->ms_title);
  } else {
    char buf[256];
    rstr_t *fmt = _("Track %d");

    snprintf(buf, sizeof(buf), rstr_get(fmt), ms->ms_tracknum);
    title = rstr_alloc(buf);
    rstr_release(fmt);
  }
  
  mp_add_trackr(parent,
		title,
		url,
		ms->ms_codec,
		ms->ms_info,
		ms->ms_isolang,
		NULL,
		_p("Embedded in file"),
		score);
  
  rstr_release(title);
}


/**
 *
 */
void
metadata_to_proptree(const metadata_t *md, prop_t *proproot, 
		     int cleanup_streams)
{
  metadata_stream_t *ms;
  int ac = 0, vc = 0, sc = 0, *pc;

  if(md->md_title != NULL)
    prop_set_rstring(prop_create(proproot, "title"), md->md_title);

  if(md->md_artist) {
    prop_set_rstring(prop_create(proproot, "artist"), md->md_artist);

    metadata_bind_artistpics(prop_create(proproot, "artist_images"),
			     md->md_artist);
  }

  if(md->md_album) {
    prop_set_rstring(prop_create(proproot, "album"),  md->md_album);

    if(md->md_artist != NULL)
      metadata_bind_albumart(prop_create(proproot, "album_art"),
			     md->md_artist, md->md_album);
  }

  TAILQ_FOREACH(ms, &md->md_streams, ms_link) {

    prop_t *p;

    switch(ms->ms_type) {
    case AVMEDIA_TYPE_AUDIO:
      p = prop_create(proproot, "audiostreams");
      pc = &ac;
      break;
    case AVMEDIA_TYPE_VIDEO:
      p = prop_create(proproot, "videostreams");
      pc = &vc;
      break;
    case AVMEDIA_TYPE_SUBTITLE:
      p = prop_create(proproot, "subtitlestreams");
      pc = &sc;
      break;
    default:
      continue;
    }
    if(cleanup_streams && *pc == 0) {
      prop_destroy_childs(p);
      *pc = 1;
    }
    metadata_stream_make_prop(ms, p);
  }

  if(md->md_format != NULL)
    prop_set_rstring(prop_create(proproot, "format"), md->md_format);

  if(md->md_duration)
    prop_set_float(prop_create(proproot, "duration"), md->md_duration);

  if(md->md_tracks)
    prop_set_int(prop_create(proproot, "tracks"), md->md_tracks);

  if(md->md_track )
    prop_set_int(prop_create(proproot, "track"), md->md_track );

  if(md->md_time)
    prop_set_int(prop_create(proproot, "timestamp"), md->md_time);

  if(md->md_manufacturer != NULL)
    prop_set_rstring(prop_create(proproot, "manufacturer"), md->md_manufacturer);

  if(md->md_equipment != NULL)
    prop_set_rstring(prop_create(proproot, "equipment"), md->md_equipment);
}



struct metadata_lazy_prop {
  void (*mlp_cb)(struct metadata_lazy_prop *mlp, int complete);
	    
  rstr_t *mlp_album;
  rstr_t *mlp_artist;
  rstr_t *mlp_title;
  rstr_t *mlp_url;
  rstr_t *mlp_imdb_id;

  int mlp_duration;
  int16_t mlp_year;
  int16_t mlp_refcount;
  char mlp_partials;
  char mlp_num_props;
  unsigned char mlp_type;

  int mlp_dsid;
  prop_t *mlp_loading;
  prop_t *mlp_source;


  prop_t *mlp_title_opt;

  prop_t *mlp_source_opt;
  prop_sub_t *mlp_source_opt_sub;

  prop_t *mlp_alt_opt;
  prop_sub_t *mlp_alt_opt_sub;

  struct {
    prop_t *p;
    prop_sub_t *s;
  } mlp_props[0];
};


/**
 *
 */
static void
mlp_release(metadata_lazy_prop_t *mlp)
{
  int i;

  mlp->mlp_refcount--;

  if(mlp->mlp_refcount > 0)
    return;

  for(i = 0; i < mlp->mlp_num_props; i++)
    prop_ref_dec(mlp->mlp_props[i].p);

  prop_destroy(mlp->mlp_title_opt);
  prop_ref_dec(mlp->mlp_title_opt);

  prop_unsubscribe(mlp->mlp_source_opt_sub);
  prop_destroy(mlp->mlp_source_opt);
  prop_ref_dec(mlp->mlp_source_opt);

  prop_unsubscribe(mlp->mlp_alt_opt_sub);
  prop_destroy(mlp->mlp_alt_opt);
  prop_ref_dec(mlp->mlp_alt_opt);

  rstr_release(mlp->mlp_artist);
  rstr_release(mlp->mlp_album);
  rstr_release(mlp->mlp_title);
  rstr_release(mlp->mlp_url);
  rstr_release(mlp->mlp_imdb_id);
  prop_ref_dec(mlp->mlp_loading);
  prop_ref_dec(mlp->mlp_source);
  free(mlp);
}


/**
 *
 */
static void
mlp_unsub_one(metadata_lazy_prop_t *mlp, prop_sub_t *s)
{
  int i;
  for(i = 0; i < mlp->mlp_num_props; i++) {
    if(mlp->mlp_props[i].s == s) {
      prop_unsubscribe(mlp->mlp_props[i].s);
      mlp->mlp_props[i].s = NULL;
      mlp->mlp_refcount--;
    }
  }
}

/**
 *
 */
static void
mlp_unsub_partials(metadata_lazy_prop_t *mlp)
{
  int i;
  mlp->mlp_refcount++;
  for(i = 0; i < mlp->mlp_partials; i++) {
    if(mlp->mlp_props[i].s) {
      prop_unsubscribe(mlp->mlp_props[i].s);
      mlp->mlp_props[i].s = NULL;
      mlp->mlp_refcount--;
    }
  }
  mlp_release(mlp);
}

/**
 *
 */
static void
mlp_unsub_complete(metadata_lazy_prop_t *mlp)
{
  int i;
  mlp->mlp_refcount++;
  for(i = mlp->mlp_partials; i < mlp->mlp_num_props; i++) {
    if(mlp->mlp_props[i].s) {
      prop_unsubscribe(mlp->mlp_props[i].s);
      mlp->mlp_props[i].s = NULL;
      mlp->mlp_refcount--;
    }
  }
  mlp_release(mlp);
}



/**
 *
 */
void
metadata_unbind(metadata_lazy_prop_t *mlp)
{
  hts_mutex_lock(&metadata_mutex);

  prop_destroy(mlp->mlp_title_opt);
  prop_ref_dec(mlp->mlp_title_opt);
  mlp->mlp_title_opt = NULL;

  prop_destroy(mlp->mlp_source_opt);
  prop_ref_dec(mlp->mlp_source_opt);
  mlp->mlp_source_opt = NULL;

  prop_destroy(mlp->mlp_alt_opt);
  prop_ref_dec(mlp->mlp_alt_opt);
  mlp->mlp_alt_opt = NULL;

  mlp_unsub_partials(mlp);
  mlp_unsub_complete(mlp);
  mlp_release(mlp);
  hts_mutex_unlock(&metadata_mutex);
}



/**
 *
 */
static void
mlp_add_artist_to_prop(void *opaque, const char *url, int width, int height)
{
  prop_t *p = prop_create_root(NULL);
  
  prop_set_string(prop_create(p, "url"), url);
  
  if(prop_set_parent(p, opaque))
    prop_destroy(p);
}


/**
 *
 */
static void
mlp_get_artist(metadata_lazy_prop_t *mlp, int complete)
{
  void *db = metadb_get();
  int r;

  if(!db_begin(db)) {
    r = metadb_get_artist_pics(db, rstr_get(mlp->mlp_artist),
			       mlp_add_artist_to_prop, mlp->mlp_props[0].p);
    
    if(r)
      lastfm_load_artistinfo(db, rstr_get(mlp->mlp_artist),
			     mlp_add_artist_to_prop, mlp->mlp_props[0].p);
    
    db_commit(db);
  }
  metadb_close(db);
}


/**
 *
 */
static void
mlp_get_album(metadata_lazy_prop_t *mlp, int complete)
{
  void *db = metadb_get();
  rstr_t *r;

  if(!db_begin(db)) {
    r = metadb_get_album_art(db,rstr_get(mlp->mlp_album),
			     rstr_get(mlp->mlp_artist));
  
    if(r == NULL) {
      // No album art available in our db, try to get some
      
      lastfm_load_albuminfo(db, rstr_get(mlp->mlp_album),
			    rstr_get(mlp->mlp_artist));
      
      r = metadb_get_album_art(db,rstr_get(mlp->mlp_album),
			       rstr_get(mlp->mlp_artist));
    }
    
    prop_set_rstring(mlp->mlp_props[0].p, r);
    rstr_release(r);
    db_commit(db);
  }
  metadb_close(db);
}



/**
 *
 */
static void
mlp_sub_partial_cb(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_prop_t *mlp = opaque;
  va_list ap;
  va_start(ap, event);
  switch(event) {
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    mlp->mlp_cb(mlp, 0);
    mlp_unsub_partials(mlp);
    break;
  case PROP_DESTROYED:
    mlp_unsub_one(mlp, va_arg(ap, prop_sub_t *));
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
mlp_sub_complete_cb(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_prop_t *mlp = opaque;
  va_list ap;
  va_start(ap, event);
  switch(event) {
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    mlp->mlp_cb(mlp, 1);
    mlp_unsub_complete(mlp);
    break;
  case PROP_DESTROYED:
    mlp_unsub_one(mlp, va_arg(ap, prop_sub_t *));
    break;
  default:
    break;
  }
  va_end(ap);
}

/**
 *
 */
static metadata_lazy_prop_t *
mlp_alloc(int props)
{
  metadata_lazy_prop_t *mlp = calloc(1, sizeof(metadata_lazy_prop_t) + 
				     sizeof(void *) * 2 * props);
  mlp->mlp_num_props = props;
  return mlp;
}


/**
 *
 */
static void
mlp_setup(metadata_lazy_prop_t *mlp, prop_t **p,
	  void (*cb)(metadata_lazy_prop_t *mlp, int complete),
	  int partials)
{
  int i;
  mlp->mlp_cb = cb;
  mlp->mlp_partials = partials;
  hts_mutex_lock(&metadata_mutex);

  for(i = 0; i < mlp->mlp_num_props; i++) {
    mlp->mlp_props[i].p = p[i];
    mlp->mlp_props[i].s = 
      prop_subscribe(PROP_SUB_TRACK_DESTROY_EXP | PROP_SUB_SUBSCRIPTION_MONITOR,
		     PROP_TAG_CALLBACK, i < partials ? 
		     mlp_sub_partial_cb : mlp_sub_complete_cb, mlp,
		     PROP_TAG_COURIER, metadata_courier,
		     PROP_TAG_ROOT, mlp->mlp_props[i].p,
		     NULL);
    mlp->mlp_refcount++;
  }
  hts_mutex_unlock(&metadata_mutex);
}



/**
 *
 */
void
metadata_bind_artistpics(prop_t *prop, rstr_t *artist)
{
  metadata_lazy_prop_t *mlp = mlp_alloc(1);
  prop_t *p = prop_ref_inc(prop);
  mlp->mlp_artist = rstr_spn(artist, ";:,-[]");
  mlp_setup(mlp, &p, mlp_get_artist, 0);
}


/**
 *
 */
void
metadata_bind_albumart(prop_t *prop, rstr_t *artist, rstr_t *album)
{
  metadata_lazy_prop_t *mlp = mlp_alloc(1);
  prop_t *p = prop_ref_inc(prop);
  mlp->mlp_artist = rstr_spn(artist, ";:,-[]");
  mlp->mlp_album  = rstr_spn(album, "[]()");
  mlp_setup(mlp, &p, mlp_get_album, 0);
}

enum {
  MOVIE_PROP_TITLE = 0,
  MOVIE_PROP_ICON,
  MOVIE_PROP_YEAR,
  MOVIE_PROP_TAGLINE,
  MOVIE_PROP_DESCRIPTION,
  MOVIE_PROP_RATING,
  MOVIE_PROP_RATING_COUNT,
  MOVIE_PROP_BACKDROP,
  MOVIE_PROP_GENRE,
  MOVIE_PROP_DIRECTOR,
  MOVIE_PROP_PRODUCER,
  MOVIE_PROP_num
};

/**
 *
 */
static metadata_source_t *
get_ms(metadata_type_t type, int id)
{
  metadata_source_t *ms;
  LIST_FOREACH(ms, &metadata_sources[type], ms_link)
    if(ms->ms_enabled && ms->ms_id == id)
      break;
  return ms;
}

/**
 * Must be in an transaction
 */
static int
mlp_get_video_info0(void *db, metadata_lazy_prop_t *mlp, int complete)
{
  metadata_t *md = NULL;
  int64_t rval;
  metadata_source_t *ms;
  struct metadata_source_list *msl = &metadata_sources[mlp->mlp_type];
  int r;
  int fixed_ds;

  LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link)
    ms->ms_mark = 0;

 redo:
  if(md != NULL)
    metadata_destroy(md);

  r = metadb_get_videoinfo(db, rstr_get(mlp->mlp_url), msl, &fixed_ds, &md);
  if(r)
    return r;

  prop_set_int(mlp->mlp_loading, 1);

  if(md == NULL || !md->md_preferred) {

    LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link) {
      if(!ms->ms_enabled)
	continue;

      const metadata_source_funcs_t *msf = ms->ms_funcs;

      if(fixed_ds && fixed_ds != ms->ms_id)
	continue;

      if(md && md->md_dsid == ms->ms_id)
	break;

      if(ms->ms_mark)
	continue;

      if(msf->query_by_imdb_id != NULL && mlp->mlp_imdb_id != NULL) {
	rval = msf->query_by_imdb_id(db, rstr_get(mlp->mlp_url),
				     rstr_get(mlp->mlp_imdb_id));
      } else if(msf->query_by_title_and_year != NULL) {
	rval = msf->query_by_title_and_year(db, rstr_get(mlp->mlp_url),
					    rstr_get(mlp->mlp_title),
					    mlp->mlp_year, mlp->mlp_duration);
      } else {
	continue;
      }

      if(rval == METADATA_DEADLOCK)
	return METADATA_DEADLOCK;

      goto redo;
    }
  }

  if(md != NULL && complete &&
     md->md_metaitem_status == METAITEM_STATUS_PARTIAL &&
     md->md_ext_id != NULL) {

    ms = get_ms(mlp->mlp_type, md->md_dsid);

    if(ms != NULL && ms->ms_funcs->query_by_id != NULL) {

      rval = ms->ms_funcs->query_by_id(db, rstr_get(mlp->mlp_url),
				       rstr_get(md->md_ext_id));

      if(rval == METADATA_DEADLOCK)
	return METADATA_DEADLOCK;
    }
    metadata_destroy(md);
    r = metadb_get_videoinfo(db, rstr_get(mlp->mlp_url), msl, &fixed_ds, &md);
    if(r) {
      prop_set_int(mlp->mlp_loading, 0);
      return r;
    }
  }

  if(md != NULL) {
    mlp->mlp_dsid = md->md_dsid;

    ms = get_ms(mlp->mlp_type, md->md_dsid);
    if(ms != NULL)
      prop_set_string(mlp->mlp_source, ms->ms_description);

    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_TITLE].p, md->md_title);
    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_ICON].p, md->md_icon);

    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_TAGLINE].p,
		     md->md_tagline);
    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_DESCRIPTION].p,
		     md->md_description);
    if(md->md_year)
      prop_set_int(mlp->mlp_props[MOVIE_PROP_YEAR].p,
		   md->md_year);
    if(md->md_rating != -1)
      prop_set_int(mlp->mlp_props[MOVIE_PROP_RATING].p,
		   md->md_rating);
    else
      prop_set_void(mlp->mlp_props[MOVIE_PROP_RATING].p);

    if(md->md_rate_count != -1)
      prop_set_int(mlp->mlp_props[MOVIE_PROP_RATING_COUNT].p,
		   md->md_rate_count);
    else
      prop_set_void(mlp->mlp_props[MOVIE_PROP_RATING_COUNT].p);


    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_BACKDROP].p, md->md_backdrop);
    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_GENRE].p, md->md_genre);
    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_DIRECTOR].p, md->md_director);
    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_PRODUCER].p, md->md_producer);
  } else {

    int i;
    for(i = 0; i < mlp->mlp_num_props; i++)
      if(i != MOVIE_PROP_TITLE)
	prop_set_void(mlp->mlp_props[i].p);

    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_TITLE].p, mlp->mlp_title);

    prop_set_void(mlp->mlp_source);
    mlp->mlp_dsid = 0;
  }

  prop_set_int(mlp->mlp_loading, 0);

  LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link)
    ms->ms_mark = 0;

  return 0;
}


/**
 *
 */
static void
mlp_get_video_info(metadata_lazy_prop_t *mlp, int complete)
{
  void *db = metadb_get();

 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  int r = mlp_get_video_info0(db, mlp, complete);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  db_commit(db);
  metadb_close(db);
}




/**
 *
 */
static void
load_alternatives(metadata_lazy_prop_t *mlp)
{
  prop_t *p = prop_create_r(mlp->mlp_alt_opt, "options");
  metadb_videoitem_alternatives(p, rstr_get(mlp->mlp_url), mlp->mlp_dsid,
				mlp->mlp_alt_opt_sub);
  prop_ref_dec(p);
}


/**
 *
 */
static void
mlp_set_preferred(metadata_lazy_prop_t *mlp, int64_t vid)
{
  void *db = metadb_get();
  int r;
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }
  r = metadb_videoitem_set_preferred(db, rstr_get(mlp->mlp_url), vid);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = mlp_get_video_info0(db, mlp, 1);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  db_commit(db);
  metadb_close(db);
}


/**
 *
 */
static void
mlp_sub_alternative(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_prop_t *mlp = opaque;
  prop_t *p;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_release(mlp);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    load_alternatives(mlp);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    if(r != NULL)
      mlp_set_preferred(mlp, atoi(rstr_get(r)));
    rstr_release(r);
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
load_sources(metadata_lazy_prop_t *mlp)
{
  metadata_source_t *ms;
  prop_t *active = NULL;
  prop_vec_t *pv = prop_vec_create(10);
  prop_t *c, *p = prop_create_r(mlp->mlp_source_opt, "options");
  int cur = metadb_item_get_preferred_ds(rstr_get(mlp->mlp_url));

  c = prop_create_root(NULL);
  prop_link(_p("Automatic"), prop_create(c, "title"));
  pv = prop_vec_append(pv, c);

  LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link) {
    if(!ms->ms_enabled)
      continue;
    c = prop_create_root(ms->ms_name);
    prop_set_string(prop_create(c, "title"), ms->ms_description);
    pv = prop_vec_append(pv, c);
    if(cur == ms->ms_id)
      active = prop_ref_inc(c);
  }

  prop_destroy_childs(p);
  prop_set_parent_vector(pv, p, NULL, NULL);

  if(active != NULL)
    prop_select_ex(active, NULL, mlp->mlp_source_opt_sub);
  else if(prop_vec_len(pv) > 0)
    prop_select_ex(prop_vec_get(pv, 0), NULL, mlp->mlp_source_opt_sub);

  prop_ref_dec(active);
  prop_vec_release(pv);
  prop_ref_dec(p);
}

/**
 *
 */
static void
mlp_set_source(metadata_lazy_prop_t *mlp, const char *name)
{
  metadata_source_t *ms = NULL;

  if(name != NULL)
    LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link)
      if(ms->ms_enabled && !strcmp(ms->ms_name, name))
	break;
  

  void *db = metadb_get();
  int r;
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }
  r = metadb_item_set_preferred_ds(db, rstr_get(mlp->mlp_url),
				   ms ? ms->ms_id : 0);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = mlp_get_video_info0(db, mlp, 1);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  db_commit(db);
  metadb_close(db);
  load_alternatives(mlp);
}


/**
 *
 */
static void
mlp_sub_source(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_prop_t *mlp = opaque;
  prop_t *p;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_release(mlp);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    load_sources(mlp);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    mlp_set_source(mlp, rstr_get(r));
    rstr_release(r);
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
metadata_bind_movie_info(metadata_lazy_prop_t **mlpp,
			 prop_t *prop, rstr_t *url, rstr_t *title, int year,
			 rstr_t *imdb_id, int duration,
			 prop_t *options)
{
  metadata_lazy_prop_t *mlp = *mlpp;

  if(mlp != NULL) {
    if(rstr_eq(url, mlp->mlp_url) &&
       rstr_eq(title, mlp->mlp_title) &&
       rstr_eq(imdb_id, mlp->mlp_imdb_id) &&
       year == mlp->mlp_year && 
       duration == mlp->mlp_duration)
      return;
    metadata_unbind(mlp);
  }

  const int too_short = duration > 0 && duration < 300;
  
  TRACE(TRACE_DEBUG, "METADATA",
	"Lookup movie %s (%d) %s, duration: %d:%02d:%02d%s",
	rstr_get(title) ?: "<no title>",
	year,
	rstr_get(imdb_id) ?: "<no IMDB tag>",
	duration / 3600,
	(duration / 60) % 60,
	duration % 60,
	too_short ? " (too short for lookup, skipping)" : "");

  if(too_short)
    return;

  *mlpp = mlp = mlp_alloc(MOVIE_PROP_num);
  prop_t *props[MOVIE_PROP_num];

  
  props[MOVIE_PROP_TITLE]        = prop_create_r(prop, "title");
  props[MOVIE_PROP_ICON]         = prop_create_r(prop, "icon");
  props[MOVIE_PROP_YEAR]         = prop_create_r(prop, "year");
  // end of partials
  props[MOVIE_PROP_TAGLINE]      = prop_create_r(prop, "tagline");
  props[MOVIE_PROP_DESCRIPTION]  = prop_create_r(prop, "description");
  props[MOVIE_PROP_RATING]       = prop_create_r(prop, "rating");
  props[MOVIE_PROP_RATING_COUNT] = prop_create_r(prop, "rating_count");
  props[MOVIE_PROP_BACKDROP]     = prop_create_r(prop, "backdrop");
  props[MOVIE_PROP_GENRE]        = prop_create_r(prop, "genre");
  props[MOVIE_PROP_DIRECTOR]     = prop_create_r(prop, "director");
  props[MOVIE_PROP_PRODUCER]     = prop_create_r(prop, "producer");

  mlp->mlp_refcount = 1;
  mlp->mlp_title = rstr_spn(title, "[]()");
  mlp->mlp_url = rstr_dup(url);
  mlp->mlp_year = year;
  mlp->mlp_duration = duration;
  mlp->mlp_imdb_id = rstr_dup(imdb_id);
  mlp->mlp_loading = prop_create_r(prop, "loading");
  mlp->mlp_source = prop_create_r(prop, "source");
  mlp->mlp_type = METADATA_TYPE_MOVIE;

  prop_t *m;
  prop_vec_t *pv = prop_vec_create(3);

  mlp->mlp_title_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlp->mlp_title_opt, "type"), "separator");
  prop_set_int(prop_create(mlp->mlp_title_opt, "enabled"), 1);
  m = prop_create(mlp->mlp_title_opt, "metadata");
  prop_link(_p("Metadata"), prop_create(m, "title"));

  pv = prop_vec_append(pv, mlp->mlp_title_opt);

  // Metadata source selection

  mlp->mlp_source_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlp->mlp_source_opt, "type"), "multiopt");
  prop_set_int(prop_create(mlp->mlp_source_opt, "enabled"), 1);
  m = prop_create(mlp->mlp_source_opt, "metadata");
  prop_link(_p("Metadata source"), prop_create(m, "title"));

  mlp->mlp_source_opt_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlp_sub_source, mlp,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, prop_create(mlp->mlp_source_opt, "options"),
		   NULL);
  mlp->mlp_refcount++;

  pv = prop_vec_append(pv, mlp->mlp_source_opt);

  // Metadata alternative selection

  mlp->mlp_alt_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlp->mlp_alt_opt, "type"), "multiopt");
  prop_set_int(prop_create(mlp->mlp_alt_opt, "enabled"), 1);
  m = prop_create(mlp->mlp_alt_opt, "metadata");
  prop_link(_p("Movie"), prop_create(m, "title"));

  mlp->mlp_alt_opt_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlp_sub_alternative, mlp,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, prop_create(mlp->mlp_alt_opt, "options"),
		   NULL);
  mlp->mlp_refcount++;

  pv = prop_vec_append(pv, mlp->mlp_alt_opt);
  


  prop_set_parent_vector(pv, options, NULL, NULL);
  prop_vec_release(pv);

  // final setup
  mlp_setup(mlp, props, mlp_get_video_info, 3);
}


#define isnum(a) ((a) >= '0' && (a) <= '9')

/**
 *
 */
rstr_t *
metadata_filename_to_title(const char *filename, int *yearp)
{
  if(yearp != NULL)
    *yearp = 0;

  char *s = mystrdupa(filename);

  url_deescape(s);

  // Strip .xxx ending in filenames
  int i = strlen(s);
  if(i > 4 && s[i - 4] == '.') {
    i -= 4;
    s[i] = 0;
  }

  while(i > 0) {
    
    if(i > 5 && s[i-5] == '.' &&
       isnum(s[i-4]) && isnum(s[i-3]) && isnum(s[i-2]) && isnum(s[i-1])) {
      if(yearp)
	*yearp = atoi(s + i - 4);
      i -= 5;
      s[i] = 0;
      continue;
    }

    if(i > 7 && s[i-7] == ' ' && s[i-6] == '(' && 
       isnum(s[i-5]) && isnum(s[i-4]) && isnum(s[i-3]) && isnum(s[i-2]) &&
       s[i-1] == ')') {
      if(yearp)
	*yearp = atoi(s + i - 5);
      i -= 7;
      s[i] = 0;
      continue;
    }

    if(i > 5 && !strncmp(s+i-5, ".720p", 5)) {
      i -= 5;
      s[i] = 0;
      continue;
    }
    
    if(i > 6 && !strncmp(s+i-6, ".1080p", 6)) {
      i -= 6;
      s[i] = 0;
      continue;
    }

    if(i > 4 && !strncmp(s+i-4, "x264", 4)) {
      i -= 4;
      s[i] = 0;
      continue;
    }

    i--;
  }

  i = 0;
  while(s[i]) {
    if(s[i] == '.')
      s[i] = ' ';
    i++;
  }
 
  return rstr_alloc(s);
}



/**
 *
 */
rstr_t *
metadata_remove_postfix(const char *in, char c)
{
  char *x = strrchr(in, c);
  if(x == NULL)
    return rstr_alloc(in);
  return rstr_allocl(in, x - in);
}


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

  prop_set(ms->ms_settings, "metadata", "enabled", NULL, PROP_SET_INT,
	   ms->ms_enabled);

  void *db = metadb_get();

  int rc = db_prepare(db, 
		      "UPDATE datasource "
		      "SET enabled = ?2 "
		      "WHERE id = ?1"
		      , -1, &stmt, NULL);

  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
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
int
metadata_add_source(const char *name, const char *description,
		    int prio,  metadata_type_t type,
		    const metadata_source_funcs_t *funcs)
{
  assert(type < METADATA_TYPE_num);

  void *db = metadb_get();
  int rc;
  int id = METADATA_ERROR;
  int enabled = 1;
  sqlite3_stmt *stmt;

 again:
  if(db_begin(db))
    goto err;

  rc = db_prepare(db, 
		  "SELECT id,prio,enabled FROM datasource WHERE name=?1",
		  -1, &stmt, NULL);
  
  if(rc != SQLITE_OK) {
    TRACE(TRACE_ERROR, "SQLITE", "SQL Error at %s:%d",
	  __FUNCTION__, __LINE__);
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

    rc = db_prepare(db, 
		    "INSERT INTO datasource "
		    "(name, prio, type, enabled) "
		    "VALUES "
		    "(?1, ?2, ?3, ?4)"
		    , -1, &stmt, NULL);

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
  ms->ms_prio = prio;
  ms->ms_name = strdup(name);
  ms->ms_description = strdup(description);
  ms->ms_id = id;
  ms->ms_funcs = funcs;
  ms->ms_enabled = enabled;

  ms->ms_settings = 
    settings_add_dir_cstr(metadata_sources_settings[type],
			  ms->ms_description, NULL, NULL, NULL, NULL);

  settings_create_bool(ms->ms_settings, "enabled", _p("Enabled"),
		       ms->ms_enabled, NULL, ms_set_enable, ms,
		       0, metadata_courier, NULL, NULL);

  ms_set_enable(ms, enabled);

  hts_mutex_lock(&metadata_mutex);
  LIST_INSERT_SORTED(&metadata_sources[type], ms, ms_link, ms_prio_cmp);
  hts_mutex_unlock(&metadata_mutex);

  return id;

 err:
  metadb_close(db);
  return METADATA_ERROR;
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
  prop_set_string(prop_create(d, "type"), "divider");
  
  prop_concat_add_source(pc, prop_create(c, "nodes"), d);
}

/**
 *
 */
void
metadata_init(void)
{
  prop_t *s;
  prop_concat_t *pc;

  hts_mutex_init(&metadata_mutex);
  metadata_courier = prop_courier_create_thread(&metadata_mutex, "metadata");

  s = settings_add_dir(NULL, _p("Metadata"), "settings", NULL,
		       _p("Metadata configuration and provider settings"),
		       NULL);

  pc = prop_concat_create(prop_create(s, "nodes"), 0);

  add_provider_class(pc, METADATA_TYPE_MOVIE, _p("Providers for Movies"));
  add_provider_class(pc, METADATA_TYPE_MUSIC, _p("Providers for Music"));

}
