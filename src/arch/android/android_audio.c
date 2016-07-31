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

#define PCM_RING_SIZE 8
#define PCM_RING_MASK (PCM_RING_SIZE - 1)


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
  int d_pcmbuf_size;

  int d_avail_buffers;

  int d_write_ptr;
  int d_read_ptr;

  float d_gain;
  float d_last_set_vol;

  int d_sleeptime;

  int d_samples_sent;

  int64_t d_timestamp[PCM_RING_SIZE];
  int d_epoch[PCM_RING_SIZE];

  int d_mark_epoch;
  int64_t d_mark_ts;
  int64_t d_mark_samples;

  int d_pause ;

} decoder_t;

extern float audio_master_volume;
extern int   audio_master_mute;

int android_system_audio_sample_rate;
int android_system_audio_frames_per_buffer;


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


static void
player_cb(SLPlayItf caller,
          void *pContext,
          SLuint32 event)
{
  decoder_t *d = pContext;
  media_pipe_t *mp = d->ad.ad_mp;

  if(event != 4) {
    TRACE(TRACE_DEBUG, "SLES" ,"Event %x", event);
    return;
  }
#if 0
  int64_t now = arch_get_avtime();
  static int64_t last;
  int64_t delta_realtime = now - last;
  last = now;
#endif
  SLmillisecond ms;
  (*d->d_pif)->GetPosition(d->d_pif, &ms);

  // Current sample being played
  int64_t current_sample = (int64_t)ms * d->ad.ad_out_sample_rate / 1000LL;

  int64_t audio_delay_samples = d->d_samples_sent - current_sample +
    d->ad.ad_tile_size * PCM_RING_SIZE;

  d->ad.ad_delay = audio_delay_samples * 1000000LL / d->ad.ad_out_sample_rate;
  if(d->d_mark_ts != PTS_UNSET) {

    hts_mutex_lock(&mp->mp_clock_mutex);

    mp->mp_audio_clock_epoch = d->d_mark_epoch;
    mp->mp_audio_clock_avtime = arch_get_avtime();
    mp->mp_audio_clock = d->d_mark_ts - d->ad.ad_delay;
    mp->mp_realtime_delta = mp->mp_audio_clock_avtime - mp->mp_audio_clock;
    hts_mutex_unlock(&mp->mp_clock_mutex);
    d->d_mark_ts = PTS_UNSET;
  }
#if 0
  
  static int last_ms;
  int delta_ms = ms - last_ms;
  last_ms = ms;

  static int last_samples_sent;
  int sd = d->d_samples_sent - last_samples_sent;
  last_samples_sent = d->d_samples_sent;

  int64_t sbt = ((int64_t)ms * 44100LL) / 1000LL;

  TRACE(TRACE_DEBUG, "SLES",
        "RTD:%7lld ts:%7d (delta:%6d) samples:%7d (delta:%6d) sbt:%9lld %9lld rtd:%9lld %c",
        delta_realtime, ms, delta_ms, d->d_samples_sent, sd, sbt,
        d->d_samples_sent - sbt, mp->mp_realtime_delta, upd ? '*' : ' ');
#endif
}



/**
 *
 */
static int
android_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  android_stop_player(d);

  int num_sles_buffers = 2;

  ad->ad_out_sample_rate = android_system_audio_sample_rate ?: 44100;

  SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
    SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, num_sles_buffers};

  ad->ad_out_sample_format  = AV_SAMPLE_FMT_S16;
  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;

  d->d_framesize = 2 * sizeof(int16_t);

  ad->ad_tile_size = android_system_audio_frames_per_buffer ?: 1024;
  while(ad->ad_tile_size < 512)
    ad->ad_tile_size *= 2;

  d->d_sleeptime = 1000 /* ms */ * ad->ad_tile_size / ad->ad_out_sample_rate;

  d->d_pcmbuf_size = d->d_framesize * ad->ad_tile_size;
  d->d_pcmbuf = calloc(PCM_RING_SIZE, d->d_pcmbuf_size);

  TRACE(TRACE_DEBUG, "SLES",
        "Player samplerate=%d framesize=%d",
        ad->ad_out_sample_rate, ad->ad_tile_size);

  SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM,
                                 2,
                                 ad->ad_out_sample_rate * 1000,
                                 SL_PCMSAMPLEFORMAT_FIXED_16,
                                 SL_PCMSAMPLEFORMAT_FIXED_16,
                                 SL_SPEAKER_FRONT_LEFT |
                                 SL_SPEAKER_FRONT_RIGHT,
                                 SL_BYTEORDER_LITTLEENDIAN};

  SLDataSource audioSrc = {&loc_bufq, &format_pcm};

  // configure audio sink
  SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX,
                                        d->d_mixer};
  SLDataSink audioSnk = {&loc_outmix, NULL};

  // create audio player
  const SLInterfaceID ids[2] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE, SL_IID_VOLUME};
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
  if((*d->d_player)->GetInterface(d->d_player, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                  &d->d_bif)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to get buffer queue interface");
    return -1;
  }

  // register callback on the buffer queue
  if((*d->d_bif)->RegisterCallback(d->d_bif, buffer_callback, ad)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to register callback");
    return -1;
  }

  // set the player callback
  if((*d->d_pif)->RegisterCallback(d->d_pif, player_cb, d)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to set playback callback");
    return -1;
  }

  if((*d->d_pif)->SetCallbackEventsMask(d->d_pif, 0x1f)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to set event mask");
    return -1;
  }

  if((*d->d_pif)->SetPositionUpdatePeriod(d->d_pif, 100)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to set position update period");
    return -1;
  }

  for(int i = 0; i < PCM_RING_SIZE; i++)
    d->d_timestamp[i] = PTS_UNSET;

  // set the player's state to playing
  if((*d->d_pif)->SetPlayState(d->d_pif, SL_PLAYSTATE_PLAYING)) {
    TRACE(TRACE_ERROR, "SLES", "Unable to set playback state");
    return -1;
  }

  (*d->d_bif)->Enqueue(d->d_bif, d->d_pcmbuf, d->d_pcmbuf_size);
  d->d_read_ptr = 0;
  d->d_write_ptr = 1;

  d->d_avail_buffers = num_sles_buffers;
  return 0;
}


