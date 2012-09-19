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
#include "db/kvstore.h"

#include "video/video_settings.h"

#include "settings.h"

static hts_mutex_t metadata_mutex;
static prop_courier_t *metadata_courier;
static struct metadata_source_list metadata_sources[METADATA_TYPE_num];
prop_t *metadata_sources_settings[METADATA_TYPE_num];

static void metadata_filename_to_title(const char *filename,
				       int *yearp, rstr_t **titlep);

static int metadata_filename_to_episode(const char *filename,
					int *season, int *episode,
					rstr_t **titlep);

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
  [CONTENT_FONT]     = "font",
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



TAILQ_HEAD(metadata_lazy_prop_queue, metadata_lazy_prop);
static struct metadata_lazy_prop_queue mlpqueue;

/**
 *
 */
struct metadata_lazy_prop {
  TAILQ_ENTRY(metadata_lazy_prop) mlp_link;

  void (*mlp_cb)(struct metadata_lazy_prop *mlp);
	    
  rstr_t *mlp_album;
  rstr_t *mlp_artist;
  rstr_t *mlp_url;
  rstr_t *mlp_filename;
  rstr_t *mlp_folder;
  rstr_t *mlp_imdb_id;

  int mlp_duration;
  int16_t mlp_refcount;
  char mlp_partials;
  char mlp_num_props;
  unsigned char mlp_type;

  unsigned char mlp_zombie : 1;
  unsigned char mlp_queued : 1;
  unsigned char mlp_want_complete : 1;
  unsigned char mlp_want_partial : 1;
  unsigned char mlp_lonely : 1;

  int mlp_dsid;
  prop_t *mlp_loading;
  prop_t *mlp_source;


  prop_t *mlp_title_opt;
  prop_t *mlp_info;
  prop_t *mlp_info_text;

  prop_t *mlp_source_opt;
  prop_sub_t *mlp_source_opt_sub;

  prop_t *mlp_alt_opt;
  prop_sub_t *mlp_alt_opt_sub;

  prop_t *mlp_sq;
  prop_sub_t *mlp_sq_sub;

  prop_t *mlp_refresh;
  prop_sub_t *mlp_refresh_sub;


  rstr_t *mlp_custom_query;

  struct {
    prop_t *p;
    prop_sub_t *s;
  } mlp_props[0];
};




/**
 *
 */
static void
mlp_enqueue(metadata_lazy_prop_t *mlp)
{
  if(mlp->mlp_zombie || mlp->mlp_queued)
    return;

  TAILQ_INSERT_TAIL(&mlpqueue, mlp, mlp_link);
  mlp->mlp_queued = 1;
}


/**
 *
 */
static void
mlp_dequeue(metadata_lazy_prop_t *mlp)
{
  if(!mlp->mlp_queued)
    return;

  TAILQ_REMOVE(&mlpqueue, mlp, mlp_link);
  mlp->mlp_queued = 0;
}


/**
 *
 */
static void
mlp_destroy(metadata_lazy_prop_t *mlp)
{
  int i;

  if(!mlp->mlp_zombie) {
    mlp->mlp_zombie = 1;
    prop_destroy(mlp->mlp_title_opt);
    prop_destroy(mlp->mlp_info);
    prop_destroy(mlp->mlp_source_opt);
    prop_destroy(mlp->mlp_alt_opt);
    prop_destroy(mlp->mlp_sq);
    prop_destroy(mlp->mlp_refresh);
  }

  mlp->mlp_refcount--;
  if(mlp->mlp_refcount > 0)
    return;

  mlp_dequeue(mlp);

  for(i = 0; i < mlp->mlp_num_props; i++) {
    prop_ref_dec(mlp->mlp_props[i].p);
    prop_unsubscribe(mlp->mlp_props[i].s);
  }
  
  prop_unsubscribe(mlp->mlp_source_opt_sub);
  prop_unsubscribe(mlp->mlp_alt_opt_sub);
  prop_unsubscribe(mlp->mlp_sq_sub);
  prop_unsubscribe(mlp->mlp_refresh_sub);

  prop_ref_dec(mlp->mlp_title_opt);
  prop_ref_dec(mlp->mlp_info);
  prop_ref_dec(mlp->mlp_info_text);
  prop_ref_dec(mlp->mlp_source_opt);
  prop_ref_dec(mlp->mlp_alt_opt);
  prop_ref_dec(mlp->mlp_refresh);

  rstr_release(mlp->mlp_artist);
  rstr_release(mlp->mlp_album);
  rstr_release(mlp->mlp_filename);
  rstr_release(mlp->mlp_imdb_id);
  prop_ref_dec(mlp->mlp_loading);
  prop_ref_dec(mlp->mlp_source);
  prop_ref_dec(mlp->mlp_sq);
  rstr_release(mlp->mlp_custom_query);
  rstr_release(mlp->mlp_url);
  free(mlp);
}



