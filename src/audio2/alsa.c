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
#include <alsa/asoundlib.h>
#include <assert.h>

#include "main.h"
#include "audio.h"
#include "media/media.h"
#include "alsa.h"
#include "misc/minmax.h"

typedef struct decoder {
  audio_decoder_t ad;
  snd_pcm_t *h;
  int64_t samples;
  int max_frames_per_write;
  void *tmp;
} decoder_t;


/**
 *
 */
static void
alsa_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  if(d->h != NULL) {
    snd_pcm_close(d->h);
    d->h = NULL;
    TRACE(TRACE_DEBUG, "ALSA", "Closing device");
  }
  free(d->tmp);
}


/**
 *
 */
static int
alsa_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  snd_pcm_t *h;
  int r;

  alsa_audio_fini(ad);

  if(d->h != NULL) {
    snd_pcm_close(d->h);
    d->h = NULL;
    TRACE(TRACE_DEBUG, "ALSA", "Closing device");
  }

  const char *dev = alsa_get_devicename();

  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
    TRACE(TRACE_ERROR, "ALSA", "Unable to open %s -- %s", 
	  dev, snd_strerror(r));
    return -1;
  }

  r = snd_pcm_set_params(h, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED,
			 2, 48000, 0, 100000);

  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "Unable to set params on %s -- %s", 
	  dev, snd_strerror(r));
    return -1;
  }

  snd_pcm_hw_params_t *hwp;
  snd_pcm_hw_params_alloca(&hwp);

  snd_pcm_hw_params_current(h, hwp);

  unsigned int val;
  snd_pcm_uframes_t psize, bsize;

  snd_pcm_hw_params_get_rate(hwp, &val, 0);
  ad->ad_out_sample_rate = val;

  snd_pcm_hw_params_get_buffer_size(hwp, &bsize);
  d->max_frames_per_write = bsize;

  snd_pcm_hw_params_get_period_size(hwp, &psize, 0);
  ad->ad_tile_size = MAX(psize, bsize / 4);

  snd_pcm_sw_params_t *swp;

  snd_pcm_sw_params_alloca(&swp);
  snd_pcm_sw_params_current(h, swp);

  snd_pcm_sw_params_set_avail_min(h, swp, ad->ad_tile_size);
  snd_pcm_sw_params(h, swp);

  TRACE(TRACE_DEBUG, "ALSA", "Opened %s  tile_size=%d, frames_per_write=%d", dev,
        ad->ad_tile_size, d->max_frames_per_write);

  ad->ad_out_sample_format = AV_SAMPLE_FMT_S16;
  ad->ad_out_sample_rate = 48000;
  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
  d->h = h;

  snd_pcm_prepare(d->h);
  

  int channels = 2;
  d->tmp = malloc(sizeof(uint16_t) * channels * d->max_frames_per_write);

  return 0;
}

/**
 *
 */
static int
alsa_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;
  media_pipe_t *mp = ad->ad_mp;
  int c;

 retry:
  c = snd_pcm_wait(d->h, 100);
  if(c >= 0) {
    c = snd_pcm_avail_update(d->h);
  }
  if(c == -EPIPE) {
    snd_pcm_prepare(d->h);
    usleep(100000);
    TRACE(TRACE_DEBUG, "ALSA", "Audio underrun");
    d->samples = 0;
    goto retry;
  }

  c = MIN(d->max_frames_per_write, c);
  c = MIN(samples, c);

  uint8_t *planes[8] = {0};
  planes[0] = d->tmp;

  c = avresample_read(ad->ad_avr, planes, c);
  snd_pcm_status_t *status;
  int err;
  snd_pcm_status_alloca(&status);
  if ((err = snd_pcm_status(d->h, status)) >= 0) {

    if(pts != AV_NOPTS_VALUE) {
      snd_htimestamp_t hts;
      snd_pcm_status_get_trigger_htstamp(status, &hts);
      int64_t ts = hts.tv_sec * 1000000LL + hts.tv_nsec / 1000;
      ts += d->samples * 1000000LL / ad->ad_out_sample_rate;

      hts_mutex_lock(&mp->mp_clock_mutex);
      mp->mp_audio_clock_avtime = ts;
      mp->mp_audio_clock = pts;
      mp->mp_audio_clock_epoch = epoch;
      hts_mutex_unlock(&mp->mp_clock_mutex);
    }
  }

  snd_pcm_sframes_t fr;
  if(!snd_pcm_delay(d->h, &fr))
    ad->ad_delay = 1000000L * fr / ad->ad_out_sample_rate;

  c = snd_pcm_writei(d->h, d->tmp, c);
  d->samples += c;
  return 0;
}


/**
 *
 */
static void
alsa_audio_pause(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->h == NULL)
    return;
  snd_pcm_pause(d->h, 1);
  d->samples = 0;
}


/**
 *
 */
static void
alsa_audio_play(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->h == NULL)
    return;
  snd_pcm_pause(d->h, 0);
  d->samples = 0;
}


/**
 *
 */
static void
alsa_audio_flush(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->h == NULL)
    return;
  snd_pcm_drop(d->h);
  snd_pcm_prepare(d->h);
  d->samples = 0;
}


/**
 *
 */
static audio_class_t alsa_audio_class = {
  .ac_alloc_size       = sizeof(decoder_t),
  .ac_fini             = alsa_audio_fini,
  .ac_reconfig         = alsa_audio_reconfig,
  .ac_deliver_unlocked = alsa_audio_deliver,
  .ac_pause            = alsa_audio_pause,
  .ac_play             = alsa_audio_play,
  .ac_flush            = alsa_audio_flush,
};


/**
 *
 */
audio_class_t *
audio_driver_init(struct prop *asettings)
{
  return &alsa_audio_class;
}