/**
 *
 */
static void
buffer_callback(SLAndroidSimpleBufferQueueItf bqif, void *context)
{
  decoder_t *d = context;
  __sync_synchronize();
  const int nr = (d->d_read_ptr + 1) & PCM_RING_MASK;
  if(nr == d->d_write_ptr) {
    TRACE(TRACE_DEBUG, "GLES", "Underrun");
    int offset = d->d_read_ptr * d->d_pcmbuf_size;
    memset(d->d_pcmbuf + offset, 0, d->d_pcmbuf_size);
    (*d->d_bif)->Enqueue(d->d_bif, d->d_pcmbuf + offset, d->d_pcmbuf_size);
    d->d_samples_sent += d->ad.ad_tile_size;
    return;
  }

  int offset = nr * d->d_pcmbuf_size;
  int64_t pts = d->d_timestamp[nr];
  int epoch = d->d_epoch[nr];
  d->d_timestamp[nr] = PTS_UNSET;
  if(pts != PTS_UNSET) {
    d->d_mark_ts = pts;
    d->d_mark_epoch = epoch;
    d->d_mark_samples = d->d_samples_sent;
  }

  (*d->d_bif)->Enqueue(d->d_bif, d->d_pcmbuf + offset, d->d_pcmbuf_size);
  d->d_samples_sent += d->ad.ad_tile_size;

  d->d_read_ptr = nr;
  __sync_synchronize();
}


/**
 *
 */
static int
android_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;

  float gain = audio_master_mute ? 0.0f : (d->d_gain * audio_master_volume);
  if(gain != d->d_last_set_vol) {
    d->d_last_set_vol = gain;

    int mb = lroundf(2000.f * log10f(gain));
    mb = MAX(mb, SL_MILLIBEL_MIN);
    (*d->d_vif)->SetVolumeLevel(d->d_vif, mb);
  }

  while(avresample_available(ad->ad_avr) >= ad->ad_tile_size) {

    __sync_synchronize();

    if(((d->d_write_ptr + 1) & PCM_RING_MASK) == d->d_read_ptr)
      return d->d_sleeptime; // Time for one slot in ring buffer

    uint8_t *data[8] = {0};
    data[0] = d->d_pcmbuf + d->d_write_ptr * d->d_pcmbuf_size;
    avresample_read(ad->ad_avr, data, ad->ad_tile_size);

    if(pts != PTS_UNSET) {
      d->d_timestamp[d->d_write_ptr] = pts;
      d->d_epoch[d->d_write_ptr] = epoch;
      pts = PTS_UNSET;
    }

    d->d_write_ptr = (d->d_write_ptr + 1) & PCM_RING_MASK;
    __sync_synchronize();
  }

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
static void
android_audio_pause(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  (*d->d_pif)->SetPlayState(d->d_pif, SL_PLAYSTATE_PAUSED);
  d->d_pause = 1;
}


/**
 *
 */
static void
android_audio_play(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  (*d->d_pif)->SetPlayState(d->d_pif, SL_PLAYSTATE_PLAYING);
  d->d_pause = 0;
}


/**
 *
 */
static void
android_audio_flush(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  //  if(d->d_pif == NULL)
    //    return;

  //  (*d->d_pif)->SetPlayState(d->d_pif, SL_PLAYSTATE_STOPPED);

  //  (*d->d_bif)->Clear(d->d_bif);
  /*
  (*d->d_pif)->SetPlayState(d->d_pif,
                            d->d_pause ? SL_PLAYSTATE_PAUSED :
                            SL_PLAYSTATE_PLAYING);

  (*d->d_pif)->SetCallbackEventsMask(d->d_pif, 0x1f);
  (*d->d_pif)->SetPositionUpdatePeriod(d->d_pif, 100);

  (*d->d_bif)->Enqueue(d->d_bif, d->d_pcmbuf, d->d_pcmbuf_size);
  */
  d->d_read_ptr = 0;
  d->d_write_ptr = 1;
  __sync_synchronize();
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
  .ac_pause          = android_audio_pause,
  .ac_play           = android_audio_play,
  .ac_flush          = android_audio_flush,
};


/**
 *
 */
audio_class_t *
audio_driver_init(struct prop *asettings)
{
  return &android_audio_class;
}

