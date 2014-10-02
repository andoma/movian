/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "media/media.h"
#include "showtime.h"
#include "audio2/audio_ext.h"
#include "event.h"
#include "playqueue.h"
#include "backend/backend.h"
#include "settings.h"
#include "misc/minmax.h"
#include "video/video_settings.h"

#include "subtitles/video_overlay.h"
#include "subtitles/dvdspu.h"

#include "media_track.h"
#include "media_settings.h"



#define MP_SKIP_LIMIT 3000000 /* µs that must before a skip back is
				 actually considered a restart */

struct AVCodecContext;

static LIST_HEAD(, codec_def) registeredcodecs;

// -------------------------------

atomic_t media_buffer_hungry; /* Set if we try to fill media buffers
                                 Code can check this and avoid doing IO
                                 intensive tasks
                              */

static hts_mutex_t media_mutex;

static prop_t *media_prop_root;
static prop_t *media_prop_sources;
static prop_t *media_prop_current;

media_pipe_t *media_primary;

void (*media_pipe_init_extra)(media_pipe_t *mp);
void (*media_pipe_fini_extra)(media_pipe_t *mp);

static int mp_seek_in_queues(media_pipe_t *mp, int64_t pos);

static void seek_by_propchange(void *opaque, prop_event_t event, ...);

static void media_eventsink(void *opaque, prop_event_t event, ...);

static void mp_set_playstatus_by_hold_locked(media_pipe_t *mp, const char *msg);

static void mp_unbecome_primary(media_pipe_t *mp);

uint8_t HTS_JOIN(sp, k0)[321];

/**
 *
 */
void
media_init(void)
{
  codec_def_t *cd;
  LIST_FOREACH(cd, &registeredcodecs, link)
    if(cd->init)
      cd->init();

  hts_mutex_init(&media_mutex);

  media_prop_root    = prop_create(prop_get_global(), "media");
  media_prop_sources = prop_create(media_prop_root, "sources");
  media_prop_current = prop_create(media_prop_root, "current");
  HTS_JOIN(sp, k0)[4] = 0x78;
  prop_subscribe(0,
		 PROP_TAG_NAME("media", "eventsink"),
		 PROP_TAG_CALLBACK, media_eventsink, NULL,
		 PROP_TAG_MUTEX, &media_mutex,
		 PROP_TAG_ROOT, media_prop_root,
		 NULL);

}


/**
 *
 */
