/*
 *  Audio decoder and features
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <libavcodec/avcodec.h>

#include "showtime.h"
#include "audio_decoder.h"
#include "audio_defs.h"
#include "event.h"
#include "misc/strtab.h"
#include "arch/halloc.h"

extern audio_fifo_t *thefifo;
extern audio_mode_t *audio_mode_current;


static void audio_mix1(audio_decoder_t *ad, audio_mode_t *am, 
		       int channels, int rate, int64_t chlayout,
		       int16_t *data0, int frames, int64_t pts, int epoch,
		       media_pipe_t *mp);

static void audio_mix2(audio_decoder_t *ad, audio_mode_t *am, 
		       int channels, int rate,
		       int16_t *data0, int frames, int64_t pts, int epoch,
		       media_pipe_t *mp);

static void close_resampler(audio_decoder_t *ad);

static int resample(audio_decoder_t *ad, int16_t *dstmix, int dstavail,
		    int *writtenp, int16_t *srcmix, int srcframes,
		    int channels);

static void ad_decode_buf(audio_decoder_t *ad, media_pipe_t *mp,
			  media_queue_t *mq, media_buf_t *mb);

static void audio_deliver(audio_decoder_t *ad, audio_mode_t *am, 
			  const void *src, 
			  int channels, int frames, int rate, int64_t pts,
			  int epoch, media_pipe_t *mp,
			  int isfloat);

static void *ad_thread(void *aux);


/**
 * Create an audio decoder pipeline.
 *
 * Called from media.c
 */
audio_decoder_t *
audio_decoder_create(media_pipe_t *mp)
{
  audio_decoder_t *ad;

  ad = calloc(1, sizeof(audio_decoder_t));
  ad->ad_mp = mp;
  ad->ad_outbuf = halloc(AVCODEC_MAX_AUDIO_FRAME_SIZE * 2);

  TAILQ_INIT(&ad->ad_hold_queue);

  hts_thread_create_joinable("audio decoder", &ad->ad_tid, ad_thread, ad,
			     THREAD_PRIO_HIGH);
  return ad;
}


/**
 * Audio decoder flush
 *
 * Remove all audio data from decoder pipeline
 */
static void
audio_decoder_flush(audio_decoder_t *ad)
{
  audio_fifo_clear_queue(&ad->ad_hold_queue);

  close_resampler(ad);

  if(ad->ad_buf != NULL) {
    ab_free(ad->ad_buf);
    ad->ad_buf = NULL;
  }
}




/**
 * Destroy an audio decoder pipeline.
 *
 * Called from media.c
 */
void
audio_decoder_destroy(audio_decoder_t *ad)
{
  mp_send_cmd_head(ad->ad_mp, &ad->ad_mp->mp_audio, MB_CTRL_EXIT);

  hts_thread_join(&ad->ad_tid);

  audio_decoder_flush(ad);

  hfree(ad->ad_outbuf, AVCODEC_MAX_AUDIO_FRAME_SIZE * 2);
 
  free(ad);
}


/**
 *
 */
static void *
ad_thread(void *aux)
{
  audio_decoder_t *ad = aux;
  media_pipe_t *mp = ad->ad_mp;
  media_queue_t *mq = &mp->mp_audio;
  media_buf_t *mb;
  int hold = 0;
  int run = 1;

  hts_mutex_lock(&mp->mp_mutex);

  while(run) {

    if((mb = TAILQ_FIRST(&mq->mq_q)) == NULL) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    if(mb->mb_data_type == MB_AUDIO && hold && mb->mb_skip == 0) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    mq->mq_freeze_tail = 1;
    mq->mq_packets_current--;
    mp->mp_buffer_current -= mb->mb_size;
    mq_update_stats(mp, mq);
    hts_cond_signal(&mp->mp_backpressure);
    hts_mutex_unlock(&mp->mp_mutex);

    switch(mb->mb_data_type) {
    case MB_CTRL_EXIT:
      run = 0;
      break;

    case MB_CTRL_PAUSE:
      /* Copy back any pending audio in the output fifo */
      audio_fifo_purge(thefifo, ad, &ad->ad_hold_queue);
      hold = 1;
      break;

    case MB_CTRL_PLAY:
      hold = 0;
      break;

    case MB_FLUSH:
      ad->ad_do_flush = 1;
      /* Flush any pending audio in the output fifo */
      audio_fifo_purge(thefifo, ad, NULL);
      audio_decoder_flush(ad);
      break;

    case MB_AUDIO:
      if(mb->mb_skip != 0)
	break;

      if(mb->mb_stream != mq->mq_stream)
	break;

      ad_decode_buf(ad, mp, mq, mb);
      break;

    case MB_END:
      mp_set_current_time(mp, AV_NOPTS_VALUE);
      break;

    default:
      abort();
    }
    hts_mutex_lock(&mp->mp_mutex);
    mq->mq_freeze_tail = 0;
    media_buf_free_locked(mp, mb);
  }
  hts_mutex_unlock(&mp->mp_mutex);
  audio_fifo_purge(thefifo, ad, NULL);
  return NULL;
}

