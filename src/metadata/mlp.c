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
#include "prop/prop_linkselected.h"

#include "main.h"
#include "media/media.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/str.h"
#include "misc/regex.h"
#include "api/lastfm.h"

#include "metadata.h"
#include "metadata_str.h"
#include "metadata_sources.h"

#include "db/db_support.h"
#include "db/kvstore.h"

#include "video/video_settings.h"

#include "settings.h"
#include "subtitles/subtitles.h"


static hts_mutex_t metadata_mutex;
static hts_cond_t metadata_loading_cond;

static int metadata_num_threads;

static void metadata_threads_start(void);

TAILQ_HEAD(metadata_lazy_prop_queue, metadata_lazy_prop);
static struct metadata_lazy_prop_queue mlpqueue;
struct metadata_lazy_prop;

/**
 *
 */
typedef struct metadata_lazy_class {
  void (*mlc_load)(void *db, struct metadata_lazy_prop *mlp);
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
  unsigned char mlp_loading : 1;

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
  metadata_threads_start();
}


/**
 *
 */
static void
mlp_unqueue(metadata_lazy_prop_t *mlp)
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
mlp_release(metadata_lazy_prop_t *mlp)
{
  mlp->mlp_refcount--;
  if(mlp->mlp_refcount > 0)
    return;

  mlp_unqueue(mlp);

  mlp->mlp_class->mlc_dtor(mlp);
  free(mlp);
}


/**
 *
 */