media_pipe_t *
mp_create(const char *name, int flags)
{
  media_pipe_t *mp;
  prop_t *p;

  mp = calloc(1, sizeof(media_pipe_t));
  mp->mp_vol_ui = 1.0f;

  mp->mp_satisfied = -1;
  mp->mp_epoch = 1;

  mp->mp_mb_pool = pool_create("packet headers", 
			       sizeof(media_buf_t),
			       POOL_ZERO_MEM);

  mp->mp_flags = flags;

  TAILQ_INIT(&mp->mp_eq);

  atomic_set(&mp->mp_refcount, 1);

  mp->mp_buffer_limit = 1 * 1024 * 1024; 

  mp->mp_name = name;

  hts_mutex_init(&mp->mp_mutex);
  hts_mutex_init(&mp->mp_clock_mutex);

  hts_mutex_init(&mp->mp_overlay_mutex);
  TAILQ_INIT(&mp->mp_overlay_queue);
  TAILQ_INIT(&mp->mp_spu_queue);

  hts_cond_init(&mp->mp_backpressure, &mp->mp_mutex);

  mp->mp_prop_root = prop_create(media_prop_sources, NULL);
  mp->mp_prop_metadata    = prop_create(mp->mp_prop_root, "metadata");

  mp->mp_prop_primary = prop_create(mp->mp_prop_root, "primary");

  mp->mp_prop_io = prop_create(mp->mp_prop_root, "io");
  mp->mp_prop_notifications = prop_create(mp->mp_prop_root, "notifications");
  mp->mp_prop_url         = prop_create(mp->mp_prop_root, "url");


  mp->mp_setting_root = prop_create(mp->mp_prop_root, "settings");

  //--------------------------------------------------
  // Video

  mp->mp_prop_video = prop_create(mp->mp_prop_root, "video");
  mp->mp_setting_video_root = prop_create(mp->mp_prop_video, "settings");
  mq_init(&mp->mp_video, mp->mp_prop_video, &mp->mp_mutex, mp);

  //--------------------------------------------------
  // Audio

  mp->mp_prop_audio = prop_create(mp->mp_prop_root, "audio");
  mp->mp_setting_audio_root = prop_create(mp->mp_prop_audio, "settings");
  mq_init(&mp->mp_audio, mp->mp_prop_audio, &mp->mp_mutex, mp);
  mp->mp_prop_audio_track_current = prop_create(mp->mp_prop_audio, "current");
  mp->mp_prop_audio_tracks = prop_create(mp->mp_prop_metadata, "audiostreams");
  prop_set_string(mp->mp_prop_audio_track_current, "audio:off");
  mp_add_track_off(mp->mp_prop_audio_tracks, "audio:off");

  mp_track_mgr_init(mp,
                    &mp->mp_audio_track_mgr,
                    mp->mp_prop_audio_tracks,
                    MEDIA_TRACK_MANAGER_AUDIO,
                    mp->mp_prop_audio_track_current);

  //--------------------------------------------------
  // Subtitles

  p = prop_create(mp->mp_prop_root, "subtitle");
  mp->mp_setting_subtitle_root = prop_create(p, "settings");
  mp->mp_prop_subtitle_track_current = prop_create(p, "current");
  mp->mp_prop_subtitle_tracks = prop_create(mp->mp_prop_metadata, 
					    "subtitlestreams");

  prop_set_string(mp->mp_prop_subtitle_track_current, "sub:off");
  mp_add_track_off(mp->mp_prop_subtitle_tracks, "sub:off");

  mp_track_mgr_init(mp,
                    &mp->mp_subtitle_track_mgr,
                    mp->mp_prop_subtitle_tracks,
                    MEDIA_TRACK_MANAGER_SUBTITLES,
                    mp->mp_prop_subtitle_track_current);


  //--------------------------------------------------
  // Buffer

  p = prop_create(mp->mp_prop_root, "buffer");
  mp->mp_prop_buffer_current = prop_create(p, "current");
  prop_set_int(mp->mp_prop_buffer_current, 0);

  mp->mp_prop_buffer_limit = prop_create(p, "limit");
  prop_set_int(mp->mp_prop_buffer_limit, mp->mp_buffer_limit);

  mp->mp_prop_buffer_delay = prop_create(p, "delay");



  // 

  mp->mp_prop_playstatus  = prop_create(mp->mp_prop_root, "playstatus");
  mp->mp_prop_pausereason = prop_create(mp->mp_prop_root, "pausereason");
  mp->mp_prop_currenttime = prop_create(mp->mp_prop_root, "currenttime");

  prop_set_float_clipping_range(mp->mp_prop_currenttime, 0, 10e6);

  mp->mp_prop_avdelta     = prop_create(mp->mp_prop_root, "avdelta");
  prop_set_float(mp->mp_prop_avdelta, 0);

  mp->mp_prop_svdelta     = prop_create(mp->mp_prop_root, "svdelta");
  prop_set_float(mp->mp_prop_svdelta, 0);

  mp->mp_prop_stats       = prop_create(mp->mp_prop_root, "stats");
  prop_set_int(mp->mp_prop_stats, mp->mp_stats);
  mp->mp_prop_shuffle     = prop_create(mp->mp_prop_root, "shuffle");
  prop_set_int(mp->mp_prop_shuffle, 0);
  mp->mp_prop_repeat      = prop_create(mp->mp_prop_root, "repeat");
  prop_set_int(mp->mp_prop_repeat, 0);

  mp->mp_prop_avdiff      = prop_create(mp->mp_prop_root, "avdiff");
  mp->mp_prop_avdiff_error= prop_create(mp->mp_prop_root, "avdiffError");

  mp->mp_prop_canSkipBackward = 
    prop_create(mp->mp_prop_root, "canSkipBackward");

  mp->mp_prop_canSkipForward = 
    prop_create(mp->mp_prop_root, "canSkipForward");

  mp->mp_prop_canSeek = 
    prop_create(mp->mp_prop_root, "canSeek");

  mp->mp_prop_canPause = 
    prop_create(mp->mp_prop_root, "canPause");

  mp->mp_prop_canEject = 
    prop_create(mp->mp_prop_root, "canEject");

  mp->mp_prop_canShuffle = 
    prop_create(mp->mp_prop_root, "canShuffle");

  mp->mp_prop_canRepeat = 
    prop_create(mp->mp_prop_root, "canRepeat");

  prop_set_int(prop_create(mp->mp_prop_root, "canStop"), 1);

  mp->mp_prop_ctrl = prop_create(mp->mp_prop_root, "ctrl");

  mp->mp_prop_model = prop_create(mp->mp_prop_root, "model");

  mp->mp_sub_currenttime = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_CALLBACK, seek_by_propchange, mp,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_ROOT, mp->mp_prop_currenttime,
		   NULL);

  mp->mp_sub_stats =
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_SET_INT, &mp->mp_stats,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_ROOT, mp->mp_prop_stats,
		   NULL);


  if(media_pipe_init_extra != NULL)
    media_pipe_init_extra(mp);

  return mp;
}


