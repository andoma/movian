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
#include "media.h"
#include "video/video_settings.h"
#include "misc/minmax.h"
#include "misc/cancellable.h"


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
mp_seek_in_queues(media_pipe_t *mp, int64_t user_time)
{
  media_buf_t *abuf, *vbuf, *vk, *mb;
  int rval = 1;

  TAILQ_FOREACH(abuf, &mp->mp_audio.mq_q_data, mb_link)
    if(abuf->mb_user_time != PTS_UNSET && abuf->mb_user_time >= user_time)
      break;

  if(abuf != NULL) {
    vk = NULL;

    TAILQ_FOREACH(vbuf, &mp->mp_video.mq_q_data, mb_link) {
      if(vbuf->mb_keyframe)
	vk = vbuf;
      if(vbuf->mb_pts != PTS_UNSET && vbuf->mb_user_time >= user_time)
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
	mp->mp_buffer_current -= mb_buffered_size(mb);
	media_buf_free_locked(mp, mb);
	adrop++;
      }
      mq_update_stats(mp, &mp->mp_audio, 1);

      while(1) {
	mb = TAILQ_FIRST(&mp->mp_video.mq_q_data);
	if(mb == vk)
	  break;
	TAILQ_REMOVE(&mp->mp_video.mq_q_data, mb, mb_link);
	mp->mp_video.mq_packets_current--;
	mp->mp_buffer_current -= mb_buffered_size(mb);
	media_buf_free_locked(mp, mb);
	vdrop++;
      }
      mq_update_stats(mp, &mp->mp_video, 1);


      while(mb != vbuf) {
	mb->mb_skip = 1;
	mb = TAILQ_NEXT(mb, mb_link);
	vskip++;
      }
      rval = 0;

      update_epoch_in_queue(&mp->mp_audio.mq_q_data, mp->mp_epoch);
      update_epoch_in_queue(&mp->mp_video.mq_q_data, mp->mp_epoch);
      update_epoch_in_queue(&mp->mp_video.mq_q_aux, mp->mp_epoch);

      mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_CTRL_FLUSH;
      mb->mb_data32 = 0;
      mb_enq(mp, &mp->mp_video, mb);

      mb = media_buf_alloc_locked(mp, 0);
      mb->mb_data_type = MB_CTRL_FLUSH;
      mb->mb_data32 = 0;
      mb_enq(mp, &mp->mp_audio, mb);

      mp_check_underrun(mp);

      TRACE(TRACE_DEBUG, "Media", "Seeking by dropping %d audio packets and %d+%d video packets from queue", adrop, vdrop, vskip);
    }
  }
  return rval;
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
		    ts / 1000000.0);

  mp->mp_epoch++;
  mp->mp_seek_base = ts;

  if(mp->mp_seek_initiate != NULL)
    mp->mp_seek_initiate(mp);

  prop_set(mp->mp_prop_root, "seektime", PROP_SET_FLOAT, ts / 1000000.0);

  if(!mp_seek_in_queues(mp, ts))
    return;

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
  mp_event_dispatch(mp, e);
}


/**
 *
 */
void
mp_seek_by_propchange(void *opaque, prop_event_t event, ...)
{
  media_pipe_t *mp = opaque;
  int64_t t;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_INT:
    t = va_arg(ap, int) * 1000000LL;
    break;
  case PROP_SET_FLOAT:
    t = va_arg(ap, double) * 1000000.0;
    break;
  default:
    return;
  }

  mp_direct_seek(mp, t);
}

/**
 *
 */
void
mp_event_dispatch(media_pipe_t *mp, event_t *e)
{
  if(mp->mp_handle_event == NULL ||
     !mp->mp_handle_event(mp, mp->mp_handle_event_opaque, e)) {
    TAILQ_INSERT_TAIL(&mp->mp_eq, e, e_link);
    hts_cond_signal(&mp->mp_backpressure);
  } else {
    event_release(e);
  }
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

  switch(e->e_type) {
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
      if(e2->e_type == e->e_type)
        break;

    if(e2 != NULL) {
      TAILQ_REMOVE(&mp->mp_eq, e2, e_link);
      event_release(e2);
    }
  }

  if(event_is_action(e, ACTION_PLAYPAUSE ) ||
     event_is_action(e, ACTION_PLAY ) ||
     event_is_action(e, ACTION_PAUSE)) {

    if(action_update_hold_by_event(mp->mp_hold_flags & MP_HOLD_PAUSE, e)) {
      mp->mp_hold_flags |= MP_HOLD_PAUSE;
    } else {
      mp->mp_hold_flags &= ~MP_HOLD_PAUSE;
    }

    mp_set_playstatus_by_hold_locked(mp, NULL);

  } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

    const event_payload_t *ep = (const event_payload_t *)e;

    mp->mp_hold_flags |= MP_HOLD_PAUSE;
    mp_set_playstatus_by_hold_locked(mp, ep->payload);


  } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {
    mp_direct_seek(mp, mp->mp_seek_base - 1000000 *
                   video_settings.seek_back_step);
  } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {
    mp_direct_seek(mp, mp->mp_seek_base + 1000000 *
                   video_settings.seek_fwd_step);
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

      cancellable_cancel(mp->mp_cancellable);
    }

    atomic_inc(&e->e_refcount);
    mp_event_dispatch(mp, e);
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
void
media_eventsink(void *opaque, event_t *e)
{
  mp_enqueue_event_locked(opaque, e);
}


/**
 *
 */
void
mp_event_set_callback(struct media_pipe *mp,
                      int (*mp_callback)(struct media_pipe *mp,
                                         void *opaque,
                                         event_t *e),
                      void *opaque)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp->mp_handle_event = mp_callback;
  mp->mp_handle_event_opaque = opaque;
  hts_mutex_unlock(&mp->mp_mutex);
}