static void
mlp_retain(metadata_lazy_prop_t *mlp)
{
  mlp->mlp_refcount++;
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

  mlp_release(mlp);
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

#if 0
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
#endif


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
mlp_artist_load(void *db, metadata_lazy_prop_t *mlp)
{
#if 0
  // lastfm artistinfo no longer give any images, so don't even do it
  metadata_lazy_artist_t *mla = (metadata_lazy_artist_t *)mlp;
  int r;

  mlp_retain(mlp);
  hts_mutex_unlock(&metadata_mutex);

  r = metadb_get_artist_pics(db, rstr_get(mla->mla_artist),
                             mlp_add_artist_to_prop, mla->mla_prop);
  if(r)
    lastfm_load_artistinfo(db, rstr_get(mla->mla_artist),
                           mlp_add_artist_to_prop, mla->mla_prop);

  hts_mutex_lock(&metadata_mutex);
  mlp_release(mlp);
#endif
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
		   PROP_TAG_MUTEX, &metadata_mutex,
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
mlp_album_load(void *db, metadata_lazy_prop_t *mlp)
{
  metadata_lazy_album_t *mla = (metadata_lazy_album_t *)mlp;
  rstr_t *r;

  mlp_retain(mlp);

  hts_mutex_unlock(&metadata_mutex);

  r = metadb_get_album_art(db, rstr_get(mla->mla_album),
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

  hts_mutex_lock(&metadata_mutex);
  mlp_release(mlp);
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
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_ROOT, prop,
		   NULL);
}





/**
 *
 */
struct metadata_lazy_video {

  metadata_lazy_prop_t mlv_mlp;
  rstr_t *mlv_initiator;
  rstr_t *mlv_url;
  rstr_t *mlv_filename;
  rstr_t *mlv_folder;
  rstr_t *mlv_imdb_id;

  prop_t *mlv_root;
  prop_t *mlv_m;

  prop_t *mlv_title_opt;
  prop_t *mlv_info_opt;
  prop_t *mlv_info_text_prop;
  rstr_t *mlv_info_text_rstr;

  prop_t *mlv_source_opt;
  prop_sub_t *mlv_source_opt_sub;

  prop_t *mlv_alt_opt;
  prop_sub_t *mlv_alt_opt_sub;

  prop_t *mlv_refresh_opt;
  prop_sub_t *mlv_refresh_sub;

  rstr_t *mlv_custom_query;
  prop_t *mlv_custom_query_opt;
  prop_sub_t *mlv_custom_query_sub;

  rstr_t *mlv_custom_title;
  prop_t *mlv_custom_title_opt;
  prop_sub_t *mlv_custom_title_sub;
 
  prop_sub_t *mlv_options_monitor_sub;

  // Triggers
  prop_sub_t *mlv_trig_title;
  prop_sub_t *mlv_trig_desc;
  prop_sub_t *mlv_trig_rating;

  float mlv_duration;
  unsigned char mlv_type;
  unsigned char mlv_lonely : 1;
  unsigned char mlv_passive : 1;
  unsigned char mlv_manual : 1;
  unsigned char mlv_qtype : 5;
  union {
    int16_t mlv_season;
    int16_t mlv_year;
  };
  int16_t mlv_episode;
  int mlv_dsid;
};


/**
 *
 */
static void
mlv_cleanup(metadata_lazy_video_t *mlv)
{
  prop_set(mlv->mlv_m, "source",       PROP_SET_VOID);
  prop_set(mlv->mlv_m, "icon",         PROP_SET_VOID);
  prop_set(mlv->mlv_m, "tagline",      PROP_SET_VOID);
  prop_set(mlv->mlv_m, "description",  PROP_SET_VOID);
  prop_set(mlv->mlv_m, "backdrop",     PROP_SET_VOID);
  prop_set(mlv->mlv_m, "genre",        PROP_SET_VOID);
  prop_set(mlv->mlv_m, "year",         PROP_SET_VOID);
  prop_set(mlv->mlv_m, "rating",       PROP_SET_VOID);
  prop_set(mlv->mlv_m, "rating_count", PROP_SET_VOID);
  prop_set(mlv->mlv_m, "vtype",        PROP_SET_VOID);
}




/**
 *
 */
void
mlv_unbind(metadata_lazy_video_t *mlv, int cleanup)
{
  hts_mutex_lock(&metadata_mutex);
  if(cleanup) {
    mlv_cleanup(mlv);
    prop_set(mlv->mlv_m, "title", PROP_SET_RSTRING, mlv->mlv_filename);
  }
  mlp_destroy(&mlv->mlv_mlp);
  hts_mutex_unlock(&metadata_mutex);
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
    case METADATA_QTYPE_MOVIE:
      qtype = _("Movie title");
      break;
    case METADATA_QTYPE_TVSHOW:
      qtype = _("Title, Season, Episode");
      break;
    }

    rstr_t *fmt = _("Metadata loaded from <b>%s</b> based on %s");

    const metadata_source_t *ms =
      metadata_source_get(mlv->mlv_type, md->md_dsid);

    snprintf(tmp, sizeof(tmp), rstr_get(fmt),
	     ms ? ms->ms_description : "???",
	     rstr_get(qtype) ?: "???");

    txt = rstr_alloc(tmp);

    rstr_release(fmt);
    rstr_release(qtype);
  }

  prop_set_rstring_ex(mlv->mlv_info_text_prop, NULL, txt, PROP_STR_RICH);
  rstr_release(mlv->mlv_info_text_rstr);

  mlv->mlv_info_text_rstr = txt;
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
query_by_filename_or_dirname(void *db, const metadata_lazy_video_t *mlv,
			     const metadata_source_funcs_t *msf, int *qtype,
                             float duration, int lonely)
{
  int year;
  rstr_t *title;
  int64_t rval;
  
  int season, episode;

  if(!metadata_filename_to_episode(rstr_get(mlv->mlv_filename), 
				   &season, &episode, &title)) {

    if(msf->query_by_episode == NULL)
      return METADATA_PERMANENT_ERROR;

    if(title == NULL) {
      if(mlv->mlv_folder != NULL)
        metadata_folder_to_season(rstr_get(mlv->mlv_folder),
                                  NULL, &title);

      if(title == NULL) {
	METADATA_TRACE(
	      "Unable to figure out name of series from %s", 
	      rstr_get(mlv->mlv_filename));
	return METADATA_PERMANENT_ERROR;
      }
      METADATA_TRACE(
	    "Performing search lookup for %s season:%d episode:%d, "
	    "based on filename and foldername",
	    rstr_get(title), season, episode);
    } else {
      METADATA_TRACE(
	    "Performing search lookup for %s season:%d episode:%d, "
	    "based on filename",
	    rstr_get(title), season, episode);
    }

    rval = msf->query_by_episode(db, rstr_get(mlv->mlv_url),
				 rstr_get(title), season, episode,
				 METADATA_QTYPE_EPISODE,
                                 rstr_get(mlv->mlv_initiator));
    *qtype = METADATA_QTYPE_EPISODE;
    rstr_release(title);
    return rval;
  }

  if(msf->query_by_title_and_year == NULL)
    return METADATA_PERMANENT_ERROR;

  if(is_reasonable_movie_name(rstr_get(mlv->mlv_filename))) {

    metadata_filename_to_title(rstr_get(mlv->mlv_filename), &year, &title);
  
    METADATA_TRACE(
	  "Performing search lookup for %s year:%d, based on filename",
	  rstr_get(title), year);

    rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					rstr_get(title), year,
					duration,
					METADATA_QTYPE_FILENAME,
                                        rstr_get(mlv->mlv_initiator));
    *qtype = METADATA_QTYPE_FILENAME;

    if(rval == METADATA_PERMANENT_ERROR && year != 0) {
      // Try without year

      METADATA_TRACE(
	    "Performing search lookup for %s without year, based on filename",
	    rstr_get(title), year);

      rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					  rstr_get(title), 0,
					  duration,
					  METADATA_QTYPE_FILENAME,
                                          rstr_get(mlv->mlv_initiator));
      *qtype = METADATA_QTYPE_FILENAME;
    }
    rstr_release(title);
  } else {
    rval = METADATA_PERMANENT_ERROR;
  }


  if(rval == METADATA_PERMANENT_ERROR && lonely && mlv->mlv_folder != NULL) {

    metadata_filename_to_title(rstr_get(mlv->mlv_folder), &year, &title);

    METADATA_TRACE(
	  "Performing search lookup for %s year:%d, based on folder name",
	  rstr_get(title), year);

    rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					rstr_get(title), year,
					duration,
					METADATA_QTYPE_DIRECTORY,
                                        rstr_get(mlv->mlv_initiator));
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
 *
 */
