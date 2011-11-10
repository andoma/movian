/*
 *  Pulseaudio audio output
 *  Copyright (C) 2009 Andreas Öman
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

#include <pulse/pulseaudio.h>
#include <libavutil/avutil.h>

#include "showtime.h"
#include "audio/audio_defs.h"
#include "notifications.h"

static pa_threaded_mainloop *mainloop;
static pa_mainloop_api *api;


typedef struct pa_audio_mode {
  audio_mode_t am;
  pa_context *context;
  pa_stream *stream;
  int stream_error;

  int cur_format;
  int cur_rate;
  int cur_isfloat;

  prop_sub_t *sub_mvol; // Master volume subscription
  prop_sub_t *sub_mute; // Master mute subscription

  pa_sample_spec ss; // Active sample spec

  pa_volume_t mastervol;
  int muted;


} pa_audio_mode_t;

/**
 *
 */
static void
stream_write_callback(pa_stream *s, size_t length, void *userdata)
{
  //  pa_audio_mode_t *pam = (pa_audio_mode_t *)userdata;
  pa_threaded_mainloop_signal(mainloop, 0);
}




/**
 *
 */
static void
stream_state_callback(pa_stream *s, void *userdata)
{
  pa_audio_mode_t *pam = (pa_audio_mode_t *)userdata;
  pa_operation *o;

  if(pa_stream_get_state(s) == PA_STREAM_FAILED) {
    pam->stream_error = pa_context_errno(pam->context);
    TRACE(TRACE_ERROR, "PA",
	  "Stream failure: %s", pa_strerror(pam->stream_error));
  }

  if(pa_stream_get_state(s) == PA_STREAM_READY && pam->muted) {
    o = pa_context_set_sink_input_mute(pam->context,
				       pa_stream_get_index(pam->stream),
				       pam->muted, NULL, NULL);
    if(o != NULL)
      pa_operation_unref(o);
  }

  pa_threaded_mainloop_signal(mainloop, 0);
}


/**
 * Setup a new stream based on the properties of the given audio_buf
 */
static void
stream_setup(pa_audio_mode_t *pam, audio_buf_t *ab)
{
  pa_stream *s;
  char buf[100];
  int flags = 0;
#if PA_API_VERSION >= 12
  pa_proplist *pl;
  media_pipe_t *mp = ab->ab_mp;
#endif
  pa_channel_map map;
  pa_cvolume cv;

  memset(&pam->ss, 0, sizeof(pa_sample_spec));

  pam->ss.format = ab->ab_isfloat ? PA_SAMPLE_FLOAT32NE : PA_SAMPLE_S16NE;
  pam->ss.rate = ab->ab_samplerate;

  switch(ab->ab_format) {
  case AM_FORMAT_PCM_STEREO:
    pam->ss.channels = 2;
    pa_channel_map_init_stereo(&map);
    break;

  case AM_FORMAT_PCM_5DOT1:
    pam->ss.channels = 6;
    pa_channel_map_init(&map); //, 6, PA_CHANNEL_MAP_WAVEEX);

    map.channels = 6;
    map.map[0] = PA_CHANNEL_POSITION_LEFT;
    map.map[1] = PA_CHANNEL_POSITION_RIGHT;
    map.map[2] = PA_CHANNEL_POSITION_CENTER;
    map.map[3] = PA_CHANNEL_POSITION_LFE;
    map.map[4] = PA_CHANNEL_POSITION_SIDE_LEFT;
    map.map[5] = PA_CHANNEL_POSITION_SIDE_RIGHT;
    break;

  case AM_FORMAT_PCM_7DOT1:
    pam->ss.channels = 8;
    pa_channel_map_init(&map);
    map.channels = 8;
    map.map[0] = PA_CHANNEL_POSITION_LEFT;
    map.map[1] = PA_CHANNEL_POSITION_RIGHT;
    map.map[2] = PA_CHANNEL_POSITION_CENTER;
    map.map[3] = PA_CHANNEL_POSITION_LFE;
    map.map[4] = PA_CHANNEL_POSITION_SIDE_LEFT;
    map.map[5] = PA_CHANNEL_POSITION_SIDE_RIGHT;
    map.map[6] = PA_CHANNEL_POSITION_REAR_LEFT;
    map.map[7] = PA_CHANNEL_POSITION_REAR_RIGHT;
    break;

  default:
    abort();
  }

  TRACE(TRACE_DEBUG, "PA", "Created stream %s",
	pa_sample_spec_snprint(buf, sizeof(buf), &pam->ss));

#if PA_API_VERSION >= 12
  pl = pa_proplist_new();
  if(mp->mp_flags & MP_VIDEO)
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "video");
  else
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "music");

  s = pa_stream_new_with_proplist(pam->context, "Showtime playback", 
				  &pam->ss, &map, pl);  
  pa_proplist_free(pl);

