/*
 *  Audio decoderuling
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

#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include "media.h"
#include "audio.h"

TAILQ_HEAD(audio_decoder_queue, audio_decoder);

typedef void (ad_mix_func_t)(int frames, int channels, int16_t *src, 
			     const uint8_t *swizzleptr,
			     int16_t *output[AUDIO_CHAN_MAX],
			     int stride, audio_mode_t *am);

typedef struct audio_decoder {
  hts_thread_t ad_tid;  /* Thread id */

  media_pipe_t *ad_mp;

  int16_t *ad_outbuf;

  audio_buf_t *ad_buf;

  int ad_do_flush;

  struct AVResampleContext *ad_resampler;
  int16_t *ad_resampler_spill[AUDIO_CHAN_MAX];
  int ad_resampler_spill_size;
  int ad_resampler_channels;
  int ad_resampler_srcrate;
  int ad_resampler_dstrate;

  int16_t *ad_resbuf;

  int64_t ad_silence_last_rt;
  int64_t ad_silence_last_pts;

  /* Upon pause we suck back packets from the output queue and
     move them here */

  struct audio_buf_queue ad_hold_queue;
  
  TAILQ_ENTRY(audio_decoder) ad_link;


} audio_decoder_t;

void audio_decoder_init(void);

audio_decoder_t *audio_decoder_create(media_pipe_t *mp);

void audio_decoder_destroy(audio_decoder_t *ad);

void audio_decoder_acquire_output(audio_decoder_t *ad);

int audio_decoder_is_silenced(audio_decoder_t *ad);

#endif /* AUDIO_DECODER_H */
