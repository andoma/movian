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
  TAILQ_INIT(&md->md_cast);
  TAILQ_INIT(&md->md_crew);
  md->md_rating = -1;
  md->md_rating_count = -1;
  md->md_idx = -1;
  return md;
}


/**
 *
 */
static void
destroy_persons(struct metadata_person_queue *q)
{
  metadata_person_t *mp;

  while((mp = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, mp, mp_link);
    rstr_release(mp->mp_name);
    rstr_release(mp->mp_character);
    rstr_release(mp->mp_department);
    rstr_release(mp->mp_job);
    rstr_release(mp->mp_portrait);
    free(mp);
  }
}


/**
 *
 */
void
metadata_destroy(metadata_t *md)
{
  if(md->md_parent != NULL)
    metadata_destroy(md->md_parent);

  rstr_release(md->md_title);
  rstr_release(md->md_album);
  rstr_release(md->md_artist);
  rstr_release(md->md_format);
  rstr_release(md->md_genre);
  rstr_release(md->md_description);
  rstr_release(md->md_tagline);
  rstr_release(md->md_imdb_id);
  rstr_release(md->md_icon);
  rstr_release(md->md_backdrop);
  rstr_release(md->md_banner_wide);
  rstr_release(md->md_manufacturer);
  rstr_release(md->md_equipment);
  rstr_release(md->md_ext_id);

  free(md->md_redirect);

  metadata_stream_t *ms;

  while((ms = TAILQ_FIRST(&md->md_streams)) != NULL) {
    TAILQ_REMOVE(&md->md_streams, ms, ms_link);
    rstr_release(ms->ms_title);
    rstr_release(ms->ms_info);
    rstr_release(ms->ms_isolang);
    rstr_release(ms->ms_codec);
    free(ms);
  }

  destroy_persons(&md->md_cast);
  destroy_persons(&md->md_crew);
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
struct metadata_lazy_prop;

/**
 *
 */
typedef struct metadata_lazy_class {
  void (*mlc_load)(struct metadata_lazy_prop *mlp);
  void (*mlc_kill)(struct metadata_lazy_prop *mlp);
  void (*mlc_dtor)(struct metadata_lazy_prop *mlp);
  size_t mlc_alloc_size;
} metadata_lazy_class_t;


/**
 *
 */
typedef struct metadata_lazy_prop {
  TAILQ_ENTRY(metadata_lazy_prop) mlp_link;
  const metadata_lazy_class_t *mlp_class;
  uint64_t mlp_req_items;
  int16_t mlp_refcount;

  unsigned char mlp_zombie : 1;
  unsigned char mlp_queued : 1;

} metadata_lazy_prop_t;


/**
 *
 */
static void *
mlp_alloc(const metadata_lazy_class_t *class)
{
  metadata_lazy_prop_t *mlp = calloc(1, class->mlc_alloc_size);
  mlp->mlp_class = class;
  mlp->mlp_refcount = 1;
  return mlp;
}





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
  if(!mlp->mlp_zombie) {
    mlp->mlp_zombie = 1;
    if(mlp->mlp_class->mlc_kill != NULL)
      mlp->mlp_class->mlc_kill(mlp);
  }

  mlp->mlp_refcount--;
  if(mlp->mlp_refcount > 0)
    return;

  mlp_dequeue(mlp);

  mlp->mlp_class->mlc_dtor(mlp);
  free(mlp);
}


/**
 *
 */