/**
 *
 */
void
mp_reinit_streams(media_pipe_t *mp)
{
  prop_destroy_childs(mp->mp_prop_audio_tracks);
  prop_destroy_childs(mp->mp_prop_subtitle_tracks);

  mp_add_track_off(mp->mp_prop_audio_tracks, "audio:off");
  prop_set_string(mp->mp_prop_audio_track_current, "audio:off");

  mp_add_track_off(mp->mp_prop_subtitle_tracks, "sub:off");
  prop_set_string(mp->mp_prop_subtitle_track_current, "sub:off");
}


/**
 *
 */
void
mp_destroy(media_pipe_t *mp)
{
  mp_unbecome_primary(mp);

  assert(mp->mp_sub_currenttime != NULL);

  hts_mutex_lock(&mp->mp_mutex);

  prop_unsubscribe(mp->mp_sub_currenttime);
  prop_unsubscribe(mp->mp_sub_stats);

#if ENABLE_MEDIA_SETTINGS
  mp_settings_clear(mp);
#endif

  mp_track_mgr_destroy(&mp->mp_audio_track_mgr);
  mp_track_mgr_destroy(&mp->mp_subtitle_track_mgr);

  hts_mutex_unlock(&mp->mp_mutex);

  mp_release(mp);

}


/**
 *
 */
void
mp_release(media_pipe_t *mp)
{
  if(atomic_dec(&mp->mp_refcount))
    return;

  event_t *e;

  /* Make sure a clean shutdown has been made */
  assert(mp->mp_audio_decoder == NULL);
  assert(mp != media_primary);


  if(media_pipe_fini_extra != NULL)
    media_pipe_fini_extra(mp);


  while((e = TAILQ_FIRST(&mp->mp_eq)) != NULL) {
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);
    event_release(e);
  }

  mq_flush(mp, &mp->mp_audio, 1);
  mq_flush(mp, &mp->mp_video, 1);

  mq_destroy(&mp->mp_audio);
  mq_destroy(&mp->mp_video);


  prop_destroy(mp->mp_prop_root);

  video_overlay_flush_locked(mp, 0);
  dvdspu_destroy_all(mp);

  hts_cond_destroy(&mp->mp_backpressure);
  hts_mutex_destroy(&mp->mp_mutex);
  hts_mutex_destroy(&mp->mp_clock_mutex);
  hts_mutex_destroy(&mp->mp_overlay_mutex);

  pool_destroy(mp->mp_mb_pool);

  if(mp->mp_satisfied == 0)
    atomic_dec(&media_buffer_hungry);

  free(mp);
}


/**
 *
 */
static void
mp_direct_seek(media_pipe_t *mp, int64_t ts)
{
  event_t *e;
  event_ts_t *ets;

  if(!(mp->mp_flags & MP_CAN_SEEK))
    return;

  ts = MAX(ts, 0);

  prop_set_float_ex(mp->mp_prop_currenttime, mp->mp_sub_currenttime,
		    ts / 1000000.0, 0);

  mp->mp_seek_base = ts;

  if(!mp_seek_in_queues(mp, ts + mp->mp_start_time)) {
    prop_set(mp->mp_prop_root, "seektime", PROP_SET_FLOAT, ts / 1000000.0);
    return;
  }

  if(mp->mp_seek_initiate != NULL)
    mp->mp_seek_initiate(mp);

  /* If there already is a seek event enqueued, update it */
  TAILQ_FOREACH(e, &mp->mp_eq, e_link) {
    if(!event_is_type(e, EVENT_SEEK))
      continue;

    ets = (event_ts_t *)e;
    ets->ts = ts;
    return;
  }

  ets = event_create(EVENT_SEEK, sizeof(event_ts_t));
  ets->ts = ts;

  e = &ets->h;
  TAILQ_INSERT_TAIL(&mp->mp_eq, e, e_link);
  hts_cond_signal(&mp->mp_backpressure);

}