/**
 *
 */
static void
audio_deliver_passthru(media_buf_t *mb, audio_decoder_t *ad, int format,
		       media_pipe_t *mp)
{
  audio_fifo_t *af = thefifo;
  audio_buf_t *ab;

  ab = af_alloc(mb->mb_size, mp);
  ab->ab_channels = 2;
  ab->ab_format   = format;
  ab->ab_samplerate= 48000;
  ab->ab_frames   = mb->mb_size;
  ab->ab_pts      = mb->mb_pts;
  ab->ab_epoch    = mb->mb_epoch;

  memcpy(ab->ab_data, mb->mb_data, mb->mb_size);
  
  ab->ab_ref = ad; /* A reference to our decoder. This is used
		      to revert out packets in the play queue during
		      a pause event */
  
  af_enq(af, ab);
}

static const size_t sample_fmt_to_size[] = {
  [SAMPLE_FMT_U8]  = sizeof(uint8_t),
  [SAMPLE_FMT_S16] = sizeof(int16_t),
  [SAMPLE_FMT_S32] = sizeof(int32_t),
  [SAMPLE_FMT_FLT] = sizeof(float),
  [SAMPLE_FMT_DBL] = sizeof(double),
};

/**
 *
 */
static void
ad_decode_buf(audio_decoder_t *ad, media_pipe_t *mp, media_queue_t *mq, 
	      media_buf_t *mb)
{
  audio_mode_t *am = audio_mode_current;
  uint8_t *buf;
  int size, r, data_size, channels, rate, frames, delay, i;
  media_codec_t *cw = mb->mb_cw;
  AVCodecContext *ctx;
  int64_t pts;
  
  if(cw == NULL) {
    /* Raw native endian PCM */


    if(ad->ad_do_flush) {
      ad->ad_do_flush = 0;
      if(mp_is_primary(mp))
	ad->ad_send_flush = 1;
    } else if(mb->mb_time != AV_NOPTS_VALUE)
      mp_set_current_time(mp, mb->mb_time);

    if(mb->mb_send_pts && mb->mb_pts != AV_NOPTS_VALUE) {
      event_ts_t *ets = event_create(EVENT_CURRENT_PTS, sizeof(event_ts_t));
      ets->ts = mb->mb_pts;
      mp_enqueue_event(mp, &ets->h);
      event_release(&ets->h);
    }

    frames = mb->mb_size / sizeof(int16_t) / mb->mb_channels;


    if(mp_is_primary(mp)) {

      /* Must copy if auto pipeline does multichannel upmixing */
      memcpy(ad->ad_outbuf, mb->mb_data, mb->mb_size);

      audio_mix1(ad, am, mb->mb_channels, mb->mb_rate, 0,
		 ad->ad_outbuf, frames,
		 mb->mb_pts, mb->mb_epoch, mp);
      
    } else {

      /* We are just suppoed to be silent, emulate some kind of 
	 delay, this is not accurate, so we also set the clock epoch
	 to zero to avoid AV sync */

      mp->mp_audio_clock_epoch = 0;

      delay = (int64_t)frames * 1000000LL / mb->mb_rate;
      usleep(delay); /* XXX: Must be better */
	
      /* Flush any packets in the pause pending queue */
      
      audio_fifo_clear_queue(&ad->ad_hold_queue);
    }
    return;
  }

  ctx = cw->codec_ctx;


  if(mp_is_primary(mp)) {
    switch(ctx->codec_id) {
    case CODEC_ID_AC3:
      if(am->am_formats & AM_FORMAT_AC3) {
	audio_deliver_passthru(mb, ad, AM_FORMAT_AC3, mp);
	return;
      }
      break;

    case CODEC_ID_DTS:
      if(am->am_formats & AM_FORMAT_DTS) {
	audio_deliver_passthru(mb, ad, AM_FORMAT_DTS, mp);
	return;
      }
      break;

    default:
      break;
    }
  }
  buf = mb->mb_data;
  size = mb->mb_size;
  pts = mb->mb_pts;
  
  while(size > 0) {

    if(ad->ad_do_flush) {
      avcodec_flush_buffers(cw->codec_ctx);
      ad->ad_do_flush = 0;
      if(mp_is_primary(mp))
	ad->ad_send_flush = 1;
    } else if(mb->mb_time != AV_NOPTS_VALUE)
      mp_set_current_time(mp, mb->mb_time);

    if(mb->mb_send_pts && mb->mb_pts != AV_NOPTS_VALUE) {
      event_ts_t *ets = event_create(EVENT_CURRENT_PTS, sizeof(event_ts_t));
      ets->ts = pts;
      mp_enqueue_event(mp, &ets->h);
      event_release(&ets->h);
    }

    if(audio_mode_stereo_only(am) &&
       cw->codec->id != CODEC_ID_TRUEHD &&
       cw->codec->id != CODEC_ID_MLP)
      ctx->request_channels = 2; /* We can only output stereo.
				    Ask codecs to do downmixing for us. */
    else
      ctx->request_channels = 0;

    data_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = buf;
    avpkt.size = size;

    if(am->am_float)
      ctx->request_sample_fmt = AV_SAMPLE_FMT_FLT;

    r = avcodec_decode_audio3(ctx, ad->ad_outbuf, &data_size, &avpkt);

    if(r == -1)
      break;

    if(mp->mp_stats)
      mp_set_mq_meta(mq, cw->codec, cw->codec_ctx);

    channels = ctx->channels;
    rate     = ctx->sample_rate;

    /* Convert to signed 16bit */

    if(data_size > 0) {

      frames = data_size / sample_fmt_to_size[ctx->sample_fmt];


      if(!mp_is_primary(mp)) {
	mp->mp_audio_clock_epoch = 0;

	delay = (int64_t)(frames / channels) * 1000000LL / rate;
	usleep(delay); /* XXX: Must be better */
	
	/* Flush any packets in the pause pending queue */
      
	audio_fifo_clear_queue(&ad->ad_hold_queue);

      } else {


	/* We are the primary audio decoder == we may play, forward
	   to the mixer stages */
	
	/* But first, if we have any pending packets (due to a
	   previous pause), release them */
      
	audio_fifo_reinsert(thefifo, &ad->ad_hold_queue);


	if(ctx->sample_fmt == SAMPLE_FMT_FLT && am->am_float && 
	   (am->am_sample_rates & AM_SR_ANY ||
	    audio_rateflag_from_rate(rate) & am->am_sample_rates)) {

	  frames /= channels;
	  audio_deliver(ad, am, ad->ad_outbuf, channels, frames,
			rate, pts, mb->mb_epoch, mp, 1);

	} else {

	  switch(ctx->sample_fmt) {
	  case SAMPLE_FMT_NONE:
	  case SAMPLE_FMT_NB:
	    return;

	  case SAMPLE_FMT_U8:
	    for(i = frames - 1; i >= 0; i--)
	      ad->ad_outbuf[i] = (((uint8_t *)ad->ad_outbuf)[i] - 0x80) << 8;
	    break;
	  case SAMPLE_FMT_S16:
	    break;
	  case SAMPLE_FMT_S32:
	    for(i = 0; i < frames; i++)
	      ad->ad_outbuf[i] = ((int32_t *)ad->ad_outbuf)[i] >> 16;
	    break;
	  case SAMPLE_FMT_FLT:
	    for(i = 0; i < frames; i++)
	      ad->ad_outbuf[i] = rintf(((float *)ad->ad_outbuf)[i] * 32768);
	    break;
	  case SAMPLE_FMT_DBL:
	    for(i = 0; i < frames; i++)
	      ad->ad_outbuf[i] = rint(((double *)ad->ad_outbuf)[i] * 32768);
	    break;
	  }
	  frames /= channels;

	  audio_mix1(ad, am, channels, rate, ctx->channel_layout,
		     ad->ad_outbuf, 
		     frames, pts, mb->mb_epoch, mp);
	}
      }
    }
    pts = AV_NOPTS_VALUE;
    buf += r;
    size -= r;
  }
}