static void
mlp_sub_cb(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_prop_t *mlp = opaque;
  va_list ap;
  int id;

  va_start(ap, event);
  switch(event) {
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    id = va_arg(ap, int);
    id = 1 << id;
    if(!(mlp->mlp_req_items & id)) {

      mlp->mlp_req_items |= id;
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
typedef struct metadata_lazy_artist {
  metadata_lazy_prop_t mla_mlp;
  rstr_t *mla_artist;
  prop_t *mla_prop;
  prop_sub_t *mla_sub;
} metadata_lazy_artist_t;



/**
 *
 */
static void
mlp_artist_load(metadata_lazy_prop_t *mlp)
{
  metadata_lazy_artist_t *mla = (metadata_lazy_artist_t *)mlp;

  void *db = metadb_get();
  int r;

  if(!db_begin(db)) {
    r = metadb_get_artist_pics(db, rstr_get(mla->mla_artist),
			       mlp_add_artist_to_prop, mla->mla_prop);
    
    if(r)
      lastfm_load_artistinfo(db, rstr_get(mla->mla_artist),
			     mlp_add_artist_to_prop, mla->mla_prop);
    
    db_commit(db);
  }
  metadb_close(db);
}



/**
 *
 */
static void
mlp_artist_dtor(metadata_lazy_prop_t *mlp)
{
  metadata_lazy_artist_t *mla = (metadata_lazy_artist_t *)mlp;
  rstr_release(mla->mla_artist);
  prop_ref_dec(mla->mla_prop);
  prop_unsubscribe(mla->mla_sub);
}


/**
 *
 */
const static metadata_lazy_class_t mlc_artist = {
  .mlc_load = mlp_artist_load,
  .mlc_dtor = mlp_artist_dtor,
  .mlc_alloc_size = sizeof(metadata_lazy_artist_t),
};


/**
 *
 */
void
metadata_bind_artistpics(prop_t *prop, rstr_t *artist)
{
  metadata_lazy_artist_t *mla = mlp_alloc(&mlc_artist);

  mla->mla_prop = prop_ref_inc(prop);
  mla->mla_artist = rstr_spn(artist, ";:,-[", 1);
  mla->mla_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY_EXP | PROP_SUB_SUBSCRIPTION_MONITOR,
		   PROP_TAG_CALLBACK_USER_INT, mlp_sub_cb, mla,
		   METADATA_PROP_ARTIST_PICTURES,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, prop,
		   NULL);
}


/**
 *
 */
typedef struct metadata_lazy_album {
  metadata_lazy_prop_t mla_mlp;
  rstr_t *mla_album;
  rstr_t *mla_artist;
  prop_t *mla_prop;
  prop_sub_t *mla_sub;
} metadata_lazy_album_t;


/**
 *
 */
static void
mlp_album_load(metadata_lazy_prop_t *mlp)
{
  metadata_lazy_album_t *mla = (metadata_lazy_album_t *)mlp;

  void *db = metadb_get();
  rstr_t *r;

  if(!db_begin(db)) {
    r = metadb_get_album_art(db,rstr_get(mla->mla_album),
			     rstr_get(mla->mla_artist));
  
    if(r == NULL) {
      // No album art available in our db, try to get some
      
      lastfm_load_albuminfo(db, rstr_get(mla->mla_album),
			    rstr_get(mla->mla_artist));
      
      r = metadb_get_album_art(db,rstr_get(mla->mla_album),
			       rstr_get(mla->mla_artist));
    }
    
    prop_set_rstring(mla->mla_prop, r);
    rstr_release(r);
    db_commit(db);
  }
  metadb_close(db);
}


/**
 *
 */
static void
mlp_album_dtor(metadata_lazy_prop_t *mlp)
{
  metadata_lazy_album_t *mla = (metadata_lazy_album_t *)mlp;
  rstr_release(mla->mla_album);
  rstr_release(mla->mla_artist);
  prop_ref_dec(mla->mla_prop);
}


/**
 *
 */
const static metadata_lazy_class_t mlc_album = {
  .mlc_load = mlp_album_load,
  .mlc_dtor = mlp_album_dtor,
  .mlc_alloc_size = sizeof(metadata_lazy_album_t),
};


/**
 *
 */
void
metadata_bind_albumart(prop_t *prop, rstr_t *artist, rstr_t *album)
{
  metadata_lazy_album_t *mla = mlp_alloc(&mlc_album);
  mla->mla_prop = prop_ref_inc(prop);
  mla->mla_artist = rstr_spn(artist, ";:,-[", 1);
  mla->mla_album  = rstr_spn(album, "[(", 1);
  mla->mla_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY_EXP | PROP_SUB_SUBSCRIPTION_MONITOR,
		   PROP_TAG_CALLBACK_USER_INT, mlp_sub_cb, mla,
		   METADATA_PROP_ALBUM_ART,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, prop,
		   NULL);
}





/**
 *
 */
struct metadata_lazy_video {

  metadata_lazy_prop_t mlv_mlp;

  rstr_t *mlv_url;
  rstr_t *mlv_filename;
  rstr_t *mlv_folder;
  rstr_t *mlv_imdb_id;

  prop_t *mlv_m;

  prop_t *mlv_title_opt;
  prop_t *mlv_info;
  prop_t *mlv_info_text;

  prop_t *mlv_source_opt;
  prop_sub_t *mlv_source_opt_sub;

  prop_t *mlv_alt_opt;
  prop_sub_t *mlv_alt_opt_sub;

  prop_t *mlv_sq;
  prop_sub_t *mlv_sq_sub;

  prop_t *mlv_refresh;
  prop_sub_t *mlv_refresh_sub;

  rstr_t *mlv_custom_query;
  rstr_t *mlv_custom_title;
 
  // Triggers
  prop_sub_t *mlv_trig_title;
  prop_sub_t *mlv_trig_desc;
  prop_sub_t *mlv_trig_rating;

  int mlv_duration;
  unsigned char mlv_type;
  unsigned char mlv_lonely : 1;
  unsigned char mlv_passive : 1;
  int mlv_dsid;
};



/**
 *
 */
void
mlv_unbind(metadata_lazy_video_t *mlv)
{
  hts_mutex_lock(&metadata_mutex);
  mlp_destroy(&mlv->mlv_mlp);
  hts_mutex_unlock(&metadata_mutex);
}


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
build_info_text(metadata_lazy_video_t *mlv, const metadata_t *md)
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

    metadata_source_t *ms = get_ms(mlv->mlv_type, md->md_dsid);

    snprintf(tmp, sizeof(tmp), rstr_get(fmt), 
	     ms ? ms->ms_description : "???",
	     rstr_get(qtype) ?: "???");

    txt = rstr_alloc(tmp);

    rstr_release(fmt);
    rstr_release(qtype);
  }

  prop_set_rstring(mlv->mlv_info_text, txt);
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
query_by_filename_or_dirname(void *db, metadata_lazy_video_t *mlv,
			     const metadata_source_funcs_t *msf, int *qtype)
{
  int year;
  rstr_t *title;
  int64_t rval;
  
  int season, episode;

  if(!metadata_filename_to_episode(rstr_get(mlv->mlv_filename), 
				   &season, &episode, &title)) {

    if(msf->query_by_episode == NULL)
      return METADATA_PERMANENT_ERROR;

    TRACE(TRACE_DEBUG, "METADATA",
	  "Performing search lookup for %s season:%d episode:%d, "
	  "based on filename",
	  rstr_get(title), season, episode);

    rval = msf->query_by_episode(db, rstr_get(mlv->mlv_url),
				 rstr_get(title), season, episode,
				 METADATA_QTYPE_EPISODE);
    *qtype = METADATA_QTYPE_EPISODE;
    rstr_release(title);
    return rval;
  }

  if(msf->query_by_title_and_year == NULL)
    return METADATA_PERMANENT_ERROR;



  metadata_filename_to_title(rstr_get(mlv->mlv_filename), &year, &title);
  
  TRACE(TRACE_DEBUG, "METADATA",
	"Performing search lookup for %s year:%d, based on filename",
	rstr_get(title), year);

  rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
				      rstr_get(title), year,
				      mlv->mlv_duration,
				      METADATA_QTYPE_FILENAME);
  *qtype = METADATA_QTYPE_FILENAME;

  if(rval == METADATA_PERMANENT_ERROR && year != 0) {
    // Try without year

    TRACE(TRACE_DEBUG, "METADATA",
	  "Performing search lookup for %s without year, based on filename",
	  rstr_get(title), year);

    rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					rstr_get(title), 0,
					mlv->mlv_duration,
					METADATA_QTYPE_FILENAME);
    *qtype = METADATA_QTYPE_FILENAME;
  }

  rstr_release(title);

  if(rval == METADATA_PERMANENT_ERROR && mlv->mlv_lonely) {

    metadata_filename_to_title(rstr_get(mlv->mlv_folder), &year, &title);
  
    TRACE(TRACE_DEBUG, "METADATA",
	  "Performing search lookup for %s year:%d, based on folder name",
	  rstr_get(title), year);

    rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					rstr_get(title), year,
					mlv->mlv_duration,
					METADATA_QTYPE_DIRECTORY);
    *qtype = METADATA_QTYPE_DIRECTORY;
    rstr_release(title);
  }

  return rval;
}


