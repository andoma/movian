/*
 *  Alsa mixer functions
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

#ifndef ALSA_MIXER_H
#define ALSA_MIXER_H

#include "hid/input.h"
#include "audio/audio_sched.h"
#include <alsa/asoundlib.h>

#define MIXER_MAX_ELEMENTS 6


typedef struct fader {

  snd_mixer_elem_t *elm;
  float multiplier;
  const char *displayname;
} fader_t;

typedef struct mixer {
  ic_t input;
  snd_mixer_t *mixer;
  int nfaders;
  fader_t faders[MIXER_MAX_ELEMENTS];
  struct asched *as;
} mixer_t;



void alsa_mixer_getstatus(float *volp, int *mutep);

int alsa_mixer_keystrike(int key);

void alsa_mixer_init(asched_t *as);

#endif /* ALSA_MIXER_H */