/**
 *
 */
void
metadata_unbind(metadata_lazy_prop_t *mlp)
{
  hts_mutex_lock(&metadata_mutex);
  mlp_destroy(mlp);
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
mlp_get_artist(metadata_lazy_prop_t *mlp)
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
mlp_get_album(metadata_lazy_prop_t *mlp)
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
    if(mlp->mlp_want_partial == 0) {
      mlp->mlp_want_partial = 1;
      mlp_enqueue(mlp);
    }
    break;
  case PROP_DESTROYED:
    mlp_destroy(mlp);
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
    if(mlp->mlp_want_complete == 0) {
      mlp->mlp_want_complete = 1;
      mlp_enqueue(mlp);
    }
    break;
  case PROP_DESTROYED:
    mlp_destroy(mlp);
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
	  void (*cb)(metadata_lazy_prop_t *mlp),
	  int partials)
{
  int i;
  mlp->mlp_cb = cb;
  mlp->mlp_partials = partials;

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
}



/**
 *
 */
void
metadata_bind_artistpics(prop_t *prop, rstr_t *artist)
{
  metadata_lazy_prop_t *mlp = mlp_alloc(1);
  prop_t *p = prop_ref_inc(prop);
  mlp->mlp_artist = rstr_spn(artist, ";:,-[", 1);
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
  mlp->mlp_artist = rstr_spn(artist, ";:,-[", 1);
  mlp->mlp_album  = rstr_spn(album, "[(", 1);
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
 *
 */
static void 
build_info_text(metadata_lazy_prop_t *mlp, const metadata_t *md)
{
  rstr_t *txt;
  
  if(md == NULL) {
    txt = _("No data found");
  } else {
    char tmp[512];

    rstr_t *qtype = NULL;

    switch(md->md_qtype) {
    case METADATA_QTYPE_FILENAME:
      qtype = _("filename");
      break;
    case METADATA_QTYPE_IMDB:
      qtype = _("IMDb ID");
      break;
    case METADATA_QTYPE_CUSTOM_IMDB:
      qtype = _("custom IMDb ID");
      break;
    case METADATA_QTYPE_DIRECTORY:
      qtype = _("folder name");
      break;
    case METADATA_QTYPE_CUSTOM:
      qtype = _("custom query");
      break;
    case METADATA_QTYPE_EPISODE:
      qtype = _("filename as TV episode");
      break;
    }

    rstr_t *fmt = _("Metadata loaded from '%s' using %s");

    metadata_source_t *ms = get_ms(mlp->mlp_type, md->md_dsid);

    snprintf(tmp, sizeof(tmp), rstr_get(fmt), 
	     ms ? ms->ms_description : "???",
	     rstr_get(qtype) ?: "???");

    txt = rstr_alloc(tmp);

    rstr_release(fmt);
    rstr_release(qtype);
  }

  prop_set_rstring(mlp->mlp_info_text, txt);
  rstr_release(txt);
}


/**
 *
 */
static int
is_qtype_compat(int qa, int qb)
{
  if(qa == qb)
    return 1;

  if(qa == METADATA_QTYPE_FILENAME_OR_DIRECTORY &&
     (qb == METADATA_QTYPE_FILENAME || qb == METADATA_QTYPE_DIRECTORY ||
      qb == METADATA_QTYPE_EPISODE))
    return 1;

  return 0;
}


/**
 *
 */
static int64_t
query_by_filename_or_dirname(void *db, metadata_lazy_prop_t *mlp,
			     const metadata_source_funcs_t *msf)
{
  int year;
  rstr_t *title;
  int64_t rval;
  
  int season, episode;

  if(!metadata_filename_to_episode(rstr_get(mlp->mlp_filename), 
				   &season, &episode, &title)) {

    if(msf->query_by_episode == NULL)
      return METADATA_PERMANENT_ERROR;

    TRACE(TRACE_DEBUG, "METADATA",
	  "Performing search lookup for %s season:%d episode:%d, "
	  "based on filename",
	  rstr_get(title), season, episode);

    rval = msf->query_by_episode(db, rstr_get(mlp->mlp_url),
				 rstr_get(title), season, episode,
				 METADATA_QTYPE_EPISODE);
    
    rstr_release(title);
    return rval;
  }

  if(msf->query_by_title_and_year == NULL)
    return METADATA_PERMANENT_ERROR;



  metadata_filename_to_title(rstr_get(mlp->mlp_filename), &year, &title);
  
  TRACE(TRACE_DEBUG, "METADATA",
	"Performing search lookup for %s year:%d, based on filename",
	rstr_get(title), year);

  rval = msf->query_by_title_and_year(db, rstr_get(mlp->mlp_url),
				      rstr_get(title), year,
				      mlp->mlp_duration,
				      METADATA_QTYPE_FILENAME);

  if(rval == METADATA_PERMANENT_ERROR && year != 0) {
    // Try without year

    rval = msf->query_by_title_and_year(db, rstr_get(mlp->mlp_url),
					rstr_get(title), 0,
					mlp->mlp_duration,
					METADATA_QTYPE_FILENAME);
  }

  rstr_release(title);

  if(rval == METADATA_PERMANENT_ERROR && mlp->mlp_lonely) {

    metadata_filename_to_title(rstr_get(mlp->mlp_folder), &year, &title);
  
    TRACE(TRACE_DEBUG, "METADATA",
	  "Performing search lookup for %s year:%d, based on folder name",
	  rstr_get(title), year);

    rval = msf->query_by_title_and_year(db, rstr_get(mlp->mlp_url),
					rstr_get(title), year,
					mlp->mlp_duration,
					METADATA_QTYPE_DIRECTORY);
    rstr_release(title);
  }

  return rval;
}



/**
 * Must be in a transaction
 */
static int
mlp_get_video_info0(void *db, metadata_lazy_prop_t *mlp, int refresh)
{
  rstr_t *title = NULL;
  metadata_t *md = NULL;
  int64_t rval;
  metadata_source_t *ms;
  struct metadata_source_list *msl = &metadata_sources[mlp->mlp_type];
  int r;
  int fixed_ds;
  const char *sq = rstr_get(mlp->mlp_custom_query);
  int sq_is_imdb_id = sq && sq[0] == 't' && sq[1] == 't' &&
    sq[2] >= '0' && sq[2] <= '9';

  if(sq && !*sq)
    sq = NULL;

  if(mlp->mlp_duration && mlp->mlp_duration < 300) {
    goto bad;
  }

  LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link)
    ms->ms_mark = 0;

 redo:
  if(md != NULL) {
    metadata_destroy(md);
    md = NULL;
  }

  if(!refresh) {
    r = metadb_get_videoinfo(db, rstr_get(mlp->mlp_url), msl, &fixed_ds, &md);
    if(r) {
      rstr_release(title);
      return r;
    }
  } else {
    refresh = 0;
    fixed_ds = 0;
  }

  prop_set_int(mlp->mlp_loading, 1);
  if(md == NULL || !md->md_preferred) {

    LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link) {
      if(!ms->ms_enabled)
	continue;

      const metadata_source_funcs_t *msf = ms->ms_funcs;

      if(fixed_ds && fixed_ds != ms->ms_id)
	continue;

      /* Figure out what query to run (or what we would like to run) */
      int qtype;
      const char *q;

      if(msf->query_by_imdb_id != NULL && sq_is_imdb_id) {
	qtype = METADATA_QTYPE_CUSTOM_IMDB;
	q = sq;
      } else if(sq != NULL) {
	qtype = METADATA_QTYPE_CUSTOM;
	q = NULL;
      } else if(msf->query_by_imdb_id != NULL && mlp->mlp_imdb_id != NULL) {
	qtype = METADATA_QTYPE_IMDB;
	q = rstr_get(mlp->mlp_imdb_id);
      } else {
	qtype = METADATA_QTYPE_FILENAME_OR_DIRECTORY;
	q = NULL;
      }

      if(md && md->md_dsid == ms->ms_id && is_qtype_compat(qtype, md->md_qtype))
	break;

      /**
       * If current metadata source is seen (marked by metadb_get_videoinfo())
       * and the query type corresponds we should be fully up to date,
       * thus continue
       */
       
      if(ms->ms_mark && is_qtype_compat(qtype, ms->ms_qtype))
	continue;

      rval = metadb_videoitem_delete_from_ds(db, rstr_get(mlp->mlp_url),
					     ms->ms_id);

      if(rval == 0) {

	switch(qtype) {
	case METADATA_QTYPE_IMDB:
	case METADATA_QTYPE_CUSTOM_IMDB:

	  TRACE(TRACE_DEBUG, "METADATA",
		"Performing IMDB lookup for %s using %s", q, ms->ms_name);

	  rval = msf->query_by_imdb_id(db, rstr_get(mlp->mlp_url), q, qtype);
	  break;

	case METADATA_QTYPE_FILENAME_OR_DIRECTORY:
	  rval = query_by_filename_or_dirname(db, mlp, msf);
	  break;

	case METADATA_QTYPE_CUSTOM:
	  if(msf->query_by_title_and_year == NULL)
	    continue;

	  TRACE(TRACE_DEBUG, "METADATA",
		"Performing custom search lookup for %s using %s", sq,
		ms->ms_name);
	  rval = msf->query_by_title_and_year(db, rstr_get(mlp->mlp_url),
					      sq, 0, mlp->mlp_duration, qtype);
	  break;

	default:
	  continue;
	}
      }

      if(rval == METADATA_DEADLOCK || rval == METADATA_TEMPORARY_ERROR) {
	if(rval == METADATA_TEMPORARY_ERROR)
	  TRACE(TRACE_DEBUG, "METADATA", "Temporary error for %s",
		rstr_get(mlp->mlp_url));

	prop_set_int(mlp->mlp_loading, 0);
	rstr_release(title);
	return rval;
      }

      if(rval == METADATA_PERMANENT_ERROR)
	rval = metadb_insert_videoitem(db, rstr_get(mlp->mlp_url), ms->ms_id,
				       "0", NULL, METAITEM_STATUS_ABSENT, 0,
				       qtype, 0);
      if(rval < 0) {
	rstr_release(title);
	return rval;
      }
      goto redo;
    }
  }

  if(md != NULL && mlp->mlp_want_complete &&
     md->md_metaitem_status == METAITEM_STATUS_PARTIAL &&
     md->md_ext_id != NULL) {

    ms = get_ms(mlp->mlp_type, md->md_dsid);

    if(ms != NULL && ms->ms_funcs->query_by_id != NULL) {

      rval = ms->ms_funcs->query_by_id(db, rstr_get(mlp->mlp_url),
				       rstr_get(md->md_ext_id));

      if(rval == METADATA_DEADLOCK) {
	rstr_release(title);
	return METADATA_DEADLOCK;
      }
    }
    metadata_destroy(md);
    r = metadb_get_videoinfo(db, rstr_get(mlp->mlp_url), msl, &fixed_ds, &md);
    if(r) {
      prop_set_int(mlp->mlp_loading, 0);
      rstr_release(title);
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
    else
      prop_set_void(mlp->mlp_props[MOVIE_PROP_YEAR].p);
      
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


    build_info_text(mlp, md);

    metadata_destroy(md);

  } else {

    int i;
  bad:
    for(i = 0; i < mlp->mlp_num_props; i++)
      if(i != MOVIE_PROP_TITLE && i != MOVIE_PROP_YEAR)
	prop_set_void(mlp->mlp_props[i].p);

    prop_set_rstring(mlp->mlp_props[MOVIE_PROP_TITLE].p, mlp->mlp_filename);
    prop_set_void(mlp->mlp_props[MOVIE_PROP_YEAR].p);

    prop_set_void(mlp->mlp_source);
    mlp->mlp_dsid = 0;

    build_info_text(mlp, NULL);
  }

  prop_set_int(mlp->mlp_loading, 0);

  LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link)
    ms->ms_mark = 0;

  rstr_release(title);
  return 0;
}


