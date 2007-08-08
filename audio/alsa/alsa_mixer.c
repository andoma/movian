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

#include <pthread.h>

#include <stdio.h>
#define  __USE_XOPEN
#include <unistd.h>
#include <stdlib.h>

#include <sys/time.h>

#include <errno.h>


#include "showtime.h"
#include "input.h"
#include "alsa_mixer.h"

#include "audio/audio_ui.h"

static mixer_t mixer0;

static int alsa_mixer_input_event(inputevent_t *ie);


static float
element_add(mixer_t *mi, const char *name, const char *displayname)
{
  fader_t *f;
  snd_mixer_selem_id_t *sid;
  long v;

  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_name(sid, name);

  f = &mi->faders[mi->nfaders];
  
  f->elm = snd_mixer_find_selem(mi->mixer, sid);
  if(f->elm == NULL)
    return 0.0;

  snd_mixer_selem_set_playback_volume_range(f->elm, 0, 100);

  f->displayname = displayname;
  f->multiplier = 1.0f;
  mi->nfaders++;

  snd_mixer_selem_get_playback_volume(f->elm,
				      SND_MIXER_SCHN_FRONT_LEFT, &v);

  return (float)v / 100.0f;

}


static int
mixer_setup(mixer_t *mi)
{
  snd_mixer_t *mixer;
  const char *mixerdev = "hw:0";
  int r;

  /* Open mixer */

  if((r = snd_mixer_open(&mixer, 0)) < 0) {
    fprintf(stderr, "Cannot open mixer\n");
    return 1;
  }

  if((r = snd_mixer_attach(mixer, mixerdev)) < 0) {
    fprintf(stderr, "Cannot attach mixer\n");
    snd_mixer_close(mixer);
    return 1;
  }
  
  if((r = snd_mixer_selem_register(mixer, NULL, NULL)) < 0) {
    fprintf(stderr, "Cannot register mixer\n");
    snd_mixer_close(mixer);
    return 1;
  }

  if((r = snd_mixer_load(mixer)) < 0) {
    fprintf(stderr, "Cannot load mixer\n");
    snd_mixer_close(mixer);
    return 1;
  }

  mi->mixer = mixer;
  return 0;

}

/****************************************************************************
 *
 * Mixer update
 *
 */

static void
mixer_write(mixer_t *mi)
{
  fader_t *f;
  int i, vol;
  asched_t *as = mi->as;

  for(i = 0; i < mi->nfaders; i++) {
    f = &mi->faders[i];

    /* Mute flag */

    if(snd_mixer_selem_has_playback_switch_joined(f->elm)) {
      snd_mixer_selem_set_playback_switch_all(f->elm, !as->as_mute);
    } else {
      snd_mixer_selem_set_playback_switch(f->elm, SND_MIXER_SCHN_FRONT_LEFT, 
					  !as->as_mute);
      snd_mixer_selem_set_playback_switch(f->elm, SND_MIXER_SCHN_FRONT_RIGHT,
					  !as->as_mute);
    }

    vol = 100.0f * as->as_mastervol * f->multiplier;

    /* Volume */

    snd_mixer_selem_set_playback_volume(f->elm, SND_MIXER_SCHN_FRONT_LEFT, 
					vol);

    snd_mixer_selem_set_playback_volume(f->elm, SND_MIXER_SCHN_FRONT_RIGHT, 
					vol);
  }
}









/****************************************************************************
 *
 *
 *
 */


static void *
mixer_thread(void *aux)
{
  mixer_t *mi = &mixer0;
  asched_t *as = mi->as;
  int key;
  float v;


  while(1) {
    key = input_getkey(&mi->input, 1);

    v = as->as_mastervol / 10.0;
    if(v < 0.01)
      v = 0.01;

    switch(key) {
    case INPUT_KEY_VOLUME_UP:
      as->as_mastervol += v;
      break;

    case INPUT_KEY_VOLUME_DOWN:
      as->as_mastervol -= v;
      break;

    case INPUT_KEY_VOLUME_MUTE:
      as->as_mute = !as->as_mute;
      break;
    }


    if(as->as_mastervol > 1.0f)
      as->as_mastervol = 1.0f;
    else if(as->as_mastervol < 0.0f)
      as->as_mastervol = 0.0f;

    mixer_write(mi);
    audio_ui_vol_changed();

  }
}


/****************************************************************************
 *
 *
 *
 */

void
alsa_mixer_init(asched_t *as)
{
  mixer_t *mi = &mixer0;
  pthread_t ptid;
  const char *mixertype;

  mi->as = as;
  
  mixer_setup(mi);

  as->as_mastervol = 0.3;

  mixertype = config_get_str("mixer-type", "master");

  if(!strcasecmp(mixertype, "5.1")) {
    as->as_mastervol = element_add(mi, "PCM", "Front L+R");
    element_add(mi, "Surround", "Surround L+R");
    element_add(mi, "LFE", "Subwoofer");
  } else {
    as->as_mastervol = element_add(mi, mixertype, mixertype);
  }

  input_init(&mi->input);

  mixer_write(mi);

  pthread_create(&ptid, NULL, &mixer_thread, NULL);

  inputhandler_register(200, alsa_mixer_input_event);
}

static int
alsa_mixer_input_event(inputevent_t *ie)
{
  mixer_t *mi = &mixer0;

  switch(ie->type) {
  default:
    break;

  case INPUT_KEY:

    switch(ie->u.key) {
    default:
      break;

    case INPUT_KEY_VOLUME_UP:
    case INPUT_KEY_VOLUME_DOWN:
    case INPUT_KEY_VOLUME_MUTE:
      input_keystrike(&mi->input, ie->u.key);
      return 1;
    }
    break;
  }
  return 0;
}