#else
  s = pa_stream_new(pam->context, "Showtime playback", &pam->ss, &map);  
#endif
  
  pa_stream_set_state_callback(s, stream_state_callback, pam);
  pa_stream_set_write_callback(s, stream_write_callback, pam);

  flags |= PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING;

  memset(&cv, 0, sizeof(cv));
  pa_cvolume_set(&cv, pam->ss.channels, pam->mastervol);
#if 1
  pa_buffer_attr pba = {0};

  pba.fragsize = (uint32_t)-1;
  pba.maxlength = 16 * 1024;
  pba.minreq    = 3  * 1024;
  pba.prebuf    = 8  * 1024;
  pba.tlength   = 12 * 1024;
#endif
  pa_stream_connect_playback(s, NULL, &pba, flags, &cv, NULL);

  pam->stream = s;
  pam->cur_rate   = ab->ab_samplerate;
  pam->cur_format = ab->ab_format;
  pam->cur_isfloat = ab->ab_isfloat;
}


/**
 * This is called whenever the context status changes 
 */
static void
stream_destroy(pa_audio_mode_t *pam)
{
  pa_stream_disconnect(pam->stream);

  pa_stream_unref(pam->stream);
  pam->stream = NULL;
}



/**
 * Sink input updated, reflect master volume and mute status into
 * showtime's properties.
 */
static void
update_sink_input_info(pa_context *c, const pa_sink_input_info *i, 
		       int eol, void *userdata)
{
  pa_audio_mode_t *pam = (pa_audio_mode_t *)userdata;
  int n;
  pa_volume_t max = PA_VOLUME_MUTED;

  if(i == NULL)
    return;

  for(n = 0; n < i->volume.channels; n++) {
    if(i->volume.values[n] > max)
      max = i->volume.values[n];
  }

  pam->mastervol = max;
  pam->muted = !!i->mute;

  prop_set_float_ex(prop_mastervol, pam->sub_mvol, 
		    pa_sw_volume_to_dB(pam->mastervol), 0);
  prop_set_int_ex(prop_mastermute, pam->sub_mute, pam->muted);
}


/**
 *
 */
static void
subscription_event_callback(pa_context *c, pa_subscription_event_type_t t,
			    uint32_t idx, void *userdata)
{
  pa_audio_mode_t *pam = (pa_audio_mode_t *)userdata;
  pa_operation *o;

  if(pam->stream == NULL ||
     pa_stream_get_state(pam->stream) != PA_STREAM_READY)
    return;

  if((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) !=
     PA_SUBSCRIPTION_EVENT_SINK_INPUT)
    return;

  if(pa_stream_get_index(pam->stream) != idx)
    return;

  o = pa_context_get_sink_input_info(pam->context, idx,
				     update_sink_input_info, pam);
  if(o != NULL)
    pa_operation_unref(o);
}


/**
 * This is called whenever the context status changes 
 */
static void 
context_state_callback(pa_context *c, void *userdata)
{
  pa_audio_mode_t *pam = (pa_audio_mode_t *)userdata;
  pa_operation *o;

  switch(pa_context_get_state(c)) {
  case PA_CONTEXT_CONNECTING:
  case PA_CONTEXT_UNCONNECTED:
  case PA_CONTEXT_AUTHORIZING:
  case PA_CONTEXT_SETTING_NAME:
    break;

  case PA_CONTEXT_READY:

    pa_context_set_subscribe_callback(pam->context, 
				      subscription_event_callback, pam);
    
    o = pa_context_subscribe(pam->context,
			     PA_SUBSCRIPTION_MASK_SINK_INPUT,
			     NULL, NULL);
    if(o != NULL)
      pa_operation_unref(o);

    TRACE(TRACE_DEBUG, "PA", "Context ready");
    pa_threaded_mainloop_signal(mainloop, 0);
    break;

  case PA_CONTEXT_TERMINATED:
    if(pam->stream != NULL) {
      pa_stream_unref(pam->stream);
      pam->stream = NULL;
    }

    TRACE(TRACE_ERROR, "PA", "Context terminated");
    pa_threaded_mainloop_signal(mainloop, 0);
    break;

  case PA_CONTEXT_FAILED:
    if(pam->stream != NULL) {
      pa_stream_unref(pam->stream);
      pam->stream = NULL;
    }

    TRACE(TRACE_ERROR, "PA",
	  "Connection failure: %s", pa_strerror(pa_context_errno(c)));
    pa_threaded_mainloop_signal(mainloop, 0);
    break;
  }
}