static int
mlv_get_video_info0(void *db, metadata_lazy_video_t *mlv, int refresh)
{
  rstr_t *title = NULL;
  metadata_t *md = NULL;
  int64_t rval;
  const metadata_source_t *ms;
  int r;
  int fixed_ds;
  const char *sq = rstr_get(mlv->mlv_custom_query);

  int sq_is_imdb_id = sq && sq[0] == 't' && sq[1] == 't' &&
    sq[2] >= '0' && sq[2] <= '9';

  if(sq && !*sq)
    sq = NULL;

  /**
   * Get a list of currently available metadata sources
   */
  metadata_source_query_info_t *msqivec;
  int num_msqi = 0;

  hts_mutex_lock(&metadata_sources_mutex);

  TAILQ_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link)
    num_msqi++;

  msqivec = alloca(num_msqi * sizeof(metadata_source_query_info_t));

  int i = 0;
  TAILQ_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link) {
    msqivec[i].msqi_ms = ms;

    msqivec[i].msqi_mark = 0;
    msqivec[i].msqi_qtype = 0;
    msqivec[i].msqi_status = 0;

    i++;
  }

  hts_mutex_unlock(&metadata_sources_mutex);

  /**
   * Ok, we are going to unlock metadata_mutex since we're going to do
   * network and DB I/O. Now we need to be a bit careful since some of
   * the members in metadata_lazy_video can change.
   *
   * The following members can change and thus, needs to be copied before
   * unlocking:
   *
   *  mlv_custom_title
   *  mlv_imdb_id
   *  mlv_lonely
   *  mlv_duration
   *
   */

  const float duration = mlv->mlv_duration;
  const int lonely = mlv->mlv_lonely;

  rstr_t *custom_title;

  if(mlv->mlv_custom_title == NULL || *rstr_get(mlv->mlv_custom_title)) {
    custom_title = NULL;
  } else {
    custom_title = rstr_dup(mlv->mlv_custom_title);
  }

  rstr_t *imdb_id = rstr_dup(mlv->mlv_imdb_id);

  mlp_retain(&mlv->mlv_mlp); // Make sure we don't get deleted

  // Avoid racing lookups between different threads
  while(mlv->mlv_mlp.mlp_loading)
    hts_cond_wait(&metadata_loading_cond, &metadata_mutex);

  mlv->mlv_mlp.mlp_loading = 1;


  hts_mutex_unlock(&metadata_mutex);


  METADATA_TRACE("Processing '%s' "
                 "custom_title=%s imdbid=%s lonely=%s duration=%f%s",
                 rstr_get(mlv->mlv_url),
                 rstr_get(custom_title),
                 rstr_get(imdb_id),
                 lonely ? "yes" : "no",
                 duration,
                 refresh ? ", force-refresh" : "");

  int disable_cache = refresh;

  /**
   * If duration is low skip this unless user have specified a custom query
   * or if we have an IMDB ID
   */
  if(duration < METADATA_DURATION_LIMIT &&
     sq == NULL &&
     rstr_get(imdb_id) == NULL) {
    goto bad;
  }


  if(!refresh) {
    /**
     * If we are not refreshing, read from database (basically our cache)
     */
    r = metadb_get_videoinfo(db, rstr_get(mlv->mlv_url),
                             msqivec, num_msqi, &fixed_ds, &md,
                             mlv->mlv_manual);
    if(r)
      goto done; // Found something, we're done

  } else {
    fixed_ds = 0;
  }

  prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 1);


  if(!mlv->mlv_manual && (md == NULL || !md->md_preferred)) {

    for(i = 0; i < num_msqi; i++) {

      metadata_source_query_info_t *msqi = msqivec + i;
      ms = msqi->msqi_ms;

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
      } else if(msf->query_by_imdb_id != NULL && imdb_id != NULL) {
	if(mlv->mlv_passive)
	  continue;

	qtype = METADATA_QTYPE_IMDB;
	q = rstr_get(imdb_id);

      } else if(mlv->mlv_qtype == METADATA_QTYPE_MOVIE) {

	if(msf->query_by_title_and_year == NULL)
	  continue;

	qtype = METADATA_QTYPE_MOVIE;
	q = NULL;

      } else if(mlv->mlv_qtype == METADATA_QTYPE_TVSHOW) {

	if(msf->query_by_episode == NULL)
	  continue;

	qtype = METADATA_QTYPE_TVSHOW;
	q = NULL;


      } else {
	if(mlv->mlv_passive)
	  continue;
	qtype = METADATA_QTYPE_FILENAME_OR_DIRECTORY;
	q = NULL;

      }

      if(!disable_cache) {
        if(md && md->md_dsid == ms->ms_id &&
           is_qtype_compat(qtype, md->md_qtype))
          break;

        /**
         * If current metadata source is seen (marked by metadb_get_videoinfo())
         * and the query type corresponds we should be fully up to date,
         * thus continue
         */

        if(msqi->msqi_mark && is_qtype_compat(qtype, msqi->msqi_qtype)) {

          /**
           * This weirdness is to be able to requery if we discover
           * that a movie is lonely in its folder
           * (To query using directory name)
           */
          if(msqi->msqi_status == METAITEM_STATUS_ABSENT &&
             msqi->msqi_qtype == METADATA_QTYPE_FILENAME &&
             lonely) {

          } else {
            continue;
          }
        }
      }

      rval = metadb_videoitem_delete_from_ds(db, rstr_get(mlv->mlv_url),
					     ms->ms_id);

      if(rval == 0) {

	switch(qtype) {
	case METADATA_QTYPE_IMDB:
	case METADATA_QTYPE_CUSTOM_IMDB:

	  METADATA_TRACE(
		"Performing IMDB lookup for %s using %s for %s",
                q, ms->ms_name, rstr_get(mlv->mlv_url));

	  rval = msf->query_by_imdb_id(db, rstr_get(mlv->mlv_url), q, qtype,
                                       rstr_get(mlv->mlv_initiator));
	  break;

	case METADATA_QTYPE_FILENAME_OR_DIRECTORY:
	  rval = query_by_filename_or_dirname(db, mlv, msf, &qtype,
                                              duration, lonely);
	  break;

	case METADATA_QTYPE_MOVIE:
	  METADATA_TRACE(
		"Performing search lookup on movie title %s, "
                "year:%d using %s for %s",
		rstr_get(mlv->mlv_filename), mlv->mlv_year, ms->ms_name,
                rstr_get(mlv->mlv_url));

	  rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					      rstr_get(mlv->mlv_filename),
					      mlv->mlv_year, duration,
					      qtype,
                                              rstr_get(mlv->mlv_initiator));
	  break;

	case METADATA_QTYPE_TVSHOW:
	  rval = msf->query_by_episode(db, rstr_get(mlv->mlv_url),
				       rstr_get(mlv->mlv_filename),
				       mlv->mlv_season, mlv->mlv_episode,
				       qtype,
                                       rstr_get(mlv->mlv_initiator));
	  break;

	case METADATA_QTYPE_CUSTOM:
	  if(msf->query_by_title_and_year == NULL)
	    continue;

	  METADATA_TRACE(
		"Performing custom search lookup for %s using %s for %s",
                sq, ms->ms_name, rstr_get(mlv->mlv_url));
	  rval = msf->query_by_title_and_year(db, rstr_get(mlv->mlv_url),
					      sq, 0, duration, qtype,
                                              rstr_get(mlv->mlv_initiator));
	  break;

	default:
	  continue;
	}
      }

      if(rval == METADATA_DEADLOCK || rval == METADATA_TEMPORARY_ERROR) {
        METADATA_TRACE( "%s for %s",
              rstr_get(mlv->mlv_url),
              rval == METADATA_DEADLOCK ? "Deadlock" : "Temporary error");

	prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);
	if(md != NULL)
	  metadata_destroy(md);

        r = rval;
        goto done;
      }

      if(rval == METADATA_PERMANENT_ERROR)
	rval = metadb_insert_videoitem(db, rstr_get(mlv->mlv_url), ms->ms_id,
				       "0", NULL, METAITEM_STATUS_ABSENT, 0,
				       qtype, ms->ms_cfgid);

      if(rval < 0) {
	prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);
	if(md != NULL)
	  metadata_destroy(md);
        r = rval;
        goto done;
      }
    }
    if(md != NULL)
      metadata_destroy(md);
    md = NULL;
    r = metadb_get_videoinfo(db, rstr_get(mlv->mlv_url),
                             msqivec, num_msqi, &fixed_ds, &md,
                             mlv->mlv_manual);
  }

  if(md != NULL &&
     md->md_metaitem_status == METAITEM_STATUS_PARTIAL &&
     md->md_ext_id != NULL &&
     (ms = metadata_source_get(mlv->mlv_type, md->md_dsid)) != NULL &&
     ms->ms_funcs->query_by_id != NULL &&
     (mlv->mlv_mlp.mlp_req_items & ms->ms_complete_props)) {
    
    METADATA_TRACE(
	  "Performing additional query for %s : %s", ms->ms_name,
	  rstr_get(md->md_ext_id));

    rval = ms->ms_funcs->query_by_id(db, rstr_get(mlv->mlv_url),
				     rstr_get(md->md_ext_id),
                                     rstr_get(mlv->mlv_initiator));
    metadata_destroy(md);

    if(rval == METADATA_DEADLOCK) {
      r = METADATA_DEADLOCK;
      goto done;
    }

    if(rval == METADATA_TEMPORARY_ERROR) {
      METADATA_TRACE( "Temporary error for %s",
	    rstr_get(mlv->mlv_url));
      r = METADATA_TEMPORARY_ERROR;
      goto done;
    }

    if(rval == METADATA_PERMANENT_ERROR)
      METADATA_TRACE( "Permanent error for %s",
	    rstr_get(mlv->mlv_url));

    r = metadb_get_videoinfo(db, rstr_get(mlv->mlv_url), msqivec, num_msqi,
                             &fixed_ds, &md, 0);
    if(r) {
      prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);
      goto done;
    }
  }

  if(mlv->mlv_m != NULL) {
    title = rstr_dup(custom_title);

    if(md != NULL) {
      mlv->mlv_dsid = md->md_dsid;
      ms = metadata_source_get(mlv->mlv_type, md->md_dsid);
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

      if(mlv->mlv_m != NULL) {

        mlv_cleanup(mlv);

        title = title ?: rstr_dup(custom_title ?: mlv->mlv_filename);
        mlv->mlv_dsid = 0;
        build_info_text(mlv, NULL);
      }
    }
    prop_set(mlv->mlv_m, "title", PROP_SET_RSTRING, title);

    prop_set(mlv->mlv_m, "loading", PROP_SET_INT, 0);
  }

  rstr_release(title);
  r = 0;

 done:
  rstr_release(custom_title);
  rstr_release(imdb_id);
  hts_mutex_lock(&metadata_mutex);

  mlv->mlv_mlp.mlp_loading = 0;
  hts_cond_broadcast(&metadata_loading_cond);

  mlp_release(&mlv->mlv_mlp);
  return r;
}


