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
#include <string.h>

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include "audio2/audio.h"
#include "misc/minmax.h"

typedef struct decoder {
  audio_decoder_t ad;
  SLObjectItf d_engine;
  SLObjectItf d_mixer;
  SLObjectItf d_player;

  // Interfaces

  SLEngineItf d_eif;
  SLPlayItf d_pif;
  SLVolumeItf d_vif;
  SLAndroidSimpleBufferQueueItf d_bif;


  int d_framesize;
  void *d_pcmbuf;

  int d_pcmbuf_offset;
  int d_pcmbuf_num_buffers;
  int d_pcmbuf_size;

  int d_avail_buffers;

  float d_gain;
  float d_last_set_vol;

} decoder_t;

extern float audio_master_volume;
extern int   audio_master_mute;

static void buffer_callback(SLAndroidSimpleBufferQueueItf bq, void *context);

/**
 *
 */
static int
android_audio_init(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  d->d_gain = 1.0f;
  d->d_last_set_vol = 0;

  if(slCreateEngine(&d->d_engine, 0, NULL, 0, NULL, NULL)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to create engine");
    return -1;
  }

  if((*d->d_engine)->Realize(d->d_engine, SL_BOOLEAN_FALSE)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to relize engine");
    return -1;
  }

  if((*d->d_engine)->GetInterface(d->d_engine, SL_IID_ENGINE, &d->d_eif)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to get interface for engine");
    return -1;
  }

  TRACE(TRACE_DEBUG, "SLES", "Engine opened");

  if((*d->d_eif)->CreateOutputMix(d->d_eif, &d->d_mixer, 0, NULL, NULL)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to create output mixer");
    return -1;
  }

  if((*d->d_mixer)->Realize(d->d_mixer, SL_BOOLEAN_FALSE)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to realize output mixer");
    return -1;
  }

  TRACE(TRACE_DEBUG, "SLES", "Mixer opened");
  return 0;
}


/**
 *
 */
static void
android_stop_player(decoder_t *d)
{
  d->d_avail_buffers = 0;
  free(d->d_pcmbuf);

  if(d->d_player != NULL) {
    (*d->d_player)->Destroy(d->d_player);
    d->d_player = NULL;
    d->d_eif = NULL;
    d->d_pif = NULL;
    d->d_vif = NULL;
    d->d_bif = NULL;
  }
}

/**
 *
 */
static void
android_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  android_stop_player(d);
  (*d->d_mixer)->Destroy(d->d_mixer);
  (*d->d_engine)->Destroy(d->d_engine);
}




/**
 *
 */
static int
android_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  android_stop_player(d);

  int num_buffers = 2;
  int num_pcm_buffers = 2;

  SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
    SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, num_buffers};

  ad->ad_out_sample_format  = AV_SAMPLE_FMT_S16;
  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;

  d->d_framesize = 2 * sizeof(int16_t);

  ad->ad_tile_size = 1024;

  d->d_pcmbuf_size = d->d_framesize * ad->ad_tile_size;
  d->d_pcmbuf = malloc(d->d_pcmbuf_size * num_pcm_buffers);

  SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM,
                                 2,
                                 SL_SAMPLINGRATE_48,
                                 SL_PCMSAMPLEFORMAT_FIXED_16,
                                 SL_PCMSAMPLEFORMAT_FIXED_16,
                                 SL_SPEAKER_FRONT_LEFT |
                                 SL_SPEAKER_FRONT_RIGHT,
                                 SL_BYTEORDER_LITTLEENDIAN};

  ad->ad_out_sample_rate = ad->ad_in_sample_rate;
  format_pcm.samplesPerSec = ad->ad_in_sample_rate * 1000;

  SLDataSource audioSrc = {&loc_bufq, &format_pcm};

  // configure audio sink
  SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX,
                                        d->d_mixer};
  SLDataSink audioSnk = {&loc_outmix, NULL};

  // create audio player
  const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
  const SLboolean req[2]     = {SL_BOOLEAN_TRUE,    SL_BOOLEAN_TRUE};

  if((*d->d_eif)->CreateAudioPlayer(d->d_eif, &d->d_player,
                                    &audioSrc, &audioSnk,
                                    2, ids, req)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to create audio player");
    return -1;
  }

  // realize the player
  if((*d->d_player)->Realize(d->d_player, SL_BOOLEAN_FALSE)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to realize audio player");
    return -1;
  }

  // get the play interface
  if((*d->d_player)->GetInterface(d->d_player, SL_IID_PLAY, &d->d_pif)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to get player interface");
    return -1;
  }


  if((*d->d_player)->GetInterface(d->d_player, SL_IID_VOLUME, &d->d_vif)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to get volume interface");
    return -1;
  }

  // get the buffer queue interface
  if((*d->d_player)->GetInterface(d->d_player, SL_IID_BUFFERQUEUE,
                                  &d->d_bif)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to get buffer queue interface");
    return -1;
  }

  // register callback on the buffer queue
  if((*d->d_bif)->RegisterCallback(d->d_bif, buffer_callback, ad)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to register callback");
    return -1;
  }

  // set the player's state to playing
  if((*d->d_pif)->SetPlayState(d->d_pif, SL_PLAYSTATE_PLAYING)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to set playback state");
    return -1;
  }
  d->d_avail_buffers = num_buffers;
  d->d_pcmbuf_num_buffers = num_pcm_buffers;
  return 0;
}