static struct strtab chnames[] = {
  { "Left",        CH_FRONT_LEFT },
  { "Right",       CH_FRONT_RIGHT },
  { "Center",      CH_FRONT_CENTER },
  { "LFE",         CH_LOW_FREQUENCY },
  { "Back Left",   CH_BACK_LEFT },
  { "Back Right",  CH_BACK_RIGHT },
  { "Side Left",   CH_SIDE_LEFT },
  { "Side Right",  CH_SIDE_RIGHT }
};


/**
 *
 */
void
astats(audio_decoder_t *ad, media_pipe_t *mp, int64_t pts, int epoch,
       int16_t *data, int frames, int channels, int64_t chlayout, int rate);

void
astats(audio_decoder_t *ad, media_pipe_t *mp, int64_t pts, int epoch,
       int16_t *data, int frames, int channels, int64_t chlayout, int rate)
{
  int v[8];
  float v0;
  int i, j, x, b;
  int64_t delay;
  int steps;
  const char *n;

  if(channels > 8)
    return;

  if(chlayout == 0 && channels == 1)
    chlayout = CH_LAYOUT_MONO;

  if(chlayout == 0 && channels == 2)
    chlayout = CH_LAYOUT_STEREO;

  if(mp->mp_cur_channels != channels || mp->mp_cur_chlayout != chlayout) {
    mp->mp_cur_chlayout = chlayout;
    mp->mp_cur_channels = channels;

    for(i = 0; i < 8; i++)
      if(mp->mp_prop_audio_channel[i] != NULL) {
	prop_destroy(mp->mp_prop_audio_channel[i]);
	mp->mp_prop_audio_channel[i] = NULL;
      }

    b = 0;
    for(i = 0; i < channels; i++) {
      mp->mp_prop_audio_channel[i] = 
	prop_create(mp->mp_prop_audio_channels_root, NULL);

      mp->mp_prop_audio_channel_level[i] = 
	prop_create(mp->mp_prop_audio_channel[i], "level");
      
      prop_set_float(mp->mp_prop_audio_channel_level[i], -100);

      n = NULL;
      while(b < 64) {
	if(chlayout & (1ULL << b)) {
	  n = val2str(1 << b, chnames);
	  b++;
	  break;
	}
	b++;
      }
      if(n == NULL)
	n = "Other";
      prop_set_string(prop_create(mp->mp_prop_audio_channel[i], "name"), n);
    }
  }

  hts_mutex_lock(&mp->mp_clock_mutex);

  if(epoch == mp->mp_audio_clock_epoch &&
     mp->mp_audio_clock != AV_NOPTS_VALUE &&
     mp->mp_audio_clock_realtime != 0 && 
     pts != AV_NOPTS_VALUE) {
    
    delay = pts - mp->mp_audio_clock + showtime_get_ts()
      - mp->mp_audio_clock_realtime;
    
    if(delay >= 0 && delay < 3000000) {
      if(ad->ad_odelay == 0)
	ad->ad_odelay = delay;
      else
	ad->ad_odelay = (ad->ad_odelay * 15 + (int)delay) / 16;
    }
  }
  hts_mutex_unlock(&mp->mp_clock_mutex);

  steps = ad->ad_odelay / (frames * 1000000 / rate);
	 
  
  for(j = 0; j < channels; j++)
    v[j] = 0;

  for(i = 0; i < frames; i++) {
    for(j = 0; j < channels; j++) {
      x = abs(*data++);
      if(v[j] < x)
	v[j] = x;
    }
  }

  i = ad->ad_peak_ptr;
  for(j = 0; j < channels; j++) {
    v0 = 20 * log10f(v[j] / 32768.0);
    if(v0 < -100)
      v0 = -100;
    ad->ad_peak_delay[i][j] = v0;
  }

  int k = (ad->ad_peak_ptr - steps) & PEAK_DELAY_MASK;

  for(j = 0; j < channels; j++)
    prop_set_float(mp->mp_prop_audio_channel_level[j], 
		   ad->ad_peak_delay[k][j]);
  ad->ad_peak_ptr = (ad->ad_peak_ptr + 1) & PEAK_DELAY_MASK;
}


