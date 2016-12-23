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
#include <unistd.h>

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_audio.h"
#include "ppapi/c/ppb_audio_config.h"
#include "ppapi/c/ppb_core.h"

#include "main.h"
#include "audio2/audio.h"
#include "media/media.h"

extern const PPB_Core *ppb_core;
extern const PPB_AudioConfig *ppb_audioconfig;
extern const PPB_Audio *ppb_audio;
extern PP_Instance g_Instance;

#define SLOTS 4
#define SLOTMASK (SLOTS - 1)

typedef struct decoder {
  audio_decoder_t ad;

  PP_Resource config;

  PP_Resource player;

  pthread_mutex_t mutex;
  pthread_cond_t cond;

  float *samples;  // SLOTS * channels * sizeof(float) * ad_tile_size

  int rdptr;
  int wrptr;

  double nacl_latency;
  int64_t fifo_latency;

} decoder_t;



/**
 *
 */
static void
audio_cb(void *sample_buffer, uint32_t buffer_size_in_bytes,
         PP_TimeDelta latency, void *user_data)
{
  decoder_t *d = user_data;

  pthread_mutex_lock(&d->mutex);

  int off = (d->rdptr & SLOTMASK) * 2 * d->ad.ad_tile_size;

  int16_t *out = sample_buffer;
  const float *src = d->samples + off;
  float s = audio_master_mute ? 0 : audio_master_volume * d->ad.ad_vol_scale;

  for(int i = 0; i < buffer_size_in_bytes / sizeof(uint16_t); i++) {
    float x = src[i] * s;
    if(x < -1.0f)
      x = -1.0f;
    else if(x > 1.0f)
      x = 1.0f;
    out[i] = x * 32767.0f;
  }

  d->rdptr++;

  d->nacl_latency = latency;

  pthread_cond_signal(&d->cond);
  pthread_mutex_unlock(&d->mutex);
}


/**
 *
 */
static void
nacl_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  free(d->samples);
  d->samples = NULL;

  if(d->player) {
    ppb_audio->StopPlayback(d->player);
    ppb_core->ReleaseResource(d->player);
    d->player = 0;
  }

  if(d->config) {
    ppb_core->ReleaseResource(d->config);
    d->config = 0;
  }

  pthread_mutex_destroy(&d->mutex);
  pthread_cond_destroy(&d->cond);
}


/**
 *
 */
static int
nacl_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  nacl_audio_fini(ad);

  pthread_mutex_init(&d->mutex, NULL);
  pthread_cond_init(&d->cond, NULL);

  int sample_rate;
  int tile_size;

  sample_rate = ppb_audioconfig->RecommendSampleRate(g_Instance);

  tile_size = ppb_audioconfig->RecommendSampleFrameCount(g_Instance,
                                                         sample_rate,
                                                         1024);

  d->config = ppb_audioconfig->CreateStereo16Bit(g_Instance,
                                                 sample_rate,
                                                 tile_size);

  if(d->config == 0) {
    sample_rate = ad->ad_in_sample_rate;

    tile_size = ppb_audioconfig->RecommendSampleFrameCount(g_Instance,
                                                           sample_rate,
                                                           1024);

    d->config = ppb_audioconfig->CreateStereo16Bit(g_Instance,
                                                   sample_rate,
                                                   tile_size);
    if(d->config == 0) {
      sample_rate = 48000;

      tile_size = ppb_audioconfig->RecommendSampleFrameCount(g_Instance,
                                                             sample_rate,
                                                             1024);

      d->config = ppb_audioconfig->CreateStereo16Bit(g_Instance,
                                                     sample_rate,
                                                     tile_size);
      if(d->config == 0) {
        TRACE(TRACE_ERROR, "AUDIO",
              "Unable to setup audio session. Audio will not work.");
      }
    }
  }


  ad->ad_out_sample_format = AV_SAMPLE_FMT_FLT;
  ad->ad_out_sample_rate = sample_rate;
  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
  ad->ad_tile_size = tile_size;

  d->samples = calloc(1, SLOTS * 2 * sizeof(float) * ad->ad_tile_size);

  d->player = ppb_audio->Create(g_Instance, d->config, audio_cb, ad);
  ppb_audio->StartPlayback(d->player);
  TRACE(TRACE_DEBUG, "AUDIO", "Audio playback started");


  d->fifo_latency = 1000000LL * ad->ad_tile_size * SLOTS / sample_rate;
  TRACE(TRACE_DEBUG, "AUDIO", "Fifo latency: %d", (int)d->fifo_latency);

  return 0;
}


/**
 *
 */
static int
nacl_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;

  pthread_mutex_lock(&d->mutex);

  while((d->rdptr & SLOTMASK) == (d->wrptr & SLOTMASK) &&
        d->wrptr != d->rdptr)
    pthread_cond_wait(&d->cond, &d->mutex);

  pthread_mutex_unlock(&d->mutex);

  int off = (d->wrptr & SLOTMASK) * 2 /* channels */ * d->ad.ad_tile_size;

  uint8_t *data[8] = {0};
  data[0] = (uint8_t *)(d->samples + off);
  avresample_read(ad->ad_avr, data, samples);
  d->wrptr++;

  if(pts != AV_NOPTS_VALUE) {

    int64_t delay = d->fifo_latency + d->nacl_latency * 1000000.0;
    ad->ad_delay = delay;

    media_pipe_t *mp = ad->ad_mp;

    hts_mutex_lock(&mp->mp_clock_mutex);
    mp->mp_audio_clock_avtime = arch_get_avtime();
    mp->mp_audio_clock_epoch = epoch;
    mp->mp_audio_clock = pts - delay;
    hts_mutex_unlock(&mp->mp_clock_mutex);
  }

  return 0;
}


/**
 *
 */
static void
nacl_audio_pause(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  ppb_audio->StopPlayback(d->player);
}


/**
 *
 */
static void
nacl_audio_play(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  ppb_audio->StartPlayback(d->player);
}


/**
 *
 */
static void
nacl_audio_flush(audio_decoder_t *ad)
{
}


/**
 *
 */
static audio_class_t nacl_audio_class = {
  .ac_alloc_size       = sizeof(decoder_t),
  .ac_fini             = nacl_audio_fini,
  .ac_reconfig         = nacl_audio_reconfig,
  .ac_deliver_unlocked = nacl_audio_deliver,
  .ac_pause            = nacl_audio_pause,
  .ac_play             = nacl_audio_play,
  .ac_flush            = nacl_audio_flush,
};



/**
 *
 */
audio_class_t *
audio_driver_init(struct prop *asettings)
{
  return &nacl_audio_class;
}