/**
 *
 */
void
mb_enq(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  int do_signal = 1;

  if(mb->mb_data_type == MB_SUBTITLE) {
    TAILQ_INSERT_TAIL(&mq->mq_q_aux, mb, mb_link);
  } else if(mb->mb_data_type > MB_CTRL) {
    TAILQ_INSERT_TAIL(&mq->mq_q_ctrl, mb, mb_link);
  } else {
    TAILQ_INSERT_TAIL(&mq->mq_q_data, mb, mb_link);
    do_signal = !mq->mq_no_data_interest;
  }
  mq->mq_packets_current++;
  mb->mb_epoch = mp->mp_epoch;
  mp->mp_buffer_current += mb->mb_size;
  mq_update_stats(mp, mq);
  if(do_signal)
    hts_cond_signal(&mq->mq_avail);
}

/**
 *
 */
void
mp_bump_epoch(media_pipe_t *mp)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp->mp_epoch++;
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
send_hold(media_pipe_t *mp)
{
  event_t *e = event_create_int(EVENT_HOLD, mp->mp_hold);
  TAILQ_INSERT_TAIL(&mp->mp_eq, e, e_link);
  hts_cond_signal(&mp->mp_backpressure);

  if(mp->mp_flags & MP_FLUSH_ON_HOLD)
    mp_flush_locked(mp);

  if(mp->mp_hold_changed != NULL)
    mp->mp_hold_changed(mp);
}


/**
 *
 */
void
mp_enqueue_event_locked(media_pipe_t *mp, event_t *e)
{
  event_select_track_t *est = (event_select_track_t *)e;
  event_int3_t *ei3;
  int64_t d;
  int dedup_event = 0;

  switch(e->e_type_x) {
  case EVENT_SELECT_AUDIO_TRACK:
    if(mp_track_mgr_select_track(&mp->mp_audio_track_mgr, est))
      return;
    dedup_event = 1;
    break;

  case EVENT_SELECT_SUBTITLE_TRACK:
    if(mp_track_mgr_select_track(&mp->mp_subtitle_track_mgr, est))
      return;
    dedup_event = 1;
    break;

  case EVENT_DELTA_SEEK_REL:
    // We want to seek thru the entire feature in 3 seconds

#define TOTAL_SEEK_TIME_IN_SECONDS 2

    ei3 = (event_int3_t *)e;

    int pre  = ei3->val1;
    int sign = ei3->val2;
    int rate = ei3->val3;

    d = pre * pre * mp->mp_duration /
      (rate*TOTAL_SEEK_TIME_IN_SECONDS*255*255);

    mp_direct_seek(mp, mp->mp_seek_base += d*sign);
    return;

  case EVENT_PLAYQUEUE_JUMP:
    dedup_event = 1;
    break;

  default:
    break;
  }

  if(dedup_event) {
    event_t *e2;
    TAILQ_FOREACH(e2, &mp->mp_eq, e_link)
      if(e2->e_type_x == e->e_type_x)
        break;

    if(e2 != NULL) {
      TAILQ_REMOVE(&mp->mp_eq, e2, e_link);
      event_release(e2);
    }
  }

  if(event_is_action(e, ACTION_PLAYPAUSE ) ||
     event_is_action(e, ACTION_PLAY ) ||
     event_is_action(e, ACTION_PAUSE)) {
    
    mp->mp_hold = action_update_hold_by_event(mp->mp_hold, e);
    mp_set_playstatus_by_hold_locked(mp, NULL);
    send_hold(mp);

  } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

    mp->mp_hold = 1;

    const event_payload_t *ep = (const event_payload_t *)e;
    mp_set_playstatus_by_hold_locked(mp, ep->payload);
    send_hold(mp);

  } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {
    mp_direct_seek(mp, mp->mp_seek_base -= 15000000);
  } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {
    mp_direct_seek(mp, mp->mp_seek_base += 15000000);
  } else if(event_is_action(e, ACTION_SHOW_MEDIA_STATS)) {
    prop_toggle_int(mp->mp_prop_stats);
  } else if(event_is_action(e, ACTION_SHUFFLE)) {
    prop_toggle_int(mp->mp_prop_shuffle);
  } else if(event_is_action(e, ACTION_REPEAT)) {
    prop_toggle_int(mp->mp_prop_repeat);
  } else if(event_is_action(e, ACTION_CYCLE_AUDIO)) {
    mp_track_mgr_next_track(&mp->mp_audio_track_mgr);
  } else if(event_is_action(e, ACTION_CYCLE_SUBTITLE)) {
    mp_track_mgr_next_track(&mp->mp_subtitle_track_mgr);
  } else if(event_is_action(e, ACTION_VOLUME_UP) ||
            event_is_action(e, ACTION_VOLUME_DOWN)) {

    switch(video_settings.dpad_up_down_mode) {
    case VIDEO_DPAD_MASTER_VOLUME:
      atomic_inc(&e->e_refcount);
      event_dispatch(e);
      break;
    case VIDEO_DPAD_PER_FILE_VOLUME:
      if(mp->mp_vol_setting == NULL)
        break;
      settings_add_int(mp->mp_vol_setting,
                       event_is_action(e, ACTION_VOLUME_UP) ? 1 : -1);
      break;
    }

  } else {

    // Forward event to player

    if(event_is_action(e, ACTION_SKIP_BACKWARD) &&
       mp->mp_seek_base >= MP_SKIP_LIMIT &&
       mp->mp_flags & MP_CAN_SEEK) {

      // Convert skip previous to track restart

      mp_direct_seek(mp, 0);
      return;
    }

    if(event_is_action(e, ACTION_STOP) ||
       event_is_action(e, ACTION_EJECT)) {
      prop_set_string(mp->mp_prop_playstatus, "stop");
    }

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) ||
       event_is_type(e, EVENT_EXIT) ||
       event_is_action(e, ACTION_STOP) ||
       event_is_action(e, ACTION_SKIP_FORWARD) ||
       event_is_action(e, ACTION_SKIP_BACKWARD)) {

      if(mp->mp_cancellable != NULL) {
        cancellable_cancel(mp->mp_cancellable);
      }
    }

    atomic_inc(&e->e_refcount);
    TAILQ_INSERT_TAIL(&mp->mp_eq, e, e_link);
    hts_cond_signal(&mp->mp_backpressure);
  }
}

