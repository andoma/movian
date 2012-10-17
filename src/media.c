/*
 *  Media streaming functions and ffmpeg wrappers
 *  Copyright (C) 2007 Andreas Öman
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
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "media.h"
#include "showtime.h"
#include "audio/audio_decoder.h"
#include "audio/audio_defs.h"
#include "event.h"
#include "playqueue.h"
#include "fileaccess/fa_libav.h"
#include "backend/backend.h"
#include "misc/isolang.h"
#include "i18n.h"
#include "video/ext_subtitles.h"
#include "video/video_settings.h"
#include "video/video_overlay.h"
#include "settings.h"
#include "db/kvstore.h"

static LIST_HEAD(, codec_def) registeredcodecs;

// -------------------------------

int media_buffer_hungry; /* Set if we try to fill media buffers
			    Code can check this and avoid doing IO
			    intensive tasks
			 */

static hts_mutex_t media_mutex;

static prop_t *media_prop_root;
static prop_t *media_prop_sources;
static prop_t *media_prop_current;

static struct media_pipe_list media_pipe_stack;
media_pipe_t *media_primary;

static int mp_seek_in_queues(media_pipe_t *mp, int64_t pos);

static void seek_by_propchange(void *opaque, prop_event_t event, ...);

static void update_av_delta(void *opaque, int value);

static void update_sv_delta(void *opaque, int value);

static void media_eventsink(void *opaque, prop_event_t event, ...);

static void track_mgr_init(media_pipe_t *mp, media_track_mgr_t *mtm,
			   prop_t *root, int type, prop_t *current);

static void track_mgr_destroy(media_track_mgr_t *mtm);

static void track_mgr_next_track(media_track_mgr_t *mtm);

static void mtm_select_track(media_track_mgr_t *mtm, event_select_track_t *est);

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

  LIST_INIT(&media_pipe_stack);

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
static void
media_buf_dtor_freedata(media_buf_t *mb)
{
  if(mb->mb_data != NULL)
    free(mb->mb_data);
}


media_buf_t *
media_buf_alloc_locked(media_pipe_t *mp, size_t size)
{
  hts_mutex_assert(&mp->mp_mutex);
  media_buf_t *mb = pool_get(mp->mp_mb_pool);
  mb->mb_dtor = media_buf_dtor_freedata;
  mb->mb_size = size;
  if(size > 0) {
    mb->mb_data = malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    memset(mb->mb_data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
  }

  return mb;
}


media_buf_t *
media_buf_alloc_unlocked(media_pipe_t *mp, size_t size)
{
  media_buf_t *mb;
  hts_mutex_lock(&mp->mp_mutex);
  mb = media_buf_alloc_locked(mp, size);
  hts_mutex_unlock(&mp->mp_mutex);
  return mb;
}


/**
 *
 */
media_buf_t *
media_buf_from_avpkt_unlocked(media_pipe_t *mp, AVPacket *pkt)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);
  mb = pool_get(mp->mp_mb_pool);
  hts_mutex_unlock(&mp->mp_mutex);

  mb->mb_dtor = media_buf_dtor_freedata;

  if(pkt->destruct == av_destruct_packet) {
    /* Move the data pointers from libav's packet */
    mb->mb_data = pkt->data;
    pkt->data = NULL;
    
    mb->mb_size = pkt->size;
    pkt->size = 0;
    
  } else {
    
    mb->mb_data = malloc(pkt->size +   FF_INPUT_BUFFER_PADDING_SIZE);
    memset(mb->mb_data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(mb->mb_data, pkt->data, pkt->size);
    mb->mb_size = pkt->size;
  }

  av_free_packet(pkt);
  return mb;
}

/**
 *
 */
void
media_buf_free_locked(media_pipe_t *mp, media_buf_t *mb)
{
  mb->mb_dtor(mb);

  if(mb->mb_cw != NULL)
    media_codec_deref(mb->mb_cw);
  
  pool_put(mp->mp_mb_pool, mb);
}


/**
 *
 */