/**
 * Volume adjusted (from within showtime), send update to PA
 *
 * Lock for PA mainloop is already held
 */
static void
set_mastervol(void *opaque, float value)
{
  pa_audio_mode_t *pam = opaque;
  pa_operation *o;
  pa_cvolume cv;

  pam->mastervol = pa_sw_volume_from_dB(value);

  if(pam->stream == NULL ||
     pa_stream_get_state(pam->stream) != PA_STREAM_READY)
    return;
  
  memset(&cv, 0, sizeof(cv));
  pa_cvolume_set(&cv, pam->ss.channels, pam->mastervol);

  o = pa_context_set_sink_input_volume(pam->context, 
				       pa_stream_get_index(pam->stream),
				       &cv, NULL, NULL);
  if(o != NULL)
    pa_operation_unref(o);
}


/**
 * Mute (from within showtime), send update to PA
 *
 * Lock for PA mainloop is already held
 */
static void
set_mastermute(void *opaque, int value)
{
  pa_audio_mode_t *pam = opaque;
  pa_operation *o;

  pam->muted = value;

  if(pam->stream == NULL ||
     pa_stream_get_state(pam->stream) != PA_STREAM_READY)
    return;

  o = pa_context_set_sink_input_mute(pam->context,
				     pa_stream_get_index(pam->stream),
				     pam->muted, NULL, NULL);
  if(o != NULL)
    pa_operation_unref(o);
}



/**
 * Prop lockmanager for locking PA mainloop
 */
static void
prop_pa_lockmgr(void *ptr, int lock)
{
  if(lock)
    pa_threaded_mainloop_lock(mainloop);
  else
    pa_threaded_mainloop_unlock(mainloop);
}

/**
 *
 */
static int
pa_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  pa_audio_mode_t *pam = (pa_audio_mode_t *)am;
  audio_buf_t *ab = NULL;
  size_t l, length;
  int64_t pts;
  media_pipe_t *mp;
  int r = 0;

  pa_threaded_mainloop_lock(mainloop);

#if PA_API_VERSION >= 12
  pa_proplist *pl = pa_proplist_new();

  pa_proplist_sets(pl, PA_PROP_APPLICATION_ID, "com.lonelycoder.hts.showtime");
  pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, "Showtime");
  
  /* Create a new connection context */
  pam->context = pa_context_new_with_proplist(api, "Showtime", pl);
  pa_proplist_free(pl);
#else
  pam->context = pa_context_new(api, "Showtime");