/**
 *
 */
static void
mlp_get_video_info(metadata_lazy_prop_t *mlp)
{
  if(mlp->mlp_zombie)
    return;

  void *db = metadb_get();

 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  int r = mlp_get_video_info0(db, mlp, 0);
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

  r = mlp_get_video_info0(db, mlp, 0);
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
    mlp_destroy(mlp);
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

  c = prop_create_root("1");
  prop_link(_p("None"), prop_create(c, "title"));
  pv = prop_vec_append(pv, c);
  if(cur == 1)
    active = prop_ref_inc(c);

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
  int id = 0;

  if(name != NULL) {

    if(!strcmp(name, "1")) {
      // dsid 1 is reserved for local file
      id = 1;
    } else {
      metadata_source_t *ms = NULL;
      LIST_FOREACH(ms, &metadata_sources[mlp->mlp_type], ms_link) {
	if(ms->ms_enabled && !strcmp(ms->ms_name, name)) {
	  id = ms->ms_id;
	  break;
	}
      }
    }
  } 

  void *db = metadb_get();
  int r;
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  r = metadb_item_set_preferred_ds(db, rstr_get(mlp->mlp_url), id);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = mlp_get_video_info0(db, mlp, 0);
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
    mlp_destroy(mlp);
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
static void
mlp_refresh_video_info(metadata_lazy_prop_t *mlp)
{
  void *db = metadb_get();
  int r;

  assert(mlp != NULL);

 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  r = metadb_item_set_preferred_ds(db, rstr_get(mlp->mlp_url), 0);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = metadb_videoitem_set_preferred(db, rstr_get(mlp->mlp_url), 0);
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
mlp_sub_actions(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_prop_t *mlp = opaque;
  va_list ap;
  event_t *e;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(mlp);
    break;

  case PROP_EXT_EVENT:
    if(mlp->mlp_zombie)
      break;

    e = va_arg(ap, event_t *);
    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      if(!strcmp(e->e_payload, "refreshMetadata")) {
	mlp_refresh_video_info(mlp);
	load_alternatives(mlp);
	const char *s = rstr_get(mlp->mlp_custom_query);

	if(s && *s) {
	  kv_url_opt_set(rstr_get(mlp->mlp_url), KVSTORE_DOMAIN_SYS,
			 "metacustomquery", KVSTORE_SET_STRING, s);
	}
      }
    }
      
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
mlp_sub_query(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_prop_t *mlp = opaque;
  va_list ap;
  rstr_t *r;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(mlp);
    break;

  case PROP_SET_RSTRING:
    r = va_arg(ap, rstr_t *);
    rstr_set(&mlp->mlp_custom_query, r);
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
metadata_lazy_prop_t *
metadata_bind_movie_info(prop_t *prop, rstr_t *url, rstr_t *filename,
			 rstr_t *imdb_id, int duration,
			 prop_t *options, prop_t *root,
			 rstr_t *folder, int lonely)
{
#if 0
  const int too_short = duration > 0 && duration < 300;
  int querytype;
  
  rstr_t *filename_title;
  int filename_year;
  
  int filename_score =
    metadata_filename_to_title(rstr_get(filename),
			       &filename_year, &filename_title);

  rstr_t *dir_title;
  int dir_year;
  int dir_score =
    metadata_filename_to_title(rstr_get(dir_name),
			       &dir_year, &dir_title);

  rstr_t *title;
  int year;
  int chosen_score;
  if(filename_score >= dir_score) {
    title = rstr_dup(filename_title);
    year = filename_year;
    chosen_score = filename_score;
    querytype = METADATA_QTYPE_FILENAME;
  } else {
    title = rstr_dup(dir_title);
    year = dir_year;
    querytype = METADATA_QTYPE_DIRECTORY;
    chosen_score = dir_score;
  }

  TRACE(TRACE_DEBUG, "METADATA",
	"Binding metadata for '%s'<score=%d> in '%s'<score=%d> [title:%s] [year: %d] %s, duration: %d:%02d:%02d%s",
	rstr_get(filename),
	filename_score,
	rstr_get(dir_name),
	dir_score,
	rstr_get(title),
	year,
	rstr_get(imdb_id) ?: "<no IMDB tag>",
	duration / 3600,
	(duration / 60) % 60,
	duration % 60,
	too_short ? " (too short for lookup, skipping)" : "");

  if(too_short || chosen_score <= 0) {
    goto done;
  }
#endif

  metadata_lazy_prop_t *mlp = mlp_alloc(MOVIE_PROP_num);
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

  mlp->mlp_refcount = 1;  // one reference for caller
  mlp->mlp_filename = rstr_dup(filename);
  mlp->mlp_folder = rstr_dup(folder);
  mlp->mlp_url = rstr_dup(url);
  mlp->mlp_duration = duration;
  mlp->mlp_imdb_id = rstr_dup(imdb_id);
  mlp->mlp_loading = prop_create_r(prop, "loading");
  mlp->mlp_source = prop_create_r(prop, "source");
  mlp->mlp_type = METADATA_TYPE_VIDEO;
  mlp->mlp_lonely = lonely;

  prop_t *m;
  prop_vec_t *pv = prop_vec_create(10);

  // Separator

  mlp->mlp_title_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlp->mlp_title_opt, "type"), "separator");
  prop_set_int(prop_create(mlp->mlp_title_opt, "enabled"), 1);
  m = prop_create(mlp->mlp_title_opt, "metadata");
  prop_link(_p("Metadata search"), prop_create(m, "title"));

  pv = prop_vec_append(pv, mlp->mlp_title_opt);

  // Info

  mlp->mlp_info = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlp->mlp_info, "type"), "info");
  prop_set_int(prop_create(mlp->mlp_info, "enabled"), 1);
  mlp->mlp_info_text = prop_create_r(prop_create(mlp->mlp_info, "metadata"),
				     "title");

  pv = prop_vec_append(pv, mlp->mlp_info);


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



  // Metadata search query

  mlp->mlp_sq = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlp->mlp_sq, "type"), "string");
  prop_set_int(prop_create(mlp->mlp_sq, "enabled"), 1);
  m = prop_create(mlp->mlp_sq, "metadata");
  prop_set_string(prop_create(mlp->mlp_sq, "action"), "refreshMetadata");
  prop_link(_p("Custom search query"), prop_create(m, "title"));
  prop_t *v = prop_create(mlp->mlp_sq, "value");

  rstr_t *cur = kv_url_opt_get_rstr(rstr_get(url), KVSTORE_DOMAIN_SYS, 
				    "metacustomquery");

  if(cur != NULL) {
    prop_set_rstring(v, cur);
    rstr_release(cur);
  }

  mlp->mlp_sq_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlp_sub_query, mlp,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, v,
		   NULL);
  mlp->mlp_refcount++;

  pv = prop_vec_append(pv, mlp->mlp_sq);
    

  // Metadata refresh

  mlp->mlp_refresh = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlp->mlp_refresh, "type"), "action");
  prop_set_string(prop_create(mlp->mlp_refresh, "action"), "refreshMetadata");
  prop_set_int(prop_create(mlp->mlp_refresh, "enabled"), 1);
  m = prop_create(mlp->mlp_refresh, "metadata");
  prop_link(_p("Refresh metadata"), prop_create(m, "title"));

  mlp->mlp_refresh_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlp_sub_actions, mlp,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, root,
		   NULL);
  mlp->mlp_refcount++;

  pv = prop_vec_append(pv, mlp->mlp_refresh);

  // Add all options

  prop_set_parent_vector(pv, options, NULL, NULL);
  prop_vec_release(pv);

  // final setup
  mlp_setup(mlp, props, mlp_get_video_info, 3);

  return mlp;
}