/**
 *
 */
static void
set_people(prop_t *parent, struct metadata_person_queue *q, int crew)
{
  metadata_person_t *mp;
  prop_destroy_childs(parent);
  TAILQ_FOREACH(mp, q, mp_link) {
    prop_t *p = prop_create_root(NULL);
    prop_set(p, "name", PROP_SET_RSTRING, mp->mp_name);
    if(crew) {
      prop_set(p, "department", PROP_SET_RSTRING, mp->mp_department);
      prop_set(p, "job", PROP_SET_RSTRING, mp->mp_job);
    } else {
      prop_set(p, "character", PROP_SET_RSTRING, mp->mp_character);
    }
    prop_set(p, "portrait", PROP_SET_RSTRING, mp->mp_portrait);

    if(prop_set_parent(p, parent)) {
      prop_destroy(p);
      break; // rest will fail as well
    }
  }
}


/**
 *
 */
static void
set_cast_n_crew(prop_t *p, metadata_t *md)
{
  prop_t *p2 = prop_create_r(p, "cast");
  set_people(p2, &md->md_cast, 0);
  prop_ref_dec(p2);

  p2 = prop_create_r(p, "crew");
  set_people(p2, &md->md_crew, 1);
  prop_ref_dec(p2);
}



/**
 * Must be in a transaction
 */
