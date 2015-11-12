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
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "media/media.h"
#include "main.h"
#include "audio2/audio_ext.h"
#include "event.h"
#include "playqueue.h"
#include "prop/prop_linkselected.h"
#include "backend/backend.h"
#include "settings.h"
#include "misc/minmax.h"
#include "task.h"

#include "subtitles/video_overlay.h"
#include "subtitles/dvdspu.h"

#include "media_track.h"
#include "media_settings.h"
#include "misc/lockmgr.h"

atomic_t media_buffer_hungry; /* Set if we try to fill media buffers
                                 Code can check this and avoid doing IO
                                 intensive tasks
                              */

static hts_mutex_t media_mutex;

static prop_t *media_prop_root;
static prop_t *media_prop_sources;
static prop_t *media_prop_current;

static media_pipe_t *media_primary;

static struct media_pipe_list media_pipelines;
static int num_media_pipelines;

void (*media_pipe_init_extra)(media_pipe_t *mp);
void (*media_pipe_fini_extra)(media_pipe_t *mp);

static void media_global_eventsink(void *opaque, prop_event_t event, ...);

static void mp_unbecome_primary(media_pipe_t *mp);

uint8_t HTS_JOIN(sp, k0)[321];

/**
 *
 */
void
media_init(void)
{
  media_codec_init();

  hts_mutex_init(&media_mutex);

  media_prop_root    = prop_create(prop_get_global(), "media");
  media_prop_sources = prop_create(media_prop_root, "sources");
  media_prop_current = prop_create(media_prop_root, "current");
  HTS_JOIN(sp, k0)[4] = 0x78;
  prop_subscribe(0,
		 PROP_TAG_NAME("media", "eventSink"),
		 PROP_TAG_CALLBACK, media_global_eventsink, NULL,
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
  mp->mp_cancellable = cancellable_create();

  mp->mp_vol_ui = 1.0f;

  mp->mp_satisfied = -1;
  mp->mp_epoch = 1;

  mp->mp_mb_pool = pool_create("packet headers",
			       sizeof(media_buf_t),
			       POOL_ZERO_MEM);

  mp->mp_flags = flags;

  hts_mutex_lock(&media_mutex);
  LIST_INSERT_HEAD(&media_pipelines, mp, mp_global_link);
  num_media_pipelines++;
  hts_mutex_unlock(&media_mutex);


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
  mp->mp_prop_audio_track_current_manual =
    prop_create(mp->mp_prop_audio, "manual");
  mp->mp_prop_audio_tracks = prop_create(mp->mp_prop_metadata, "audiostreams");
  prop_linkselected_create(mp->mp_prop_audio_tracks,
                           mp->mp_prop_audio, "active", NULL);

  prop_set_string(mp->mp_prop_audio_track_current, "audio:off");


  mp_track_mgr_init(mp,
                    &mp->mp_audio_track_mgr,
                    mp->mp_prop_audio_tracks,
                    MEDIA_TRACK_MANAGER_AUDIO,
                    mp->mp_prop_audio_track_current,
                    prop_create(mp->mp_prop_audio, "sorted"));

  //--------------------------------------------------
  // Subtitles

  p = prop_create(mp->mp_prop_root, "subtitle");
  mp->mp_setting_subtitle_root = prop_create(p, "settings");
  mp->mp_prop_subtitle_track_current = prop_create(p, "current");
  mp->mp_prop_subtitle_track_current_manual = prop_create(p, "manual");
  mp->mp_prop_subtitle_tracks = prop_create(mp->mp_prop_metadata,
					    "subtitlestreams");
  prop_linkselected_create(mp->mp_prop_subtitle_tracks, p, "active", NULL);


  prop_set_string(mp->mp_prop_subtitle_track_current, "sub:off");
  mp_add_track_off(mp->mp_prop_subtitle_tracks, "sub:off");

  mp_track_mgr_init(mp,
                    &mp->mp_subtitle_track_mgr,
                    mp->mp_prop_subtitle_tracks,
                    MEDIA_TRACK_MANAGER_SUBTITLES,
                    mp->mp_prop_subtitle_track_current,
                    prop_create(p, "sorted"));

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
		   PROP_TAG_CALLBACK, mp_seek_by_propchange, mp,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_ROOT, mp->mp_prop_currenttime,
		   NULL);

  mp->mp_sub_eventsink =
    prop_subscribe(0,
		   PROP_TAG_NAME("media", "eventSink"),
                   PROP_TAG_CALLBACK_EVENT, media_eventsink, mp,
                   PROP_TAG_LOCKMGR, mp_lockmgr,
                   PROP_TAG_MUTEX, mp,
		   PROP_TAG_NAMED_ROOT, mp->mp_prop_root, "media",
		   NULL);


  if(media_pipe_init_extra != NULL)
    media_pipe_init_extra(mp);

  return mp;
}