/**
 *
 */
void
mp_enqueue_event(media_pipe_t *mp, event_t *e)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp_enqueue_event_locked(mp, e);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
event_t *
mp_dequeue_event(media_pipe_t *mp)
{
  event_t *e;
  hts_mutex_lock(&mp->mp_mutex);

  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL)
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);

  TAILQ_REMOVE(&mp->mp_eq, e, e_link);
  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}


/**
 *
 */
event_t *
mp_dequeue_event_deadline(media_pipe_t *mp, int timeout)
{
  event_t *e;

  hts_mutex_lock(&mp->mp_mutex);

  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL) {
    if(hts_cond_wait_timeout(&mp->mp_backpressure, &mp->mp_mutex, timeout))
      break;
  }
  if(e != NULL)
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);

  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}


/**
 *
 */
static void
update_epoch_in_queue(struct media_buf_queue *q, int epoch)
{
  media_buf_t *mb;
  TAILQ_FOREACH(mb, q, mb_link)
    mb->mb_epoch = epoch;
}


/**
 *
 */
static int
mp_seek_in_queues(media_pipe_t *mp, int64_t pos)
{
  media_buf_t *abuf, *vbuf, *vk, *mb;
  int rval = 1;

  TAILQ_FOREACH(abuf, &mp->mp_audio.mq_q_data, mb_link)
    if(abuf->mb_pts != PTS_UNSET && abuf->mb_pts >= pos)
      break;

  if(abuf != NULL) {
    vk = NULL;

    TAILQ_FOREACH(vbuf, &mp->mp_video.mq_q_data, mb_link) {
      if(vbuf->mb_keyframe)
	vk = vbuf;
      if(vbuf->mb_pts != PTS_UNSET && vbuf->mb_pts >= pos)
	break;
    }
    
    if(vbuf != NULL && vk != NULL) {
      int adrop = 0, vdrop = 0, vskip = 0;
      while(1) {
	mb = TAILQ_FIRST(&mp->mp_audio.mq_q_data);
	if(mb == abuf)
	  break;
	TAILQ_REMOVE(&mp->mp_audio.mq_q_data, mb, mb_link);
	mp->mp_audio.mq_packets_current--;
	mp->mp_buffer_current -= mb->mb_size;
	media_buf_free_locked(mp, mb);
	adrop++;
      }
      mq_update_stats(mp, &mp->mp_audio);

      while(1) {
	mb = TAILQ_FIRST(&mp->mp_video.mq_q_data);
	if(mb == vk)
	  break;
	TAILQ_REMOVE(&mp->mp_video.mq_q_data, mb, mb_link);
	mp->mp_video.mq_packets_current--;
	mp->mp_buffer_current -= mb->mb_size;
	media_buf_free_locked(mp, mb);
	vdrop++;
      }
      mq_update_stats(mp, &mp->mp_video);


      while(mb != vbuf) {
	mb->mb_skip = 1;
	mb = TAILQ_NEXT(mb, mb_link);
	vskip++;
      }
      rval = 0;

      mp->mp_epoch++;
      update_epoch_in_queue(&mp->mp_audio.mq_q_data, mp->mp_epoch);
      update_epoch_in_queue(&mp->mp_video.mq_q_data, mp->mp_epoch);
      update_epoch_in_queue(&mp->mp_video.mq_q_aux, mp->mp_epoch);

      mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_CTRL_FLUSH;
      mb_enq(mp, &mp->mp_video, mb);

      mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_CTRL_FLUSH;
      mb_enq(mp, &mp->mp_audio, mb);


      TRACE(TRACE_DEBUG, "Media", "Seeking by dropping %d audio packets and %d+%d video packets from queue", adrop, vdrop, vskip);
    }
  }
  return rval;
}

