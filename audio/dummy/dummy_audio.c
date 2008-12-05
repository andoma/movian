/*
 *  Dummy audio output
 *  Copyright (C) 2008 Andreas Öman
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

#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "showtime.h"
#include "audio/audio.h"

static int
dummy_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  audio_buf_t *ab;
  while(1) {

    ab = af_deq(af, 1);

    if(am != audio_mode_current) {
      /* We're not the selected audio output anymore, return. */
      ab_free(ab);
      break;
    }

    usleep(1000000 * ab->ab_frames / 48000);
    ab_free(ab);
  }
  return 0;
}

/**
 *
 */
void audio_dummy_init(void);

void
audio_dummy_init(void)
{
  audio_mode_t *am;

  am = calloc(1, sizeof(audio_mode_t));
  /* Absolute minimum requirements */
  am->am_formats = AM_FORMAT_PCM_STEREO;
  am->am_sample_rates = AM_SR_48000;
  am->am_title = strdup("No audio output");
  am->am_id = strdup("dummy");

  am->am_entry = dummy_audio_start;

  TAILQ_INIT(&am->am_mixer_controllers);
  audio_mode_register(am);
}
