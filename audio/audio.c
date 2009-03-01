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
#include "settings.h"
#include "audio.h"
#include "audio_ui.h"
#include "audio_fifo.h"
#include "audio_decoder.h"

audio_mode_t *audio_mode_current;
hts_mutex_t audio_mode_mutex;

static struct audio_mode_queue audio_modes;

static prop_t *audio_settings_root;
static setting_t *audio_settings_current_device;

static void *audio_output_thread(void *aux);

static char *audio_stored_device;

/**
 *
 */
static void
audio_global_save_settings(void)
{
  htsmsg_t *m = htsmsg_create_map();
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
  htsmsg_t *m = hts_settings_load("audio/current");
  const char *cur;

  if(m == NULL)
    return;

  cur = htsmsg_get_str(m, "current");

  if(cur)
    audio_stored_device = strdup(cur);

  htsmsg_destroy(m);
}

/**
 *
 */
static void
audio_change_output_device(void *opaque, const char *string)
{
  audio_mode_t *am;
 
  TAILQ_FOREACH(am, &audio_modes, am_link)
    if(!strcmp(string, am->am_id))
      audio_mode_current = am;

  audio_global_save_settings();
}


/**
 *
 */
void
audio_init(void)
{
  audio_settings_root = settings_add_dir(NULL, "audio", "Audio settings",
					 "audio");
  
  audio_settings_current_device = 
    settings_add_multiopt(audio_settings_root, "currentdevice", 
			  "Current output device",
			  audio_change_output_device, NULL);

  audio_global_load_settings();

  hts_mutex_init(&audio_mode_mutex);

  TAILQ_INIT(&audio_modes);

  audio_decoder_init();

#define AUDIO_INIT_SUBSYS(name) \
 do {extern void audio_## name ##_init(void); audio_## name ##_init();}while(0)

#ifdef CONFIG_LIBASOUND
  AUDIO_INIT_SUBSYS(alsa);
#endif
  AUDIO_INIT_SUBSYS(dummy);

  hts_thread_create_detached(audio_output_thread, NULL);

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

  if(audio_mode_current == NULL)
    audio_mode_current = TAILQ_FIRST(&audio_modes);


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
audio_mode_load_mixer_map(htsmsg_t *m, audio_mode_t *am, 
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
audio_mode_save_mixer_map(htsmsg_t *m, audio_mode_t *am, 
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
  htsmsg_t *m = htsmsg_create_map();

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
static void
am_set_phantom_center(void *opaque, int value)
{
  audio_mode_t *am = opaque;
  am->am_phantom_center = !!value;
  audio_mode_save_settings(am);
}

/**
 *
 */
static void
am_set_phantom_lfe(void *opaque, int value)
{
  audio_mode_t *am = opaque;
  am->am_phantom_lfe = !!value;
  audio_mode_save_settings(am);
}

/**
 *
 */
static void
am_set_small_front(void *opaque, int value)
{
  audio_mode_t *am = opaque;
  am->am_small_front = !!value;
  audio_mode_save_settings(am);
}

/**
 *
 */
static void
am_set_force_downmix(void *opaque, int value)
{
  audio_mode_t *am = opaque;
  am->am_force_downmix = !!value;
  audio_mode_save_settings(am);
}

/**
 *
 */
static void
am_set_swap_surround(void *opaque, int value)
{
  audio_mode_t *am = opaque;
  am->am_swap_surround = !!value;
  audio_mode_save_settings(am);
}

/**
 *
 */
static void
am_set_mixer(audio_mode_t *am, int type, const char *string)
{
  mixer_controller_t *mc;

  if(string == NULL || !strcmp(string, "notavail")) {
    am->am_mixers[type] = NULL;
  } else {
    TAILQ_FOREACH(mc, &am->am_mixer_controllers, mc_link) {
      if(!strcmp(mc->mc_title, string))
	break;
    }
    am->am_mixers[type] = mc;
  }
  audio_mode_save_settings(am);
}




/**
 *
 */
static void 
am_set_mixer_master(void *opaque, const char *string)
{
  am_set_mixer(opaque, AM_MIXER_MASTER, string);
}

static void 
am_set_mixer_front(void *opaque, const char *string)
{
  am_set_mixer(opaque, AM_MIXER_FRONT, string);
}

static void 
am_set_mixer_rear(void *opaque, const char *string)
{
  am_set_mixer(opaque, AM_MIXER_REAR, string);
}

static void 
am_set_mixer_center(void *opaque, const char *string)
{
  am_set_mixer(opaque, AM_MIXER_CENTER, string);
}

static void 
am_set_mixer_center_lfe(void *opaque, const char *string)
{
  am_set_mixer(opaque, AM_MIXER_CENTER_AND_LFE, string);
}

static void 
am_set_mixer_lfe(void *opaque, const char *string)
{
  am_set_mixer(opaque, AM_MIXER_LFE, string);
}

static void 
am_set_mixer_side(void *opaque, const char *string)
{
  am_set_mixer(opaque, AM_MIXER_SIDE, string);
}


/**
 *
 */

static void
audio_add_mixer_map(prop_t *r, audio_mode_t *am, int type, 
		    const char *id, const char *title,
		    void (*setf)(void *opaque, const char *string))
{
  mixer_controller_t *mc;
  setting_t *p;

  p = settings_add_multiopt(r, id, title, setf, am);
  
  settings_multiopt_add_opt(p, "notavail", "Not avilable",
			    am->am_mixers[type] == NULL);

  TAILQ_FOREACH(mc, &am->am_mixer_controllers, mc_link)
    settings_multiopt_add_opt(p, mc->mc_title, mc->mc_title,
			      am->am_mixers[type] == mc);
}


 
/**
 *
 */
void
audio_mode_register(audio_mode_t *am)
{
  prop_t *r;
  int multich = am->am_formats & (AM_FORMAT_PCM_5DOT1 | AM_FORMAT_PCM_7DOT1);
  htsmsg_t *m;
  int selected;
  char buf[256];

  TAILQ_INSERT_TAIL(&audio_modes, am, am_link);

  selected = audio_stored_device && !strcmp(audio_stored_device, am->am_id);


  settings_multiopt_add_opt(audio_settings_current_device,
			    am->am_id, am->am_title, selected);


  if(selected)
    audio_mode_current = am;


  if(multich == 0 && TAILQ_FIRST(&am->am_mixer_controllers) == NULL)
    return;

  m = hts_settings_load("audio/devices/%s", am->am_id);


  snprintf(buf, sizeof(buf), "Configuration for %s", am->am_title);
  r = settings_add_dir(audio_settings_root, am->am_id, buf, "audio");

  if(multich) {
    settings_add_bool(r, "phantom_center", "Phantom center",
		      0, m, am_set_phantom_center, am,
		      SETTINGS_INITIAL_UPDATE);
    settings_add_bool(r, "phantom_lfe", "Phantom LFE",
		      0, m, am_set_phantom_lfe, am,
		      SETTINGS_INITIAL_UPDATE);
    settings_add_bool(r, "small_front", "Small front speakers",
		      0, m, am_set_small_front, am,
		      SETTINGS_INITIAL_UPDATE);
    settings_add_bool(r, "force_downmix", "Force stereo downmix",
		      0, m, am_set_force_downmix, am,
		      SETTINGS_INITIAL_UPDATE);
    settings_add_bool(r, "swap_surround", "Swap LFE+center with surround",
		      0, m, am_set_swap_surround, am,
		      SETTINGS_INITIAL_UPDATE);
  }

  if(m != NULL) {
    audio_mode_load_mixer_map(m, am, "mixer_master",     AM_MIXER_MASTER);
    audio_mode_load_mixer_map(m, am, "mixer_front",      AM_MIXER_FRONT);
    audio_mode_load_mixer_map(m, am, "mixer_rear",       AM_MIXER_REAR);
    audio_mode_load_mixer_map(m, am, "mixer_center",     AM_MIXER_CENTER);
    audio_mode_load_mixer_map(m, am, "mixer_center_lfe", AM_MIXER_CENTER_AND_LFE);
    audio_mode_load_mixer_map(m, am, "mixer_lfe",        AM_MIXER_LFE);
    audio_mode_load_mixer_map(m, am, "mixer_side",       AM_MIXER_SIDE);
  }

  if(TAILQ_FIRST(&am->am_mixer_controllers) != NULL) {
    audio_add_mixer_map(r, am, AM_MIXER_MASTER, "mixer_master",
			"Master volume", am_set_mixer_master);
    audio_add_mixer_map(r, am, AM_MIXER_FRONT, "mixer_front",
			"Front speakers", am_set_mixer_front);

    audio_add_mixer_map(r, am, AM_MIXER_REAR, "mixer_rear",
			"Rear speakers", am_set_mixer_rear);

    audio_add_mixer_map(r, am, AM_MIXER_CENTER, "mixer_center",
			"Center speaker", am_set_mixer_center);

    audio_add_mixer_map(r, am, AM_MIXER_CENTER_AND_LFE, "mixer_center_lfe",
			"Center + LFE speakers", am_set_mixer_center_lfe);

    audio_add_mixer_map(r, am, AM_MIXER_LFE, "mixer_lfe",
			"LFE speaker", am_set_mixer_lfe);

    audio_add_mixer_map(r, am, AM_MIXER_SIDE, "mixer_side",
			"Side speakers", am_set_mixer_side);
  }

  if(m != NULL)
    htsmsg_destroy(m);
}
