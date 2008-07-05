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
#include "audio_decoder.h"

#include "layout/layout_forms.h"
#include "layout/layout_support.h"

audio_mode_t *audio_mode_current;
pthread_mutex_t audio_mode_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct audio_mode_queue audio_modes;

static void *audio_output_thread(void *aux);

static glw_t *audio_selection_model;
static glw_focus_stack_t *audio_selection_gfs;

/**
 *
 */
void
audio_init(void)
{
  pthread_t ptid;

  TAILQ_INIT(&audio_modes);
  audio_decoder_init();
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
static int
audio_output_switch(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  audio_mode_t *am = opaque;

  switch(signal) {
  case GLW_SIGNAL_SELECTED_SELF:
    audio_mode_current = am;
    break;
  default:
    break;
  }
  return 0;
}

/**
 *
 */
static void
audio_mode_add_switch_on_off(glw_t *parent, const char *name, int *store)
{
  struct layout_form_entry_list lfelist;
  glw_t *s;

  TAILQ_INIT(&lfelist);

  s = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, parent, 
		 GLW_ATTRIB_FILENAME, "settings/audio-settings-switch",
		 NULL);

  layout_update_str(s, "title", name);

  layout_form_add_option(s, "switches", "Off", 0);
  layout_form_add_option(s, "switches", "On",  1);
#if 0
  LFE_ADD_OPTION(&lfelist, "switches", store);

  layout_form_initialize(&lfelist, parent, audio_selection_gfs, NULL, 0);
#endif
}


/**
 *
 */
void
audio_mode_register(audio_mode_t *am)
{
  struct layout_form_entry_list lfelist;
  glw_t *t, *ico, *p;
  char buf[50];
  int multich = am->am_formats & (AM_FORMAT_PCM_5DOT1 | AM_FORMAT_PCM_7DOT1);

  TAILQ_INSERT_TAIL(&audio_modes, am, am_link);


  /* Add the control deck */

  TAILQ_INIT(&lfelist);

  ico = glw_create(GLW_MODEL,
		   GLW_ATTRIB_FILENAME, "settings/audio-output-icon",
		   NULL);

  layout_update_filename(ico, "icon", am->am_icon);
  layout_update_str(ico, "title", am->am_title);

  glw_set(ico,
	  GLW_ATTRIB_SIGNAL_HANDLER, audio_output_switch, am, 200,
	  NULL);

  t = layout_form_add_tab2(audio_selection_model,
			   "audio_output_list", ico,
			   "audio_output_container",
			   "settings/audio-output-tab");

  snprintf(buf, sizeof(buf), "%s%s%s%s%s",
	   am->am_formats & AM_FORMAT_PCM_STEREO ? "Stereo  " : "",
	   am->am_formats & AM_FORMAT_PCM_5DOT1  ? "5.1  "    : "",
	   am->am_formats & AM_FORMAT_PCM_7DOT1  ? "7.1 "     : "",
	   am->am_formats & AM_FORMAT_AC3        ? "AC3 "     : "",
	   am->am_formats & AM_FORMAT_DTS        ? "DTS "     : "");
  layout_update_str(t, "output_formats", buf);


  layout_update_str(t, "title", am->am_long_title);

  p = glw_find_by_id(t, "settings_list", 0);
  
  if(multich && p) {
    audio_mode_add_switch_on_off(p, "Phantom Center:", &am->am_phantom_center);
    audio_mode_add_switch_on_off(p, "Phantom LFE:", &am->am_phantom_lfe);
    audio_mode_add_switch_on_off(p, "Small Front:", &am->am_small_front);
  }

  LFE_ADD(&lfelist, "settings_list");

  layout_form_initialize(&lfelist, t, audio_selection_gfs, NULL, 0);


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
  audio_selection_gfs = gfs;

  t = layout_form_add_tab(m,
			  "settings_list",     "settings/audio-icon",
			  "settings_container","settings/audio-tab");
  
  if(t == NULL)
    return;

  audio_selection_model = t;

  LFE_ADD(&lfelist, "audio_output_list");

  layout_form_initialize(&lfelist, m, gfs, ic, 0);
}