/**
 *
 */
void
mlp_set_imdb_id(metadata_lazy_prop_t *mlp, rstr_t *imdb_id)
{
  hts_mutex_lock(&metadata_mutex);
  rstr_set(&mlp->mlp_imdb_id, imdb_id);
  mlp_dequeue(mlp);
  mlp->mlp_cb(mlp);
  hts_mutex_unlock(&metadata_mutex);
}

/**
 *
 */
void
mlp_set_duration(metadata_lazy_prop_t *mlp, int duration)
{
  hts_mutex_lock(&metadata_mutex);
  mlp->mlp_duration = duration;
  mlp_dequeue(mlp);
  mlp->mlp_cb(mlp);
  hts_mutex_unlock(&metadata_mutex);
}


/**
 *
 */
void
mlp_set_lonely(metadata_lazy_prop_t *mlp, int lonely)
{
  hts_mutex_lock(&metadata_mutex);
  mlp->mlp_lonely = lonely;
  mlp_dequeue(mlp);
  mlp->mlp_cb(mlp);
  hts_mutex_unlock(&metadata_mutex);
}


#define isnum(a) ((a) >= '0' && (a) <= '9')

/**
 *
 */
static const char *stopstrings[] = {
  "1080",
  "1080P",
  "3D",
  "720",
  "720P",
  "AC3",
  "AE",
  "AHDTV",
  "ANALOG",
  "AUDIO",
  "BDRIP",
  "CAM",
  "CD",
  "CD1",
  "CD2",
  "CD3",
  "CHRONO",
  "COLORIZED",
  "COMPLETE",
  "CONVERT",
  "CUSTOM",
  "DC",
  "DDC",
  "DIRFIX",
  "DISC",
  "DISC1",
  "DISC2",
  "DISC3",
  "DIVX",
  "DOLBY",
  "DSR",
  "DTS",
  "DTV",
  "DUAL",
  "DUBBED",
  "DVBRIP",
  "DVDRIP",
  "DVDSCR",
  "DVDSCREENER",
  "EXTENDED",
  "FINAL",
  "FS",
  "HARDCODED",
  "HARDSUB",
  "HARDSUBBED",
  "HD",
  "HDDVDRIP",
  "HDRIP",
  "HDTV",
  "HR",
  "INT",
  "INTERNAL",
  "LASERDISC",
  "LIMITED",
  "LINE",
  "LIVE.AUDIO",
  "MP3",
  "MULTI",
  "NATIVE",
  "NFOFIX",
  "NTSC",
  "OAR",
  "P2P",
  "PAL",
  "PDTV",
  "PPV",
  "PREAIR",
  "PROOFFIX",
  "PROPER",
  "PT",
  "R1",
  "R2",
  "R3",
  "R4",
  "R5",
  "R6",
  "RATED",
  "RC",
  "READ.NFO",
  "READNFO",
  "REMASTERED",
  "REPACK",
  "RERIP",
  "RETAIL",
  "SAMPLEFIX",
  "SATRIP",
  "SCR",
  "SCREENER",
  "SE",
  "STV",
  "SUBBED",
  "SUBFORCED",
  "SUBS",
  "SVCD",
  "SYNCFIX",
  "TC",
  "TELECINE",
  "TELESYNC",
  "THEATRICAL",
  "TS",
  "TVRIP",
  "UNCUT",
  "UNRATED",
  "UNSUBBED",
  "VCDRIP",
  "VHSRIP",
  "WATERMARKED",
  "WORKPRINT",
  "WP",
  "WS",
  "X264",
  "XVID",
  NULL,
};