/**
 * Audio mixing stage 1
 *
 * All stages that reduces the number of channels is performed here.
 * This reduces the CPU load required during the (optional) resampling.
 */
static void
audio_mix1(audio_decoder_t *ad, audio_mode_t *am, 
	   int channels, int rate, int64_t chlayout,
	   int16_t *data0, int frames, int64_t pts, int epoch,
	   media_pipe_t *mp)
{
  int x, y, z, i;
  int16_t *data, *src, *dst;
  int rf = audio_rateflag_from_rate(rate);

  astats(ad, mp, pts, epoch, data0, frames, channels, chlayout, rate);


  /**
   * 5.1 to stereo downmixing, coeffs are stolen from AAC spec
   */
  if(channels == 6 && audio_mode_stereo_only(am)) {

    src = data0;
    dst = data0;

    for(i = 0; i < frames; i++) {

      x = (src[0] * 26869) >> 16;
      y = (src[1] * 26869) >> 16;

      z = (src[2] * 19196) >> 16;
      x += z;
      y += z;

      z = (src[3] * 13571) >> 16;
      x += z;
      y += z;

      z = (src[4] * 13571) >> 16;
      x -= z;
      y += z;

      z = (src[5] * 19196) >> 16;
      x -= z;
      y += z;

      src += 6;

      *dst++ = CLIP16(x);
      *dst++ = CLIP16(y);
    }
    channels = 2;
  }

  /**
   * Phantom LFE, mix it into front speakers
   */
  if(am->am_phantom_lfe && channels > 5) {
    data = data0;
    for(i = 0; i < frames; i++) {
      x = data[0];
      y = data[1];

      z = (data[3] * 46334) >> 16;
      x += z;
      y += z;

      data[0] = CLIP16(x);
      data[1] = CLIP16(y);
      data[3] = 0;
      data += channels;
    }
  }


  /**
   * Phantom center, mix it into front speakers
   */
  if(am->am_phantom_center && channels > 4) {
    data = data0;
    for(i = 0; i < frames; i++) {
      x = data[0];
      y = data[1];

      z = (data[2] * 46334) >> 16;
      x += z;
      y += z;

      data[0] = CLIP16(x);
      data[1] = CLIP16(y);
      data[2] = 0;
      data += channels;
    }
  }

  /**
   * Resampling
   */
  if(rf & am->am_sample_rates || am->am_sample_rates & AM_SR_ANY) {
    close_resampler(ad);
    audio_mix2(ad, am, channels, rate, data0, frames, pts, epoch, mp);
  } else {

    int dstrate = 48000;
    int consumed;
    int written;
    int resbufsize = 4096;

    if(ad->ad_resampler_srcrate  != rate    || 
       ad->ad_resampler_dstrate  != dstrate ||
       ad->ad_resampler_channels != channels) {

      /* Must reconfigure, close */
      close_resampler(ad);

      ad->ad_resampler_srcrate  = rate;
      ad->ad_resampler_dstrate  = dstrate;
      ad->ad_resampler_channels = channels;
    }

    if(ad->ad_resampler == NULL) {
      ad->ad_resbuf = malloc(resbufsize * sizeof(int16_t) * 6);
      ad->ad_resampler = av_resample_init(dstrate, rate, 16, 10, 0, 1.0);
    }

    src = data0;
    rate= dstrate;

    /* If we have something in spill buffer, adjust PTS */
    /* XXX: need this ?, it's very small */
    if(pts != AV_NOPTS_VALUE)
      pts -= 1000000LL * ad->ad_resampler_spill_size / rate;

    while(frames > 0) {
      consumed = 
	resample(ad, ad->ad_resbuf, resbufsize,
		 &written, src, frames, channels);
      src += consumed * channels;
      frames -= consumed;

      audio_mix2(ad, am, channels, rate, ad->ad_resbuf, written, 
		 pts, epoch, mp);
      pts = AV_NOPTS_VALUE;
      if(consumed == 0 && written == 0)
	break;
    }
  }
}


 /**
  * Audio mixing stage 2
  *
  * All stages that increases the number of channels is performed here now
  * after resampling is done
  */