void
media_buf_free_unlocked(media_pipe_t *mp, media_buf_t *mb)
{
  hts_mutex_lock(&mp->mp_mutex);
  media_buf_free_locked(mp, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}




/**
 *
 */
static void
mq_init(media_queue_t *mq, prop_t *p, hts_mutex_t *mutex, media_pipe_t *mp)
{
  mq->mq_mp = mp;
  TAILQ_INIT(&mq->mq_q);

  mq->mq_packets_current = 0;
  mq->mq_stream = -1;
  hts_cond_init(&mq->mq_avail, mutex);
  mq->mq_prop_qlen_cur = prop_create(p, "dqlen");
  mq->mq_prop_qlen_max = prop_create(p, "dqmax");

  mq->mq_prop_bitrate = prop_create(p, "bitrate");

  mq->mq_prop_decode_avg  = prop_create(p, "decodetime_avg");
  mq->mq_prop_decode_peak = prop_create(p, "decodetime_peak");

  mq->mq_prop_upload_avg  = prop_create(p, "uploadtime_avg");
  mq->mq_prop_upload_peak = prop_create(p, "uploadtime_peak");

  mq->mq_prop_codec       = prop_create(p, "codec");
  mq->mq_prop_too_slow    = prop_create(p, "too_slow");
}


/**
 *
 */
static void
mq_destroy(media_queue_t *mq)
{
  hts_cond_destroy(&mq->mq_avail);
}


/**
 *
 */
media_pipe_t *
mp_create(const char *name, int flags, const char *type)
{
  media_pipe_t *mp;
  prop_t *p;

  mp = calloc(1, sizeof(media_pipe_t));

  mp->mp_satisfied = -1;
  mp->mp_epoch = 1;

  mp->mp_mb_pool = pool_create("packet headers", 
			       sizeof(media_buf_t),
			       POOL_ZERO_MEM);

  mp->mp_flags = flags;

  TAILQ_INIT(&mp->mp_eq);

  mp->mp_refcount = 1;

  mp->mp_buffer_limit = 1 * 1024 * 1024; 

  mp->mp_name = name;

  hts_mutex_init(&mp->mp_mutex);
  hts_mutex_init(&mp->mp_clock_mutex);
  hts_cond_init(&mp->mp_backpressure, &mp->mp_mutex);
  mp->mp_pc = prop_courier_create_thread(&mp->mp_mutex, "mp");

  mp->mp_prop_root = prop_create(media_prop_sources, NULL);
  mp->mp_prop_metadata    = prop_create(mp->mp_prop_root, "metadata");

  mp->mp_prop_type = prop_create(mp->mp_prop_root, "type");
  prop_set_string(mp->mp_prop_type, type);

  mp->mp_prop_primary = prop_create(mp->mp_prop_root, "primary");

  mp->mp_prop_io = prop_create(mp->mp_prop_root, "io");
  mp->mp_prop_notifications = prop_create(mp->mp_prop_root, "notifications");
  mp->mp_prop_url         = prop_create(mp->mp_prop_root, "url");


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

  track_mgr_init(mp, &mp->mp_audio_track_mgr, mp->mp_prop_audio_tracks,
		 MEDIA_TRACK_MANAGER_AUDIO, mp->mp_prop_audio_track_current);

  //--------------------------------------------------
  // Subtitles

  p = prop_create(mp->mp_prop_root, "subtitle");
  mp->mp_setting_subtitle_root = prop_create(p, "settings");
  mp->mp_prop_subtitle_track_current = prop_create(p, "current");
  mp->mp_prop_subtitle_tracks = prop_create(mp->mp_prop_metadata, 
					    "subtitlestreams");

  prop_set_string(mp->mp_prop_subtitle_track_current, "sub:off");
  mp_add_track_off(mp->mp_prop_subtitle_tracks, "sub:off");

  track_mgr_init(mp, &mp->mp_subtitle_track_mgr, mp->mp_prop_subtitle_tracks,
		 MEDIA_TRACK_MANAGER_SUBTITLES,
		 mp->mp_prop_subtitle_track_current);


  //--------------------------------------------------
  // Buffer

  p = prop_create(mp->mp_prop_root, "buffer");
  mp->mp_prop_buffer_current = prop_create(p, "current");
  prop_set_int(mp->mp_prop_buffer_current, 0);

  mp->mp_prop_buffer_limit = prop_create(p, "limit");
  prop_set_int(mp->mp_prop_buffer_limit, mp->mp_buffer_limit);


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

  mp->mp_prop_audio_channels_root = prop_create(mp->mp_prop_audio, "channels");

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

  mp->mp_prop_model = prop_create(mp->mp_prop_root, "model");

  mp->mp_sub_currenttime = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_CALLBACK, seek_by_propchange, mp,
		   PROP_TAG_COURIER, mp->mp_pc,
		   PROP_TAG_ROOT, mp->mp_prop_currenttime,
		   NULL);

  mp->mp_sub_stats =
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_SET_INT, &mp->mp_stats,
		   PROP_TAG_COURIER, mp->mp_pc,
		   PROP_TAG_ROOT, mp->mp_prop_stats,
		   NULL);


  //--------------------------------------------------
  // Settings

  mp->mp_setting_vzoom = 
    settings_create_int(mp->mp_setting_video_root, "vzoom",
			_p("Video zoom"), video_settings.vzoom, NULL, 50, 200,
			1, NULL, mp, SETTINGS_INITIAL_UPDATE,
			"%", mp->mp_pc, NULL, NULL);

  mp->mp_setting_av_delta = 
    settings_create_int(mp->mp_setting_audio_root, "avdelta",
			_p("Audio delay"), 0, NULL, -5000, 5000,
			50, update_av_delta, mp, SETTINGS_INITIAL_UPDATE,
			"ms", mp->mp_pc, NULL, NULL);

  mp->mp_setting_sv_delta = 
    settings_create_int(mp->mp_setting_subtitle_root, "svdelta",
			_p("Subtitle delay"), 0, NULL, -60000, 60000,
			100, update_sv_delta, mp, SETTINGS_INITIAL_UPDATE,
			"ms", mp->mp_pc, NULL, NULL);

  mp->mp_setting_sub_scale = 
    settings_create_int(mp->mp_setting_subtitle_root, "subscale",
			_p("Subtitle scaling"), subtitle_settings.scaling,
			NULL, 30, 500, 5, NULL, NULL, 0,
			"%", mp->mp_pc, NULL, NULL);

  mp->mp_setting_sub_on_video = 
    settings_create_bool(mp->mp_setting_subtitle_root, "subonvideoframe",
			 _p("Align subtitles on video frame"), 
			 subtitle_settings.align_on_video, NULL,
			 NULL, NULL, 0,
			 mp->mp_pc, NULL, NULL);

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
 * Must be called with mp locked
 */
static void
mq_flush(media_pipe_t *mp, media_queue_t *mq)
{
  media_buf_t *mb, *next;

  for(mb = TAILQ_FIRST(&mq->mq_q); mb != NULL; mb = next) {
    next = TAILQ_NEXT(mb, mb_link);

    if(mb->mb_data_type == MB_CTRL_EXIT)
      continue;

    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    mq->mq_packets_current--;
    mp->mp_buffer_current -= mb->mb_size;
    media_buf_free_locked(mp, mb);
  }
  mq_update_stats(mp, mq);
}


/**
 *
 */
static void
mp_destroy(media_pipe_t *mp)
{
  event_t *e;

  /* Make sure a clean shutdown has been made */
  assert(mp->mp_audio_decoder == NULL);
  assert(mp != media_primary);
  assert(!(mp->mp_flags & MP_ON_STACK));


  setting_destroy(mp->mp_setting_av_delta);
  setting_destroy(mp->mp_setting_sv_delta);
  setting_destroy(mp->mp_setting_sub_scale);
  setting_destroy(mp->mp_setting_sub_on_video);
  setting_destroy(mp->mp_setting_vzoom);

  prop_unsubscribe(mp->mp_sub_currenttime);
  prop_unsubscribe(mp->mp_sub_stats);

  track_mgr_destroy(&mp->mp_audio_track_mgr);
  track_mgr_destroy(&mp->mp_subtitle_track_mgr);

  prop_courier_destroy(mp->mp_pc);

  while((e = TAILQ_FIRST(&mp->mp_eq)) != NULL) {
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);
    event_release(e);
  }

  mq_flush(mp, &mp->mp_audio);
  mq_flush(mp, &mp->mp_video);

  mq_destroy(&mp->mp_audio);
  mq_destroy(&mp->mp_video);

  prop_destroy(mp->mp_prop_root);

  hts_cond_destroy(&mp->mp_backpressure);
  hts_mutex_destroy(&mp->mp_mutex);
  hts_mutex_destroy(&mp->mp_clock_mutex);

  pool_destroy(mp->mp_mb_pool);

  if(mp->mp_satisfied == 0)
    atomic_add(&media_buffer_hungry, -1);

  free(mp);
}