/**
 *
 */
static void
mlv_load(void *db, metadata_lazy_prop_t *mlp)
{
  mlv_get_video_info0(db, (metadata_lazy_video_t *)mlp, 0);
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
  metadb_videoitem_set_preferred(db, rstr_get(mlv->mlv_url), vid);
  mlv_get_video_info0(db, mlv, 0);
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
  const metadata_source_t *ms;
  prop_t *active = NULL;
  prop_vec_t *pv = prop_vec_create(10);
  prop_t *c, *p = prop_create_r(mlv->mlv_source_opt, "options");
  int cur = metadb_item_get_preferred_ds(rstr_get(mlv->mlv_url));

  if(!mlv->mlv_manual) {
    c = prop_create_root("0");
    prop_link(_p("Automatic"), prop_create(c, "title"));
    pv = prop_vec_append(pv, c);
  }

  hts_mutex_lock(&metadata_sources_mutex);

  TAILQ_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link) {
    if(!ms->ms_enabled)
      continue;
    c = prop_create_root(ms->ms_name);
    prop_set_string(prop_create(c, "title"), ms->ms_description);
    pv = prop_vec_append(pv, c);
    if(cur == ms->ms_id)
      active = prop_ref_inc(c);
  }
  hts_mutex_unlock(&metadata_sources_mutex);

  c = prop_create_root("1");
  prop_link(_p("None"), prop_create(c, "title"));
  pv = prop_vec_append(pv, c);
  if(cur == 1 || (mlv->mlv_manual && cur == 0))
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

      hts_mutex_lock(&metadata_sources_mutex);

      TAILQ_FOREACH(ms, &metadata_sources[mlv->mlv_type], ms_link) {
	if(ms->ms_enabled && !strcmp(ms->ms_name, name)) {
	  id = ms->ms_id;
	  break;
	}
      }

      hts_mutex_unlock(&metadata_sources_mutex);

    }
  }

  void *db = metadb_get();
  metadb_item_set_preferred_ds(db, rstr_get(mlv->mlv_url), id);
  mlv_get_video_info0(db, mlv, 0);
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
  assert(mlv != NULL);

  metadb_item_set_preferred_ds(db, rstr_get(mlv->mlv_url), 0);
  metadb_videoitem_set_preferred(db, rstr_get(mlv->mlv_url), 0);
  mlv_get_video_info0(db, mlv, 1);
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
      const event_payload_t *ep = (const event_payload_t *)e;
      if(!strcmp(ep->payload, "item.metadata.refresh")) {
	mlv_refresh_video_info(mlv);
	load_alternatives(mlv);
	const char *s;

	s = rstr_get(mlv->mlv_custom_query);
	if(s && *s)
	  kv_url_opt_set(rstr_get(mlv->mlv_url), KVSTORE_DOMAIN_SYS,
			 "metacustomquery", KVSTORE_SET_STRING, s);
	else
	  kv_url_opt_set(rstr_get(mlv->mlv_url), KVSTORE_DOMAIN_SYS,
			 "metacustomquery", KVSTORE_SET_VOID);
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
mlv_custom_query_cb(void *opaque, prop_event_t event, ...)
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
mlv_custom_title_cb(void *opaque, prop_event_t event, ...)
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
  prop_destroy(mlv->mlv_info_opt);
  prop_destroy(mlv->mlv_source_opt);
  prop_destroy(mlv->mlv_alt_opt);
  prop_destroy(mlv->mlv_custom_query_opt);
  prop_destroy(mlv->mlv_custom_title_opt);
  prop_destroy(mlv->mlv_refresh_opt);
}


