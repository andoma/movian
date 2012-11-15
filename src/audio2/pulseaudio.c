#include <pulse/pulseaudio.h>
#include <assert.h>

#include "showtime.h"
#include "audio.h"
#include "media.h"

static pa_threaded_mainloop *mainloop;
static pa_mainloop_api *api;
static pa_context *ctx;

typedef struct decoder {
  audio_decoder_t ad;
  pa_stream *s;
  int framesize;
} decoder_t;

/**
 *
 */
static void
pulseaudio_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  
  if(d->s) {
    pa_stream_disconnect(d->s);
    pa_stream_unref(d->s);
  }
}


/**
 *
 */
static void
stream_write_callback(pa_stream *s, size_t length, void *userdata)
{
  audio_decoder_t *ad = userdata;
  media_pipe_t *mp = ad->ad_mp;
  hts_mutex_lock(&mp->mp_mutex);
  hts_cond_signal(&mp->mp_audio.mq_avail);
  hts_mutex_unlock(&mp->mp_mutex);
}


static void
stream_state_callback(pa_stream *s, void *userdata)
{
  pa_threaded_mainloop_signal(mainloop, 0);
}


/**
 *
 */
static int
pulseaudio_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  pa_threaded_mainloop_lock(mainloop);

  while(1) {
    switch(pa_context_get_state(ctx)) {
    case PA_CONTEXT_UNCONNECTED:
      // do reconnect?
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      pa_threaded_mainloop_wait(mainloop);
      continue;

    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
      pa_threaded_mainloop_unlock(mainloop);
      return -1;

    case PA_CONTEXT_READY:
      break;
    }
    break;
  }

  if(d->s) {
    pa_stream_disconnect(d->s);
    pa_stream_unref(d->s);
  }

  pa_sample_spec ss = {0};
  pa_channel_map map;


  ad->ad_out_sample_rate = ad->ad_in_sample_rate;
  ss.rate = ad->ad_in_sample_rate;
  
  switch(ad->ad_in_sample_format) {
  case AV_SAMPLE_FMT_S32:
  case AV_SAMPLE_FMT_S32P:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S32;
    ss.format = PA_SAMPLE_S32NE;
    d->framesize = sizeof(int32_t);
    break;

  case AV_SAMPLE_FMT_S16:
  case AV_SAMPLE_FMT_S16P:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S16;
    ss.format = PA_SAMPLE_S16NE;
    d->framesize = sizeof(int16_t);
    break;

  default:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_FLT;
    ss.format = PA_SAMPLE_FLOAT32NE;
    d->framesize = sizeof(float);
    break;
  }


  switch(ad->ad_in_channel_layout) {
  case AV_CH_LAYOUT_MONO:
    ss.channels = 1;
    pa_channel_map_init_mono(&map);
    ad->ad_out_channel_layout = AV_CH_LAYOUT_MONO;
    break;

  default:
    ss.channels = 2;
    pa_channel_map_init_stereo(&map);
    ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
    break;
  }

  d->framesize *= ss.channels;

  ad->ad_num_samples = pa_context_get_tile_size(ctx, &ss) / d->framesize;

  char buf[100];
  TRACE(TRACE_DEBUG, "PA", "Created stream %s (tilesize=%d)",
	pa_sample_spec_snprint(buf, sizeof(buf), &ss),
	ad->ad_num_samples);

#if PA_API_VERSION >= 12
  pa_proplist *pl = pa_proplist_new();
  media_pipe_t *mp = ad->ad_mp;
  if(mp->mp_flags & MP_VIDEO)
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "video");
  else
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "music");

  d->s = pa_stream_new_with_proplist(ctx, "Showtime playback", 
				     &ss, &map, pl);  
  pa_proplist_free(pl);

#else
  d->s = pa_stream_new(ctx, "Showtime playback", &ss, &map);  
#endif
 
  int flags = 0;

  pa_stream_set_state_callback(d->s, stream_state_callback, d);
  pa_stream_set_write_callback(d->s, stream_write_callback, d);

  flags |= PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING;

  pa_stream_connect_playback(d->s, NULL, NULL, flags, NULL, NULL);

  while(1) {
    switch(pa_stream_get_state(d->s)) {
    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
      pa_threaded_mainloop_wait(mainloop);
      continue;

    case PA_STREAM_READY:
      pa_threaded_mainloop_unlock(mainloop);
      return 0;

    case PA_STREAM_TERMINATED:
    case PA_STREAM_FAILED:
      pa_threaded_mainloop_unlock(mainloop);
      return 1;
    }
  }
}


static void
pulseaudio_audio_cork(audio_decoder_t *ad, int b)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->s == NULL)
    return;
  pa_operation *o = pa_stream_cork(d->s, b, NULL, NULL);
  if(o != NULL)
    pa_operation_unref(o);
}