#endif
  if(pam->context == NULL) {
    pa_threaded_mainloop_unlock(mainloop);
    return -1;
  }

  pa_context_set_state_callback(pam->context, context_state_callback, pam);

  /* Connect the context */
  if(pa_context_connect(pam->context, NULL, 0, NULL) < 0) {
    TRACE(TRACE_ERROR, "PA", "pa_context_connect() failed: %s",
	  pa_strerror(pa_context_errno(pam->context)));
    pa_threaded_mainloop_unlock(mainloop);
    return -1;
  }

 /* Need at least one packet of audio */

  /* Subscribe to updates of master volume */
  pam->sub_mvol = 
    prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_CALLBACK_FLOAT, set_mastervol, pam,
		   PROP_TAG_ROOT, prop_mastervol,
		   PROP_TAG_EXTERNAL_LOCK, mainloop, prop_pa_lockmgr,
		   NULL);

  /* Subscribe to updates of master volume mute */
  pam->sub_mute = 
    prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_CALLBACK_INT, set_mastermute, pam,
		   PROP_TAG_ROOT, prop_mastermute,
		   PROP_TAG_EXTERNAL_LOCK, mainloop, prop_pa_lockmgr,
		   NULL);
 
  while(1) {

    if(ab == NULL) {
      pa_threaded_mainloop_unlock(mainloop);
      ab = af_deq2(af, 1, am);
      pa_threaded_mainloop_lock(mainloop);

      if(ab == AF_EXIT) {
	ab = NULL;
	break;
      }
    }

    if(pa_context_get_state(pam->context) == PA_CONTEXT_TERMINATED ||
       pa_context_get_state(pam->context) == PA_CONTEXT_FAILED) {
      r = -1;
      break;
    }
    if(pam->stream != NULL &&
       (pam->cur_format != ab->ab_format ||
	pam->cur_rate   != ab->ab_samplerate || 
	pam->cur_isfloat != ab->ab_isfloat)) {
      stream_destroy(pam);
    }

     if(pam->stream == NULL &&
       pa_context_get_state(pam->context) == PA_CONTEXT_READY) {
      /* Context is ready, but we don't have a stream yet, set it up */
      stream_setup(pam, ab);
    }

    if(pam->stream == NULL) {
      pa_threaded_mainloop_wait(mainloop);
      continue;
    }


    switch(pa_stream_get_state(pam->stream)) {
    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
      pa_threaded_mainloop_wait(mainloop);
      continue;
    
    case PA_STREAM_READY:
      break;

    case PA_STREAM_TERMINATED:
    case PA_STREAM_FAILED:
      pa_stream_unref(pam->stream);
      pam->stream = NULL;

      char msg[100];

      snprintf(msg, sizeof(msg),
	       "Audio stream disconnected from "
	       "PulseAudio server -- %s.",
	       pa_strerror(pam->stream_error));
      
      mp_flush(ab->ab_mp, 0);
      mp_enqueue_event(ab->ab_mp, event_create_str(EVENT_INTERNAL_PAUSE, msg));

      audio_fifo_purge(af, NULL, NULL);

      if(ab != NULL) {
	ab_free(ab);
	ab = NULL;
      }
      continue;
    }

    if(ab->ab_flush) {
      pa_operation *o;
      o = pa_stream_flush(pam->stream, NULL, NULL);
      if(o != NULL)
	pa_operation_unref(o);
      ab->ab_flush = 0;
    }

    l = pa_stream_writable_size(pam->stream);
    
    if(l == 0) {
      pa_threaded_mainloop_wait(mainloop);
      continue;
    }

    length = ab->ab_frames * pa_frame_size(&pam->ss) - ab->ab_tmp;
    
    if(l > length)
      l = length;
    
    if((pts = ab->ab_pts) != AV_NOPTS_VALUE && ab->ab_mp != NULL) {
      int64_t pts;
      pa_usec_t delay;

      pts = ab->ab_pts;
      ab->ab_pts = AV_NOPTS_VALUE;

      if(!pa_stream_get_latency(pam->stream, &delay, NULL)) {

	mp = ab->ab_mp;
	hts_mutex_lock(&mp->mp_clock_mutex);
	mp->mp_audio_clock = pts - delay;
	mp->mp_audio_clock_realtime = showtime_get_ts();
	mp->mp_audio_clock_epoch = ab->ab_epoch;
	hts_mutex_unlock(&mp->mp_clock_mutex);
      }
    }

    pa_stream_write(pam->stream, ab->ab_data + ab->ab_tmp,
		    l, NULL, 0LL, PA_SEEK_RELATIVE);

    ab->ab_tmp += l;

    assert(ab->ab_tmp <= ab->ab_frames * pa_frame_size(&pam->ss));

    if(ab->ab_frames * pa_frame_size(&pam->ss) == ab->ab_tmp) {
      ab_free(ab);
      ab = NULL;
    }
  }

  prop_unsubscribe(pam->sub_mvol);
  prop_unsubscribe(pam->sub_mute);

  if(pam->stream != NULL)
    stream_destroy(pam);

  pa_threaded_mainloop_unlock(mainloop);
  pa_context_unref(pam->context);

  if(ab != NULL) {
    ab_free(ab);
    ab = NULL;
  }

  return r;
}

/**
 *
 */
int
audio_pa_init(void)
{
  pa_audio_mode_t *pam;
  audio_mode_t *am;

  TRACE(TRACE_DEBUG, "PA", "Headerversion: %s, library: %s",
	pa_get_headers_version(), pa_get_library_version());

  mainloop = pa_threaded_mainloop_new();
  api = pa_threaded_mainloop_get_api(mainloop);

  pa_threaded_mainloop_lock(mainloop);
  pa_threaded_mainloop_start(mainloop);
  pa_threaded_mainloop_unlock(mainloop);

  pam = calloc(1, sizeof(pa_audio_mode_t));
  am = &pam->am;
  am->am_multich_controls = 1;
  am->am_formats = 
    AM_FORMAT_PCM_STEREO | AM_FORMAT_PCM_5DOT1 | AM_FORMAT_PCM_7DOT1;
  am->am_sample_rates = AM_SR_ANY;
  am->am_title = strdup("Pulseaudio");
  am->am_id = strdup("pulseaudio");
  am->am_preferred_size = 1024;
  am->am_entry = pa_audio_start;
  am->am_float = 1;

  audio_mode_register(am);
  return 1;
}