/**
 *
 */
static void
mlv_dtor(metadata_lazy_prop_t *mlp)
{
  metadata_lazy_video_t *mlv = (metadata_lazy_video_t *)mlp;
  rstr_release(mlv->mlv_initiator);
  prop_unsubscribe(mlv->mlv_trig_title);
  prop_unsubscribe(mlv->mlv_trig_desc);
  prop_unsubscribe(mlv->mlv_trig_rating);

  prop_unsubscribe(mlv->mlv_options_monitor_sub);

  prop_unsubscribe(mlv->mlv_source_opt_sub);
  prop_unsubscribe(mlv->mlv_alt_opt_sub);
  prop_unsubscribe(mlv->mlv_refresh_sub);
  prop_unsubscribe(mlv->mlv_custom_title_sub);
  prop_unsubscribe(mlv->mlv_custom_query_sub);

  prop_ref_dec(mlv->mlv_title_opt);
  prop_ref_dec(mlv->mlv_info_opt);
  prop_ref_dec(mlv->mlv_info_text_prop);
  rstr_release(mlv->mlv_info_text_rstr);
  prop_ref_dec(mlv->mlv_source_opt);
  prop_ref_dec(mlv->mlv_alt_opt);
  prop_ref_dec(mlv->mlv_refresh_opt);

  prop_ref_dec(mlv->mlv_custom_query_opt);
  prop_ref_dec(mlv->mlv_custom_title_opt);

  rstr_release(mlv->mlv_filename);
  rstr_release(mlv->mlv_imdb_id);
  rstr_release(mlv->mlv_custom_query);
  rstr_release(mlv->mlv_custom_title);
  rstr_release(mlv->mlv_url);
  rstr_release(mlv->mlv_folder);

  prop_ref_dec(mlv->mlv_m);
  prop_ref_dec(mlv->mlv_root);
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
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_NAMED_ROOT, m, "metadata",
		   PROP_TAG_NAME_VECTOR, vec,
		   NULL);
  return s;
}