/**
 *
 */
static void
pulseaudio_audio_pause(audio_decoder_t *ad)
{
  pulseaudio_audio_cork(ad, 1);
}


/**
 *
 */
static void
pulseaudio_audio_play(audio_decoder_t *ad)
{
  pulseaudio_audio_cork(ad, 0);
}


/**
 *
 */
static void
pulseaudio_audio_flush(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->s == NULL)
    return;
  pa_operation *o = pa_stream_flush(d->s, NULL, NULL);
  if(o != NULL)
    pa_operation_unref(o);
}


/**
 *
 */
static int
pulseaudio_audio_deliver(audio_decoder_t *ad, int samples,
			 int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;
  size_t bytes = samples * d->framesize;
  void *buf;
  assert(d->s != NULL);
  

  pa_threaded_mainloop_lock(mainloop);

  int writable = pa_stream_writable_size(d->s);
  if(writable == 0) {
    pa_threaded_mainloop_unlock(mainloop);
    return 1; 
  }

  pa_stream_begin_write(d->s, &buf, &bytes);
  if(bytes == 0) {
    pa_threaded_mainloop_unlock(mainloop);
    return 1; 
  }

  int rsamples = bytes / d->framesize;
  uint8_t *data[8] = {0};
  data[0] = (uint8_t *)buf;
  assert(rsamples <= samples);
  avresample_read(ad->ad_avr, data, rsamples);

  pa_usec_t delay;

  if(!pa_stream_get_latency(d->s, &delay, NULL)) {
    
    ad->ad_delay = delay;
    if(pts != AV_NOPTS_VALUE) {
      media_pipe_t *mp = ad->ad_mp;
      hts_mutex_lock(&mp->mp_clock_mutex);
      mp->mp_audio_clock = pts - delay;
      mp->mp_audio_clock_avtime = showtime_get_avtime();
      mp->mp_audio_clock_epoch = epoch;
      hts_mutex_unlock(&mp->mp_clock_mutex);
    }
  }

  pa_stream_write(d->s, buf, bytes, NULL, 0LL, PA_SEEK_RELATIVE);
  pa_threaded_mainloop_unlock(mainloop);
  return 0;

}


/**
 *
 */
static audio_class_t pulseaudio_audio_class = {
  .ac_alloc_size = sizeof(decoder_t),
  
  .ac_fini = pulseaudio_audio_fini,
  .ac_reconfig = pulseaudio_audio_reconfig,
  .ac_deliver = pulseaudio_audio_deliver,

  .ac_pause = pulseaudio_audio_pause,
  .ac_play  = pulseaudio_audio_play,
  .ac_flush = pulseaudio_audio_flush,
};


/**
 * This is called whenever the context status changes 
 */
static void 
context_state_callback(pa_context *c, void *userdata)
{
  pa_operation *o;

  switch(pa_context_get_state(c)) {
  case PA_CONTEXT_CONNECTING:
  case PA_CONTEXT_UNCONNECTED:
  case PA_CONTEXT_AUTHORIZING:
  case PA_CONTEXT_SETTING_NAME:
    break;

  case PA_CONTEXT_READY:

    //    pa_context_set_subscribe_callback(c, subscription_event_callback, pam);
    
    o = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK_INPUT,
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
audio_class_t *
audio_driver_init(void)
{
  TRACE(TRACE_DEBUG, "PA", "Headerversion: %s, library: %s",
	pa_get_headers_version(), pa_get_library_version());

  mainloop = pa_threaded_mainloop_new();
  api = pa_threaded_mainloop_get_api(mainloop);

  pa_threaded_mainloop_lock(mainloop);
  pa_threaded_mainloop_start(mainloop);

#if PA_API_VERSION >= 12
  pa_proplist *pl = pa_proplist_new();

  pa_proplist_sets(pl, PA_PROP_APPLICATION_ID, "com.lonelycoder.hts.showtime");
  pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, "Showtime");
  
  /* Create a new connection context */
  ctx = pa_context_new_with_proplist(api, "Showtime", pl);
  pa_proplist_free(pl);
#else
  ctx = pa_context_new(api, "Showtime");
#endif

  pa_context_set_state_callback(ctx, context_state_callback, NULL);

  /* Connect the context */
  if(pa_context_connect(ctx, NULL, 0, NULL) < 0) {
    TRACE(TRACE_ERROR, "PA", "pa_context_connect() failed: %s",
	  pa_strerror(pa_context_errno(ctx)));
    pa_threaded_mainloop_unlock(mainloop);
    return NULL;
  }

  pa_threaded_mainloop_unlock(mainloop);


  return &pulseaudio_audio_class;
}

