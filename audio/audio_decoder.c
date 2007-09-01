/*
 *  Audio decoder
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
ad_decode_buf(audio_decoder_t *ad, media_pipe_t *mp, media_buf_t *mb)
{
  uint8_t *buf;
  size_t size;
  int r, data_size, frames, reconfigure;
  int16_t *d;
  channel_offset_t *chlayout;
  codecwrap_t *cw = mb->mb_cw;
  AVCodecContext *ctx;

  ctx = cw->codec_ctx;

  buf = mb->mb_data;
  size = mb->mb_size;

  while(size > 0) {

    wrap_lock_codec(cw);
    r = avcodec_decode_audio(ctx, ad->ad_outbuf, &data_size, buf, size);

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


    frames = data_size / sizeof(uint16_t) / ad->ad_channels;

    if(r == -1 || data_size == 0)
      break;

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
 
    if(chlayout != NULL) {

      if(reconfigure)
	audio_mixer_source_config(ad->ad_output, ad->ad_rate,
				  ad->ad_channels, chlayout);

      d = ad->ad_outbuf;

      mp->mp_time_feedback = mb->mb_time; /* low resolution informational
					     time */

      audio_mixer_source_int16(ad->ad_output, ad->ad_outbuf, frames,
			       mb->mb_pts);

      if(mb->mb_pts != AV_NOPTS_VALUE) {
	mp->mp_clock = mb->mb_pts - ad->ad_output->as_avg_delay;
	mp->mp_clock_valid = 1;
      }
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

  while(ad->ad_run) {
    mb = mb_dequeue_wait(mp, &mp->mp_audio);
    switch(mb->mb_data_type) {
    default:
      break;

    case MB_FLUSH:
      audio_mixer_source_flush(ad->ad_output);
      break;

    case MB_AUDIO:
      ad_decode_buf(ad, mp, mb);
      break;
    }
    media_buf_free(mb);
  }
  printf("thread returning\n");
  return NULL;
}



static void
ad_create(media_pipe_t *mp)
{
  audio_decoder_t *ad;

  ad = mp->mp_audio_decoder = calloc(1, sizeof(audio_decoder_t));
  ad->ad_mp = mp;
  ad->ad_run = 1;
  ad->ad_outbuf = av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
  ad->ad_output = audio_source_create(mp);

  pthread_create(&ad->ad_ptid, NULL, ad_thread, ad);
}



static void
ad_destroy(media_pipe_t *mp, audio_decoder_t *ad)
{
  ad->ad_run = 0;
 
  mp_send_cmd(mp, &mp->mp_audio, MB_NOP);
  printf("Waiting for audio thread\n");
  pthread_join(ad->ad_ptid, NULL);
  printf("\tAudio thread joined\n");

  audio_source_destroy(ad->ad_output);

  av_freep(&ad->ad_outbuf);
  free(ad);

  mp->mp_audio_decoder = NULL;
}


void
audio_decoder_change_play_status(media_pipe_t *mp)
{
  printf("audio_decode_change_play_status %d\n", mp->mp_playstatus);
  switch(mp->mp_playstatus) {
  case MP_PLAY:
  case MP_PAUSE:
    if(mp->mp_audio_decoder == NULL)
      ad_create(mp);
    break;

  case MP_STOP:
    if(mp->mp_audio_decoder != NULL)
      ad_destroy(mp, mp->mp_audio_decoder);
    break;
  }
}