/**
 *
 */
static void
mlv_add_options(metadata_lazy_video_t *mlv)
{
  prop_t *p;
  prop_vec_t *pv = prop_vec_create(10);
  prop_t *options;

  // -------------------------------------------------------------------
  // Separator

  p = mlv->mlv_title_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set(p, "type",    PROP_SET_STRING, "separator");
  prop_set(p, "enabled", PROP_SET_INT, 1);

  prop_link(_p("Metadata"),
	    prop_create(prop_create(mlv->mlv_title_opt, "metadata"), "title"));

  pv = prop_vec_append(pv, p);

  // -------------------------------------------------------------------
  // Info

  p = mlv->mlv_info_opt = prop_ref_inc(prop_create_root(NULL));
  prop_set(p, "type",    PROP_SET_STRING, "info");
  prop_set(p, "enabled", PROP_SET_INT, 1);
  mlv->mlv_info_text_prop = prop_create_r(prop_create(p, "metadata"), "title");
  prop_set_rstring_ex(mlv->mlv_info_text_prop, NULL, mlv->mlv_info_text_rstr,
                      PROP_STR_RICH);

  pv = prop_vec_append(pv, p);


  // -------------------------------------------------------------------
  // Metadata source selection

  p = mlv->mlv_source_opt = prop_ref_inc(prop_create_root(NULL));

  prop_set(p, "type",    PROP_SET_STRING, "multiopt");
  prop_set(p, "enabled", PROP_SET_INT, 1);

  prop_link(_p("Metadata source"),
	    prop_create(prop_create(p, "metadata"), "title"));

  options = prop_create(p, "options");
  prop_linkselected_create(options, p, "current", "value");

  mlv->mlv_source_opt_sub =
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_source, mlv,
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_ROOT, options,
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, p);

  // -------------------------------------------------------------------
  // Metadata alternative selection

  p = mlv->mlv_alt_opt = prop_ref_inc(prop_create_root(NULL));

  prop_set(p, "type",    PROP_SET_STRING, "multiopt");
  prop_set(p, "enabled", PROP_SET_INT, 1);

  prop_link(_p("Movie"),
	    prop_create(prop_create(p, "metadata"), "title"));

  options = prop_create(p, "options");
  prop_linkselected_create(options, p, "current", "value");

  mlv->mlv_alt_opt_sub =
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_alternative, mlv,
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_ROOT, options,
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, p);



  // -------------------------------------------------------------------
  // Metadata search query

  p = mlv->mlv_custom_query_opt = prop_ref_inc(prop_create_root(NULL));

  prop_set(p, "type",    PROP_SET_STRING, "string");
  prop_set(p, "enabled", PROP_SET_INT, 1);
  prop_set(p, "action",  PROP_SET_STRING, "item.metadata.refresh");
  prop_set(p, "value",   PROP_SET_RSTRING, mlv->mlv_custom_query);

  prop_link(_p("Custom search query"),
	    prop_create(prop_create(p, "metadata"), "title"));

 
  mlv->mlv_custom_query_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_NAME("option", "value"),
		   PROP_TAG_CALLBACK, mlv_custom_query_cb, mlv,
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_NAMED_ROOT, p, "option",
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, p);
    
  // -------------------------------------------------------------------
  // Metadata refresh

  p = mlv->mlv_refresh_opt = prop_ref_inc(prop_create_root(NULL));

  prop_set(p, "type",    PROP_SET_STRING, "action");
  prop_set(p, "enabled", PROP_SET_INT, 1);
  prop_set(p, "action",  PROP_SET_STRING, "item.metadata.refresh");

  prop_link(_p("Refresh metadata"),
	    prop_create(prop_create(p, "metadata"), "title"));

  mlv->mlv_refresh_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, mlv_sub_actions, mlv,
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_ROOT, mlv->mlv_root,
		   NULL);
  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, p);

  // -------------------------------------------------------------------
  // Custom movie title

  p = mlv->mlv_custom_title_opt = prop_ref_inc(prop_create_root(NULL));

  prop_set(p, "type",    PROP_SET_STRING, "string");
  prop_set(p, "enabled", PROP_SET_INT, 1);
  prop_set(p, "action",  PROP_SET_STRING, "item.metadata.refresh");
  prop_set(p, "value",   PROP_SET_RSTRING, mlv->mlv_custom_title);

  prop_link(_p("Custom title"),
	    prop_create(prop_create(p, "metadata"), "title"));

  mlv->mlv_custom_title_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_NAME("option", "value"),
		   PROP_TAG_CALLBACK, mlv_custom_title_cb, mlv,
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_NAMED_ROOT, p, "option",
		   NULL);

  mlv->mlv_mlp.mlp_refcount++;

  pv = prop_vec_append(pv, p);

  // Add all options

  prop_t *all = prop_create_r(mlv->mlv_root, "options");

  prop_set_parent_vector(pv, all, NULL, NULL);
  prop_vec_release(pv);

  prop_ref_dec(all);

}


