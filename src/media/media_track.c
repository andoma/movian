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
#include <unistd.h>

#include "main.h"
#include "media/media.h"
#include "misc/isolang.h"
#include "i18n.h"
#include "db/kvstore.h"
#include "misc/minmax.h"

#include "subtitles/ext_subtitles.h"
#include "subtitles/dvdspu.h"
#include "subtitles/subtitles.h"

#include "media_track.h"

static void mp_load_ext_sub(media_pipe_t *mp, const char *url);

/**
 *
 */
prop_t *
mp_add_trackr(prop_t *parent,
	      rstr_t *title,
	      const char *url,
	      rstr_t *format,
	      rstr_t *longformat,
	      rstr_t *isolang,
	      rstr_t *source,
	      prop_t *sourcep,
	      int basescore,
              int autosel)
{
  prop_t *p = prop_create_root(NULL);
  prop_t *retval = prop_ref_inc(p);
  prop_t *s = prop_create(p, "source");

  prop_set(p, "url", PROP_SET_STRING, url);
  prop_set(p, "format", PROP_SET_RSTRING, format);
  prop_set(p, "longformat", PROP_SET_RSTRING, longformat);
  
  if(sourcep != NULL)
    prop_link(sourcep, s);
  else
    prop_set_rstring(s, source);

  if(isolang != NULL) {
    prop_set(p, "isolang", PROP_SET_RSTRING, isolang);
    
    const char *language = iso_639_2_lang(rstr_get(isolang));
    if(language) {
      prop_set(p, "language", PROP_SET_STRING, language);
    } else {
      prop_set(p, "language", PROP_SET_RSTRING, isolang);
    }
  }

  prop_set(p, "title", PROP_SET_RSTRING, title);
  prop_set(p, "basescore", PROP_SET_INT, basescore);
  prop_set(p, "autosel", PROP_SET_INT, autosel);

  if(prop_set_parent(p, parent))
    prop_destroy(p);
  return retval;
}


/**
 *
 */
static prop_t *
mp_add_track_ex(prop_t *parent,
                const char *title,
                const char *url,
                const char *format,
                const char *longformat,
                const char *isolang,
                const char *source,
                prop_t *sourcep,
                int basescore,
                int autosel)
{
  rstr_t *rtitle      = rstr_alloc(title);
  rstr_t *rformat     = rstr_alloc(format);
  rstr_t *rlongformat = rstr_alloc(longformat);
  rstr_t *risolang    = rstr_alloc(isolang);
  rstr_t *rsource     = rstr_alloc(source);

  prop_t *p = mp_add_trackr(parent, rtitle, url, rformat, rlongformat, risolang,
                            rsource, sourcep, basescore, autosel);
  rstr_release(rtitle);
  rstr_release(rformat);
  rstr_release(rlongformat);
  rstr_release(risolang);
  rstr_release(rsource);
  return p;
}


/**
 *
 */
void
mp_add_track(prop_t *parent,
	     const char *title,
	     const char *url,
	     const char *format,
	     const char *longformat,
	     const char *isolang,
	     const char *source,
	     prop_t *sourcep,
	     int basescore,
             int autosel)
{

  prop_t *p = mp_add_track_ex(parent, title, url, format, longformat, isolang,
                              source, sourcep, basescore, autosel);
  prop_ref_dec(p);
}


/**
 *
 */
void
mp_add_track_off(prop_t *prop, const char *url)
{
  prop_t *p =
    mp_add_track_ex(prop, "Off", url, NULL, NULL, NULL, NULL, NULL, 1000000, 1);

  prop_ref_dec(p);
}




typedef struct media_track {
  TAILQ_ENTRY(media_track) mt_link;

  RB_ENTRY(media_track) mt_sorted_link;

  prop_sub_t *mt_sub_url;
  char *mt_url;

  prop_sub_t *mt_sub_isolang;

  prop_sub_t *mt_sub_basescore;

  prop_sub_t *mt_sub_autosel;

  media_track_mgr_t *mt_mtm;
  prop_t *mt_root;

  prop_t *mt_sorted_node;

  int mt_autosel;

  int mt_isolang_score;
  int mt_base_score;
  int mt_final_score;

} media_track_t;


