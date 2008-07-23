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
#include "audio_fifo.h"
#include "app.h"

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
TAILQ_HEAD(mixer_controller_queue, mixer_controller);

#define AM_MIXER_MASTER         0
#define AM_MIXER_FRONT          1
#define AM_MIXER_REAR           2
#define AM_MIXER_CENTER         3
#define AM_MIXER_LFE            4
#define AM_MIXER_CENTER_AND_LFE 5
#define AM_MIXER_SIDE           6
#define AM_MIXER_NUM            7


#define audio_mode_stereo_only(am) \
  ((((am)->am_formats & AM_FORMAT_PCM_MASK) == AM_FORMAT_PCM_STEREO) ||\
   (am)->am_force_downmix)

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
  char *am_id;
  char *am_icon;

  int (*am_entry)(struct audio_mode *am, audio_fifo_t *af);

  uint8_t am_swizzle[AUDIO_CHAN_MAX]; /* channel swizzling */


  uint32_t am_phantom_center;
  uint32_t am_phantom_lfe;
  uint32_t am_small_front;
  uint32_t am_force_downmix;

  int am_preferred_size;

  struct mixer_controller_queue am_mixer_controllers;

  struct mixer_controller *am_mixers[AM_MIXER_NUM];

  TAILQ_ENTRY(audio_mode) am_link;

} audio_mode_t;

extern audio_mode_t *audio_mode_current;

void audio_mode_register(audio_mode_t *am);

void audio_init(void);

void audio_alsa_init(void);

void audio_settings_init(appi_t *ai, glw_t *m);


/**
 * Mixer controllers
 */
typedef struct mixer_controller {
  TAILQ_ENTRY(mixer_controller) mc_link;

  const char *mc_title;
  
  enum {
    MC_TYPE_SLIDER,
    MC_TYPE_SWITCH
  } mc_type;


  float mc_min;
  float mc_max;

  float (*mc_get_volume)(struct mixer_controller *mc);
  float (*mc_set_volume)(struct mixer_controller *mc, float db);

  int (*mc_get_mute)(struct mixer_controller *mc);
  int (*mc_set_mute)(struct mixer_controller *mc, int on);

} mixer_controller_t;

void audio_mixer_init(void);



/**
 * Global struct for volume control
 */
typedef struct volume_control {

  int vc_master_mute;
  float vc_master_vol;

  float vc_front_vol;
  float vc_center_vol;
  float vc_lfe_vol;
  float vc_rear_vol;
  float vc_side_vol;

  int vc_soft_gain[8];

  int vc_soft_gain_needed;

} volume_control_t;


extern volume_control_t global_volume;

#endif /* AUDIO__H */