/**
 *
 */
media_codec_t *
media_codec_ref(media_codec_t *cw)
{
  atomic_inc(&cw->refcount);
  return cw;
}

/**
 *
 */
void
media_codec_deref(media_codec_t *cw)
{
  if(atomic_dec(&cw->refcount))
    return;
#if ENABLE_LIBAV
  if(cw->ctx != NULL && cw->ctx->codec != NULL)
    avcodec_close(cw->ctx);

  if(cw->ctx != cw->fmt_ctx && cw->fmt_ctx != NULL &&
     cw->fmt_ctx->codec != NULL)
    avcodec_close(cw->fmt_ctx);
#endif

  if(cw->close != NULL)
    cw->close(cw);

  if(cw->ctx != cw->fmt_ctx)
    free(cw->ctx);

  if(cw->fmt_ctx && cw->fw == NULL)
    free(cw->fmt_ctx);

#if ENABLE_LIBAV
  if(cw->parser_ctx != NULL)
    av_parser_close(cw->parser_ctx);

  if(cw->fw != NULL)
    media_format_deref(cw->fw);
#endif

  free(cw);
}


/**
 *
 */
media_codec_t *
media_codec_create(int codec_id, int parser,
		   struct media_format *fw, struct AVCodecContext *ctx,
		   const media_codec_params_t *mcp, media_pipe_t *mp)
{
  media_codec_t *mc = calloc(1, sizeof(media_codec_t));
  codec_def_t *cd;

  mc->mp = mp;
  mc->fmt_ctx = ctx;
  mc->codec_id = codec_id;
  
#if ENABLE_LIBAV
  if(ctx != NULL && mcp != NULL) {
    assert(ctx->extradata      == mcp->extradata);
    assert(ctx->extradata_size == mcp->extradata_size);
  }
#endif

  if(mcp != NULL) {
    mc->sar_num = mcp->sar_num;
    mc->sar_den = mcp->sar_den;
  }

  LIST_FOREACH(cd, &registeredcodecs, link)
    if(!cd->open(mc, mcp, mp))
      break;

  if(cd == NULL) {
    free(mc);
    return NULL;
  }

#if ENABLE_LIBAV
  if(parser) {
    assert(fw == NULL);

    const AVCodec *codec = avcodec_find_decoder(codec_id);
    assert(codec != NULL);
    mc->fmt_ctx = avcodec_alloc_context3(codec);
    mc->parser_ctx = av_parser_init(codec_id);
  }
#endif

  atomic_set(&mc->refcount, 1);
  mc->fw = fw;

  if(fw != NULL) {
    assert(!parser);
    atomic_inc(&fw->refcount);
  }

  return mc;
}


/**
 *
 */
void
mp_init_audio(struct media_pipe *mp)
{
  if(mp->mp_audio_decoder == NULL)
    mp->mp_audio_decoder = audio_decoder_create(mp);
}

/**
 *
 */