/**
 *
 */
static void
mlv_options_cb(void *opaque, prop_event_t event, ...)
{
  metadata_lazy_video_t *mlv = opaque;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    mlp_destroy(&mlv->mlv_mlp);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    mlv_add_options(mlv);
    mlv->mlv_mlp.mlp_refcount--;
    prop_unsubscribe(mlv->mlv_options_monitor_sub);
    mlv->mlv_options_monitor_sub = NULL;
    break;

  default:
    break;
  }
  va_end(ap);
}

/**
 *
 */
metadata_lazy_video_t *
metadata_bind_video_info(rstr_t *url, rstr_t *filename,
			 rstr_t *imdb_id, float duration,
			 prop_t *root,
			 rstr_t *folder, int lonely, int passive,
			 int year, int season, int episode,
                         int manual, rstr_t *initiator)
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
  mlv->mlv_manual = manual;
  mlv->mlv_root = prop_ref_inc(root);
  mlv->mlv_initiator = rstr_dup(initiator);
  mlv->mlv_m = prop_create_r(root, "metadata");


  if(season >= 0 && episode >= 0) {
    mlv->mlv_qtype = METADATA_QTYPE_TVSHOW;
    mlv->mlv_season = season;
    mlv->mlv_episode = episode;
  } else if(year >= 0) {
    mlv->mlv_qtype = METADATA_QTYPE_MOVIE;
    mlv->mlv_year = year;
  }

  mlv->mlv_custom_title = metadb_item_get_user_title(rstr_get(mlv->mlv_url));
  mlv->mlv_custom_query = kv_url_opt_get_rstr(rstr_get(mlv->mlv_url),
					      KVSTORE_DOMAIN_SYS, 
					      "metacustomquery");

  hts_mutex_lock(&metadata_mutex);

  mlv->mlv_trig_title =
    mlv_sub(mlv, mlv->mlv_m, "title", METADATA_PROP_TITLE);
  mlv->mlv_trig_desc =
    mlv_sub(mlv, mlv->mlv_m, "description", METADATA_PROP_DESCRIPTION);
  mlv->mlv_trig_rating =
    mlv_sub(mlv, mlv->mlv_m, "rating", METADATA_PROP_RATING);

  mlv->mlv_mlp.mlp_refcount++;

  mlv->mlv_options_monitor_sub = 
    prop_subscribe(PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_NAME("node", "options"),
		   PROP_TAG_CALLBACK, mlv_options_cb, mlv,
		   PROP_TAG_MUTEX, &metadata_mutex,
		   PROP_TAG_NAMED_ROOT, root, "node",
		   NULL);

  hts_mutex_unlock(&metadata_mutex);

  return mlv;
}


