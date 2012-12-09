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
  pa_sample_spec ss;

  int blocked;
} decoder_t;


/**
 *
 */
int64_t
showtime_get_avtime(void)
{
  return showtime_get_ts();
}


/**
 *
 */
static void
stream_write_callback(pa_stream *s, size_t length, void *userdata)
{
  decoder_t *d = userdata;
  media_pipe_t *mp = d->ad.ad_mp;
  int writable = pa_stream_writable_size(d->s);

  if(writable && d->blocked) {
    d->blocked = 0;
    mp_send_cmd(mp, &mp->mp_audio, MB_CTRL_UNBLOCK);
  }
}


static void
stream_state_callback(pa_stream *s, void *userdata)
{
  pa_threaded_mainloop_signal(mainloop, 0);
}


static const struct {
  int64_t avmask;
  enum pa_channel_position papos;
} av2pa_map[] = {
  { AV_CH_FRONT_LEFT,             PA_CHANNEL_POSITION_FRONT_LEFT },
  { AV_CH_FRONT_RIGHT,            PA_CHANNEL_POSITION_FRONT_RIGHT },
  { AV_CH_FRONT_CENTER,           PA_CHANNEL_POSITION_FRONT_CENTER },
  { AV_CH_LOW_FREQUENCY,          PA_CHANNEL_POSITION_LFE },
  { AV_CH_BACK_LEFT,              PA_CHANNEL_POSITION_REAR_LEFT },
  { AV_CH_BACK_RIGHT,             PA_CHANNEL_POSITION_REAR_RIGHT },
  { AV_CH_FRONT_LEFT_OF_CENTER,   PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER },
  { AV_CH_FRONT_RIGHT_OF_CENTER,  PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER },
  { AV_CH_BACK_CENTER,            PA_CHANNEL_POSITION_REAR_CENTER },
  { AV_CH_SIDE_LEFT,              PA_CHANNEL_POSITION_SIDE_LEFT },
  { AV_CH_SIDE_RIGHT,             PA_CHANNEL_POSITION_SIDE_RIGHT },
  { AV_CH_TOP_CENTER,             PA_CHANNEL_POSITION_TOP_CENTER },
  { AV_CH_TOP_FRONT_LEFT,         PA_CHANNEL_POSITION_TOP_FRONT_LEFT },
  { AV_CH_TOP_FRONT_CENTER,       PA_CHANNEL_POSITION_TOP_FRONT_CENTER },
  { AV_CH_TOP_FRONT_RIGHT,        PA_CHANNEL_POSITION_TOP_FRONT_RIGHT },
  { AV_CH_TOP_BACK_LEFT,          PA_CHANNEL_POSITION_TOP_REAR_LEFT },
  { AV_CH_TOP_BACK_CENTER,        PA_CHANNEL_POSITION_TOP_REAR_CENTER },
  { AV_CH_TOP_BACK_RIGHT,         PA_CHANNEL_POSITION_TOP_REAR_RIGHT },
  { AV_CH_STEREO_LEFT,            PA_CHANNEL_POSITION_FRONT_LEFT },
  { AV_CH_STEREO_RIGHT,           PA_CHANNEL_POSITION_FRONT_RIGHT },
};



/**
 *
 */
static int
pulseaudio_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  int i;

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

  pa_channel_map map;


  ad->ad_out_sample_rate = ad->ad_in_sample_rate;
  d->ss.rate = ad->ad_in_sample_rate;
  
  switch(ad->ad_in_sample_format) {
  case AV_SAMPLE_FMT_S32:
  case AV_SAMPLE_FMT_S32P:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S32;
    d->ss.format = PA_SAMPLE_S32NE;
    d->framesize = sizeof(int32_t);
    break;

  case AV_SAMPLE_FMT_S16:
  case AV_SAMPLE_FMT_S16P:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S16;
    d->ss.format = PA_SAMPLE_S16NE;
    d->framesize = sizeof(int16_t);
    break;

  default:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_FLT;
    d->ss.format = PA_SAMPLE_FLOAT32NE;
    d->framesize = sizeof(float);
    break;
  }

  switch(ad->ad_in_channel_layout) {
  case AV_CH_LAYOUT_MONO:
    d->ss.channels = 1;
    ad->ad_out_channel_layout = AV_CH_LAYOUT_MONO;
    pa_channel_map_init_mono(&map);
    break;


  case AV_CH_LAYOUT_STEREO:
    d->ss.channels = 2;
    ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
    pa_channel_map_init_stereo(&map);

    
  default:
    pa_channel_map_init(&map);
    for(i = 0; i < sizeof(av2pa_map) / sizeof(av2pa_map[0]); i++) {
      if(ad->ad_in_channel_layout & av2pa_map[i].avmask) {
	ad->ad_out_channel_layout |= av2pa_map[i].avmask;
	map.map[map.channels++] = av2pa_map[i].papos;
      }
    }
    d->ss.channels = map.channels;
    break;
  }

  d->framesize *= d->ss.channels;

  ad->ad_tile_size = pa_context_get_tile_size(ctx, &d->ss) / d->framesize;

  char buf[100];
  char buf2[PA_CHANNEL_MAP_SNPRINT_MAX];
  TRACE(TRACE_DEBUG, "PA", "Created stream %s [%s] (tilesize=%d)",
	pa_sample_spec_snprint(buf, sizeof(buf), &d->ss),
	pa_channel_map_snprint(buf2, sizeof(buf2), &map),
	ad->ad_tile_size);