static int
mlv_get_video_info0(void *db, metadata_lazy_video_t *mlv, int refresh)
{
  rstr_t *title = NULL;
  metadata_t *md = NULL;
  int64_t rval;
  metadata_source_t *ms;
  struct metadata_source_list *msl = &metadata_sources[mlv->mlv_type];
  int r;
  int fixed_ds;
  const char *sq = rstr_get(mlv->mlv_custom_query);
  rstr_t *custom_title = mlv->mlv_custom_title;

  int sq_is_imdb_id = sq && sq[0] == 't' && sq[1] == 't' &&
    sq[2] >= '0' && sq[2] <= '9';

  if(sq && !*sq)
    sq = NULL;

  if(custom_title && !*rstr_get(custom_title))
    custom_title = NULL;

  if(mlv->mlv_duration && mlv->mlv_duration < 300) {
    goto bad;
  }

  LIST_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link)
    ms->ms_mark = 0;

 redo:
  if(md != NULL) {
    metadata_destroy(md);
    md = NULL;
  }

  if(!refresh) {
    r = metadb_get_videoinfo(db, rstr_get(mlv->mlv_url), msl, &fixed_ds, &md);
    if(r) {
      return r;
    }
  } else {
    refresh = 0;
    fixed_ds = 0;
  }

  prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 1);

  if(md == NULL || !md->md_preferred) {

    LIST_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link) {

      /* Skip disabled datasources */
      if(!ms->ms_enabled)
	continue;

      const metadata_source_funcs_t *msf = ms->ms_funcs;

      /* If we have a fixed datasource (requested by user)
       * skip all other datasources 
       */
      if(fixed_ds && fixed_ds != ms->ms_id)
	continue;

      /* Figure out what query to run */
      int qtype;
      const char *q;

      if(msf->query_by_imdb_id != NULL && sq_is_imdb_id) {
	qtype = METADATA_QTYPE_CUSTOM_IMDB;
	q = sq;
      } else if(sq != NULL) {
	qtype = METADATA_QTYPE_CUSTOM;
	q = NULL;
      } else if(msf->query_by_imdb_id != NULL && mlv->mlv_imdb_id != NULL) {
	qtype = METADATA_QTYPE_IMDB;
	q = rstr_get(mlv->mlv_imdb_id);

	if(mlv->mlv_passive)
	  continue;

      } else {
	qtype = METADATA_QTYPE_FILENAME_OR_DIRECTORY;
	q = NULL;

	if(mlv->mlv_passive)
	  continue;
      }

      if(md && md->md_dsid == ms->ms_id && is_qtype_compat(qtype, md->md_qtype))
	break;

      /**
       * If current metadata source is seen (marked by metadb_get_videoinfo())
       * and the query type corresponds we should be fully up to date,
       * thus continue
       */
       
      if(ms->ms_mark && is_qtype_compat(qtype, ms->ms_qtype)) {

	/**
	 * This weirdness is to be able to requery if we discover
	 * that a movie is lonely in its folder
	 * (To query using directory name)
	 */
	if(ms->ms_status == METAITEM_STATUS_ABSENT &&
	   ms->ms_qtype == METADATA_QTYPE_FILENAME &&
	   mlv->mlv_lonely) {

	} else {
	  continue;
	}
      }

      rval = metadb_videoitem_delete_from_ds(db, rstr_get(mlv->mlv_url),
					     ms->ms_id);

      if(rval == 0) {

	switch(qtype) {
	case METADATA_QTYPE_IMDB:
	case METADATA_QTYPE_CUSTOM_IMDB:

	  TRACE(TRACE_DEBUG, "METADATA",
		"Performing IMDB lookup for %s using %s", q, ms->ms_name);

	  rval = msf->query_by_imdb_id(db, rstr_get(mlv->mlv_url), q, qtype);
	  break;

	case METADATA_QTYPE_FILENAME_OR_DIRECTORY:
	  rval = query_by_filename_or_dirname(db, mlv, msf, &qtype);
	  break;

	case METADATA_QTYPE_CUSTOM:
	  if(msf->query_by_title_and_year == NULL)
	    continue;

	  TRACE(TRACE_DEBUG, "METADATA",
		"Performing custom search lookup for %s using %s", sq,
		ms->ms_name);
	  rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					      sq, 0, mlv->mlv_duration, qtype);
	  break;

	default:
	  continue;
	}
      }

      if(rval == METADATA_DEADLOCK || rval == METADATA_TEMPORARY_ERROR) {
	if(rval == METADATA_TEMPORARY_ERROR)
	  TRACE(TRACE_DEBUG, "METADATA", "Temporary error for %s",
		rstr_get(mlv->mlv_url));

	prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);
	if(md != NULL)
	  metadata_destroy(md);
	return rval;
      }

      if(rval == METADATA_PERMANENT_ERROR)
	rval = metadb_insert_videoitem(db, rstr_get(mlv->mlv_url), ms->ms_id,
				       "0", NULL, METAITEM_STATUS_ABSENT, 0,
				       qtype, ms->ms_cfgid);

      if(rval < 0) {
	prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);
	if(md != NULL)
	  metadata_destroy(md);
	return rval;
      }
      goto redo;
    }
  }

  if(md != NULL &&
     md->md_metaitem_status == METAITEM_STATUS_PARTIAL &&
     md->md_ext_id != NULL &&
     (ms = get_ms(mlv->mlv_type, md->md_dsid)) != NULL &&
     ms->ms_funcs->query_by_id != NULL &&
     (mlv->mlv_mlp.mlp_req_items & ms->ms_complete_props)) {
    
    TRACE(TRACE_DEBUG, "METADATA",
	  "Performing additional query for %s : %s", ms->ms_name,
	  rstr_get(md->md_ext_id));

    rval = ms->ms_funcs->query_by_id(db, rstr_get(mlv->mlv_url),
				     rstr_get(md->md_ext_id));
    
    metadata_destroy(md);

    if(rval == METADATA_DEADLOCK)
      return METADATA_DEADLOCK;

    if(rval == METADATA_TEMPORARY_ERROR) {
      TRACE(TRACE_DEBUG, "METADATA", "Temporary error for %s",
	    rstr_get(mlv->mlv_url));
      return rval;
    }

    if(rval == METADATA_PERMANENT_ERROR)
      TRACE(TRACE_DEBUG, "METADATA", "Permanent error for %s",
	    rstr_get(mlv->mlv_url));

    r = metadb_get_videoinfo(db, rstr_get(mlv->mlv_url), msl, &fixed_ds, &md);
    if(r) {
      prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);
      return r;
    }
  }

  title = rstr_dup(custom_title);

  if(md != NULL) {
    mlv->mlv_dsid = md->md_dsid;
    ms = get_ms(mlv->mlv_type, md->md_dsid);
    if(ms != NULL)
      prop_set(mlv->mlv_m, "source", PROP_SET_STRING, ms->ms_description);

    prop_set(mlv->mlv_m, "icon",        PROP_SET_RSTRING, md->md_icon);
    prop_set(mlv->mlv_m, "tagline",     PROP_SET_RSTRING, md->md_tagline);
    prop_set(mlv->mlv_m, "description", PROP_SET_RSTRING, md->md_description);
    prop_set(mlv->mlv_m, "backdrop",    PROP_SET_RSTRING, md->md_backdrop);
    prop_set(mlv->mlv_m, "genre",       PROP_SET_RSTRING, md->md_genre);

    prop_set(mlv->mlv_m, "year",
	      md->md_year ? PROP_SET_INT : PROP_SET_VOID,
	      md->md_year);

    prop_set(mlv->mlv_m, "rating",
	      md->md_rating >= 0 ? PROP_SET_INT : PROP_SET_VOID,
	      md->md_rating);

    prop_set(mlv->mlv_m, "rating_count",
	      md->md_rating_count >= 0 ? PROP_SET_INT : PROP_SET_VOID,
	      md->md_rating_count);
      
    set_cast_n_crew(mlv->mlv_m, md);

    metadata_t *season = NULL;
    metadata_t *series = NULL;

    if(md->md_parent && md->md_parent->md_type == METADATA_TYPE_SEASON) {
      season = md->md_parent;
      // It's a TV serie

      prop_t *pepi = prop_create_r(mlv->mlv_m, "episode");
      prop_t *psea = prop_create_r(mlv->mlv_m, "season");
      prop_t *pser = prop_create_r(mlv->mlv_m, "series");

      prop_set(pepi, "number",     PROP_SET_INT,     md->md_idx);
      prop_set(pepi, "title",      PROP_SET_RSTRING, md->md_title);
      prop_set(pepi, "backdrop",   PROP_SET_RSTRING, md->md_backdrop);
      prop_set(pepi, "bannerWide", PROP_SET_RSTRING, md->md_banner_wide);
      prop_set(pepi, "icon",       PROP_SET_RSTRING, md->md_icon);

      prop_set(psea, "number",     PROP_SET_INT,     season->md_idx);
      prop_set(psea, "title",      PROP_SET_RSTRING, season->md_title);
      prop_set(psea, "backdrop",   PROP_SET_RSTRING, season->md_backdrop);
      prop_set(psea, "bannerWide", PROP_SET_RSTRING, season->md_banner_wide);
      prop_set(psea, "icon",       PROP_SET_RSTRING, season->md_icon);

      set_cast_n_crew(psea, season);

      if(season->md_parent &&
	 season->md_parent->md_type == METADATA_TYPE_SERIES) {
	series = season->md_parent;

	prop_set(pser, "title",      PROP_SET_RSTRING, series->md_title);
	prop_set(pser, "backdrop",   PROP_SET_RSTRING, series->md_backdrop);
	prop_set(pser, "bannerWide", PROP_SET_RSTRING, series->md_banner_wide);
	prop_set(pser, "icon",       PROP_SET_RSTRING, series->md_icon);
	set_cast_n_crew(pser, series);
      }

      title = title ?: rstr_dup(mlv->mlv_filename);

      prop_ref_dec(pepi);
      prop_ref_dec(psea);
      prop_ref_dec(pser);

    } else {
      title = title ?: rstr_dup(md->md_title);
    }

    if(season)
      prop_set(mlv->mlv_m, "vtype", PROP_SET_STRING, "tvseries");
    else
      prop_set(mlv->mlv_m, "vtype", PROP_SET_VOID);

    build_info_text(mlv, md);
    metadata_destroy(md);

  } else {

  bad:

    prop_set(mlv->mlv_m, "source",      PROP_SET_VOID);
    prop_set(mlv->mlv_m, "icon",        PROP_SET_VOID);
    prop_set(mlv->mlv_m, "tagline",     PROP_SET_VOID);
    prop_set(mlv->mlv_m, "description", PROP_SET_VOID);
    prop_set(mlv->mlv_m, "backdrop",    PROP_SET_VOID);
    prop_set(mlv->mlv_m, "genre",       PROP_SET_VOID);
    prop_set(mlv->mlv_m, "year",        PROP_SET_VOID);
    prop_set(mlv->mlv_m, "rating",      PROP_SET_VOID);
    prop_set(mlv->mlv_m, "rating_count",PROP_SET_VOID);
    prop_set(mlv->mlv_m, "vtype",       PROP_SET_VOID);

    title = title ?: rstr_dup(custom_title ?: mlv->mlv_filename);

    mlv->mlv_dsid = 0;

    build_info_text(mlv, NULL);
  }

  prop_set(mlv->mlv_m, "title", PROP_SET_RSTRING, title);
  rstr_release(title);

  prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);

  LIST_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link)
    ms->ms_mark = 0;

  return 0;
}


