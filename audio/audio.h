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

typedef struct audio_mode {
  
  uint32_t am_formats;
#define AM_FORMAT_PCM 0x1
#define AM_FORMAT_AC3 0x2
#define AM_FORMAT_DTS 0x4

  int am_channels;

  


} audio_mode_t;




void audio_init(void);

void audio_alsa_init(void);

void audio_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic);

#endif /* AUDIO__H */