/**
 *
 */
static void
buffer_callback(SLAndroidSimpleBufferQueueItf bqif, void *context)
{
  decoder_t *d = context;
  media_pipe_t *mp = d->ad.ad_mp;
#if 0
  int64_t now = arch_get_avtime();
  static int64_t last;
  int64_t delta_realtime = now - last;
  last = now;
  SLAndroidSimpleBufferQueueState qs;
  (*d->d_bif)->GetState(bqif, &qs);
  /*
  TRACE(TRACE_DEBUG, "SLES", "qstate: %d %d %lld",
        qs.count, qs.index, delta_realtime);
  */
#endif
  hts_mutex_lock(&mp->mp_mutex);
  d->d_avail_buffers++;
  hts_cond_signal(&mp->mp_audio.mq_avail);
  hts_mutex_unlock(&mp->mp_mutex);

  
}


/**
 *
 */
static int
android_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;
  SLresult result;



  float gain = audio_master_mute ? 0.0f : (d->d_gain * audio_master_volume);
  if(gain != d->d_last_set_vol) {
    d->d_last_set_vol = gain;

    int mb = lroundf(2000.f * log10f(gain));
    mb = MAX(mb, SL_MILLIBEL_MIN);
    (*d->d_vif)->SetVolumeLevel(d->d_vif, mb);
  }

  if(d->d_avail_buffers == 0)
    return 1;

  assert(samples <= ad->ad_tile_size);

  void *pcm = d->d_pcmbuf + (d->d_pcmbuf_offset * d->d_pcmbuf_size);

  uint8_t *data[8] = {0};
  data[0] = pcm;
  int r = avresample_read(ad->ad_avr, data, ad->ad_tile_size);
  result = (*d->d_bif)->Enqueue(d->d_bif, pcm, r * d->d_framesize);

  d->d_avail_buffers--;
  d->d_pcmbuf_offset++;

  if(d->d_pcmbuf_offset == d->d_pcmbuf_num_buffers)
    d->d_pcmbuf_offset = 0;

  if(pts != AV_NOPTS_VALUE) {

    media_pipe_t *mp = ad->ad_mp;
    hts_mutex_lock(&mp->mp_clock_mutex);

    //    int delay = 42666; // 2 * 1024 samples in 48kHz
    //    pts -= delay;

    int64_t now = arch_get_avtime();
#if 0
    static int64_t last;
    int64_t delta_realtime = now - last;
    last = now;
#endif

    int64_t nowpts = now - pts + 350000;
    if(mp->mp_audio_clock_epoch != epoch) {

      mp->mp_audio_clock_epoch = epoch;
      mp->mp_realtime_delta = nowpts;
    } else {
      int64_t realtime_delta = nowpts;
      mp->mp_realtime_delta += (realtime_delta - mp->mp_realtime_delta) * 0.01;
    }

    mp->mp_audio_clock = pts - 42600;
    mp->mp_audio_clock_avtime = now;
    hts_mutex_unlock(&mp->mp_clock_mutex);
    /*
    TRACE(TRACE_DEBUG, "SLES", "delta_realtime = %8lld -> %lld  %lld",
          delta_realtime, nowpts, mp->mp_realtime_delta);
    */
  }

  if(result)
    TRACE(TRACE_ERROR, "SLES", "Enqueue failed 0x%x", result);

  return 0;
}


/**
 *
 */
static void
android_set_volume(audio_decoder_t *ad, float scale)
{
  decoder_t *d = (decoder_t *)ad;
  d->d_gain = scale;
}

/**
 *
 */
static audio_class_t android_audio_class = {
  .ac_alloc_size     = sizeof(decoder_t),
  .ac_init           = android_audio_init,
  .ac_fini           = android_audio_fini,
  .ac_reconfig       = android_audio_reconfig,
  .ac_deliver_locked = android_audio_deliver,
  .ac_set_volume     = android_set_volume,
};


/**
 *
 */
audio_class_t *
audio_driver_init(struct prop *asettings)
{
  return &android_audio_class;
}

