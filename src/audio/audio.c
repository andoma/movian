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
#include "audio_defs.h"
#include "audio_fifo.h"
#include "audio_decoder.h"
#include "notifications.h"

audio_mode_t *audio_mode_current;
hts_mutex_t audio_mode_mutex;

static struct audio_mode_queue audio_modes;

static prop_t *audio_settings_root;
static setting_t *audio_settings_current_device;

static void *audio_output_thread(void *aux);
static void audio_mastervol_init(void);

static char *audio_stored_device;

/**
 *
 */
int
audio_rateflag_from_rate(int rate)
{
  switch(rate) {
    case 96000: return AM_SR_96000;
    case 48000: return AM_SR_48000;
    case 44100: return AM_SR_44100;
    case 32000: return AM_SR_32000;
    case 24000: return AM_SR_24000;
    default:    return 0;
  }
}

/**
 *
 */
int
audio_rate_from_rateflag(int flag)
{
  switch(flag) {
    case AM_SR_96000: return 96000; break;
    case AM_SR_48000: return 48000; break;
    case AM_SR_44100: return 44100; break;
    case AM_SR_32000: return 32000; break;
    case AM_SR_24000: return 24000; break;
    default: return 0; break;
  }
}

const char *
audio_format_to_string(int format)
{
  switch(format) {
    case AM_FORMAT_PCM_STEREO: return "PCM stereo"; break;
    case AM_FORMAT_PCM_5DOT1: return "PCM 5dot1"; break;
    case AM_FORMAT_PCM_7DOT1: return "PCM 7dot1"; break;
    case AM_FORMAT_AC3: return "AC3"; break;
    case AM_FORMAT_DTS: return "DTS"; break;
    default: return "unknown"; break;
  }
}

/**
 *
 */
static void
audio_global_save_settings(void)
{
  htsmsg_t *m = htsmsg_create_map();
  htsmsg_add_str(m, "current", audio_mode_current->am_id);
  htsmsg_store_save(m, "audio/current");
  htsmsg_destroy(m);
}

/**
 *
 */
static void
audio_global_load_settings(void)
{
  htsmsg_t *m = htsmsg_store_load("audio/current");
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
  audio_mastervol_init();

  audio_settings_root = settings_add_dir(NULL, "Audio output", "sound", NULL);
  
  audio_settings_current_device = 
    settings_create_multiopt(audio_settings_root, "currentdevice", 
			     "Current output device",
			     audio_change_output_device, NULL);
  
  audio_global_load_settings();

  hts_mutex_init(&audio_mode_mutex);

  TAILQ_INIT(&audio_modes);

#ifdef CONFIG_LIBOGC
  audio_wii_init();
#endif

  int have_pulse_audio  __attribute__((unused)) = 0;

#ifdef CONFIG_LIBPULSE
  have_pulse_audio |= audio_pa_init();
#endif

#ifdef CONFIG_LIBASOUND
  audio_alsa_init(have_pulse_audio);
#endif

#ifdef CONFIG_COREAUDIO
  audio_coreaudio_init();
#endif

  audio_dummy_init();

  hts_thread_create_detached("audio output", audio_output_thread, NULL);
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

  am = audio_mode_current;

  while(1) {
    r = am->am_entry(am, af);
    am = audio_mode_current;

    if(r == -1) {
      /* Device error, sleep to avoid busy looping.
	 Hopefully the error will resolve itself (if another app
	 is blocking output, etc)
      */
      sleep(1);
    } else {

      notify_add(NOTIFY_INFO, NULL, 5, "Switching audio output to %s", 
		 am->am_title);
    }

  }
  return NULL;
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
  htsmsg_add_s32(m, "delay", am->am_audio_delay);

  htsmsg_store_save(m, "audio/devices/%s", am->am_id);
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
am_set_av_sync(void *opaque, int value)
{
  audio_mode_t *am = opaque;
  am->am_audio_delay = value;
  audio_mode_save_settings(am);
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
  char buf[256];

  TAILQ_INSERT_TAIL(&audio_modes, am, am_link);

  if(audio_mode_current == NULL ||
     (audio_stored_device && !strcmp(audio_stored_device, am->am_id)))
    audio_mode_current = am;
  
  settings_multiopt_add_opt(audio_settings_current_device,
			    am->am_id, am->am_title, am == audio_mode_current);
  
  m = htsmsg_store_load("audio/devices/%s", am->am_id);


  snprintf(buf, sizeof(buf), "Configuration for %s", am->am_title);
  r = settings_add_dir(audio_settings_root, buf, "sound", NULL);

  settings_create_int(r, "delay", "Audio/Video sync delay",
		      0, m, -1000, 1000, 10, am_set_av_sync, am,
		      SETTINGS_INITIAL_UPDATE, "ms", NULL, NULL, NULL);

  if(multich) {
    settings_create_bool(r, "phantom_center", "Phantom center",
		      0, m, am_set_phantom_center, am,
		      SETTINGS_INITIAL_UPDATE, NULL, NULL, NULL);
    settings_create_bool(r, "phantom_lfe", "Phantom LFE",
		      0, m, am_set_phantom_lfe, am,
		      SETTINGS_INITIAL_UPDATE, NULL, NULL, NULL);
    settings_create_bool(r, "small_front", "Small front speakers",
		      0, m, am_set_small_front, am,
		      SETTINGS_INITIAL_UPDATE, NULL, NULL, NULL);
    settings_create_bool(r, "force_downmix", "Force stereo downmix",
		      0, m, am_set_force_downmix, am,
		      SETTINGS_INITIAL_UPDATE, NULL, NULL, NULL);
    settings_create_bool(r, "swap_surround", "Swap LFE+center with surround",
		      0, m, am_set_swap_surround, am,
		      SETTINGS_INITIAL_UPDATE, NULL, NULL, NULL);
  }

  if(m != NULL)
    htsmsg_destroy(m);
}



prop_t *prop_mastervol, *prop_mastermute;

/**
 *
 */
static void
save_matervol(void *opaque, float value)
{
  htsmsg_t *m = htsmsg_create_map();
  TRACE(TRACE_DEBUG, "audio", "Master volume set to %f dB", value);

  htsmsg_add_s32(m, "master-volume", value * 1000);
  htsmsg_store_save(m, "audiomixer");
  htsmsg_destroy(m);
}


/**
 *
 */
void
audio_mastervol_init(void)
{
  htsmsg_t *m = htsmsg_store_load("audiomixer");
  int32_t i32;
  prop_t *prop_audio;

  prop_audio = prop_create(prop_get_global(), "audio");
  prop_mastervol  = prop_create(prop_audio, "mastervolume");
  prop_mastermute = prop_create(prop_audio, "mastermute");

  prop_set_float_clipping_range(prop_mastervol, -75, 0);

  prop_set_int(prop_mastermute, 0);

  if(m != NULL && !htsmsg_get_s32(m, "master-volume", &i32))
    prop_set_float(prop_mastervol, (float)i32 / 1000);
  
  htsmsg_destroy(m);

  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		 PROP_TAG_CALLBACK_FLOAT, save_matervol, NULL,
		 PROP_TAG_ROOT, prop_mastervol,
		 NULL);
}