/**
 *
 */
static void
metadata_filename_to_title(const char *filename, int *yearp, rstr_t **titlep)
{
  int year = 0;

  char *s = mystrdupa(filename);

  url_deescape(s);

  int i = strlen(s);

  while(i > 0) {
    
    if(i > 5 && s[i-5] == '.' &&
       isnum(s[i-4]) && isnum(s[i-3]) && isnum(s[i-2]) && isnum(s[i-1])) {
      year = atoi(s + i - 4);
      i -= 5;
      s[i] = 0;
      continue;
    }

    if(i > 7 && s[i-7] == ' ' && s[i-6] == '(' && 
       isnum(s[i-5]) && isnum(s[i-4]) && isnum(s[i-3]) && isnum(s[i-2]) &&
       s[i-1] == ')') {
      year = atoi(s + i - 5);
      i -= 7;
      s[i] = 0;
      continue;
    }

    int j;
    for(j = 0; stopstrings[j] != NULL; j++) {
      int len = strlen(stopstrings[j]);
      if(i > len+1 && (s[i-len-1] == '.' || s[i-len-1] == ' ') &&
	 !strncasecmp(s+i-len, stopstrings[j], len) &&
	 (s[i] == '.' || s[i] == ' ' || s[i] == '-' || s[i] == 0)) {
	i -= len+1;
	s[i] = 0;
	break;
      }
    }
    
    if(stopstrings[j] != NULL)
      continue;

    i--;
  }

  for(i = 0; s[i]; i++) {
    if(s[i] == '.') {
      s[i] = ' ';
    }
  }
 
  if(yearp != NULL)
    *yearp = year;

  if(titlep != NULL)
    *titlep = rstr_alloc(s);
}