/**
 *
 */
void
mp_ref_dec(media_pipe_t *mp)
{
  if(atomic_add(&mp->mp_refcount, -1) == 1)
    mp_destroy(mp);
}



/**
 *
 */
static void
mp_direct_seek(media_pipe_t *mp, int64_t ts)
{
  event_t *e;
  event_ts_t *ets;

  ts = MAX(ts, 0);

  prop_set_float_ex(mp->mp_prop_currenttime, mp->mp_sub_currenttime,
		    ts / 1000000.0, 0);

  mp->mp_seek_base = ts;


  if(!mp_seek_in_queues(mp, ts + mp->mp_start_time)) {
    prop_set_float(prop_create(mp->mp_prop_root, "seektime"), ts / 1000000.0);
    return;
  }

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
  mp->mp_epoch++;

  e = &ets->h;
  TAILQ_INSERT_TAIL(&mp->mp_eq, e, e_link);
  hts_cond_signal(&mp->mp_backpressure);
}




/**
 *
 */
static void
mb_enq_tail(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  TAILQ_INSERT_TAIL(&mq->mq_q, mb, mb_link);
  mq->mq_packets_current++;
  mb->mb_epoch = mp->mp_epoch;
  mp->mp_buffer_current += mb->mb_size;
  mq_update_stats(mp, mq);
  hts_cond_signal(&mq->mq_avail);
}

/**
 *
 */
static void
mb_enq_head(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  TAILQ_INSERT_HEAD(&mq->mq_q, mb, mb_link);
  mq->mq_packets_current++;
  mb->mb_epoch = mp->mp_epoch;
  mp->mp_buffer_current += mb->mb_size;
  mq_update_stats(mp, mq);
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
mp_send_cmd_head_locked(media_pipe_t *mp, media_queue_t *mq, int cmd)
{
  media_buf_t *mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = cmd;
  mb_enq_head(mp, mq, mb);
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
}


/**
 *
 */
static void
mp_enqueue_event_locked(media_pipe_t *mp, event_t *e)
{
  event_select_track_t *est = (event_select_track_t *)e;
  event_int3_t *ei3;
  int64_t d;

  switch(e->e_type_x) {
  case EVENT_SELECT_AUDIO_TRACK:
    mtm_select_track(&mp->mp_audio_track_mgr, est);

    //    mp->mp_audio_track_mgr.mtm_user_set |= est->manual;
    break;
  case EVENT_SELECT_SUBTITLE_TRACK:
    mtm_select_track(&mp->mp_subtitle_track_mgr, est);

    //    mp->mp_subtitle_track_mgr.mtm_user_set |= est->manual;
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
  default:
    break;
  }

  if(event_is_action(e, ACTION_PLAYPAUSE ) ||
     event_is_action(e, ACTION_PLAY ) ||
     event_is_action(e, ACTION_PAUSE)) {
    
    mp->mp_hold = action_update_hold_by_event(mp->mp_hold, e);
    if(mp->mp_flags & MP_VIDEO)
      mp_send_cmd_head_locked(mp, &mp->mp_video, mp->mp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_send_cmd_head_locked(mp, &mp->mp_audio, mp->mp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, mp->mp_hold, NULL);
    send_hold(mp);
    return;
      
  } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

    mp->mp_hold = 1;

    if(mp->mp_flags & MP_VIDEO)
      mp_send_cmd_head_locked(mp, &mp->mp_video, mp->mp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_send_cmd_head_locked(mp, &mp->mp_audio, mp->mp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, mp->mp_hold, e->e_payload);
    send_hold(mp);
    return;

  } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {
    mp_direct_seek(mp, mp->mp_seek_base -= 15000000);
    return;
  }

  if(event_is_action(e, ACTION_SEEK_FORWARD)) {
    mp_direct_seek(mp, mp->mp_seek_base += 15000000);
    return;
  }

  atomic_add(&e->e_refcount, 1);
  TAILQ_INSERT_TAIL(&mp->mp_eq, e, e_link);
  hts_cond_signal(&mp->mp_backpressure);
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
event_t *
mp_wait_for_empty_queues(media_pipe_t *mp)
{
  event_t *e;
  hts_mutex_lock(&mp->mp_mutex);

  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL &&
	(mp->mp_audio.mq_packets_current || mp->mp_video.mq_packets_current))
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);

  if(e != NULL)
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);

  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}


/**
 *
 */
void
mq_update_stats(media_pipe_t *mp, media_queue_t *mq)
{
  int satisfied = mp->mp_eof ||
    mp->mp_buffer_current == 0 ||
    mp->mp_buffer_current * 8 > mp->mp_buffer_limit * 7;

  if(satisfied) {
    if(mp->mp_satisfied == 0) {
      atomic_add(&media_buffer_hungry, -1);
      mp->mp_satisfied = 1;
    }
  } else {
    if(mp->mp_satisfied != 0) {
      atomic_add(&media_buffer_hungry, 1);
      mp->mp_satisfied = 0;
    }
  }


  if(mp->mp_stats) {
    prop_set_int(mq->mq_prop_qlen_cur, mq->mq_packets_current);
    prop_set_int(mp->mp_prop_buffer_current, mp->mp_buffer_current);
  }
}

/**
 *
 */
static int64_t
mq_realtime_delay(media_queue_t *mq)
{
  media_buf_t *f, *l;

  f = TAILQ_FIRST(&mq->mq_q);
  l = TAILQ_LAST(&mq->mq_q, media_buf_queue);

  if(f != NULL) {
    if(f->mb_epoch == l->mb_epoch) {
      int64_t d = l->mb_pts - f->mb_pts;
      return d;
    }
  }
  return 0;
}


/**
 *
 */
