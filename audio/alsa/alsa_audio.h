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

#ifndef ALSA_AUDIO_H
#define ALSA_AUDIO_H

#include <alsa/asoundlib.h>
#include "media.h"
#include "audio/audio_sched.h"


#define ACTX_MAX_CHANNELS 6

typedef enum {
  ACTX_NONE,
  ACTX_ANALOG_MONO,
  ACTX_ANALOG_STEREO,
  ACTX_ANALOG_5CHAN,
  ACTX_ANALOG_5DOT1,

} actx_mode_t;


typedef struct audio_ctx {
  actx_mode_t mode;

  pthread_mutex_t cfg_mutex;
  pthread_cond_t cfg_cond;
  int reset;

  snd_pcm_t *handle;

  unsigned long rate;
  unsigned int orate;
  int channels;

  snd_pcm_uframes_t  buffer_size;
  snd_pcm_uframes_t  period_size;

  struct AVResampleContext *resampler;

  struct {
    int16_t *data;
  } resampler_spill[ACTX_MAX_CHANNELS];

  int resampler_spill_size;

  int16_t *data;

  int64_t last_ts;
  int64_t ts;

 

#define ACTX_FIFO_LEN (16 * 1024)
#define ACTX_FIFO_MASK (ACTX_FIFO_LEN - 1)
#define ACTX_FIFO_IMASK ~ACTX_FIFO_MASK

  int16_t *fifo;
  int fifo_wp;
  int fifo_rp;
  int fifo_len;
  int fifo_channels;
  pthread_mutex_t fifo_mutex;
  pthread_cond_t fifo_cond;

  float peak[ACTX_MAX_CHANNELS];

} audio_ctx_t;

void alsa_audio_init(asched_t *as);

#endif /* ALSA_AUDIO */