#if PA_API_VERSION >= 12
  pa_proplist *pl = pa_proplist_new();
  media_pipe_t *mp = ad->ad_mp;
  if(mp->mp_flags & MP_VIDEO)
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "video");
  else
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "music");

  d->s = pa_stream_new_with_proplist(ctx, "Showtime playback", 
				     &d->ss, &map, pl);
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



/**
 *
 */
static void
pulseaudio_audio_cork(audio_decoder_t *ad, int b)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->s == NULL)
    return;
  pa_threaded_mainloop_lock(mainloop);
  pa_operation *o = pa_stream_cork(d->s, b, NULL, NULL);
  if(o != NULL)
    pa_operation_unref(o);
  pa_threaded_mainloop_unlock(mainloop);
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
  pa_threaded_mainloop_lock(mainloop);
  pa_operation *o = pa_stream_flush(d->s, NULL, NULL);
  if(o != NULL)
    pa_operation_unref(o);
  pa_threaded_mainloop_unlock(mainloop);
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
    d->blocked = 1;
    pa_threaded_mainloop_unlock(mainloop);
    return 1; 
  }

  pa_stream_begin_write(d->s, &buf, &bytes);
  assert(bytes > 0);

  int rsamples = bytes / d->framesize;
  uint8_t *data[8] = {0};
  data[0] = (uint8_t *)buf;
  assert(rsamples <= samples);
  avresample_read(ad->ad_avr, data, rsamples);

  if(pts != AV_NOPTS_VALUE) {

    media_pipe_t *mp = ad->ad_mp;
    hts_mutex_lock(&mp->mp_clock_mutex);

    const pa_timing_info *ti = pa_stream_get_timing_info(d->s);
    if(ti != NULL) {
      mp->mp_audio_clock_avtime = 
	ti->timestamp.tv_sec * 1000000LL + ti->timestamp.tv_usec;

      int64_t busec = pa_bytes_to_usec(ti->write_index - ti->read_index,
				       &d->ss);

      int64_t delay = ti->sink_usec + busec + ti->transport_usec;
      mp->mp_audio_clock = pts - delay;
      mp->mp_audio_clock_epoch = epoch;
    }
    hts_mutex_unlock(&mp->mp_clock_mutex);
  }

  pa_stream_write(d->s, buf, bytes, NULL, 0LL, PA_SEEK_RELATIVE);

  pa_threaded_mainloop_unlock(mainloop);
  return 0;
}


/**
 *
 */
static void
pulseaudio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  pa_threaded_mainloop_lock(mainloop);
  if(d->s) {
    pa_stream_disconnect(d->s);

    while(pa_stream_get_state(d->s) != PA_STREAM_TERMINATED &&
	  pa_stream_get_state(d->s) != PA_STREAM_FAILED)
      pa_threaded_mainloop_wait(mainloop);
    pa_stream_unref(d->s);
  }
  pa_threaded_mainloop_unlock(mainloop);
}

/**
 *
 */
static audio_class_t pulseaudio_audio_class = {
  .ac_alloc_size   = sizeof(decoder_t),
  .ac_fini         = pulseaudio_fini,
  .ac_play         = pulseaudio_audio_play,
  .ac_pause        = pulseaudio_audio_pause,
  .ac_flush        = pulseaudio_audio_flush,
  .ac_reconfig     = pulseaudio_audio_reconfig,
  .ac_deliver_unlocked = pulseaudio_audio_deliver,
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

