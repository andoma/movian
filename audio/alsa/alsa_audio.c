/*
 *  Alsa audio output
 *  Copyright (C) 2007 Andreas Öman
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

#include <pthread.h>
#include <math.h>
#include <stdio.h>
#define  __USE_XOPEN
#include <unistd.h>
#include <stdlib.h>

#include <sys/time.h>

#include <errno.h>

#include "showtime.h"
#include "alsa_audio.h"
#include "alsa_mixer.h"
#include "audio/audio_sched.h"

audio_ctx_t actx0;

static void
audio_reset(audio_ctx_t *actx)
{
  if(actx->handle == NULL)
    return;

  actx->reset = 1;
  pthread_cond_signal(&actx->fifo_cond);

  pthread_mutex_lock(&actx->cfg_mutex);
  while(actx->handle != NULL) 
    pthread_cond_wait(&actx->cfg_cond, &actx->cfg_mutex);
  pthread_mutex_unlock(&actx->cfg_mutex);
}

static void
audio_fifo_clear(audio_ctx_t *actx)
{
  pthread_mutex_lock(&actx->fifo_mutex);
  actx->fifo_wp = 0;
  actx->fifo_rp = 0;
  actx->fifo_len = 0;
  actx->resampler_spill_size = 0;
  pthread_mutex_unlock(&actx->fifo_mutex);
  
}

static void
audio_configure(audio_ctx_t *actx, int channels, int rate, int period_hint)
{
  snd_pcm_hw_params_t *hwp;
  snd_pcm_sw_params_t *swp;
  snd_pcm_t *h;
  const char *dev;
  int r, dir;
  actx_mode_t mode;
  snd_pcm_uframes_t period_size_min;
  snd_pcm_uframes_t period_size_max;
  snd_pcm_uframes_t buffer_size_min;
  snd_pcm_uframes_t buffer_size_max;
  snd_pcm_uframes_t period_size;
  snd_pcm_uframes_t buffer_size;

  switch(channels) {
  case 1:
    mode = ACTX_ANALOG_MONO;
    break;
  case 2:
    mode = ACTX_ANALOG_STEREO;
    break;
  case 5:
    mode = ACTX_ANALOG_5CHAN;
    break;
  case 6:
    mode = ACTX_ANALOG_5DOT1;
    break;
  default:
    printf("Warning: cannot handle # of channels: %d\n", channels);

    mode = ACTX_NONE;
    break;
  }

  if(actx->handle != NULL && actx->mode == mode && actx->rate == rate)
    return;

  /* Clear fifo */

  audio_fifo_clear(actx);
  actx->fifo_channels = channels;


  if(actx->resampler != NULL) {
    av_resample_close(actx->resampler);
    actx->resampler = NULL;
  }


  actx->mode = 0;  
  actx->rate = rate;

  audio_reset(actx);
  
  switch(mode) {
  case ACTX_NONE:
  default:
    return;

  case ACTX_ANALOG_MONO:
    dev = "pcm.hts_mono";
    channels = 1;
    break;
  case ACTX_ANALOG_STEREO:
    dev = "pcm.hts_stereo";
    channels = 2;
    break;
  case ACTX_ANALOG_5CHAN:
    dev = "pcm.hts_5chan";
    channels = 5;
    break;
  case ACTX_ANALOG_5DOT1:
    dev = "pcm.hts_5dot1";
    channels = 6;
    break;
  }

  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
    fprintf(stderr, "audio: Cannot open audio device %s (%s)\n",
		dev, snd_strerror(r));
    return;
  }

  snd_pcm_hw_params_alloca(&hwp);

  snd_pcm_hw_params_any(h, hwp);
  snd_pcm_hw_params_set_access(h, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(h, hwp, SND_PCM_FORMAT_S16_LE);

  actx->orate = 48000;

  if((r = snd_pcm_hw_params_set_rate_near(h, hwp, &actx->orate, 0)) < 0) {
    fprintf(stderr, "audio: Cannot set rate to %d (%s)\n", 
	    rate, snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  fprintf(stderr, "audio: rate = %d\n", actx->orate);


  if((r = snd_pcm_hw_params_set_channels(h, hwp, channels)) < 0) {
    fprintf(stderr, "audio: Cannot set # of channels to %d (%s)\n",
	    rate, snd_strerror(r));

    snd_pcm_close(h);
    return;
  }
  
  /* Configurue buffer size */

  snd_pcm_hw_params_get_buffer_size_min(hwp, &buffer_size_min);
  snd_pcm_hw_params_get_buffer_size_max(hwp, &buffer_size_max);
  buffer_size = buffer_size_max;
  buffer_size = 5460 * 2;

  fprintf(stderr, "audio: attainable buffer size %lu - %lu, trying %lu\n",
	  buffer_size_min, buffer_size_max, buffer_size);


  dir = 0;
  r = snd_pcm_hw_params_set_buffer_size_near(h, hwp, &buffer_size);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to set buffer size %lu (%s)\n",
	    buffer_size, snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  r = snd_pcm_hw_params_get_buffer_size(hwp, &actx->buffer_size);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to get buffer size (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }
  fprintf(stderr, "audio: Buffer size = %lu\n", actx->buffer_size);


  /* Configurue period */


  dir = 0;
  snd_pcm_hw_params_get_period_size_min(hwp, &period_size_min, &dir);
  dir = 0;
  snd_pcm_hw_params_get_period_size_max(hwp, &period_size_max, &dir);

  printf("audio: period_hint = %d\n", period_hint);

  period_size = 1365;

  fprintf(stderr, "audio: attainable period size %lu - %lu, trying %lu\n",
	  period_size_min, period_size_max, period_size);


  dir = 1;
  r = snd_pcm_hw_params_set_period_size_near(h, hwp, &period_size, &dir);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to set period size %lu (%s)\n",
	    period_size, snd_strerror(r));
    snd_pcm_close(h);
    return;
  }


  dir = 0;
  r = snd_pcm_hw_params_get_period_size(hwp, &actx->buffer_size, &dir);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to get period size (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }


  /* write the hw params */
  r = snd_pcm_hw_params(h, hwp);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure hardware parameters (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  /*
   * Software parameters
   */

  snd_pcm_sw_params_alloca(&swp);
  snd_pcm_sw_params_current(h, swp);

  
  r = snd_pcm_sw_params_set_avail_min(h, swp, actx->buffer_size / 2);

  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure wakeup threshold (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  r = snd_pcm_sw_params_set_xfer_align(h, swp, 1);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure xfer alignment (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }


  snd_pcm_sw_params_set_start_threshold(h, swp, 0);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure start threshold (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }
  
  r = snd_pcm_sw_params(h, swp);
  if(r < 0) {
    fprintf(stderr, "audio: Cannot set soft parameters (%s)\n", 
		snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  r = snd_pcm_prepare(h);
  if(r < 0) {
    fprintf(stderr, "audio: Cannot prepare audio for playback (%s)\n", 
		snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  pthread_mutex_lock(&actx->cfg_mutex);

  actx->channels = channels;
  actx->handle = h;
  actx->mode = mode;

  actx->resampler = av_resample_init(actx->orate, actx->rate, 
				     16, 10, 0, 1.0);

  printf("audio: period size = %ld\n", actx->period_size);
  printf("audio: buffer size = %ld\n", actx->buffer_size);

  printf("rate : %ld, orate = %u\n", actx->rate, actx->orate);

  pthread_cond_signal(&actx->cfg_cond);

  pthread_mutex_unlock(&actx->cfg_mutex);


}



static void *
audio_fifo_thread(void *aux)
{
  audio_ctx_t *actx = aux;
  int c, w, x, i, j, v;
  int16_t *d;
  int peak[ACTX_MAX_CHANNELS];
  float peakf[ACTX_MAX_CHANNELS];

  while(1) {

    if(actx->handle == NULL) {

      pthread_mutex_lock(&actx->cfg_mutex);
      while(actx->handle == NULL) 
	pthread_cond_wait(&actx->cfg_cond, &actx->cfg_mutex);
      pthread_mutex_unlock(&actx->cfg_mutex);
    }

    c = snd_pcm_wait(actx->handle, 100);

    if(actx->reset) {
    reset:
      pthread_mutex_lock(&actx->cfg_mutex);
      snd_pcm_close(actx->handle);
      actx->handle = NULL; 
      actx->reset = 0;
      pthread_cond_signal(&actx->cfg_cond);
      pthread_mutex_unlock(&actx->cfg_mutex);
      continue;
    }
    
    if(c == 0)
      continue;

    if(c >= 0) 
      c = snd_pcm_avail_update(actx->handle);
    if(c == -EPIPE) {
      printf("fifo underrun\n");
      snd_pcm_prepare(actx->handle);
      continue;
    }

    pthread_mutex_lock(&actx->fifo_mutex);

    while(actx->fifo_len < ACTX_FIFO_LEN / 2 && actx->reset == 0)
      pthread_cond_wait(&actx->fifo_cond, &actx->fifo_mutex);

    if(actx->reset) {
      pthread_mutex_unlock(&actx->fifo_mutex);
      goto reset;
    }

    w = FFMIN(actx->fifo_len, ACTX_FIFO_LEN - actx->fifo_rp);
    w = FFMIN(w, c);

    d = actx->fifo + actx->fifo_rp * actx->fifo_channels;

    x = snd_pcm_writei(actx->handle, d, w);

    memset(peak, 0, sizeof(peak));

    for(i = 0; i < w; i++) {
      for(j = 0; j < actx->channels; j++) {
	v = *d++;
	peak[j] += v < 0 ? -v : v;
      }
    }

    for(j = 0; j < actx->channels; j++) {
      peakf[j] = log10((float)peak[j] / (float)w / 2.0f) / 4.0f;
    }


    switch(actx->channels) {
    case 1:
      actx->peak[0] = 0;
      actx->peak[1] = 0;
      actx->peak[2] = 0;
      actx->peak[3] = 0;
      actx->peak[4] = peakf[0];
      actx->peak[5] = 0;
      break;

    case 2:
      actx->peak[0] = peakf[0];
      actx->peak[1] = peakf[1];
      actx->peak[2] = 0;
      actx->peak[3] = 0;
      actx->peak[4] = 0;
      actx->peak[5] = 0;
      break;

    case 5:
      actx->peak[0] = peakf[0];
      actx->peak[1] = peakf[2];
      actx->peak[2] = peakf[3];
      actx->peak[3] = peakf[4];
      actx->peak[4] = peakf[1];
      actx->peak[5] = 0;
      break;

    case 6:
      actx->peak[0] = peakf[1];
      actx->peak[1] = peakf[3];
      actx->peak[2] = peakf[4];
      actx->peak[3] = peakf[5];
      actx->peak[4] = peakf[2];
      actx->peak[5] = peakf[0];
      break;
    }


    actx->fifo_len -= x;
    actx->fifo_rp = (actx->fifo_rp + x) & ACTX_FIFO_MASK;

    pthread_cond_signal(&actx->fifo_cond);
    pthread_mutex_unlock(&actx->fifo_mutex);
  }
}









static int
actx_resample(audio_ctx_t *actx, int16_t *dstmix, int dstavail,
	      int *writtenp, int16_t *srcmix, int srcframes)
{
  int c, i, j;
  int16_t *src;
  int16_t *dst;
  int written = 0;
  int consumed;
  int srcsize;
  int channels = actx->channels;
  int spill = actx->resampler_spill_size;
  

  if(spill > srcframes)
    srcframes = 0;
    
  dst = malloc(dstavail * sizeof(uint16_t));

  for(c = 0; c < channels; c++) {

    if(actx->resampler_spill[c].data != NULL) {

      srcsize = spill + srcframes;

      src = malloc(srcsize * sizeof(uint16_t));

      j = 0;

      for(i = 0; i < spill; i++)
	src[j++] = actx->resampler_spill[c].data[i];

      for(i = 0; i < srcframes; i++)
	src[j++] = srcmix[i * channels + c];

      free(actx->resampler_spill[c].data);
      actx->resampler_spill[c].data = NULL;

    } else {

      srcsize = srcframes;

      src = malloc(srcsize * sizeof(uint16_t));

      for(i = 0; i < srcframes; i++)
	src[i] = srcmix[i * channels + c];

    }

    written = av_resample(actx->resampler, dst, src, &consumed, 
			  srcsize, dstavail, c == channels - 1);

    if(consumed != srcsize) {
      actx->resampler_spill_size = srcsize - consumed;

      actx->resampler_spill[c].data = 
	malloc(actx->resampler_spill_size * sizeof(uint16_t));

      memcpy(actx->resampler_spill[c].data, src + consumed, 
	     actx->resampler_spill_size * sizeof(uint16_t));
    }

    for(i = 0; i < written; i++)
      dstmix[i * channels + c] = dst[i];

    free(src);
  }

  *writtenp = written;

  free(dst);

  return srcframes;
}



static int
audio_bitrate_by_ctx(AVCodecContext *ctx)
{
  int bitrate;
  /* for PCM codecs, compute bitrate directly */
  switch(ctx->codec_id) {
  case CODEC_ID_PCM_S32LE:
  case CODEC_ID_PCM_S32BE:
  case CODEC_ID_PCM_U32LE:
  case CODEC_ID_PCM_U32BE:
    bitrate = ctx->sample_rate * ctx->channels * 32;
    break;
  case CODEC_ID_PCM_S24LE:
  case CODEC_ID_PCM_S24BE:
  case CODEC_ID_PCM_U24LE:
  case CODEC_ID_PCM_U24BE:
  case CODEC_ID_PCM_S24DAUD:
    bitrate = ctx->sample_rate * ctx->channels * 24;
    break;
  case CODEC_ID_PCM_S16LE:
  case CODEC_ID_PCM_S16BE:
  case CODEC_ID_PCM_U16LE:
  case CODEC_ID_PCM_U16BE:
    bitrate = ctx->sample_rate * ctx->channels * 16;
    break;
  case CODEC_ID_PCM_S8:
  case CODEC_ID_PCM_U8:
  case CODEC_ID_PCM_ALAW:
  case CODEC_ID_PCM_MULAW:
    bitrate = ctx->sample_rate * ctx->channels * 8;
    break;
  default:
    bitrate = ctx->bit_rate;
    break;
  }
  return bitrate;
}


static void
audio_decode(asched_t *as, audio_ctx_t *actx, media_pipe_t *mp,
	     media_buf_t *mb)
{
  uint8_t *buf;
  int16_t *data = actx->data;
  int buf_size, r, data_size, frames, channels, rate;
  snd_pcm_sframes_t delay;
  AVCodecContext *ctx;
  codecwrap_t *cw = mb->mb_cw;
  int s, f, c;
  int16_t *dst;
  media_queue_t *mq = &mp->mp_audio;
  time_t now;
  const char *chantxt;

  ctx = cw->codec_ctx;

  buf = mb->mb_data;
  buf_size = mb->mb_size;

  while(1) {
    wrap_lock_codec(cw);

    r = avcodec_decode_audio(ctx, data, &data_size, buf, buf_size);
    channels = ctx->channels;
    rate = ctx->sample_rate;

    time(&now);
    if(now != mq->mq_info_last_time) {
      mq->mq_info_rate = mq->mq_info_rate_acc / 125;
      mq->mq_info_last_time = now;
      mq->mq_info_rate_acc = 0;
    }
    mq->mq_info_rate_acc += buf_size;

    mq->mq_info_rate = audio_bitrate_by_ctx(ctx) / 1000;

    nice_codec_name(mq->mq_info_codec, sizeof(mq->mq_info_codec), ctx);
 
    switch(channels) {
    case 1:
      chantxt = "Mono";
      break;
    case 2:
      chantxt = "Stereo";
      break;
    case 5:
      chantxt = "5.0";
      break;
    case 6:
      chantxt = "5.1";
      break;
    default:
      chantxt = "???";

    }

    snprintf(mq->mq_info_output_type, sizeof(mq->mq_info_output_type),
	     "%s %.1fkHz",
	     chantxt,
	     (float)rate / 1000.0f);

    wrap_unlock_codec(cw);
  
    if(r == -1 || data_size == 0)
      break;

    frames = data_size / sizeof(uint16_t) / channels;
    audio_configure(actx, channels, rate, frames);
    if(actx->handle == NULL)
      break;
    
    mp->mp_time_feedback = mb->mb_time;

    while(frames > 0) {
 
      /* */
      
      if(mb->mb_pts != actx->last_ts) {
	actx->ts = mb->mb_pts;
	actx->last_ts = mb->mb_pts;
      }

      if(snd_pcm_delay(actx->handle, &delay))
	delay = 0;
      delay += actx->fifo_len;

      delay = (delay * 1000 / actx->rate) * 1000;
      
      mp->mp_clock = actx->ts - delay;
      mp->mp_clock_valid = 1;

       /* Fill fifo */

      pthread_mutex_lock(&actx->fifo_mutex);
      while(actx->fifo_len == ACTX_FIFO_LEN)
	pthread_cond_wait(&actx->fifo_cond, &actx->fifo_mutex);

      f = FFMIN(ACTX_FIFO_LEN - actx->fifo_len, 
		ACTX_FIFO_LEN - actx->fifo_wp);

      dst = actx->fifo + actx->fifo_wp * actx->fifo_channels;

      if(actx->rate == actx->orate) {
	s = FFMIN(f, frames);
	memcpy(dst, data, s * sizeof(uint16_t) * actx->fifo_channels);
	
	c = s;

      } else {
	c = actx_resample(actx, dst, f, &s, data, frames);
      }

      actx->fifo_wp = (actx->fifo_wp + s) & ACTX_FIFO_MASK;
      actx->fifo_len += s;

  
      /* */

      pthread_cond_signal(&actx->fifo_cond);
      pthread_mutex_unlock(&actx->fifo_mutex);
      
      
      actx->ts += (1000000 * c / actx->rate);
      frames -= c;
      data += c * channels;
    }

    buf += r;
    buf_size -= r;
  }

  /* If paused, keep holding lock to freeze audio deliver thread */

  if(as->as_active != NULL && 
     mp_get_playstatus(as->as_active) == MP_PAUSE) {
    pthread_mutex_lock(&actx->fifo_mutex);
    while(as->as_active != NULL &&
	  mp_get_playstatus(as->as_active) == MP_PAUSE) {
      usleep(100000);
    }
    pthread_mutex_unlock(&actx->fifo_mutex);
  }

}


static void *
audio_decode_thread(void *aux)
{
  asched_t *as = aux;
  media_pipe_t *mp;
  media_buf_t *mb;
  audio_ctx_t *actx = &actx0;
  pthread_t ptid;
  int i;

  actx->data = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
  actx->fifo = malloc(sizeof(uint16_t) * ACTX_MAX_CHANNELS * ACTX_FIFO_LEN);

  actx->fifo_wp = 0;
  actx->fifo_rp = 0;

  pthread_mutex_init(&actx->fifo_mutex, NULL);
  pthread_cond_init(&actx->fifo_cond, NULL);
		    

  pthread_create(&ptid, NULL, audio_fifo_thread, actx);

  while(1) {

    pthread_mutex_lock(&as->as_lock);
    while((mp = as->as_active) == NULL) {
      pthread_mutex_unlock(&as->as_lock);
      usleep(250000);
      pthread_mutex_lock(&as->as_lock);
    }
    pthread_mutex_unlock(&as->as_lock);

    mb = mb_dequeue_wait(mp, &mp->mp_audio);

    switch(mb->mb_data_type) {
    case MB_RESET:
      audio_reset(actx);
      goto audio_flush;

    case MB_AUDIO:
      audio_decode(as, actx, mp, mb);
      break;

    case MB_FLUSH:
      audio_fifo_clear(actx);

    audio_flush:
      for(i = 0; i < ACTX_MAX_CHANNELS; i++)
	actx->peak[i] = 0;

      mp->mp_clock_valid = 0;
      break;

    default:
      break;
    }
    media_buf_free(mb);
  }
}




void
alsa_audio_init(asched_t *as)
{
  pthread_t ptid;

  pthread_create(&ptid, NULL, audio_decode_thread, as);
}
