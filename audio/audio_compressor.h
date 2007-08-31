/*
 *  Audio compressor
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

#ifndef AUDIO_COMPRESSOR_H
#define AUDIO_COMPRESSOR_H

#include "audio_mixer.h"

struct compressor_data {
  enum {
    AUDIO_COMPRESSOR_OFF,
    AUDIO_COMPRESSOR_SOFT,
    AUDIO_COMPRESSOR_HARD,
    AUDIO_COMPRESSOR_USER_SETTINGS,
  } mode;

  int holdtime;    /* in ms */
  float thresdb;
  float postgaindb;
  float ratiocfg;


  float lp;

  float ratio;
  float thres;
  float postgain;
  int holdsamples;  /* holdtime, but in compressor samples */
  float hpeak;
  float gain;
  int hold;
};

extern struct compressor_data post_mixer_compressor;

void audio_compressor(float *data, struct compressor_data *comp,
		      audio_mixer_t *mi);

void audio_compressor_update_config(struct compressor_data *comp,
				    audio_mixer_t *mi);

void audio_compressor_menu_setup(glw_t *a);

void audio_compressor_setup(void);

#endif /* AUDIO_COMPRESSOR_H */
