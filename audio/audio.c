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

#include <libglw/glw.h>
#include "app.h"

audio_mode_t *audio_mode_current;
hts_mutex_t audio_mode_mutex;

static struct audio_mode_queue audio_modes;

static void *audio_output_thread(void *aux);


/**
 *
 */
static void
audio_global_save_settings(void)
{
  htsmsg_t *m = htsmsg_create();
  htsmsg_add_str(m, "current", audio_mode_current->am_id);
  hts_settings_save(m, "audio/current");
  htsmsg_destroy(m);
}

/**
 *
 */
static void
audio_global_load_settings(void)
{
  audio_mode_t *am;
  const char *cur;
  htsmsg_t *m = hts_settings_load("audio/current");

  if(m == NULL)
    return;

  if((cur = htsmsg_get_str(m, "current")) != NULL)
    TAILQ_FOREACH(am, &audio_modes, am_link)
      if(!strcmp(cur, am->am_id))
	audio_mode_current = am;

  htsmsg_destroy(m);
}


/**
 *
 */
void
audio_init(void)
{
  hts_thread_t ptid;

  hts_mutex_init(&audio_mode_mutex);

  TAILQ_INIT(&audio_modes);
  audio_decoder_init();
  audio_alsa_init();

  hts_thread_create(&ptid, audio_output_thread, NULL);

  audio_mixer_init();
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

  audio_global_load_settings();

  while(1) {
    am = audio_mode_current;
    fprintf(stderr, "Audio output using %s\n", am->am_title);
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
static void
audio_mode_load_mixer_map( htsmsg_t *m, audio_mode_t *am, 
			   const char *name, int type)
{
  const char *s;
  mixer_controller_t *mc;
  if((s = htsmsg_get_str(m, name)) == NULL)
    return;

  TAILQ_FOREACH(mc, &am->am_mixer_controllers, mc_link) {
    if(!strcmp(mc->mc_title, s))
      am->am_mixers[type] = mc;
  }
}

/**
 *
 */
static void
audio_mode_load_settings(audio_mode_t *am)
{
  htsmsg_t *m = hts_settings_load("audio/devices/%s", am->am_id);
  
  if(m == NULL)
    return;

  htsmsg_get_u32(m, "phantom_center", &am->am_phantom_center);
  htsmsg_get_u32(m, "phantom_lfe", &am->am_phantom_lfe);
  htsmsg_get_u32(m, "small_front", &am->am_small_front);
  htsmsg_get_u32(m, "force_downmix", &am->am_force_downmix);
  htsmsg_get_u32(m, "swap_surround", &am->am_swap_surround);

  audio_mode_load_mixer_map(m, am, "mixer_master",     AM_MIXER_MASTER);
  audio_mode_load_mixer_map(m, am, "mixer_front",      AM_MIXER_FRONT);
  audio_mode_load_mixer_map(m, am, "mixer_rear",       AM_MIXER_REAR);
  audio_mode_load_mixer_map(m, am, "mixer_center",     AM_MIXER_CENTER);
  audio_mode_load_mixer_map(m, am, "mixer_center_lfe", AM_MIXER_CENTER_AND_LFE);
  audio_mode_load_mixer_map(m, am, "mixer_lfe",        AM_MIXER_LFE);
  audio_mode_load_mixer_map(m, am, "mixer_side",       AM_MIXER_SIDE);

  htsmsg_destroy(m);

}

/**
 *
 */
static void
audio_mode_save_mixer_map( htsmsg_t *m, audio_mode_t *am, 
			   const char *name, int type)
{
  if(am->am_mixers[type] == NULL)
    return;
  htsmsg_add_str(m, name, am->am_mixers[type]->mc_title);
}


/**
 *
 */
static void
audio_mode_save_settings(audio_mode_t *am)
{
  htsmsg_t *m = htsmsg_create();

  htsmsg_add_u32(m, "phantom_center", am->am_phantom_center);
  htsmsg_add_u32(m, "phantom_lfe", am->am_phantom_lfe);
  htsmsg_add_u32(m, "small_front", am->am_small_front);
  htsmsg_add_u32(m, "force_downmix", am->am_force_downmix);
  htsmsg_add_u32(m, "swap_surround", am->am_swap_surround);

  audio_mode_save_mixer_map(m, am, "mixer_master",     AM_MIXER_MASTER);
  audio_mode_save_mixer_map(m, am, "mixer_front",      AM_MIXER_FRONT);
  audio_mode_save_mixer_map(m, am, "mixer_rear",       AM_MIXER_REAR);
  audio_mode_save_mixer_map(m, am, "mixer_center",     AM_MIXER_CENTER);
  audio_mode_save_mixer_map(m, am, "mixer_center_lfe", AM_MIXER_CENTER_AND_LFE);
  audio_mode_save_mixer_map(m, am, "mixer_lfe",        AM_MIXER_LFE);
  audio_mode_save_mixer_map(m, am, "mixer_side",       AM_MIXER_SIDE);

  hts_settings_save(m, "audio/devices/%s", am->am_id);
  htsmsg_destroy(m);
}

/**
 *
 */
void
audio_mode_register(audio_mode_t *am)
{
  TAILQ_INSERT_TAIL(&audio_modes, am, am_link);
  audio_mode_load_settings(am);
}




/**
 *
 */
static void
audio_mode_change(void *opaque, void *opaque2, int value)
{
  audio_mode_current = opaque;
  audio_global_save_settings();
}

/**
 *
 */
static void
audio_opt_int_cb(void *opaque, void *opaque2, int value)
{
  audio_mode_t *am = opaque2;
  int *ptr = opaque;
  *ptr = value;
  audio_mode_save_settings(am);
}

/**
 *
 */
static void
audio_add_int_option_on_off(audio_mode_t *am, glw_t *l, const char *title,
			    unsigned int *ptr)
{
  glw_t *opt, *sel;
  glw_prop_t *r;
  
  r = glw_prop_create(NULL, "option", GLW_GP_DIRECTORY);
  glw_prop_set_string(glw_prop_create(r, "title", GLW_GP_STRING), title);

  opt = glw_model_create("theme://settings/audio/audio-option.model", l,
			 0, r, NULL);

  if((sel = glw_find_by_id(opt, "options", 0)) != NULL) {
    glw_selection_add_text_option(sel, "Off", 
				  audio_opt_int_cb, ptr, am, 0, *ptr == 0);
    glw_selection_add_text_option(sel, "On", 
				  audio_opt_int_cb, ptr, am, 1, *ptr == 1);
  }
}

/**
 *
 */
static void
audio_mixdev_cb(void *opaque, void *opaque2, int value)
{
  audio_mode_t *am = opaque;
  mixer_controller_t *mc = opaque2;

  am->am_mixers[value] = mc;
  audio_mode_save_settings(am);
}


/**
 *
 */
static void
audio_add_mixer_map(audio_mode_t *am, glw_t *p, int type, const char *title)
{
  glw_t *opt, *sel;
  mixer_controller_t *mc;
  glw_prop_t *r;

  r = glw_prop_create(NULL, "option", GLW_GP_DIRECTORY);
  glw_prop_set_string(glw_prop_create(r, "title", GLW_GP_STRING), title);

  opt = glw_model_create("theme://settings/audio/audio-option.model", p,
			 0, r, NULL);

  if((sel = glw_find_by_id(opt, "options", 0)) == NULL)
    return;

  glw_selection_add_text_option(sel, "Not available", audio_mixdev_cb,
				am, NULL, type, 
				am->am_mixers[type] == mc);

  TAILQ_FOREACH(mc, &am->am_mixer_controllers, mc_link) {
    glw_selection_add_text_option(sel, mc->mc_title, audio_mixdev_cb, 
				  am, mc, type, 
				  am->am_mixers[type] == mc);
  }
}



/**
 *
 */
static void
audio_mode_add_to_settings(audio_mode_t *am, glw_t *parent)
{
  glw_t *w, *le, *l;
  glw_t *deck;
  char buf[50];
  int multich = am->am_formats & (AM_FORMAT_PCM_5DOT1 | AM_FORMAT_PCM_7DOT1);
  glw_prop_t *r;

  r = glw_prop_create(NULL, "audiodevice", GLW_GP_DIRECTORY);


  if((w = glw_find_by_id(parent, "outputdevice_list", 0)) == NULL)
    return;

  le = glw_selection_add_text_option(w, am->am_title, audio_mode_change,
				     am, NULL, 0, 0);

  snprintf(buf, sizeof(buf), "%s%s%s%s%s",
	   am->am_formats & AM_FORMAT_PCM_STEREO ? "Stereo  " : "",
	   am->am_formats & AM_FORMAT_PCM_5DOT1  ? "5.1  "    : "",
	   am->am_formats & AM_FORMAT_PCM_7DOT1  ? "7.1 "     : "",
	   am->am_formats & AM_FORMAT_AC3        ? "AC3 "     : "",
	   am->am_formats & AM_FORMAT_DTS        ? "DTS "     : "");

  glw_prop_set_string(glw_prop_create(r, "outputformats", GLW_GP_STRING), buf);


  deck = glw_model_create("theme://settings/audio/audio-device-settings.model",
			  NULL, 0, r, NULL);

  if((l = glw_find_by_id(deck, "controllers", 0)) == NULL)
    return;

  if(multich) {
    audio_add_int_option_on_off(am, l, "Phantom Center:",
				&am->am_phantom_center);
    audio_add_int_option_on_off(am, l, "Phantom LFE:",
				&am->am_phantom_lfe);
    audio_add_int_option_on_off(am, l, "Small Front:",
				&am->am_small_front);
    audio_add_int_option_on_off(am, l, "Force Stereo Downmix:",
				&am->am_force_downmix);
    audio_add_int_option_on_off(am, l, "Swap C+LFE with Sr:",
				&am->am_swap_surround);
  }

  /**
   * Add any mixer controllers
   */

  if(TAILQ_FIRST(&am->am_mixer_controllers) != NULL) {
    audio_add_mixer_map(am, l, AM_MIXER_MASTER,  "Master volume:");
    audio_add_mixer_map(am, l, AM_MIXER_FRONT,   "Front volume:");
    audio_add_mixer_map(am, l, AM_MIXER_REAR,    "Rear volume:");
    audio_add_mixer_map(am, l, AM_MIXER_CENTER,  "Center volume:");
    audio_add_mixer_map(am, l, AM_MIXER_LFE,     "LFE volume:");
    audio_add_mixer_map(am, l, AM_MIXER_CENTER_AND_LFE, "Center + LFE volume:");
    audio_add_mixer_map(am, l, AM_MIXER_SIDE,    "Side volume:");
  }

  glw_add_tab(parent, NULL, le, "outputdevice_deck", deck);
}

/**
 *
 */
void
audio_settings_init(appi_t *ai, glw_t *m)
{
  glw_t *icon = glw_model_create("theme://settings/audio/audio-icon.model",
				 NULL, 0, NULL);
  glw_t *tab  = glw_model_create("theme://settings/audio/audio.model",
				 NULL, 0, NULL);
  audio_mode_t *am;
  glw_t *w;

  glw_add_tab(m, "settings_list", icon, "settings_deck", tab);

  TAILQ_FOREACH(am, &audio_modes, am_link)
    audio_mode_add_to_settings(am, tab);

  if((w = glw_find_by_id(tab, "outputdevice_list", 0)) != NULL) {
    w = glw_selection_get_widget_by_opaque(w, audio_mode_current);
    if(w != NULL)
      glw_select(w);
  }
}
