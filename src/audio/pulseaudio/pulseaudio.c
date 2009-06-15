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

#include "showtime.h"
#include "audio/audio.h"

static pa_threaded_mainloop *mainloop;
static pa_mainloop_api *api;


typedef struct pa_audio_mode {
  audio_mode_t am;
  pa_context *context;
  pa_stream *stream;
  int framesize;

  int cur_format;
  int cur_rate;

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
  //  pa_audio_mode_t *pam = (pa_audio_mode_t *)userdata;
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
  int n;
  int flags = 0;
  pa_sample_spec ss;
#if PA_API_VERSION >= 12
  pa_proplist *pl;
  media_pipe_t *mp = ab->ab_mp;
#endif
  pa_channel_map map;

  memset(&ss, 0, sizeof(ss));

  ss.format = PA_SAMPLE_S16NE; 
  switch(ab->ab_rate) {
  default:
  case AM_SR_96000: ss.rate = 96000; break;
  case AM_SR_48000: ss.rate = 48000; break;
  case AM_SR_44100: ss.rate = 44100; break;
  case AM_SR_32000: ss.rate = 32000; break;
  case AM_SR_24000: ss.rate = 24000; break;
  }

  switch(ab->ab_format) {
  case AM_FORMAT_PCM_STEREO:
    ss.channels = 2;
    pa_channel_map_init_stereo(&map);
    break;

  case AM_FORMAT_PCM_5DOT1:
    ss.channels = 6;
    pa_channel_map_init_auto(&map, 6, PA_CHANNEL_MAP_ALSA);
    break;

  case AM_FORMAT_PCM_7DOT1:
    ss.channels = 8;
    pa_channel_map_init_auto(&map, 8, PA_CHANNEL_MAP_ALSA);
    break;
  default:
    abort();
  }

  pam->framesize = pa_frame_size(&ss);

  TRACE(TRACE_DEBUG, "PA", "Created stream %s",
	pa_sample_spec_snprint(buf, sizeof(buf), &ss));

#if PA_API_VERSION >= 12
  pl = pa_proplist_new();
  if(mp->mp_flags & MP_VIDEO)
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "video");
  else
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "music");

  s = pa_stream_new_with_proplist(pam->context, "Showtime playback", 
				  &ss, &map, pl);  
  pa_proplist_free(pl);

#else
  s = pa_stream_new(pam->context, "Showtime playback", &ss, &map);  
#endif
  
  pa_stream_set_state_callback(s, stream_state_callback, pam);
  pa_stream_set_write_callback(s, stream_write_callback, pam);

  flags |= PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING;
  n = pa_stream_connect_playback(s, NULL, NULL, flags, NULL, NULL);

  pam->stream = s;
  pam->cur_rate   = ab->ab_rate;
  pam->cur_format = ab->ab_format;
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
 *
 */
static void
subscription_event_callback(pa_context *c, pa_subscription_event_type_t t,
			    uint32_t idx, void *userdata)
{
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
			     PA_SUBSCRIPTION_MASK_ALL,
			     NULL, NULL);
    if(o != NULL)
      pa_operation_unref(o);

    TRACE(TRACE_DEBUG, "PA", "Context ready");
    pa_threaded_mainloop_signal(mainloop, 0);
    break;

  case PA_CONTEXT_TERMINATED:
    TRACE(TRACE_ERROR, "PA", "Context terminated");
    pa_threaded_mainloop_signal(mainloop, 0);
    break;

  case PA_CONTEXT_FAILED:
    TRACE(TRACE_ERROR, "PA",
	  "Connection failure: %s", pa_strerror(pa_context_errno(c)));
    pa_threaded_mainloop_signal(mainloop, 0);
    break;
  }
}



/**
 *
 */
static int
pa_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  pa_audio_mode_t *pam = (pa_audio_mode_t *)am;
  audio_buf_t *ab;
  size_t l, length;
  int64_t pts;
  media_pipe_t *mp;
  //  pa_operation *o;

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
  ab = af_deq(af, 1);

  while(am == audio_mode_current) {

    if(ab == NULL) {
      pa_threaded_mainloop_unlock(mainloop);
      ab = af_deq(af, 1);
      pa_threaded_mainloop_lock(mainloop);
    }

    if(pam->stream != NULL &&
       (pam->cur_format != ab->ab_format ||
	pam->cur_rate   != ab->ab_rate)) {
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

    if(pa_stream_get_state(pam->stream) != PA_STREAM_READY) {
      pa_threaded_mainloop_wait(mainloop);
      continue;
    }
    
    l = pa_stream_writable_size(pam->stream);
    
    if(l == 0) {
      pa_threaded_mainloop_wait(mainloop);
      continue;
    }

    length = ab->ab_frames * pam->framesize - ab->ab_tmp;
    
    if(l > length)
      l = length;
    
    pa_stream_write(pam->stream, ab->ab_data + ab->ab_tmp,
		    l, NULL, 0LL, PA_SEEK_RELATIVE);

    ab->ab_tmp += l;

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
    assert(ab->ab_tmp <= ab->ab_frames * pam->framesize);

    if(ab->ab_frames * pam->framesize == ab->ab_tmp) {
      ab_free(ab);
      ab = NULL;
    }
  }

  if(pam->stream != NULL)
    stream_destroy(pam);

  pa_threaded_mainloop_unlock(mainloop);
  pa_context_unref(pam->context);

  if(ab != NULL) {
    ab_free(ab);
    ab = NULL;
  }

  return 0;
}

/**
 *
 */
void audio_pa_init(void);

void
audio_pa_init(void)
{
  pa_audio_mode_t *pam;
  audio_mode_t *am;

  TRACE(TRACE_INFO, "PA", "Headerversion: %s, library: %s",
	pa_get_headers_version(), pa_get_library_version());

  mainloop = pa_threaded_mainloop_new();
  api = pa_threaded_mainloop_get_api(mainloop);

  pa_threaded_mainloop_lock(mainloop);
  pa_threaded_mainloop_start(mainloop);
  pa_threaded_mainloop_unlock(mainloop);

  pam = calloc(1, sizeof(pa_audio_mode_t));
  am = &pam->am;
  am->am_formats = 
    AM_FORMAT_PCM_STEREO | AM_FORMAT_PCM_5DOT1 | AM_FORMAT_PCM_7DOT1;
  am->am_sample_rates = AM_SR_96000 | AM_SR_48000 | AM_SR_44100 | 
    AM_SR_32000 | AM_SR_24000;
  am->am_title = strdup("Pulseaudio");
  am->am_id = strdup("pulseaudio");
  am->am_preferred_size = 4096;
  am->am_entry = pa_audio_start;

  TAILQ_INIT(&am->am_mixer_controllers);
  audio_mode_register(am);
}