static event_type_t
mtm_event_type(media_track_mgr_t *mtm)
{
  switch(mtm->mtm_type) {
  case MEDIA_TRACK_MANAGER_AUDIO:
    return EVENT_SELECT_AUDIO_TRACK;

  case MEDIA_TRACK_MANAGER_SUBTITLES:
    return EVENT_SELECT_SUBTITLE_TRACK;

  default:
    return 0;
  }
}


/**
 *
 */
static void
mtm_send_sub_unload(media_pipe_t *mp)
{
  media_buf_t *mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = MB_CTRL_EXT_SUBTITLE;
  mb->mb_data = NULL;
  mb_enq(mp, &mp->mp_video, mb);
}


/**
 *
 */
static void
mtm_suggest(media_track_mgr_t *mtm)
{
  media_track_t *mt;

  if(TAILQ_FIRST(&mtm->mtm_tracks) == NULL) {
    // All tracks deleted, clear the user-has-configured flag
    mtm->mtm_user_set = 0;

    if(mtm->mtm_type == MEDIA_TRACK_MANAGER_SUBTITLES) {
      // Stop any pending load of subtitles
      mystrset(&mtm->mtm_mp->mp_subtitle_loader_url, NULL);
      mtm_send_sub_unload(mtm->mtm_mp);
    }
    return;
  }

  if(mtm->mtm_user_set)
    return;

  RB_FOREACH(mt, &mtm->mtm_sorted_tracks, mt_sorted_link) {
    if(mt->mt_url == NULL)
      continue;

    if(!strcmp(mt->mt_url, "sub:off") || !strcmp(mt->mt_url, "audio:off"))
      continue;

    if(!mt->mt_autosel)
      continue;
    break;
  }

  if(mt == mtm->mtm_suggested_track)
    return;
  mtm->mtm_suggested_track = mt;
  if(mt == NULL)
    return;

  TRACE(TRACE_DEBUG, "media", "Selecting track %s, score %d",
        mt->mt_url, mt->mt_final_score);

  prop_select_ex(mt->mt_root, NULL, mtm->mtm_node_sub);
  event_t *e = event_create_select_track(mt->mt_url, mtm_event_type(mtm), 0);
  mp_enqueue_event_locked(mtm->mtm_mp, e);
  event_release(e);
  prop_set_string(mtm->mtm_selector, "score");
}


/**
 *
 */
static int
track_score_cmp(const media_track_t *a, const media_track_t *b)
{
  if(a->mt_final_score != b->mt_final_score)
    return b->mt_final_score - a->mt_final_score;
  int x = strcmp(a->mt_url ?: "", b->mt_url ?: "");
  if(x)
    return x;
  return a < b ? 1 : -1;
}


/**
 *
 */
static void
mt_resort(media_track_t *mt)
{
  media_track_mgr_t *mtm = mt->mt_mtm;

  if(mt->mt_sorted_node)
    RB_REMOVE(&mtm->mtm_sorted_tracks, mt, mt_sorted_link);

  RB_INSERT_SORTED(&mtm->mtm_sorted_tracks, mt,
                   mt_sorted_link, track_score_cmp);

  media_track_t *next = RB_NEXT(mt, mt_sorted_link);

  if(mt->mt_sorted_node == NULL) {
    mt->mt_sorted_node = prop_create_root(NULL);
    prop_link(mt->mt_root, mt->mt_sorted_node);

    if(prop_set_parent_ex(mt->mt_sorted_node, mtm->mtm_sorted_nodes,
                          next ? next->mt_sorted_node : NULL, 0))
      abort();

  } else {
    assert(mt != next);
    prop_move(mt->mt_sorted_node, next ? next->mt_sorted_node : NULL);
  }
}


/**
 *
 */
static void
mt_set_url(void *opaque, const char *str)
{
  media_track_t *mt = opaque;
  media_track_mgr_t *mtm = mt->mt_mtm;

  mystrset(&mt->mt_url, str);

  mt_resort(mt);

  if(mtm->mtm_user_pref != NULL && mt->mt_url != NULL &&
     !strcmp(rstr_get(mtm->mtm_user_pref), mt->mt_url)) {

    mtm->mtm_user_set = 1;
    TRACE(TRACE_DEBUG, "media",
          "Selecting track %s (previously selected by user)",
          mt->mt_url);
    prop_select_ex(mt->mt_root, NULL, mtm->mtm_node_sub);
    event_t *e = event_create_select_track(mt->mt_url,
                                           mtm_event_type(mtm), 0);
    mp_enqueue_event_locked(mtm->mtm_mp, e);
    event_release(e);
    prop_set_string(mtm->mtm_selector, "user");
    return;
  }

  mtm_suggest(mtm);
}