static int
metadata_filename_to_episode(const char *s,
			     int *seasonp, int *episodep,
			     rstr_t **titlep)
{
  int i, j;
  int len = strlen(s);
  int season = -1;
  int episode = -1;
  for(i = 0; i < len; i++) {
    if((s[i] == 's' || s[i] == 'S') && isnum(s[i+1]) && isnum(s[i+2])) {
      int o = 3+i;
      if(s[o] == '.')
	o++;
  
      if((s[o] == 'e' || s[o] == 'E') && isnum(s[o+1]) && isnum(s[o+2])) {
	season = atoi(s+i+1);
	episode = atoi(s+o+1);
	break;
      }
    }
  }
  

  if(season == -1 || episode == -1)
    return -1;

  *seasonp = season;
  *episodep = episode;

  char *t = mystrdupa(s);
  url_deescape(t);

  for(j= 0; j < i; j++) {
    if(t[j] == '.') {
      t[j] = ' ';
    }
  }
  t[j] = 0;

  if(titlep != NULL)
    *titlep = rstr_alloc(t);
  return 0;
}





/**
 *
 */
rstr_t *
metadata_remove_postfix_rstr(rstr_t *name)
{
  const char *str = rstr_get(name);
  int len = strlen(str);
  if(len > 4 && str[len - 4] == '.') {
    return rstr_allocl(str, len - 4);
  }
  return rstr_dup(name);
}