event_t *
mb_enqueue_with_events(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  event_t *e = NULL;
  
  hts_mutex_lock(&mp->mp_mutex);
#if 0
  printf("ENQ %s %d/%d %d/%d\n", mq == &mp->mp_video ? "video" : "audio",
	 mq->mq_packets_current, mq->mq_packets_threshold,
	 mp->mp_buffer_current,  mp->mp_buffer_limit);
#endif
	 
  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL &&
	mp->mp_video.mq_packets_current >= (mp->mp_video.mq_stream != -1 ? 5 : 0) &&
	mp->mp_audio.mq_packets_current >= (mp->mp_audio.mq_stream != -1 ? 5 : 0) &&
	(mp->mp_buffer_current + mb->mb_size > mp->mp_buffer_limit ||
	 (mp->mp_max_realtime_delay != 0 && 
	  mq_realtime_delay(mq) > mp->mp_max_realtime_delay)))
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
  
  if(e != NULL) {
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);
  } else {
    mb_enq_tail(mp, mq, mb);
  }

  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}



/**
 * Return -1 if queues are full. return 0 if enqueue succeeded.
 */
int
mb_enqueue_no_block(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb,
		    int auxtype)
{
  hts_mutex_lock(&mp->mp_mutex);
  
  if(mp->mp_buffer_current + mb->mb_size > mp->mp_buffer_limit &&
     mq->mq_packets_current < 5) {
      hts_mutex_unlock(&mp->mp_mutex);
    return -1;
  }

  if(auxtype != -1) {
    media_buf_t *after;
    TAILQ_FOREACH_REVERSE(after, &mq->mq_q, media_buf_queue, mb_link) {
      if(after->mb_data_type == auxtype)
	break;
    }
    
    if(after == NULL)
      TAILQ_INSERT_HEAD(&mq->mq_q, mb, mb_link);
    else
      TAILQ_INSERT_AFTER(&mq->mq_q, after, mb, mb_link);

  } else {
    TAILQ_INSERT_TAIL(&mq->mq_q, mb, mb_link);
  }

  mq->mq_packets_current++;
  mp->mp_buffer_current += mb->mb_size;
  mb->mb_epoch = mp->mp_epoch;
  mq_update_stats(mp, mq);
  hts_cond_signal(&mq->mq_avail);

  hts_mutex_unlock(&mp->mp_mutex);
  return 0;
}


/**
 *
 */
