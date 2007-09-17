/*
 *  Audio decoder thread
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

#include "showtime.h"
#include "audio_decoder.h"
#include "audio_mixer.h"

extern int mixer_hw_output_formats;

static channel_offset_t layout_mono[] = {
  {MIXER_CHANNEL_CENTER, 0},
  {MIXER_CHANNEL_NONE,   0}
};


static channel_offset_t layout_stereo[] = {
  {MIXER_CHANNEL_LEFT,  0},
  {MIXER_CHANNEL_RIGHT, 1},
  {MIXER_CHANNEL_NONE,  0}
};


static channel_offset_t layout_5dot1_a[] = {
  {MIXER_CHANNEL_LEFT,     0},
  {MIXER_CHANNEL_CENTER,   1},
  {MIXER_CHANNEL_RIGHT,    2},
  {MIXER_CHANNEL_SR_LEFT,  3},
  {MIXER_CHANNEL_SR_RIGHT, 4},
  {MIXER_CHANNEL_LFE,      5},
  {MIXER_CHANNEL_NONE,  0}
};


static channel_offset_t layout_5dot1_b[] = {
  {MIXER_CHANNEL_CENTER,   0},
  {MIXER_CHANNEL_LEFT,     1},
  {MIXER_CHANNEL_RIGHT,    2},
  {MIXER_CHANNEL_SR_LEFT,  3},
  {MIXER_CHANNEL_SR_RIGHT, 4},
  {MIXER_CHANNEL_LFE,      5},
  {MIXER_CHANNEL_NONE,     0}
};


static void
ad_update_clock(audio_decoder_t *ad, media_pipe_t *mp, media_buf_t *mb)
{
  if(ad->ad_output->as_avg_delay > 0) {
    if(mb->mb_pts != AV_NOPTS_VALUE)
      mp->mp_clock = mb->mb_pts - ad->ad_output->as_avg_delay;
    mp->mp_clock_valid = 1;
  } else {
    mp->mp_clock_valid = 0;
  }

}



static void
ab_enq_passthru(audio_decoder_t *ad, media_pipe_t *mp, media_buf_t *mb,
		int format)
{
  audio_buf_t *ab;

  ab = af_alloc_dynamic(mb->mb_size);
  memcpy(ab_dataptr(ab), mb->mb_data, mb->mb_size);
  ab->payload_type = format;
  af_enq(&ad->ad_output->as_fifo, ab);
  ad_update_clock(ad, mp, mb);
}


static void
ad_decode_buf(audio_decoder_t *ad, media_pipe_t *mp, media_buf_t *mb)
{
  uint8_t *buf;
  int size;
  int r, data_size, frames, reconfigure;
  channel_offset_t *chlayout;
  codecwrap_t *cw = mb->mb_cw;
  AVCodecContext *ctx;

  ctx = cw->codec_ctx;

  if(primary_audio == mp) switch(ctx->codec_id) {
  case CODEC_ID_AC3:
    if(mixer_hw_output_formats & AUDIO_OUTPUT_AC3) {
      ab_enq_passthru(ad, mp, mb, AUDIO_OUTPUT_AC3);
      return;
    }
    break;

  case CODEC_ID_DTS:
    if(mixer_hw_output_formats & AUDIO_OUTPUT_DTS) {
      ab_enq_passthru(ad, mp, mb, AUDIO_OUTPUT_DTS);
      return;
    }
    break;

  default:
    break;
  }
 
  buf = mb->mb_data;
  size = mb->mb_size;


  while(size > 0) {

    wrap_lock_codec(cw);
    r = avcodec_decode_audio(ctx, ad->ad_outbuf, &data_size, buf, size);
    if(r == -1) {
      wrap_unlock_codec(cw);
      break;
    }

    if(ad->ad_channels != ctx->channels || ad->ad_rate != ctx->sample_rate || 
       ad->ad_codec != ctx->codec_id) {
      reconfigure = 1;
      ad->ad_channels = ctx->channels;
      ad->ad_rate     = ctx->sample_rate;
      ad->ad_codec    = ctx->codec_id;
    } else {
      reconfigure = 0;
    }

    wrap_unlock_codec(cw);
  
    switch(ad->ad_channels) {
    case 1:
      chlayout = layout_mono;
      break;
    case 2:
      chlayout = layout_stereo;
      break;
    case 6:
      switch(ctx->codec_id) {
      case CODEC_ID_DTS:
      case CODEC_ID_AAC:
	chlayout = layout_5dot1_b;
	break;
      default:
	chlayout = layout_5dot1_a;
	break;
      }
      break;
    default:
      chlayout = NULL;
    }

    if(reconfigure)
      audio_mixer_source_config(ad->ad_output, ad->ad_rate,
				ad->ad_channels, chlayout);

    frames = data_size / sizeof(uint16_t) / ad->ad_channels;

    if(frames > 0 && chlayout != NULL) {

      mp->mp_time_feedback = mb->mb_time; /* low resolution informational
					     time */
      audio_mixer_source_int16(ad->ad_output, ad->ad_outbuf, frames,
			       mb->mb_pts);
      
      ad_update_clock(ad, mp, mb);
    }
    buf += r;
    size -= r;
  }
}



static void *
ad_thread(void *aux)
{
  audio_decoder_t *ad = aux;
  media_pipe_t *mp = ad->ad_mp;
  media_buf_t *mb;

  while((mb = mb_dequeue_wait(mp, &mp->mp_audio)) != NULL) {

    switch(mb->mb_data_type) {
    default:
      break;

    case MB_RESET:
      audio_mixer_source_flush(ad->ad_output);
      break;

    case MB_AUDIO:
      ad_decode_buf(ad, mp, mb);
      break;
    }
    media_buf_free(mb);
  }
  return NULL;
}



void
audio_decoder_create(media_pipe_t *mp)
{
  audio_decoder_t *ad;

  ad = mp->mp_audio_decoder = calloc(1, sizeof(audio_decoder_t));
  ad->ad_mp = mp;
  ad->ad_outbuf = av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
  ad->ad_output = audio_source_create(mp);
  pthread_create(&ad->ad_ptid, NULL, ad_thread, ad);
}



void
audio_decoder_join(media_pipe_t *mp, audio_decoder_t *ad)
{
  pthread_join(ad->ad_ptid, NULL);
  audio_source_destroy(ad->ad_output);
  av_freep(&ad->ad_outbuf);
  free(ad);
  mp->mp_audio_decoder = NULL;
}
