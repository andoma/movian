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

#ifndef AUDIO_H
#define AUDIO_H

#include <libglw/glw.h>
#include "hid/input.h"
#include "audio_fifo.h"

#define AUDIO_CHAN_MAX 8

/**
 * The channel order expected by audio output is as follows
 *
 * 0 - Front Left
 * 1 - Front Right
 * 2 - Rear Left
 * 3 - Rear Right
 * 4 - Center
 * 5 - LFE
 * 6 - Side Left
 * 7 - Side Right
 *
 */

TAILQ_HEAD(audio_mode_queue, audio_mode);


#define audio_mode_stereo_only(am) \
  (((am)->am_formats & AM_FORMAT_PCM_MASK) == AM_FORMAT_PCM_STEREO)

typedef struct audio_mode {
  uint32_t am_formats;
#define AM_FORMAT_PCM_STEREO        0x1
#define AM_FORMAT_PCM_5DOT1         0x2
#define AM_FORMAT_PCM_7DOT1         0x4
#define AM_FORMAT_PCM_MASK          0x7

#define AM_FORMAT_AC3               0x8
#define AM_FORMAT_DTS               0x10

  uint32_t am_sample_rates;
#define AM_SR_96000 0x1
#define AM_SR_48000 0x2
#define AM_SR_44100 0x4
#define AM_SR_32000 0x8
#define AM_SR_24000 0x10

  unsigned int am_sample_rate;

  char *am_title;
  char *am_long_title;
  char *am_icon;

  int (*am_entry)(struct audio_mode *am, audio_fifo_t *af);

  uint8_t am_swizzle[AUDIO_CHAN_MAX]; /* channel swizzling */


  int am_phantom_center;
  int am_phantom_lfe;
  int am_small_front;

  int am_preferred_size;

  TAILQ_ENTRY(audio_mode) am_link;

} audio_mode_t;

extern audio_mode_t *audio_mode_current;

void audio_mode_register(audio_mode_t *am);

void audio_init(void);

void audio_alsa_init(void);

void audio_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic);

#endif /* AUDIO__H */