void
mp_become_primary(struct media_pipe *mp)
{
  mp_init_audio(mp);

  if(media_primary == mp)
    return;

  hts_mutex_lock(&media_mutex);

  assert(mp->mp_flags & MP_PRIMABLE);

  if(media_primary != NULL) {
    prop_set_int(media_primary->mp_prop_primary, 0);

    event_t *e = event_create_action(ACTION_STOP);
    mp_enqueue_event(media_primary, e);
    event_release(e);
  }

  media_primary = mp_retain(mp);

  prop_select(mp->mp_prop_root);
  prop_link(mp->mp_prop_root, media_prop_current);
  prop_set_int(mp->mp_prop_primary, 1);

  hts_mutex_unlock(&media_mutex);
}


/**
 *
 */
static void
mp_unbecome_primary(media_pipe_t *mp)
{
  hts_mutex_lock(&media_mutex);

  assert(mp->mp_flags & MP_PRIMABLE);

  if(media_primary == mp) {
    /* We were primary */
    prop_set_int(mp->mp_prop_primary, 0);
    prop_unlink(media_prop_current);

    media_primary = NULL;
    mp_release(mp); // mp could be free'd here */
    prop_unselect(media_prop_sources);
  }
  hts_mutex_unlock(&media_mutex);
}


/**
 *
 */
void
mp_shutdown(struct media_pipe *mp)
{
  mp_unbecome_primary(mp);

  if(mp->mp_audio_decoder != NULL) {
    audio_decoder_destroy(mp->mp_audio_decoder);
    mp->mp_audio_decoder = NULL;
  }
}


/**
 *
 */
static void
seek_by_propchange(void *opaque, prop_event_t event, ...)
{
  media_pipe_t *mp = opaque;
  int64_t t;
  int how = 0;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_INT:
    t = va_arg(ap, int) * 1000000LL;
    break;
  case PROP_SET_FLOAT:
    t = va_arg(ap, double) * 1000000.0;
    (void)va_arg(ap, prop_t *);
    how = va_arg(ap, int);
    break;
  default:
    return;
  }

  if(how == PROP_SET_TENTATIVE)
    return;

  mp_direct_seek(mp, t);
}


/**
 *
 */
void
mp_set_current_time(media_pipe_t *mp, int64_t ts, int epoch, int64_t delta)
{
  if(ts == PTS_UNSET)
    return;

  ts -= delta;

  hts_mutex_lock(&mp->mp_mutex);

  if(epoch == mp->mp_epoch) {

    prop_set_float_ex(mp->mp_prop_currenttime, mp->mp_sub_currenttime,
		      ts / 1000000.0, 0);
    
    event_ts_t *ets = event_create(EVENT_CURRENT_TIME, sizeof(event_ts_t));
    ets->ts = ts;
    ets->epoch = epoch;
    mp->mp_seek_base = ts;
    mp_enqueue_event_locked(mp, &ets->h);
    event_release(&ets->h);
  }
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
media_eventsink(void *opaque, prop_event_t event, ...)
{
  event_t *e;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_EXT_EVENT)
    return;

  e = va_arg(ap, event_t *);

  if(event_is_type(e, EVENT_PLAYTRACK)) {
#if ENABLE_PLAYQUEUE
    playqueue_event_handler(e);
#endif
  } else if(media_primary != NULL) {
    mp_enqueue_event(media_primary, e);
  } else {
#if ENABLE_PLAYQUEUE
    playqueue_event_handler(e);
#endif
  }
}


/**
 *
 */
static void
mp_set_playstatus_by_hold_locked(media_pipe_t *mp, const char *msg)
{
  int cmd = mp->mp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY;
  if(mp->mp_flags & MP_VIDEO)
    mp_send_cmd_locked(mp, &mp->mp_video, cmd);
  mp_send_cmd_locked(mp, &mp->mp_audio, cmd);

  prop_set_string(mp->mp_prop_playstatus, mp->mp_hold ? "pause" : "play");
  prop_set_string(mp->mp_prop_pausereason, 
		  mp->mp_hold ? (msg ?: "Paused by user") : NULL);
}


/**
 *
 */
void
mp_set_playstatus_by_hold(media_pipe_t *mp, int hold, const char *msg)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp->mp_hold = hold;
  mp_set_playstatus_by_hold_locked(mp, msg);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