/**
 *
 */
static void
mt_update_score(media_track_t *mt)
{

  if(mt->mt_base_score < 0 || mt->mt_isolang_score < 0)
    return;

  int score = MAX(mt->mt_base_score + mt->mt_isolang_score, 0);

  if(mt->mt_final_score == score)
    return;

  prop_set(mt->mt_root, "score", PROP_SET_INT, score);

  mt->mt_final_score = score;
  mt_resort(mt);
  mtm_suggest(mt->mt_mtm);
}


/**
 *
 */
static void
mt_set_isolang(void *opaque, const char *str)
{
  media_track_t *mt = opaque;

  switch(mt->mt_mtm->mtm_type) {
  case MEDIA_TRACK_MANAGER_AUDIO:
    mt->mt_isolang_score = str ? i18n_audio_score(str) : 0;
    break;
  case MEDIA_TRACK_MANAGER_SUBTITLES:
    mt->mt_isolang_score = str ? i18n_subtitle_score(str) : 0;
    break;
  default:
    mt->mt_isolang_score = 0;
    break;
  }

  mt_update_score(mt);
}


/**
 *
 */
static void
mt_set_basescore(void *opaque, int v)
{
  media_track_t *mt = opaque;
  mt->mt_base_score = v;

  mt_update_score(mt);
}


/**
 *
 */
static void
mt_set_autosel(void *opaque, int v)
{
  media_track_t *mt = opaque;
  mt->mt_autosel = v;
  mtm_suggest(mt->mt_mtm);
}


/**
 *
 */
static void
mtm_add_track(media_track_mgr_t *mtm, prop_t *root, media_track_t *before)
{
  media_track_t *mt = calloc(1, sizeof(media_track_t));
  media_pipe_t *mp = mtm->mtm_mp;

  prop_tag_set(root, mtm, mt);
  mt->mt_mtm = mtm;
  mt->mt_root = prop_ref_inc(root);

  mt->mt_isolang_score = -1;
  mt->mt_base_score = -1;
  mt->mt_final_score = -1;
  mt->mt_autosel = -1;

  if(before) {
    TAILQ_INSERT_BEFORE(before, mt, mt_link);
  } else {
    TAILQ_INSERT_TAIL(&mtm->mtm_tracks, mt, mt_link);
  }


  mt->mt_sub_url =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "url"),
		   PROP_TAG_CALLBACK_STRING, mt_set_url, mt,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_NAMED_ROOT, root, "node",
		   NULL);

  mt->mt_sub_autosel =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "autosel"),
		   PROP_TAG_CALLBACK_INT, mt_set_autosel, mt,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_NAMED_ROOT, root, "node",
		   NULL);

  mt->mt_sub_isolang =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "isolang"),
		   PROP_TAG_CALLBACK_STRING, mt_set_isolang, mt,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_NAMED_ROOT, root, "node",
		   NULL);

  mt->mt_sub_basescore =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "basescore"),
		   PROP_TAG_CALLBACK_INT, mt_set_basescore, mt,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_NAMED_ROOT, root, "node",
		   NULL);

}


/**
 *
 */
static void
mt_destroy(media_track_mgr_t *mtm, media_track_t *mt)
{
  if(mtm->mtm_suggested_track == mt)
    mtm->mtm_suggested_track = NULL;

  if(mtm->mtm_current == mt)
    mtm->mtm_current = NULL;

  if(mt->mt_sorted_node) {
    RB_REMOVE(&mtm->mtm_sorted_tracks, mt, mt_sorted_link);
    prop_destroy(mt->mt_sorted_node);
  }
  TAILQ_REMOVE(&mtm->mtm_tracks, mt, mt_link);

  prop_unsubscribe(mt->mt_sub_url);
  prop_unsubscribe(mt->mt_sub_isolang);
  prop_unsubscribe(mt->mt_sub_basescore);
  prop_unsubscribe(mt->mt_sub_autosel);
  free(mt->mt_url);
  prop_ref_dec(mt->mt_root);
  free(mt);
}


/**
 *
 */
static void
mtm_clear(media_track_mgr_t *mtm)
{
  media_track_t *mt;
  while((mt = TAILQ_FIRST(&mtm->mtm_tracks)) != NULL) {
    prop_tag_clear(mt->mt_root, mtm);
    mt_destroy(mtm, mt);
  }
}