/**
 *
 */
void
mlv_set_imdb_id(metadata_lazy_video_t *mlv, rstr_t *imdb_id)
{
  hts_mutex_lock(&metadata_mutex);
  if(!rstr_eq(mlv->mlv_imdb_id, imdb_id)) {
    rstr_set(&mlv->mlv_imdb_id, imdb_id);
    mlp_enqueue(&mlv->mlv_mlp);
  }
  hts_mutex_unlock(&metadata_mutex);
}


/**
 *
 */
void
mlv_set_duration(metadata_lazy_video_t *mlv, float duration)
{
  hts_mutex_lock(&metadata_mutex);
  if(mlv->mlv_duration != duration) {
    mlv->mlv_duration = duration;
    mlp_enqueue(&mlv->mlv_mlp);
  }
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
    METADATA_TRACE("Item '%s' is %slonely", rstr_get(mlv->mlv_url),
                   lonely ? "" : "not ");

    mlv->mlv_lonely = lonely;
    mlp_enqueue(&mlv->mlv_mlp);
  }
  hts_mutex_unlock(&metadata_mutex);
}


/**
 *
 */
int
mlv_direct_query(void *db, rstr_t *url, rstr_t *filename,
                 const char *imdb_id, float duration, const char *folder,
                 int lonely)
{
  metadata_lazy_video_t mlv;
  memset(&mlv, 0, sizeof(mlv));

  mlv.mlv_url      = rstr_dup(url);
  mlv.mlv_filename = rstr_dup(filename);
  mlv.mlv_imdb_id  = rstr_alloc(imdb_id);
  mlv.mlv_folder   = rstr_alloc(folder);
  mlv.mlv_lonely   = 1;
  mlv.mlv_duration = duration;
  mlv.mlv_type     = METADATA_TYPE_VIDEO;

  hts_mutex_lock(&metadata_mutex);
  int r = mlv_get_video_info0(db, &mlv, 1);
  hts_mutex_unlock(&metadata_mutex);
  rstr_release(mlv.mlv_url);
  rstr_release(mlv.mlv_filename);
  rstr_release(mlv.mlv_imdb_id);
  rstr_release(mlv.mlv_folder);
  return r;
}


/**
 *
 */
static void *
metadata_thread(void *aux)
{
  void *db = NULL;

  hts_mutex_lock(&metadata_mutex);

  while(1) {

    metadata_lazy_prop_t *mlp;

    mlp = TAILQ_FIRST(&mlpqueue);
    if(mlp == NULL)
      break;

    if(db == NULL)
      db = metadb_get();

    TAILQ_REMOVE(&mlpqueue, mlp, mlp_link);
    mlp->mlp_queued = 0;
    if(!mlp->mlp_zombie)
      mlp->mlp_class->mlc_load(db, mlp);
  }

  metadata_num_threads--;

  hts_mutex_unlock(&metadata_mutex);

  if(db != NULL)
    metadb_close(db);

  return NULL;
}


/**
 *
 */
static void
metadata_threads_start(void)
{
  if(metadata_num_threads >= 4)
    return;
  metadata_num_threads++;
  hts_thread_create_detached("metadata", metadata_thread, NULL,
                             THREAD_PRIO_METADATA);
}


/**
 *
 */
void
mlp_init(void)
{
  TAILQ_INIT(&mlpqueue);
  hts_mutex_init(&metadata_mutex);
  hts_cond_init(&metadata_loading_cond, &metadata_mutex);
}