void
mp_set_url(media_pipe_t *mp, const char *url, const char *parent_url,
           const char *parent_title)
{
  prop_set_string(mp->mp_prop_url, url);
#if ENABLE_MEDIA_SETTINGS
  hts_mutex_lock(&mp->mp_mutex);
  mp_settings_init(mp, url, parent_url, parent_title);
  hts_mutex_unlock(&mp->mp_mutex);
#endif
}


/**
 *
 */
void
mp_set_duration(media_pipe_t *mp, int64_t duration)
{
  if(duration == AV_NOPTS_VALUE) {
    mp->mp_duration = 0;
    prop_set(mp->mp_prop_metadata, "duration", PROP_SET_VOID);
    return;
  }
  mp->mp_duration = duration;

  float d = mp->mp_duration / 1000000.0;
  prop_set(mp->mp_prop_metadata, "duration", PROP_SET_FLOAT, d);

  if(duration && mp->mp_prop_metadata_source)
    prop_set(mp->mp_prop_metadata_source, "duration", PROP_SET_FLOAT, d);
}

/**
 *
 */
static void
mp_set_clr_flags_locked(media_pipe_t *mp, int set, int clr)
{
  mp->mp_flags &= ~clr;
  mp->mp_flags |= set;

  prop_set_int(mp->mp_prop_canSeek,  mp->mp_flags & MP_CAN_SEEK  ? 1 : 0);
  prop_set_int(mp->mp_prop_canPause, mp->mp_flags & MP_CAN_PAUSE ? 1 : 0);
  prop_set_int(mp->mp_prop_canEject, mp->mp_flags & MP_CAN_EJECT ? 1 : 0);
}


/**
 *
 */
void
mp_set_clr_flags(media_pipe_t *mp, int set, int clr)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp_set_clr_flags_locked(mp, set, clr);
  hts_mutex_unlock(&mp->mp_mutex);
}

/**
 *
 */
void
mp_configure(media_pipe_t *mp, int flags, int buffer_size, int64_t duration,
             const char *type)
{
  hts_mutex_lock(&mp->mp_mutex);

  mystrset(&mp->mp_subtitle_loader_url, NULL);

  mp->mp_framerate.num = 0;
  mp->mp_framerate.den = 1;

  mp->mp_max_realtime_delay = INT32_MAX;

  mp_set_clr_flags_locked(mp, flags,
                          MP_FLUSH_ON_HOLD |
                          MP_ALWAYS_SATISFIED |
                          MP_CAN_SEEK |
                          MP_CAN_PAUSE |
                          MP_CAN_EJECT);

  prop_set(mp->mp_prop_root, "type", PROP_SET_STRING, type);

  switch(buffer_size) {
  case MP_BUFFER_NONE:
    mp->mp_buffer_limit = 0;
    break;

  case MP_BUFFER_SHALLOW:
    mp->mp_buffer_limit = 1 * 1024 * 1024;
    break;

  case MP_BUFFER_DEEP:
    mp->mp_buffer_limit = 32 * 1024 * 1024;
    break;
  }

  prop_set_int(mp->mp_prop_buffer_limit, mp->mp_buffer_limit);
  mp_set_duration(mp, duration);

  if(mp->mp_clock_setup != NULL)
    mp->mp_clock_setup(mp, mp->mp_audio.mq_stream != -1);

  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
void
mp_set_cancellable(media_pipe_t *mp, struct cancellable *c)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp->mp_cancellable = c;
  hts_mutex_unlock(&mp->mp_mutex);
}



/**
 *
 */
static int
codec_def_cmp(const codec_def_t *a, const codec_def_t *b)
{
  return a->prio - b->prio;
}

/**
 *
 */
void
media_register_codec(codec_def_t *cd)
{
  LIST_INSERT_SORTED(&registeredcodecs, cd, link, codec_def_cmp, codec_def_t);
}



/**
 *
 */
int
mp_lockmgr(void *ptr, int op)
{
  media_pipe_t *mp = ptr;

  switch(op) {
  case PROP_LOCK_UNLOCK:
    hts_mutex_unlock(&mp->mp_mutex);
    return 0;
  case PROP_LOCK_LOCK:
    hts_mutex_lock(&mp->mp_mutex);
    return 0;
  case PROP_LOCK_TRY:
    return hts_mutex_trylock(&mp->mp_mutex);

  case PROP_LOCK_RETAIN:
    atomic_inc(&mp->mp_refcount);
    return 0;

  case PROP_LOCK_RELEASE:
    mp_release(mp);
    return 0;
  }
  abort();
}