/**
 * Callback for tracking changes to the tracks
 */
static void
mtm_update_tracks(void *opaque, prop_event_t event, ...)
{
  media_track_mgr_t *mtm = opaque;
  prop_t *p1, *p2;

  va_list ap;
  va_start(ap, event);

  switch(event) {

  case PROP_ADD_CHILD:
    mtm_add_track(mtm, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    mtm_add_track(mtm, p1, prop_tag_get(p2, mtm));
    break;

  case PROP_DEL_CHILD:
    mt_destroy(mtm, prop_tag_clear(va_arg(ap, prop_t *), mtm));
    mtm_suggest(mtm);
    break;

  case PROP_MOVE_CHILD:
    // NOP
    break;
    
  case PROP_SET_DIR:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_SET_VOID:
    mtm_clear(mtm);
    mtm_suggest(mtm);
    break;

  case PROP_SELECT_CHILD:
    break;
    
  default:
    abort();
  }
}


/**
 *
 */
static void
mtm_set_current(void *opaque, const char *str)
{
  media_track_mgr_t *mtm = opaque;
  mystrset(&mtm->mtm_current_url, str);

  if(str == NULL)
    return;

  media_track_t *mt;
  TAILQ_FOREACH(mt, &mtm->mtm_tracks, mt_link) {
    if(mt->mt_url != NULL && !strcmp(mt->mt_url, str)) {
      mtm->mtm_current = mt;
      return;
    }
  }
}


/**
 *
 */
static void
mtm_set_url(void *opaque, const char *str)
{
  media_track_mgr_t *mtm = opaque;

  mystrset(&mtm->mtm_canonical_url, str);
  rstr_t *r;
  r = kv_url_opt_get_rstr(str, KVSTORE_DOMAIN_SYS,
			  mtm->mtm_type == MEDIA_TRACK_MANAGER_AUDIO ?
			  "audioTrack" : "subtitleTrack");
  rstr_set(&mtm->mtm_user_pref, r);
  rstr_release(r);
}

/**
 *
 */
void
mp_track_mgr_init(media_pipe_t *mp, media_track_mgr_t *mtm, prop_t *root,
                  int type, prop_t *current, prop_t *sorted)
{
  TAILQ_INIT(&mtm->mtm_tracks);
  mtm->mtm_mp = mp;
  mtm->mtm_type = type;

  mtm->mtm_sorted_nodes = sorted;

  mtm->mtm_node_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK, mtm_update_tracks, mtm,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_ROOT, root,
		   NULL);

  mtm->mtm_current_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_STRING, mtm_set_current, mtm,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_ROOT, current,
		   NULL);

  mtm->mtm_url_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_STRING, mtm_set_url, mtm,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_ROOT, mp->mp_prop_url,
		   NULL);
}


/**
 *
 */
void
mp_track_mgr_destroy(media_track_mgr_t *mtm)
{
  prop_unsubscribe(mtm->mtm_node_sub);
  prop_unsubscribe(mtm->mtm_current_sub);
  prop_unsubscribe(mtm->mtm_url_sub);
  mtm_clear(mtm);
  free(mtm->mtm_current_url);
  free(mtm->mtm_canonical_url);
  rstr_release(mtm->mtm_user_pref);
}


/**
 *
 */
void
mp_track_mgr_next_track(media_track_mgr_t *mtm)
{
  media_track_t *mt;

  mt = mtm->mtm_current;

  if(mt != NULL) {
    if(mt->mt_sorted_node != NULL)
      mt = RB_NEXT(mt, mt_sorted_link);
    else
      mt = TAILQ_NEXT(mt, mt_link);
  }

  if(mt == NULL) {
    mt = RB_FIRST(&mtm->mtm_sorted_tracks);
    if(mt == NULL)
      mt = TAILQ_FIRST(&mtm->mtm_tracks);
  }

  if(mt != mtm->mtm_current) {
    TRACE(TRACE_DEBUG, "media", "Selecting next track %s (cycle)",
          mt->mt_url);
    prop_select_ex(mt->mt_root, NULL, mtm->mtm_node_sub);
    event_t *e = event_create_select_track(mt->mt_url, mtm_event_type(mtm), 1);
    mp_enqueue_event_locked(mtm->mtm_mp, e);
    event_release(e);
    mtm->mtm_current = mt;
  }
}