void
mb_enqueue_always(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  hts_mutex_lock(&mp->mp_mutex);
  mb_enq_tail(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
void
mb_enqueue_always_head(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  hts_mutex_lock(&mp->mp_mutex);
  mb_enq_head(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static int
mp_seek_in_queues(media_pipe_t *mp, int64_t pos)
{
  media_buf_t *abuf, *vbuf, *vk, *mb;
  int rval = 1;

  TAILQ_FOREACH(abuf, &mp->mp_audio.mq_q, mb_link)
    if(abuf->mb_pts != AV_NOPTS_VALUE && abuf->mb_pts >= pos)
      break;

  if(abuf != NULL) {
    vk = NULL;

    TAILQ_FOREACH(vbuf, &mp->mp_video.mq_q, mb_link) {
      if(vbuf->mb_keyframe)
	vk = vbuf;
      if(vbuf->mb_pts != AV_NOPTS_VALUE && vbuf->mb_pts >= pos)
	break;
    }
    
    if(vbuf != NULL && vk != NULL) {
      int adrop = 0, vdrop = 0, vskip = 0;
      while(1) {
	mb = TAILQ_FIRST(&mp->mp_audio.mq_q);
	if(mb == abuf)
	  break;
	TAILQ_REMOVE(&mp->mp_audio.mq_q, mb, mb_link);
	mp->mp_audio.mq_packets_current--;
	mp->mp_buffer_current -= mb->mb_size;
	media_buf_free_locked(mp, mb);
	adrop++;
      }
      mq_update_stats(mp, &mp->mp_audio);

      while(1) {
	mb = TAILQ_FIRST(&mp->mp_video.mq_q);
	if(mb == vk)
	  break;
	TAILQ_REMOVE(&mp->mp_video.mq_q, mb, mb_link);
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
      mb->mb_skip = 2;
      rval = 0;

      mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_BLACKOUT;
      mb_enq_head(mp, &mp->mp_video, mb);

      mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_FLUSH;
      mb_enq_head(mp, &mp->mp_video, mb);

      mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_FLUSH;
      mb_enq_tail(mp, &mp->mp_audio, mb);


      TRACE(TRACE_DEBUG, "Media", "Seeking by dropping %d audio packets and %d+%d video packets from queue", adrop, vdrop, vskip);
    }
  }
  return rval;
}


/**
 *
 */
void
mp_flush(media_pipe_t *mp, int blank)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mq_flush(mp, a);
  mq_flush(mp, v);

  if(v->mq_stream >= 0) {
    mb = media_buf_alloc_locked(mp, 0);
    mb->mb_data_type = MB_FLUSH;
    mb_enq_tail(mp, v, mb);

    mb = media_buf_alloc_locked(mp, 0);
    mb->mb_data_type = MB_BLACKOUT;
    mb_enq_tail(mp, v, mb);
  }

  if(a->mq_stream >= 0) {
    mb = media_buf_alloc_locked(mp, 0);
    mb->mb_data_type = MB_FLUSH;
    mb_enq_tail(mp, a, mb);
  }

  if(mp->mp_satisfied == 0) {
    atomic_add(&media_buffer_hungry, -1);
    mp->mp_satisfied = 1;
  }

  hts_mutex_unlock(&mp->mp_mutex);

}

/**
 *
 */
void
mp_end(media_pipe_t *mp)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  if(v->mq_stream >= 0) {
    mb = media_buf_alloc_locked(mp, 0);
    mb->mb_data_type = MB_END;
    mb_enq_tail(mp, v, mb);
  }

  if(a->mq_stream >= 0) {
    mb = media_buf_alloc_locked(mp, 0);
    mb->mb_data_type = MB_END;
    mb_enq_tail(mp, a, mb);
  }
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
void
mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = cmd;
  mb_enq_tail(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}



/**
 *
 */
void
mp_send_cmd_head(media_pipe_t *mp, media_queue_t *mq, int cmd)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp_send_cmd_head_locked(mp, mq, cmd);
  hts_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */
void
mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d)
{
 media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = cmd;
  mb->mb_data = d;
  mb_enq_tail(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
void
mp_send_cmd_u32_head(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = cmd;
  mb->mb_data32 = u;
  mb_enq_head(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */

void
mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = cmd;
  mb->mb_data32 = u;
  mb_enq_tail(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}



/**
 *
 */
media_codec_t *
media_codec_ref(media_codec_t *cw)
{
  atomic_add(&cw->refcount, 1);
  return cw;
}

/**
 *
 */
void
media_codec_deref(media_codec_t *cw)
{
  media_format_t *fw = cw->fw;

  if(atomic_add(&cw->refcount, -1) > 1)
    return;

  if(cw->codec_ctx != NULL && cw->codec_ctx->codec != NULL)
    avcodec_close(cw->codec_ctx);

  if(cw->close != NULL)
    cw->close(cw);

  if(cw->codec_ctx_alloced)
    free(cw->codec_ctx);

  if(cw->parser_ctx != NULL)
    av_parser_close(cw->parser_ctx);
  
  if(fw != NULL)
    media_format_deref(fw);

  free(cw);
}

/**
 *
 */
static int
media_codec_create_lavc(media_codec_t *cw, enum CodecID id,
			AVCodecContext *ctx, media_codec_params_t *mcp)
{
  cw->codec = avcodec_find_decoder(id);

  if(cw->codec == NULL)
    return -1;
  
  if(ctx == NULL || id == CODEC_ID_AC3) {
    cw->codec_ctx = avcodec_alloc_context();
    cw->codec_ctx_alloced = 1;
  } else {
    cw->codec_ctx = ctx;
  }

  //  cw->codec_ctx->debug = FF_DEBUG_PICT_INFO | FF_DEBUG_BUGS;

  cw->codec_ctx->codec_id   = cw->codec->id;
  cw->codec_ctx->codec_type = cw->codec->type;
  cw->codec_ctx->opaque = cw;

  if(mcp != NULL && mcp->extradata != NULL) {
    cw->codec_ctx->extradata = calloc(1, mcp->extradata_size +
				      FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(cw->codec_ctx->extradata, mcp->extradata, mcp->extradata_size);
    cw->codec_ctx->extradata_size = mcp->extradata_size;
  }

  if(id == CODEC_ID_H264 && gconf.concurrency > 1) {
    cw->codec_ctx->thread_count = gconf.concurrency;
    if(mcp && mcp->cheat_for_speed)
      cw->codec_ctx->flags2 |= CODEC_FLAG2_FAST;
  }

  if(audio_mode_prefer_float() && cw->codec->id != CODEC_ID_AAC)
    cw->codec_ctx->request_sample_fmt = AV_SAMPLE_FMT_FLT;

  if(avcodec_open(cw->codec_ctx, cw->codec) < 0) {
    if(ctx == NULL)
      free(cw->codec_ctx);
    cw->codec = NULL;
    return -1;
  }
  return 0;
}


/**
 *
 */
media_codec_t *
media_codec_create(int codec_id, int parser,
		   media_format_t *fw, AVCodecContext *ctx,
		   media_codec_params_t *mcp, media_pipe_t *mp)
{
  media_codec_t *mc = calloc(1, sizeof(media_codec_t));
  codec_def_t *cd;

  LIST_FOREACH(cd, &registeredcodecs, link)
    if(!cd->open(mc, codec_id, ctx, mcp, mp))
      break;

  if(cd == NULL) {
    if(media_codec_create_lavc(mc, codec_id, ctx, mcp)) {
      free(mc);
      return NULL;
    }
  }

  mc->parser_ctx = parser ? av_parser_init(codec_id) : NULL;
  mc->refcount = 1;
  mc->fw = fw;
  mc->codec_id = codec_id;

  if(fw != NULL)
    atomic_add(&fw->refcount, 1);

  return mc;
}


/**
 *
 */
media_format_t *
media_format_create(AVFormatContext *fctx)
{
  media_format_t *fw = malloc(sizeof(media_format_t));
  fw->refcount = 1;
  fw->fctx = fctx;
  return fw;
}


/**
 *
 */
void
media_format_deref(media_format_t *fw)
{
  if(atomic_add(&fw->refcount, -1) > 1)
    return;
  fa_libav_close_format(fw->fctx);
  free(fw);
}


/**
 * 
 */
static void
mp_set_primary(media_pipe_t *mp)
{
  media_primary = mp;
  prop_select(mp->mp_prop_root);
  prop_link(mp->mp_prop_root, media_prop_current);
  prop_set_int(mp->mp_prop_primary, 1);
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

    LIST_INSERT_HEAD(&media_pipe_stack, media_primary, mp_stack_link);
    media_primary->mp_flags |= MP_ON_STACK;

    event_t *e = event_create_action(ACTION_STOP);
    mp_enqueue_event(media_primary, e);
    event_release(e);
  }

  mp_ref_inc(mp);
  mp_set_primary(mp);

  hts_mutex_unlock(&media_mutex);
}


/**
 *
 */
void
mp_shutdown(struct media_pipe *mp)
{
  if(mp->mp_audio_decoder != NULL) {
    audio_decoder_destroy(mp->mp_audio_decoder);
    mp->mp_audio_decoder = NULL;
  }

  hts_mutex_lock(&media_mutex);

  assert(mp->mp_flags & MP_PRIMABLE);

  if(media_primary == mp) {
    /* We were primary */
    
    prop_set_int(mp->mp_prop_primary, 0);
    prop_unlink(media_prop_current);

    media_primary = NULL;
    mp_ref_dec(mp); // mp could be free'd here */

    /* Anyone waiting to regain playback focus? */
    if((mp = LIST_FIRST(&media_pipe_stack)) != NULL) {

      assert(mp->mp_flags & MP_ON_STACK);
      LIST_REMOVE(mp, mp_stack_link);
      mp->mp_flags &= ~MP_ON_STACK;
      mp_set_primary(mp);
    } else {
      prop_unselect(media_prop_sources);
    }


  } else if(mp->mp_flags & MP_ON_STACK) {
    // We are on the stack

    LIST_REMOVE(mp, mp_stack_link);
    mp->mp_flags &= ~MP_ON_STACK;

    mp_ref_dec(mp); // mp could be free'd here */
  }
  hts_mutex_unlock(&media_mutex);
}



/**
 *
 */
static void
codec_details(AVCodecContext *ctx, char *buf, size_t size, const char *lead)
{
  const char *cfg;

  if(ctx->codec_type == AVMEDIA_TYPE_AUDIO) {

    if(ctx->sample_rate % 1000 == 0) {
      snprintf(buf, size, "%s%d kHz", lead, ctx->sample_rate / 1000);
    } else {
      snprintf(buf, size, "%s%.1f kHz", lead, (float)ctx->sample_rate / 1000);
    }
    lead = ", ";

    switch(ctx->channels) {
    case 1: 
      cfg = "mono";
      break;
    case 2: 
      cfg = "stereo";
      break;
    case 6: 
      cfg = "5.1";
      break;
    default:
      snprintf(buf + strlen(buf), size - strlen(buf), ", %d channels",
	       ctx->channels);
      cfg = NULL;
      break;
    }
    if(cfg != NULL) {
      snprintf(buf + strlen(buf), size - strlen(buf), ", %s", cfg);
    }
  }

  if(ctx->width) {
    snprintf(buf + strlen(buf), size - strlen(buf),
	     "%s%dx%d", lead, ctx->width, ctx->height);
    lead = ", ";
  }

  if(ctx->bit_rate > 2000000)
    snprintf(buf + strlen(buf), size - strlen(buf),
	     "%s%.1f Mb/s", lead, (float)ctx->bit_rate / 1000000);
  else if(ctx->bit_rate)
    snprintf(buf + strlen(buf), size - strlen(buf),
	     "%s%d kb/s", lead, ctx->bit_rate / 1000);
}

/**
 * Update codec info in property
 */ 
void
media_update_codec_info_prop(prop_t *p, AVCodecContext *ctx)
{
  char tmp[100];

  if(ctx == NULL) {
    tmp[0] = 0;
  } else {
    snprintf(tmp, sizeof(tmp), "%s", ctx->codec->long_name);
    codec_details(ctx, tmp + strlen(tmp), sizeof(tmp) - strlen(tmp), ", ");
  }
  prop_set_string(p, tmp);
}


/**
 * Update codec info in text widgets
 */ 
void
media_get_codec_info(AVCodecContext *ctx, char *buf, size_t size)
{
  snprintf(buf, size, "%s\n", ctx->codec->long_name);
  codec_details(ctx, buf + strlen(buf), size - strlen(buf), "");
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
static void
update_av_delta(void *opaque, int v)
{
  media_pipe_t *mp = opaque;
  mp->mp_avdelta = v * 1000;
  TRACE(TRACE_DEBUG, "AVSYNC", "Set to %d ms", v);
}


/**
 *
 */
static void
update_sv_delta(void *opaque, int v)
{
  media_pipe_t *mp = opaque;
  mp->mp_svdelta = v * 1000;
  TRACE(TRACE_DEBUG, "SVSYNC", "Set to %ds", v);
}


/**
 *
 */
void
mp_set_current_time(media_pipe_t *mp, int64_t ts, int epoch, int64_t delta)
{
  if(ts == AV_NOPTS_VALUE)
    return;

  ts -= delta;

  hts_mutex_lock(&mp->mp_mutex);

  if(epoch == mp->mp_epoch) {

    prop_set_float_ex(mp->mp_prop_currenttime, mp->mp_sub_currenttime,
		      ts / 1000000.0, 0);
    
    event_ts_t *ets = event_create(EVENT_CURRENT_TIME, sizeof(event_ts_t));
    ets->ts = ts;
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
    playqueue_event_handler(e);

  } else if(media_primary != NULL) {
    if(event_is_action(e, ACTION_SHOW_MEDIA_STATS)) {
      prop_toggle_int(media_primary->mp_prop_stats);
    } else if(event_is_action(e, ACTION_SHUFFLE)) {
      prop_toggle_int(media_primary->mp_prop_shuffle);
    } else if(event_is_action(e, ACTION_REPEAT)) {
      prop_toggle_int(media_primary->mp_prop_repeat);
    } else if(event_is_action(e, ACTION_CYCLE_AUDIO)) {
      track_mgr_next_track(&media_primary->mp_audio_track_mgr);
    } else if(event_is_action(e, ACTION_CYCLE_SUBTITLE)) {
      track_mgr_next_track(&media_primary->mp_subtitle_track_mgr);
    } else {
      mp_enqueue_event(media_primary, e);
    }
  } else {
    playqueue_event_handler(e);
  }
}

/**
 *
 */
void
mp_set_playstatus_by_hold(media_pipe_t *mp, int hold, const char *msg)
{
  prop_set_string(mp->mp_prop_playstatus, hold ? "pause" : "play");
  prop_set_string(mp->mp_prop_pausereason, 
		  hold ? (msg ?: "Paused by user") : NULL);
}


/**
 *
 */
void
mp_set_playstatus_stop(media_pipe_t *mp)
{
  prop_set_string(mp->mp_prop_playstatus, "stop");
}

/**
 *
 */
void
mp_set_url(media_pipe_t *mp, const char *url)
{
  prop_set_string(mp->mp_prop_url, url);
}

/**
 *
 */
void
mp_configure(media_pipe_t *mp, int caps, int buffer_size, int64_t duration)
{
  mp->mp_max_realtime_delay = 0;

  prop_set_int(mp->mp_prop_canSeek,  caps & MP_PLAY_CAPS_SEEK  ? 1 : 0);
  prop_set_int(mp->mp_prop_canPause, caps & MP_PLAY_CAPS_PAUSE ? 1 : 0);
  prop_set_int(mp->mp_prop_canEject, caps & MP_PLAY_CAPS_EJECT ? 1 : 0);

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

  mp->mp_duration = duration;
  prop_set_int(mp->mp_prop_buffer_limit, mp->mp_buffer_limit);
}


extern void avcodec_get_channel_layout_string(char *buf, int buf_size,
					      int nb_channels,
					      int64_t channel_layout);
/**
 * 
 */
void
metadata_from_ffmpeg(char *dst, size_t dstlen, AVCodec *codec, 
		     AVCodecContext *avctx)
{
  char *n;
  int off = snprintf(dst, dstlen, "%s", codec->name);

  n = dst;
  while(*n) {
    *n = toupper((int)*n);
    n++;
  }

  if(codec->id  == CODEC_ID_H264) {
    const char *p;
    switch(avctx->profile) {
    case FF_PROFILE_H264_BASELINE:  p = "Baseline";  break;
    case FF_PROFILE_H264_MAIN:      p = "Main";      break;
    case FF_PROFILE_H264_EXTENDED:  p = "Extended";  break;
    case FF_PROFILE_H264_HIGH:      p = "High";      break;
    case FF_PROFILE_H264_HIGH_10:   p = "High 10";   break;
    case FF_PROFILE_H264_HIGH_422:  p = "High 422";  break;
    case FF_PROFILE_H264_HIGH_444:  p = "High 444";  break;
    case FF_PROFILE_H264_CAVLC_444: p = "CAVLC 444"; break;
    default:                        p = NULL;        break;
    }

    if(p != NULL && avctx->level != FF_LEVEL_UNKNOWN)
      off += snprintf(dst + off, dstlen - off,
		      ", %s (Level %d.%d)",
		      p, avctx->level / 10, avctx->level % 10);
  }
    
  if(avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
    char buf[64];

    avcodec_get_channel_layout_string(buf, sizeof(buf), avctx->channels,
				      avctx->channel_layout);
					    
    off += snprintf(dst + off, dstlen - off, ", %d Hz, %s",
		    avctx->sample_rate, buf);
  }

  if(avctx->width)
    off += snprintf(dst + off, dstlen - off,
		    ", %dx%d", avctx->width, avctx->height);
  
  if(avctx->codec_type == AVMEDIA_TYPE_AUDIO && avctx->bit_rate)
    off += snprintf(dst + off, dstlen - off,
		    ", %d kb/s", avctx->bit_rate / 1000);

}

/**
 * 
 */
void
mp_set_mq_meta(media_queue_t *mq, AVCodec *codec, AVCodecContext *avctx)
{
  char buf[128];
  metadata_from_ffmpeg(buf, sizeof(buf), codec, avctx);
  prop_set_string(mq->mq_prop_codec, buf);
}



/**
 *
 */
void
mp_add_trackr(prop_t *parent,
	      rstr_t *title,
	      const char *url,
	      rstr_t *format,
	      rstr_t *longformat,
	      rstr_t *isolang,
	      rstr_t *source,
	      prop_t *sourcep,
	      int score)
{
  prop_t *p = prop_create_root(NULL);
  prop_t *s;

  prop_set_string(prop_create(p, "url"), url);
  prop_set_rstring(prop_create(p, "format"), format);
  prop_set_rstring(prop_create(p, "longformat"), longformat);
  
  s = prop_create(p, "source");

  if(sourcep != NULL)
    prop_link(sourcep, s);
  else
    prop_set_rstring(s, source);

  if(isolang != NULL) {
    prop_set_rstring(prop_create(p, "isolang"), isolang);
    
    const char *language = isolang_iso2lang(rstr_get(isolang));
    if(language != NULL) {
      prop_set_string(prop_create(p, "language"), language);
    } else {
      prop_set_rstring(prop_create(p, "language"), isolang);
    }
  }

  prop_set_rstring(prop_create(p, "title"), title);
  prop_set_int(prop_create(p, "score"), score);

  if(prop_set_parent(p, parent))
    prop_destroy(p);
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
	     int score)
{
  rstr_t *rtitle      = rstr_alloc(title);
  rstr_t *rformat     = rstr_alloc(format);
  rstr_t *rlongformat = rstr_alloc(longformat);
  rstr_t *risolang    = rstr_alloc(isolang);
  rstr_t *rsource     = rstr_alloc(source);

  mp_add_trackr(parent, rtitle, url, rformat, rlongformat, risolang,
		rsource, sourcep, score);
  
  rstr_release(rtitle);
  rstr_release(rformat);
  rstr_release(rlongformat);
  rstr_release(risolang);
  rstr_release(rsource);
}


/**
 *
 */
void
mp_add_track_off(prop_t *prop, const char *url)
{
  mp_add_track(prop, "Off", url, NULL, NULL, NULL, NULL, NULL, 0);
}




typedef struct media_track {
  TAILQ_ENTRY(media_track) mt_link;
  prop_sub_t *mt_sub_url;
  char *mt_url;

  prop_sub_t *mt_sub_isolang;
  int mt_isolang_score;

  prop_sub_t *mt_sub_basescore;
  int mt_base_score;

  media_track_mgr_t *mt_mtm;
  prop_t *mt_root;

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
mtm_rethink(media_track_mgr_t *mtm)
{
  media_track_t *mt, *best = NULL;
  int thres = 20;
  int best_score = 0;

  if (mtm->mtm_current_url) {
    TAILQ_FOREACH(mt, &mtm->mtm_tracks, mt_link)
      if(mt->mt_url != NULL && !strcmp(mt->mt_url, mtm->mtm_current_url))
	break;
  } else
    mt = NULL;

  if(mt != NULL)
    prop_select_ex(mt->mt_root, NULL, mtm->mtm_node_sub);

  if(TAILQ_FIRST(&mtm->mtm_tracks) == NULL) {
    // All tracks deleted, clear the user-has-configured flag
    mtm->mtm_user_set = 0;
    return;
  }

  if(mtm->mtm_type == MEDIA_TRACK_MANAGER_AUDIO ||
     subtitle_settings.always_select)
    thres = 0;

  if(mtm->mtm_user_set)
    return;

  TAILQ_FOREACH(mt, &mtm->mtm_tracks, mt_link) {
    if(mt->mt_url == NULL ||
       mt->mt_base_score == -1 ||
       mt->mt_isolang_score == -1)
      continue;

    if(mtm->mtm_user_pref != NULL && !strcmp(rstr_get(mtm->mtm_user_pref),
					     mt->mt_url)) {

      mtm->mtm_user_set = 1;
      event_t *e = event_create_select_track(mt->mt_url,
					     mtm_event_type(mtm), 0);
      mp_enqueue_event_locked(mtm->mtm_mp, e);
      event_release(e);
      return;
    }

    if(!strcmp(mt->mt_url, "sub:off") || !strcmp(mt->mt_url, "audio:off"))
      continue;

    int score = mt->mt_base_score + mt->mt_isolang_score;

    if(score >= thres && (best == NULL || score > best_score)) {
      best = mt;
      best_score = score;
    }
  }

  if(best == mtm->mtm_suggested_track)
    return;

  mtm->mtm_suggested_track = best;


  if(best != NULL) {

    event_t *e = event_create_select_track(best->mt_url,
					   mtm_event_type(mtm), 0);
    mp_enqueue_event_locked(mtm->mtm_mp, e);
    event_release(e);
  }
}


/**
 *
 */
static void
mt_set_url(void *opaque, const char *str)
{
  media_track_t *mt = opaque;
  mystrset(&mt->mt_url, str);
  mtm_rethink(mt->mt_mtm);
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
  mtm_rethink(mt->mt_mtm);
}


/**
 *
 */
static void
mt_set_basescore(void *opaque, int v)
{
  media_track_t *mt = opaque;
  mt->mt_base_score = v;
  mtm_rethink(mt->mt_mtm);
}


/**
 *
 */
static void
mtm_add_track(media_track_mgr_t *mtm, prop_t *root, media_track_t *before)
{
  media_track_t *mt = calloc(1, sizeof(media_track_t));

  prop_tag_set(root, mtm, mt);
  mt->mt_mtm = mtm;
  mt->mt_root = root;

  mt->mt_isolang_score = -1;
  mt->mt_base_score = -1;

  if(before) {
    TAILQ_INSERT_BEFORE(before, mt, mt_link);
  } else {
    TAILQ_INSERT_TAIL(&mtm->mtm_tracks, mt, mt_link);
  }

  mt->mt_sub_url =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "url"),
		   PROP_TAG_CALLBACK_STRING, mt_set_url, mt,
		   PROP_TAG_COURIER, mtm->mtm_mp->mp_pc,
		   PROP_TAG_NAMED_ROOT, root, "node",
		   NULL);

  mt->mt_sub_isolang =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "isolang"),
		   PROP_TAG_CALLBACK_STRING, mt_set_isolang, mt,
		   PROP_TAG_COURIER, mtm->mtm_mp->mp_pc,
		   PROP_TAG_NAMED_ROOT, root, "node",
		   NULL);

  mt->mt_sub_basescore =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "score"),
		   PROP_TAG_CALLBACK_INT, mt_set_basescore, mt,
		   PROP_TAG_COURIER, mtm->mtm_mp->mp_pc,
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

  TAILQ_REMOVE(&mtm->mtm_tracks, mt, mt_link);

  prop_unsubscribe(mt->mt_sub_url);
  prop_unsubscribe(mt->mt_sub_isolang);
  prop_unsubscribe(mt->mt_sub_basescore);
  free(mt->mt_url);
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
    mtm_rethink(mtm);
    break;

  case PROP_MOVE_CHILD:
    // NOP
    break;
    
  case PROP_SET_DIR:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_SET_VOID:
    mtm_clear(mtm);
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
  mtm_rethink(mtm);
}


/**
 *
 */
static void
mtm_set_url(void *opaque, const char *str)
{
  rstr_t *r;
  media_track_mgr_t *mtm = opaque;

  mystrset(&mtm->mtm_canonical_url, str);
  r = kv_url_opt_get_rstr(str, KVSTORE_DOMAIN_SYS, 
			  mtm->mtm_type == MEDIA_TRACK_MANAGER_AUDIO ?
			  "audioTrack" : "subtitleTrack");
  rstr_set(&mtm->mtm_user_pref, r);
  rstr_release(r);
}

/**
 *
 */
static void
track_mgr_init(media_pipe_t *mp, media_track_mgr_t *mtm, prop_t *root,
	       int type, prop_t *current)
{
  TAILQ_INIT(&mtm->mtm_tracks);
  mtm->mtm_mp = mp;
  mtm->mtm_type = type;

  mtm->mtm_node_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK, mtm_update_tracks, mtm,
		   PROP_TAG_COURIER, mp->mp_pc,
		   PROP_TAG_ROOT, root,
		   NULL);

  mtm->mtm_current_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_STRING, mtm_set_current, mtm,
		   PROP_TAG_COURIER, mp->mp_pc,
		   PROP_TAG_ROOT, current,
		   NULL);

  mtm->mtm_url_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_STRING, mtm_set_url, mtm,
		   PROP_TAG_COURIER, mp->mp_pc,
		   PROP_TAG_ROOT, mp->mp_prop_url,
		   NULL);
}


/**
 *
 */
static void
track_mgr_destroy(media_track_mgr_t *mtm)
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
static void
track_mgr_next_track(media_track_mgr_t *mtm)
{
  media_pipe_t *mp = mtm->mtm_mp;
  media_track_t *mt;

  hts_mutex_lock(&mp->mp_mutex);
  
  mt = mtm->mtm_current ? TAILQ_NEXT(mtm->mtm_current, mt_link) : NULL;
  
  if(mt == NULL)
    mt = TAILQ_FIRST(&mtm->mtm_tracks);

  if(mt != mtm->mtm_current) {
    event_t *e = event_create_select_track(mt->mt_url, mtm_event_type(mtm), 1);
    mp_enqueue_event_locked(mtm->mtm_mp, e);
    event_release(e);
    mtm->mtm_current = mt;
  }

  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
mtm_select_track(media_track_mgr_t *mtm, event_select_track_t *est)
{
  if(!est->manual)
    return;

  mtm->mtm_user_set = 1;
  if(!mtm->mtm_canonical_url)
    return;

  kv_url_opt_set(mtm->mtm_canonical_url, KVSTORE_DOMAIN_SYS,
		 mtm->mtm_type == MEDIA_TRACK_MANAGER_AUDIO ?
		 "audioTrack" : "subtitleTrack",
		 KVSTORE_SET_STRING, est->id);
}



/**
 *
 */
static void
ext_sub_dtor(media_buf_t *mb)
{
  if(mb->mb_data != NULL)
    subtitles_destroy(mb->mb_data);
}


/**
 *
 */
void
mp_load_ext_sub(media_pipe_t *mp, const char *url)
{
  media_buf_t *mb = media_buf_alloc_unlocked(mp, 0);
  mb->mb_data_type = MB_EXT_SUBTITLE;
  
  if(url != NULL)
    mb->mb_data = subtitles_load(mp, url);
  
  mb->mb_dtor = ext_sub_dtor;
  mb_enq_head(mp, &mp->mp_video, mb);
}


/**
 *
 */
void
media_register_codec(codec_def_t *cd)
{
  LIST_INSERT_HEAD(&registeredcodecs, cd, link);
}
