/*
 *  Audio framework
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

#pragma once

#include "audio_fifo.h"

TAILQ_HEAD(audio_mode_queue, audio_mode);

#define audio_mode_stereo_only(am) \
  ((((am)->am_formats & AM_FORMAT_PCM_MASK) == AM_FORMAT_PCM_STEREO) ||\
   (am)->am_force_downmix)

typedef struct audio_mode {
  uint32_t am_formats;
#define AM_FORMAT_PCM_MONO          0x1
#define AM_FORMAT_PCM_STEREO        0x2
#define AM_FORMAT_PCM_5DOT0         0x4
#define AM_FORMAT_PCM_5DOT1         0x8
#define AM_FORMAT_PCM_6DOT1         0x10
#define AM_FORMAT_PCM_7DOT1         0x20
#define AM_FORMAT_PCM_MASK          0x3f

#define AM_FORMAT_AC3               0x40
#define AM_FORMAT_DTS               0x80

  uint32_t am_sample_rates;
#define AM_SR_96000 0x1
#define AM_SR_48000 0x2
#define AM_SR_44100 0x4
#define AM_SR_32000 0x8
#define AM_SR_24000 0x10
#define AM_SR_ANY   0x20
  char am_float; // Can also output PCM in floating point mode
  char am_multich_controls;

  char *am_title;
  char *am_description;
  char *am_id;

  int (*am_entry)(struct audio_mode *am, audio_fifo_t *af);

  uint32_t am_phantom_center;
  uint32_t am_phantom_lfe;
  uint32_t am_small_front;
  uint32_t am_force_downmix;
  uint32_t am_swap_surround;  /* Swap center+LFE with surround channels */
  int am_audio_delay;

  int am_preferred_size;

  TAILQ_ENTRY(audio_mode) am_link;

} audio_mode_t;

int audio_rateflag_from_rate(int rate);
const char *audio_format_to_string(int format);

void audio_mode_register(audio_mode_t *am);

void audio_init(void);
void audio_fini(void);

extern prop_t *prop_mastervol, *prop_mastermute;

#define CLIP16(a) ((a) > 32767 ? 32767 : ((a) < -32768 ? -32768 : a))

int audio_mode_prefer_float(void);

/**
 * Audio drivers
 */
extern void audio_wii_init(void);
extern int audio_pa_init(void);
extern void audio_alsa_init(int);
extern void audio_coreaudio_init(void);
extern void audio_dummy_init(void);
extern void audio_ps3_init(void);