/**
 *
 */
void
mp_reset(media_pipe_t *mp)
{
  mp_unhold(mp, MP_HOLD_PRE_BUFFERING | MP_HOLD_STREAM |  MP_HOLD_SYNC);
  cancellable_reset(mp->mp_cancellable);

  prop_set(mp->mp_prop_io, "bitrate", PROP_SET_VOID);
  prop_set(mp->mp_prop_io, "bitrateValid", PROP_SET_VOID);

  prop_t *p = prop_create(mp->mp_prop_io, "infoNodes");
  prop_destroy_childs(p);

  prop_destroy_childs(mp->mp_prop_audio_tracks);
  prop_destroy_childs(mp->mp_prop_subtitle_tracks);

  prop_set_void(mp->mp_prop_audio_track_current);
  prop_set_int(mp->mp_prop_audio_track_current_manual, 0);


  mp_add_track_off(mp->mp_prop_subtitle_tracks, "sub:off");
  prop_set_string(mp->mp_prop_subtitle_track_current, "sub:off");
  prop_set_int(mp->mp_prop_subtitle_track_current_manual, 0);
}


/**
 *
 */
void
mp_destroy(media_pipe_t *mp)
{
  hts_mutex_lock(&media_mutex);
  LIST_REMOVE(mp, mp_global_link);
  num_media_pipelines--;
  hts_mutex_unlock(&media_mutex);

  mp_unbecome_primary(mp);

  assert(mp->mp_sub_currenttime != NULL);

  hts_mutex_lock(&mp->mp_mutex);

  prop_unsubscribe(mp->mp_sub_currenttime);
  prop_unsubscribe(mp->mp_sub_eventsink);

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
static void
mp_final_release(void *aux)
{
  media_pipe_t *mp = aux;
  prop_destroy(mp->mp_prop_root);
  free(mp);
}


/**
 * prop_mutex can be held here, so we need to avoid locking it
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



  video_overlay_flush_locked(mp, 0);
  dvdspu_destroy_all(mp);

  hts_cond_destroy(&mp->mp_backpressure);
  hts_mutex_destroy(&mp->mp_mutex);
  hts_mutex_destroy(&mp->mp_clock_mutex);
  hts_mutex_destroy(&mp->mp_overlay_mutex);

  pool_destroy(mp->mp_mb_pool);

  if(mp->mp_satisfied == 0)
    atomic_dec(&media_buffer_hungry);

  cancellable_release(mp->mp_cancellable);

  /**
   * We need to destroy mp_prop_root but there is a risk that prop_mutex
   * is held here, so we need to dispatch
   */
  task_run(mp_final_release, mp);
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

  if(timeout == 0) {
    e = TAILQ_FIRST(&mp->mp_eq);
  } else {

    int64_t ts = arch_get_ts() + timeout * 1000LL;

    while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL) {
      if(hts_cond_wait_timeout_abs(&mp->mp_backpressure, &mp->mp_mutex, ts))
        break;
    }
  }

  if(e != NULL)
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);

  hts_mutex_unlock(&mp->mp_mutex);
  return e;
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
  hts_mutex_lock(&mp->mp_mutex);
  mp_flush_locked(mp, 1);
  hts_mutex_unlock(&mp->mp_mutex);

  mp_unbecome_primary(mp);

  if(mp->mp_audio_decoder != NULL) {
    audio_decoder_destroy(mp->mp_audio_decoder);
    mp->mp_audio_decoder = NULL;
  }
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
		      ts / 1000000.0);

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
void
mp_set_playstatus_by_hold_locked(media_pipe_t *mp, const char *msg)
{
  int hold = !!mp->mp_hold_flags;

  if(hold == mp->mp_hold_gate)
    return;

  mp->mp_hold_gate = hold;

  int cmd = hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY;

  if(mp->mp_flags & MP_VIDEO)
    mp_send_cmd_locked(mp, &mp->mp_video, cmd);

  mp_send_cmd_locked(mp, &mp->mp_audio, cmd);

  if(!hold)
    prop_set_void(mp->mp_prop_pausereason);
  else
    prop_set_string(mp->mp_prop_pausereason, msg ?: "Paused by user");

  prop_set_string(mp->mp_prop_playstatus, hold ? "pause" : "play");

  mp_event_dispatch(mp, event_create_int(EVENT_HOLD, hold));

  if(mp->mp_flags & MP_FLUSH_ON_HOLD)
    mp_flush_locked(mp, 0);

  if(mp->mp_hold_changed != NULL)
    mp->mp_hold_changed(mp);
}


