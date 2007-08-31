/*
 *  Audio mixer
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

#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include "audio_fifo.h"

#define MIXER_CHANNEL_NONE     -1
#define MIXER_CHANNEL_LEFT     0
#define MIXER_CHANNEL_RIGHT    1
#define MIXER_CHANNEL_SR_LEFT  2
#define MIXER_CHANNEL_SR_RIGHT 3
#define MIXER_CHANNEL_CENTER   4
#define MIXER_CHANNEL_LFE      5

typedef struct channel_offset {
  int channel;
  int offset;
} channel_offset_t;


#define AUDIO_MIXER_MAX_CHANNELS 8

#define AUDIO_MIXER_COEFF_MATRIX_SIZE \
 (AUDIO_MIXER_MAX_CHANNELS * AUDIO_MIXER_MAX_CHANNELS)

typedef struct audio_source {
  LIST_ENTRY(audio_source) as_link;
  LIST_ENTRY(audio_source) as_tmplink;
  audio_fifo_t as_fifo;
  audio_buf_t *as_src_buf;
  float *as_src;

  float as_avg_delay;

  int as_fullness;

  audio_buf_t *as_dst_buf;
  float *as_saved_dst;

  struct AVResampleContext *as_resampler;
  int16_t *as_spillbuf[AUDIO_MIXER_MAX_CHANNELS];
  int as_spill;

  int as_rate;
  int as_channels;

  float as_coeffs[AUDIO_MIXER_MAX_CHANNELS][AUDIO_MIXER_MAX_CHANNELS];

  float as_gain;
  float as_target_gain;

  media_pipe_t *as_mp;

} audio_source_t;

typedef struct audio_mixer {
  int period_size;
  int channels;
  int rate;
  int words;   /* period_size * output_channels */

} audio_mixer_t;

extern audio_mixer_t mixer_output;


void audio_mixer_setup_output(int channels, int period_size, int rate);

audio_source_t *audio_source_create(media_pipe_t *mp);

void audio_source_destroy(audio_source_t *as);

void audio_mixer_source_int16(audio_source_t *as, int16_t *data, int frames,
			      int64_t pts);

void audio_mixer_source_config(audio_source_t *as, int rate, int srcchannels,
			       channel_offset_t *chlayout);

void audio_source_prio(audio_source_t *as);

#endif /* AUDIO_MIXER_H */