static void
audio_mix2(audio_decoder_t *ad, audio_mode_t *am, 
	   int channels, int rate, int16_t *data0, int frames, int64_t pts,
	   int epoch, media_pipe_t *mp)
{
  int x, y, i, c;
  int16_t *data, *src, *dst;

  /**
   * Mono expansion (ethier to center speaker or to L + R)
   * We also mix to LFE if possible
   */
  if(channels == 1) {
    src = data0 + frames;

    if(am->am_formats & AM_FORMAT_PCM_5DOT1 && !am->am_phantom_center &&
       !am->am_force_downmix) {
      
      /* Mix mono to center and LFE */

      dst = data0 + frames * 6;
      for(i = 0; i < frames; i++) {
	src--;
	dst -= 6;

	x = *src;

	dst[0] = 0;
	dst[1] = 0;
	dst[2] = x;
	dst[3] = x;
	dst[4] = 0;
	dst[5] = 0;
      }
      channels = 6;
    } else if(am->am_formats & AM_FORMAT_PCM_5DOT1 && !am->am_force_downmix) {

      /* Mix mono to L + R and LFE */

      dst = data0 + frames * 6;
      for(i = 0; i < frames; i++) {
	src--;
	dst -= 6;

	x = *src;

	dst[3] = x;
	x = (x * 46334) >> 16;
	dst[0] = x;
	dst[1] = x;
	dst[2] = 0;
	dst[4] = 0;
	dst[5] = 0;
      }
      channels = 6;
    } else {
      /* Mix mono to L + R  */

      dst = data0 + frames * 2;
      for(i = 0; i < frames; i++) {
	src--;
	dst -= 2;

	x = *src;

	x = (x * 46334) >> 16;

	dst[0] = x;
	dst[1] = x;
      }
      channels = 2;
    }
  } else /* 'Small front' already dealt with */ { 

    /**
     * Small front speakers (need to mix front audio to LFE)
     */
    if(am->am_formats & AM_FORMAT_PCM_5DOT1 && am->am_small_front) {
      if(channels >= 6) {
	data = data0;
	for(i = 0; i < frames; i++) {
	  x = data[3] + (data[0] + data[1]) / 2;
	  data[3] = CLIP16(x);
	  data += channels;
	}
      } else {
	src = data0 + frames * channels;
	dst = data0 + frames * 6;

	for(i = 0; i < frames; i++) {
	  src -= channels;
	  dst -= 6;

	  x = (src[0] + src[1]) / 2;
	
	  for(c = 0; c < channels; c++)
	    dst[c] = src[c];

	  dst[2] = 0;
	  dst[3] = x;
	  dst[4] = 0;
	  dst[5] = 0;
	}
	channels = 6;
      }
    }
  }

  /**
   * Swap Center + LFE with Surround
   */
  if(am->am_swap_surround && channels > 5) {
    data = data0;
    for(i = 0; i < frames; i++) {
      x = data[4];
      y = data[5];
      data[4] = data[2];
      data[5] = data[3];
      data[2] = x;
      data[3] = y;
      
      data += channels;
    }
  }

  audio_deliver(ad, am, data0, channels, frames, rate, pts, epoch, mp, 0);
}