/**
 *
 */
static void
mlv_load(metadata_lazy_prop_t *mlp)
{
  if(mlp->mlp_zombie)
    return;

  metadata_lazy_video_t *mlv = (metadata_lazy_video_t *)mlp;

  void *db = metadb_get();

 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  int r = mlv_get_video_info0(db, mlv, 0);
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
load_alternatives(metadata_lazy_video_t *mlv)
{
  prop_t *p = prop_create_r(mlv->mlv_alt_opt, "options");
  metadb_videoitem_alternatives(p, rstr_get(mlv->mlv_url), mlv->mlv_dsid,
				mlv->mlv_alt_opt_sub);
  prop_ref_dec(p);
}


/**
 *
 */
static void
mlv_set_preferred(metadata_lazy_video_t *mlv, int64_t vid)
{
  void *db = metadb_get();
  int r;
 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }
  r = metadb_videoitem_set_preferred(db, rstr_get(mlv->mlv_url), vid);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = mlv_get_video_info0(db, mlv, 0);
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
mlv_sub_alternative(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_video_t *mlv = opaque;
  prop_t *p;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(&mlv->mlv_mlp);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    load_alternatives(mlv);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    if(r != NULL)
      mlv_set_preferred(mlv, atoi(rstr_get(r)));
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
load_sources(metadata_lazy_video_t *mlv)
{
  metadata_source_t *ms;
  prop_t *active = NULL;
  prop_vec_t *pv = prop_vec_create(10);
  prop_t *c, *p = prop_create_r(mlv->mlv_source_opt, "options");
  int cur = metadb_item_get_preferred_ds(rstr_get(mlv->mlv_url));

  c = prop_create_root(NULL);
  prop_link(_p("Automatic"), prop_create(c, "title"));
  pv = prop_vec_append(pv, c);

  LIST_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link) {
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
    prop_select_ex(active, NULL, mlv->mlv_source_opt_sub);
  else if(prop_vec_len(pv) > 0)
    prop_select_ex(prop_vec_get(pv, 0), NULL, mlv->mlv_source_opt_sub);

  prop_ref_dec(active);
  prop_vec_release(pv);
  prop_ref_dec(p);
}

/**
 *
 */
static void
mlv_set_source(metadata_lazy_video_t *mlv, const char *name)
{
  int id = 0;

  if(name != NULL) {

    if(!strcmp(name, "1")) {
      // dsid 1 is reserved for local file
      id = 1;
    } else {
      metadata_source_t *ms = NULL;
      LIST_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link) {
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

  r = metadb_item_set_preferred_ds(db, rstr_get(mlv->mlv_url), id);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = mlv_get_video_info0(db, mlv, 0);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  db_commit(db);
  metadb_close(db);
  load_alternatives(mlv);
}


/**
 *
 */
static void
mlv_sub_source(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_video_t *mlv = opaque;
  prop_t *p;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(&mlv->mlv_mlp);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    load_sources(mlv);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    rstr_t *r = prop_get_name(p);
    mlv_set_source(mlv, rstr_get(r));
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
mlv_refresh_video_info(metadata_lazy_video_t *mlv)
{
  void *db = metadb_get();
  int r;

  assert(mlv != NULL);

 again:
  if(db_begin(db)) {
    metadb_close(db);
    return;
  }

  r = metadb_item_set_preferred_ds(db, rstr_get(mlv->mlv_url), 0);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = metadb_videoitem_set_preferred(db, rstr_get(mlv->mlv_url), 0);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  r = mlv_get_video_info0(db, mlv, 1);
  if(r == METADATA_DEADLOCK) {
    db_rollback_deadlock(db);
    goto again;
  }

  db_commit(db);
  metadb_close(db);
  load_alternatives(mlv);
}


/**
 *
 */
static void
mlv_sub_actions(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_video_t *mlv = opaque;
  va_list ap;
  event_t *e;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(&mlv->mlv_mlp);
    break;

  case PROP_EXT_EVENT:
    if(mlv->mlv_mlp.mlp_zombie)
      break;

    e = va_arg(ap, event_t *);
    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      if(!strcmp(e->e_payload, "refreshMetadata")) {
	mlv_refresh_video_info(mlv);
	load_alternatives(mlv);
	const char *s;

	s = rstr_get(mlv->mlv_custom_query);
	if(s && *s)
	  kv_url_opt_set(rstr_get(mlv->mlv_url), KVSTORE_DOMAIN_SYS,
			 "metacustomquery", KVSTORE_SET_STRING, s);
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
mlv_sub_query(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_video_t *mlv = opaque;
  va_list ap;
  rstr_t *r;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(&mlv->mlv_mlp);
    break;

  case PROP_SET_RSTRING:
    r = va_arg(ap, rstr_t *);
    rstr_set(&mlv->mlv_custom_query, r);
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
mlv_sub_custom_title(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_video_t *mlv = opaque;
  va_list ap;
  rstr_t *r;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(&mlv->mlv_mlp);
    break;

  case PROP_SET_RSTRING:
    r = va_arg(ap, rstr_t *);
    rstr_set(&mlv->mlv_custom_title, r);

    rstr_t *custom_title = r;

    metadb_item_set_user_title(rstr_get(mlv->mlv_url),
			       rstr_get(mlv->mlv_custom_title));

    if(custom_title && !*rstr_get(custom_title)) {
      mlv_refresh_video_info(mlv);
    } else {
      prop_set(mlv->mlv_m, "title", PROP_SET_RSTRING,
	       custom_title ?: mlv->mlv_filename);
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
mlv_kill(metadata_lazy_prop_t *mlp)
{
  metadata_lazy_video_t *mlv = (metadata_lazy_video_t *)mlp;
  prop_destroy(mlv->mlv_title_opt);
  prop_destroy(mlv->mlv_info);
  prop_destroy(mlv->mlv_source_opt);
  prop_destroy(mlv->mlv_alt_opt);
  prop_destroy(mlv->mlv_sq);
  prop_destroy(mlv->mlv_refresh);
}

/**
 *
 */
static void
mlv_dtor(metadata_lazy_prop_t *mlp)
{
  metadata_lazy_video_t *mlv = (metadata_lazy_video_t *)mlp;

  prop_unsubscribe(mlv->mlv_trig_title);
  prop_unsubscribe(mlv->mlv_trig_desc);
  prop_unsubscribe(mlv->mlv_trig_rating);

  
  prop_unsubscribe(mlv->mlv_source_opt_sub);
  prop_unsubscribe(mlv->mlv_alt_opt_sub);
  prop_unsubscribe(mlv->mlv_sq_sub);
  prop_unsubscribe(mlv->mlv_refresh_sub);

  prop_ref_dec(mlv->mlv_title_opt);
  prop_ref_dec(mlv->mlv_info);
  prop_ref_dec(mlv->mlv_info_text);
  prop_ref_dec(mlv->mlv_source_opt);
  prop_ref_dec(mlv->mlv_alt_opt);
  prop_ref_dec(mlv->mlv_refresh);

  rstr_release(mlv->mlv_filename);
  rstr_release(mlv->mlv_imdb_id);
  prop_ref_dec(mlv->mlv_sq);
  rstr_release(mlv->mlv_custom_query);
  rstr_release(mlv->mlv_custom_title);
  rstr_release(mlv->mlv_url);
}


/**
 *
 */
const static metadata_lazy_class_t mlc_video = {
  .mlc_load = mlv_load,
  .mlc_dtor = mlv_dtor,
  .mlc_kill = mlv_kill,
  .mlc_alloc_size = sizeof(metadata_lazy_video_t),
};


/**
 *
 */
static prop_sub_t *
mlv_sub(metadata_lazy_video_t *mlv, prop_t *m,
	const char *name, metadata_prop_t id)
{
  const char *vec[3];
  vec[0] = "metadata";
  vec[1] = name;
  vec[2] = 0;
  mlv->mlv_mlp.mlp_refcount++;

  prop_sub_t *s = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY_EXP | PROP_SUB_SUBSCRIPTION_MONITOR,
		   PROP_TAG_CALLBACK_USER_INT, mlp_sub_cb, mlv, id,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_NAMED_ROOT, m, "metadata",
		   PROP_TAG_NAME_VECTOR, vec,
		   NULL);
  return s;
}

/**
 *
 */
metadata_lazy_video_t *
metadata_bind_video_info(prop_t *prop, rstr_t *url, rstr_t *filename,
			 rstr_t *imdb_id, int duration,
			 prop_t *options, prop_t *root,
			 rstr_t *folder, int lonely, int passive)
{
  metadata_lazy_video_t *mlv = mlp_alloc(&mlc_video);

  mlv->mlv_filename = rstr_dup(filename);
  mlv->mlv_folder = rstr_dup(folder);
  mlv->mlv_url = rstr_dup(url);
  mlv->mlv_duration = duration;
  mlv->mlv_imdb_id = rstr_dup(imdb_id);
  mlv->mlv_type = METADATA_TYPE_VIDEO;
  mlv->mlv_lonely = lonely;
  mlv->mlv_passive = passive;
  mlv->mlv_m = prop_ref_inc(prop);

  mlv->mlv_trig_title =
    mlv_sub(mlv, prop, "title", METADATA_PROP_TITLE);
  mlv->mlv_trig_desc =
    mlv_sub(mlv, prop, "description", METADATA_PROP_DESCRIPTION);
  mlv->mlv_trig_rating =
    mlv_sub(mlv, prop, "rating", METADATA_PROP_RATING);
  

  prop_t *m;
  prop_vec_t *pv = prop_vec_create(10);

  // Separator

  mlv->mlv_title_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlv->mlv_title_opt, "type"), "separator");
  prop_set_int(prop_create(mlv->mlv_title_opt, "enabled"), 1);
  m = prop_create(mlv->mlv_title_opt, "metadata");
  prop_link(_p("Metadata search"), prop_create(m, "title"));

  pv = prop_vec_append(pv, mlv->mlv_title_opt);

  // Info

  mlv->mlv_info = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlv->mlv_info, "type"), "info");
  prop_set_int(prop_create(mlv->mlv_info, "enabled"), 1);
  mlv->mlv_info_text = prop_create_r(prop_create(mlv->mlv_info, "metadata"),
				     "title");

  pv = prop_vec_append(pv, mlv->mlv_info);


  // Metadata source selection

  mlv->mlv_source_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlv->mlv_source_opt, "type"), "multiopt");
  prop_set_int(prop_create(mlv->mlv_source_opt, "enabled"), 1);
  m = prop_create(mlv->mlv_source_opt, "metadata");
  prop_link(_p("Metadata source"), prop_create(m, "title"));

  mlv->mlv_source_opt_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_source, mlv,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, prop_create(mlv->mlv_source_opt, "options"),
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, mlv->mlv_source_opt);

  // Metadata alternative selection

  mlv->mlv_alt_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlv->mlv_alt_opt, "type"), "multiopt");
  prop_set_int(prop_create(mlv->mlv_alt_opt, "enabled"), 1);
  m = prop_create(mlv->mlv_alt_opt, "metadata");
  prop_link(_p("Movie"), prop_create(m, "title"));

  mlv->mlv_alt_opt_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_alternative, mlv,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, prop_create(mlv->mlv_alt_opt, "options"),
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, mlv->mlv_alt_opt);



  // Metadata search query

  mlv->mlv_sq = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlv->mlv_sq, "type"), "string");
  prop_set_int(prop_create(mlv->mlv_sq, "enabled"), 1);
  m = prop_create(mlv->mlv_sq, "metadata");
  prop_set_string(prop_create(mlv->mlv_sq, "action"), "refreshMetadata");
  prop_link(_p("Custom search query"), prop_create(m, "title"));
  prop_t *v = prop_create(mlv->mlv_sq, "value");

  rstr_t *cur = kv_url_opt_get_rstr(rstr_get(url), KVSTORE_DOMAIN_SYS, 
				    "metacustomquery");

  if(cur != NULL) {
    prop_set_rstring(v, cur);
    rstr_release(cur);
  }

  mlv->mlv_sq_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_query, mlv,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, v,
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, mlv->mlv_sq);
    

  // Metadata refresh

  mlv->mlv_refresh = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlv->mlv_refresh, "type"), "action");
  prop_set_string(prop_create(mlv->mlv_refresh, "action"), "refreshMetadata");
  prop_set_int(prop_create(mlv->mlv_refresh, "enabled"), 1);
  m = prop_create(mlv->mlv_refresh, "metadata");
  prop_link(_p("Refresh metadata"), prop_create(m, "title"));

  mlv->mlv_refresh_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_actions, mlv,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, root,
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, mlv->mlv_refresh);

  // Custom movie title

  mlv->mlv_sq = prop_ref_inc(prop_create_root(NULL));
  prop_set_string(prop_create(mlv->mlv_sq, "type"), "string");
  prop_set_int(prop_create(mlv->mlv_sq, "enabled"), 1);
  m = prop_create(mlv->mlv_sq, "metadata");
  prop_set_string(prop_create(mlv->mlv_sq, "action"), "refreshMetadata");
  prop_link(_p("Custom title"), prop_create(m, "title"));
  v = prop_create(mlv->mlv_sq, "value");

  cur = metadb_item_get_user_title(rstr_get(url));

  if(cur != NULL) {
    prop_set_rstring(v, cur);
    rstr_release(cur);
  }

  mlv->mlv_sq_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_custom_title, mlv,
		   PROP_TAG_COURIER, metadata_courier,
		   PROP_TAG_ROOT, v,
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, mlv->mlv_sq);

  // Add all options

  prop_set_parent_vector(pv, options, NULL, NULL);
  prop_vec_release(pv);

  return mlv;
}


/**
 *
 */
void
mlv_set_imdb_id(metadata_lazy_video_t *mlv, rstr_t *imdb_id)
{
  hts_mutex_lock(&metadata_mutex);
  rstr_set(&mlv->mlv_imdb_id, imdb_id);
  mlp_dequeue(&mlv->mlv_mlp);
  mlv_load(&mlv->mlv_mlp);
  hts_mutex_unlock(&metadata_mutex);
}

/**
 *
 */
void
mlv_set_duration(metadata_lazy_video_t *mlv, int duration)
{
  hts_mutex_lock(&metadata_mutex);
  mlv->mlv_duration = duration;
  mlp_dequeue(&mlv->mlv_mlp);
  mlv_load(&mlv->mlv_mlp);
  hts_mutex_unlock(&metadata_mutex);
}


/**
 *
 */
void
mlv_set_lonely(metadata_lazy_video_t *mlv, int lonely)
{
  hts_mutex_lock(&metadata_mutex);
  if(mlv->mlv_lonely != lonely) {
    mlv->mlv_lonely = lonely;
    mlp_dequeue(&mlv->mlv_mlp);
    mlv_load(&mlv->mlv_mlp);
  }
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
    mlp->mlp_class->mlc_load(mlp);
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