/**
 *
 */
int
mp_track_mgr_select_track(media_track_mgr_t *mtm, event_select_track_t *est)
{
  const int is_audio = mtm->mtm_type == MEDIA_TRACK_MANAGER_AUDIO;
  int rval = 0;
  const char *id = est->id;
  media_pipe_t *mp = mtm->mtm_mp;

  if(is_audio) {

    TRACE(TRACE_DEBUG, "Media", "Switching to audio track %s", id);


    if(!strcmp(id, "audio:off")) {

      mp->mp_audio.mq_stream = -1;

    } else if(!strncmp(id, "libav:", strlen("libav:"))) {

      mp->mp_audio.mq_stream =  atoi(id + strlen("libav:"));
      rval = 1;
    }

    prop_set_string(mp->mp_prop_audio_track_current, id);
    prop_set_int(mp->mp_prop_audio_track_current_manual, est->manual);

  } else {

    TRACE(TRACE_INFO, "Media", "Switching to subtitle track %s", id);

    // Sending an empty MB_CTRL_EXT_SUBTITLE will cause unload
    mp_send_cmd_locked(mp, &mp->mp_video, MB_CTRL_EXT_SUBTITLE);

    // Make the subtitle loader not inject new sub even if running
    mystrset(&mp->mp_subtitle_loader_url, NULL);

    prop_set_string(mp->mp_prop_subtitle_track_current, id);
    prop_set_int(mp->mp_prop_subtitle_track_current_manual, est->manual);
    mp->mp_video.mq_stream2 = -1;

    if(mystrbegins(id, "sub:")) {

    } else if(!strncmp(id, "libav:", strlen("libav:"))) {

      mp->mp_video.mq_stream2 = atoi(id + strlen("libav:"));
      rval = 1;

    } else {

      mp_load_ext_sub(mp, id);
    }
  }

  if(!est->manual)
    return rval;

  mtm->mtm_user_set = 1;
  if(!mtm->mtm_canonical_url)
    return rval;

  kv_url_opt_set(mtm->mtm_canonical_url, KVSTORE_DOMAIN_SYS,
                 is_audio ? "audioTrack" : "subtitleTrack",
		 KVSTORE_SET_STRING, id);
  return rval;
}




/**
 *
 */
static void
ext_sub_dtor(media_buf_t *mb)
{
  if(mb->mb_data != NULL)
    subtitles_destroy((void *)mb->mb_data);
}


/**
 *
 */
static void *
subtitle_loader_thread(void *aux)
{
  media_pipe_t *mp = aux;

  // Delay a short while to make the subtitle list stabilize
  usleep(100000);

  hts_mutex_lock(&mp->mp_mutex);
  while(1) {

    if(mp->mp_subtitle_loader_url == NULL)
      break;

    char *url = strdup(mp->mp_subtitle_loader_url);

    hts_mutex_unlock(&mp->mp_mutex);
    ext_subtitles_t *es = subtitles_load(mp, url);
    hts_mutex_lock(&mp->mp_mutex);

    if(mp->mp_subtitle_loader_url == NULL ||
       strcmp(mp->mp_subtitle_loader_url, url)) {
      // What we loaded is no longer relevant, destroy it
      free(url);
      if(es != NULL)
        subtitles_destroy(es);
      continue;
    }
    free(url);

    if(es != NULL) {

      // If we failed to load, don't do anything special

      media_buf_t *mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_CTRL_EXT_SUBTITLE;
      mb->mb_data = (void *)es;
      mb->mb_dtor = ext_sub_dtor;
      mb_enq(mp, &mp->mp_video, mb);
    }

    mystrset(&mp->mp_subtitle_loader_url, NULL);
  }
  mp->mp_subtitle_loader_running = 0;
  hts_mutex_unlock(&mp->mp_mutex);
  mp_release(mp);
  return NULL;
}



/**
 *
 */
static void
mp_load_ext_sub(media_pipe_t *mp, const char *url)
{
  if(!mp->mp_subtitle_loader_running) {
    mp->mp_subtitle_loader_running = 1;
    hts_thread_create_detached("subtitleloader",
                               subtitle_loader_thread,
                               mp_retain(mp), THREAD_PRIO_BGTASK);
  }
  mystrset(&mp->mp_subtitle_loader_url, url);
}