/**
 * Enqueue audio into fifo.
 * We slice the audio into fixed size blocks, if 'am_preferred_size' is
 * set by the audio output module, we use that size, otherwise 1024 frames.
 */
static void
audio_deliver(audio_decoder_t *ad, audio_mode_t *am, const void *src, 
	      int channels, int frames, int rate, int64_t pts, int epoch,
	      media_pipe_t *mp, int isfloat)
{
  audio_buf_t *ab = ad->ad_buf;
  audio_fifo_t *af = thefifo;
  int outsize = am->am_preferred_size ?: 1024;
  int c, r;
  int sample_size = (1 + isfloat) * 2;
  int format;

  switch(channels) {
  case 2: format = AM_FORMAT_PCM_STEREO; break;
  case 6: format = AM_FORMAT_PCM_5DOT1;  break;
  case 7: format = AM_FORMAT_PCM_6DOT1;  break;
  case 8: format = AM_FORMAT_PCM_7DOT1;  break;
  default:
    return;
  }

  while(frames > 0) {

    if(ab != NULL && (ab->ab_channels != channels ||
		      ab->ab_isfloat != isfloat)) {
      /* Channels have changed, flush buffer */
      ab_free(ab);
      ab = NULL;
    }

    if(ab == NULL) {
      ab = af_alloc(sample_size * channels * outsize, mp);
      ab->ab_channels = channels;
      ab->ab_alloced = outsize;
      ab->ab_format = format;
      ab->ab_samplerate = rate;
      ab->ab_isfloat = isfloat;
      ab->ab_frames = 0;
      ab->ab_pts = AV_NOPTS_VALUE;
    }

    if(ab->ab_pts == AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE) {
      pts -= 1000000LL * ab->ab_frames / rate;
      ab->ab_pts = pts; 
      ab->ab_epoch = epoch;
    }
    pts = AV_NOPTS_VALUE;


    r = ab->ab_alloced - ab->ab_frames;
    c = r < frames ? r : frames;

    memcpy(ab->ab_data + sample_size * channels * ab->ab_frames,
	   src,          sample_size * channels * c);

    src           += c * channels * sample_size;
    ab->ab_frames += c;
    frames        -= c;

    if(ab->ab_frames == ab->ab_alloced) {
      ab->ab_ref = ad; /* A reference to our decoder. This is used
			  to revert out packets in the play queue during
			  a pause event */
      if(ad->ad_send_flush) {
	ab->ab_flush = 1;
	ad->ad_send_flush = 0;
      }

      af_enq(af, ab);
      ab = NULL;
    }
  }
  ad->ad_buf = ab;
}