/**
 *
 */
void
mp_hold(media_pipe_t *mp, int flag, const char *msg)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp->mp_hold_flags |= flag;
  mp_set_playstatus_by_hold_locked(mp, msg);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
void
mp_unhold(media_pipe_t *mp, int flag)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp->mp_hold_flags &= ~flag;
  mp_set_playstatus_by_hold_locked(mp, NULL);
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

  prop_set_string(mp->mp_prop_playstatus, "play");

  mp->mp_framerate.num = 0;
  mp->mp_framerate.den = 1;

  mp->mp_max_realtime_delay = INT32_MAX;

  mp_set_clr_flags_locked(mp, flags,
                          MP_PRE_BUFFERING |
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

  if(mp->mp_flags & MP_PRE_BUFFERING)
    mp_check_underrun(mp);

  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
int
mp_lockmgr(void *ptr, lockmgr_op_t op)
{
  media_pipe_t *mp = ptr;

  switch(op) {
  case LOCKMGR_UNLOCK:
    hts_mutex_unlock(&mp->mp_mutex);
    return 0;
  case LOCKMGR_LOCK:
    hts_mutex_lock(&mp->mp_mutex);
    return 0;
  case LOCKMGR_TRY:
    return hts_mutex_trylock(&mp->mp_mutex);

  case LOCKMGR_RETAIN:
    atomic_inc(&mp->mp_refcount);
    return 0;

  case LOCKMGR_RELEASE:
    mp_release(mp);
    return 0;
  }
  abort();
}


/**
 * Global eventsink (not tied to a specific media_pipe)
 */
static void
media_global_eventsink(void *opaque, prop_event_t event, ...)
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
void
media_global_hold(int on, int flag)
{
  int i;
  int count;
  media_pipe_t *mp;

  hts_mutex_lock(&media_mutex);
  count = num_media_pipelines;
  media_pipe_t **mpv = alloca(count * sizeof(media_pipe_t *));

  i = 0;
  LIST_FOREACH(mp, &media_pipelines, mp_global_link)
    mpv[i++] = mp_retain(mp);

  hts_mutex_unlock(&media_mutex);

  for(i = 0; i < count; i++) {
    mp = mpv[i];

    if(on)
      mp_hold(mp, flag, NULL);
    else
      mp_unhold(mp, flag);

    mp_release(mp);
  }
}


/**
 *
 */
void
mp_underrun(media_pipe_t *mp)
{
  mp->mp_hold_flags |= MP_HOLD_PRE_BUFFERING;
  mp_set_playstatus_by_hold_locked(mp, NULL);
}