/**
 *
 */
rstr_t *
metadata_remove_postfix(const char *str)
{
  int len = strlen(str);
  if(len > 4 && str[len - 4] == '.') {
    return rstr_allocl(str, len - 4);
  }
  return rstr_allocl(str, len);
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
metadata_source_t *
metadata_add_source(const char *name, const char *description,
		    int prio,  metadata_type_t type,
		    const metadata_source_funcs_t *funcs)
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

  return ms;

 err:
  metadb_close(db);
  return NULL;
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
  
  prop_concat_add_source(pc, prop_create(c, "nodes"), d);
}


/**
 *
 */
static void
mlp_dispatch(void)
{
  metadata_lazy_prop_t *mlp;
  while((mlp = TAILQ_FIRST(&mlpqueue)) != NULL) {
    TAILQ_REMOVE(&mlpqueue, mlp, mlp_link);
    mlp->mlp_queued = 0;
    mlp->mlp_cb(mlp);
  }
}



/**
 *
 */
static void *
metadata_thread(void *aux)
{
  hts_mutex_lock(&metadata_mutex);
  while(1) {
    struct prop_notify_queue exp, nor;

    int timo = TAILQ_FIRST(&mlpqueue) != NULL ? 50 : 0;
    hts_mutex_unlock(&metadata_mutex);
    int r = prop_courier_wait(metadata_courier, &nor, &exp, timo);
    hts_mutex_lock(&metadata_mutex);

    prop_notify_dispatch(&exp);
    prop_notify_dispatch(&nor);

    if(r)
      mlp_dispatch();
  }
  return NULL;
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

  metadata_courier = prop_courier_create_waitable();
  TAILQ_INIT(&mlpqueue);

  hts_thread_create_detached("metadata", metadata_thread, NULL, 
			     THREAD_PRIO_LOW);
  
  s = settings_add_dir(NULL, _p("Metadata"), "settings", NULL,
		       _p("Metadata configuration and provider settings"),
		       NULL);

  pc = prop_concat_create(prop_create(s, "nodes"), 0);

  add_provider_class(pc, METADATA_TYPE_VIDEO, _p("Providers for Video"));
  add_provider_class(pc, METADATA_TYPE_MUSIC, _p("Providers for Music"));

}
