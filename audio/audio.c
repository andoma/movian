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

#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "audio.h"
#include "audio_ui.h"
#include "audio_fifo.h"

#include "layout/layout_forms.h"

audio_mode_t *audio_mode_current;
pthread_mutex_t audio_mode_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct audio_mode_queue audio_modes;

static void *audio_output_thread(void *aux);

/**
 *
 */
void
audio_init(void)
{
  pthread_t ptid;

  TAILQ_INIT(&audio_modes);
  audio_alsa_init();

  audio_widget_make();

  pthread_create(&ptid, NULL, audio_output_thread, NULL);
}


/**
 *
 */
audio_fifo_t *thefifo;
audio_fifo_t af0;

static void *
audio_output_thread(void *aux)
{
  audio_mode_t *am;
  int r;
  audio_fifo_t *af = &af0;

  audio_fifo_init(af, 16000, 8000);
  thefifo = af;

  am = TAILQ_FIRST(&audio_modes);
  audio_mode_current = am;

  while(1) {
    am = audio_mode_current;
    printf("Audio output using %s\n", am->am_title);
    r = am->am_entry(am, af);

    if(r == -1) {
      /* Device error, sleep to avoid busy looping.
	 Hopefully the error will resolve itself (if another app
	 is blocking output, etc)
      */
      sleep(1);
    }
  }
  return NULL;
}



/**
 *
 */
void
audio_mode_register(audio_mode_t *am)
{
  TAILQ_INSERT_TAIL(&audio_modes, am, am_link);
}



/**
 *
 */
void
audio_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic)
{
  struct layout_form_entry_list lfelist;
  glw_t *t;

  TAILQ_INIT(&lfelist);

  t = layout_form_add_tab(m,
			  "settings_list",     "settings/audio-icon",
			  "settings_container","settings/audio-tab");
  
  if(t == NULL)
    return;

  LFE_ADD(&lfelist, "audiocontrollers");

  layout_form_initialize(&lfelist, m, gfs, ic, 0);
}