/**
 *
 */
static void
close_resampler(audio_decoder_t *ad)
{
  int c;

  if(ad->ad_resampler == NULL) 
    return;

  free(ad->ad_resbuf);
  ad->ad_resbuf = NULL;

  av_resample_close(ad->ad_resampler);
  
  for(c = 0; c < 8; c++) {
    free(ad->ad_resampler_spill[c]);
    ad->ad_resampler_spill[c] = NULL;
  }

  ad->ad_resampler_spill_size = 0;
  ad->ad_resampler_channels = 0;
  ad->ad_resampler = NULL;
}


/**
 *
 */
static int
resample(audio_decoder_t *ad, int16_t *dstmix, int dstavail,
	 int *writtenp, int16_t *srcmix, int srcframes, int channels)
{
  int c, i, j;
  int16_t *src;
  int16_t *dst;
  int written = 0;
  int consumed;
  int srcsize;
  int spill = ad->ad_resampler_spill_size;
  
   if(spill > srcframes)
     srcframes = 0;

   dst = malloc(dstavail * sizeof(uint16_t));

   for(c = 0; c < channels; c++) {

     if(ad->ad_resampler_spill[c] != NULL) {

       srcsize = spill + srcframes;

       src = malloc(srcsize * sizeof(uint16_t));

       j = 0;

       for(i = 0; i < spill; i++)
	 src[j++] = ad->ad_resampler_spill[c][i];

       for(i = 0; i < srcframes; i++)
	 src[j++] = srcmix[i * channels + c];

       free(ad->ad_resampler_spill[c]);
       ad->ad_resampler_spill[c] = NULL;

     } else {

       srcsize = srcframes;

       src = malloc(srcsize * sizeof(uint16_t));

       for(i = 0; i < srcframes; i++)
	 src[i] = srcmix[i * channels + c];

     }

     written = av_resample(ad->ad_resampler, dst, src, &consumed, 
			   srcsize, dstavail, c == channels - 1);

     if(consumed != srcsize) {
       ad->ad_resampler_spill_size = srcsize - consumed;

       ad->ad_resampler_spill[c] = 
	 malloc(ad->ad_resampler_spill_size * sizeof(uint16_t));

       memcpy(ad->ad_resampler_spill[c], src + consumed, 
	      ad->ad_resampler_spill_size * sizeof(uint16_t));
     }

     for(i = 0; i < written; i++)
       dstmix[i * channels + c] = dst[i];

     free(src);
   }

   *writtenp = written;

   free(dst);

   return srcframes;
 }



