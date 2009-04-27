/*
 *  Audio mixer
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "showtime.h"
#include "audio.h"
#include "event.h"

volume_control_t global_volume;

static event_queue_t audio_mixer_event_queue;
static prop_t *prop_mastervol, *prop_mastermute;

/**
 *
 */
static void
volume_update_widget(volume_control_t *vc)
{
  prop_set_float(prop_mastervol, vc->vc_master_vol);
  prop_set_int(prop_mastermute, vc->vc_master_mute);
}


/**
 *
 */
static float
volume_update_slider(audio_mode_t *am, int id, float db, int mute)
{
  mixer_controller_t *mc = am->am_mixers[id];

  if(mc == NULL)
    /* Slider not available. Return spill (or muted volume) */
    return mute ? -1000 : db;

  if(mute) {
    if(mc->mc_set_mute == NULL || mc->mc_set_mute(mc, mute))
      db = -1000; /* Mute failed, lower volume a lot instead */
  } else if(mc->mc_set_mute != NULL)
    mc->mc_set_mute(mc, 0); /* Unmute */
  return mc->mc_set_volume(mc, db);
}


/**
 *
 */
static int
set_soft_gain(volume_control_t *vc, int ch, float db)
{
#define SOFTGAIN_TOLERANCE 1024

  int fg = 65536 * pow(10, (db / 10));
  vc->vc_soft_gain[ch] = fg;

  return fg < 0x10000 - SOFTGAIN_TOLERANCE;
}


/**
 *
 */
static int
audio_mixer_update(volume_control_t *vc)
{
  float ms, s;
  float cv, lv;
  int need_softgain = 0;

  audio_mode_t *am = audio_mode_current;

  if(am == NULL)
    return 0;

  if(TAILQ_FIRST(&am->am_mixer_controllers) == NULL)
    return -1; /* Can't change volume here */


  ms = volume_update_slider(am, AM_MIXER_MASTER, vc->vc_master_vol,
			    vc->vc_master_mute);

  s = volume_update_slider(am, AM_MIXER_FRONT, vc->vc_front_vol + ms, 0);
  need_softgain |= set_soft_gain(vc, 0, s); /* Left  */
  need_softgain |= set_soft_gain(vc, 1, s); /* Right */
  

  /* Center and LFE may be controlled on the same stereo channel,
   * so we try to address such a mapping first.
   */

  if(vc->vc_center_vol == vc->vc_lfe_vol) {
    cv = lv = volume_update_slider(am, AM_MIXER_CENTER_AND_LFE,
				   vc->vc_center_vol + ms, 0);
  } else {
    cv = vc->vc_center_vol + ms;
    lv = vc->vc_lfe_vol    + ms;
  }

  s = volume_update_slider(am, AM_MIXER_CENTER, cv, 0);
  need_softgain |= set_soft_gain(vc, 4, s); /* Center  */

  
  s = volume_update_slider(am, AM_MIXER_LFE,    lv, 0);
  need_softgain |= set_soft_gain(vc, 5, s); /* LFE */

  s = volume_update_slider(am, AM_MIXER_REAR, vc->vc_rear_vol + ms, 0);
  need_softgain |= set_soft_gain(vc, 2, s); /* SR Left  */
  need_softgain |= set_soft_gain(vc, 3, s); /* SR Right */

  s = volume_update_slider(am, AM_MIXER_SIDE, vc->vc_side_vol + ms, 0);
  need_softgain |= set_soft_gain(vc, 6, s); /* Side Left  */
  need_softgain |= set_soft_gain(vc, 7, s); /* Side Right */
  
  vc->vc_soft_gain_needed = need_softgain;

  volume_update_widget(vc);

  return 0;
}


/**
 *
 */
static int
audio_mixer_event_handler(event_t *e, void *opaque)
{
  switch(e->e_type) {

  case EVENT_VOLUME_UP:
  case EVENT_VOLUME_DOWN:
  case EVENT_VOLUME_MUTE_TOGGLE:
    break;
  default:
    return 0;
  }

  event_enqueue(&audio_mixer_event_queue, e);
  return 1;
}


/**
 *
 */
static void
audio_mixer_load(void)
{
  htsmsg_t *m = htsmsg_store_load("audiomixer");
  int32_t i32;

  if(m == NULL)
    return;

  if(!htsmsg_get_s32(m, "master-volume", &i32))
    global_volume.vc_master_vol = (float)i32 / 1000;

  htsmsg_destroy(m);
}


/**
 *
 */
static void
audio_mixer_save(void)
{
  htsmsg_t *m = htsmsg_create_map();

  htsmsg_add_s32(m, "master-volume", 
		 global_volume.vc_master_vol * 1000);
  htsmsg_store_save(m, "audiomixer");
  htsmsg_destroy(m);
}


/**
 *
 */
static void *
audio_mixer_thread(void *aux)
{
  event_t *e;
  event_initqueue(&audio_mixer_event_queue);

  global_volume.vc_master_vol = -50;

  audio_mixer_load();

  event_handler_register("audio mixer", audio_mixer_event_handler,
			 EVENTPRI_AUDIO_MIXER, NULL);
  
  audio_mixer_update(&global_volume);
    

  while(1) {
    e = event_get(-1, &audio_mixer_event_queue);

    switch(e->e_type) {
    case EVENT_VOLUME_UP:
      global_volume.vc_master_vol += 1;
      if(global_volume.vc_master_vol > 6)
	global_volume.vc_master_vol = 6;
      audio_mixer_save();
      break;

    case EVENT_VOLUME_DOWN:
      global_volume.vc_master_vol -= 1;
      if(global_volume.vc_master_vol < -75)
	global_volume.vc_master_vol = -75;
      audio_mixer_save();
      break;
    case EVENT_VOLUME_MUTE_TOGGLE:
      global_volume.vc_master_mute = !global_volume.vc_master_mute;
      break;
      
    default:
      break;
    }

    event_unref(e);

    audio_mixer_update(&global_volume);
  }
}


/**
 *
 */
void
audio_mixer_init(void)
{
  prop_t *prop_audio;

  prop_audio = prop_create(prop_get_global(), "audio");
  prop_mastervol  = prop_create(prop_audio, "mastervolume");
  prop_mastermute = prop_create(prop_audio, "mastermute");

  hts_thread_create_detached(audio_mixer_thread, NULL);
}
