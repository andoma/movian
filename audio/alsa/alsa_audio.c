/*
 *  Alsa audio output
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
#include <math.h>
#include <stdio.h>
#define  __USE_XOPEN
#include <unistd.h>
#include <stdlib.h>

#include <errno.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>

#include "showtime.h"
#include "hid/input.h"
#include "audio/audio_mixer.h"
#include "audio/audio_ui.h"
#include "audio/audio_iec958.h"
#include "libhts/htscfg.h"

static snd_pcm_t *alsa_handle;
static int alsa_channels;
static unsigned int alsa_rate;
static int alsa_period_size;

extern int mixer_hw_output_delay;
extern int mixer_hw_output_formats;

extern float mixer_output_matrix[AUDIO_MIXER_MAX_CHANNELS]
                                [AUDIO_MIXER_MAX_CHANNELS];

extern audio_fifo_t mixer_output_fifo;


static int alsa_mixer_setup(void);

static void
alsa_configure(int format)
{
  snd_pcm_hw_params_t *hwp;
  snd_pcm_sw_params_t *swp;
  snd_pcm_t *h;
  const char *dev;
  int r, dir;
  snd_pcm_uframes_t period_size_min;
  snd_pcm_uframes_t period_size_max;
  snd_pcm_uframes_t buffer_size_min;
  snd_pcm_uframes_t buffer_size_max;
  snd_pcm_uframes_t period_size;
  snd_pcm_uframes_t buffer_size;
  int channels;

  if(alsa_handle != NULL) {
    snd_pcm_close(alsa_handle);
    alsa_handle = NULL;
  }

  switch(format) {
  case AUDIO_OUTPUT_PCM:
    channels = atoi(config_get_str("alsa-channels", "2"));
    dev = config_get_str("alsa-pcm-device", "default");
    fprintf(stderr, "audio: configuring for PCM; %d channels\n", channels);
    break;

  case AUDIO_OUTPUT_AC3:
    channels = 2;
    dev = config_get_str("alsa-ac3-device", NULL);
    fprintf(stderr, "audio: configuring for AC3 output\n");
    break;

  case AUDIO_OUTPUT_DTS:
    channels = 2;
    dev = config_get_str("alsa-dts-device", NULL);
    fprintf(stderr, "audio: configuring for DTS output\n");
    break;

  default:
    return;
  }

  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
    fprintf(stderr, "audio: Cannot open audio device %s (%s)\n",
		dev, snd_strerror(r));
    return;
  }

  fprintf(stderr, "audio: using device \"%s\"\n", dev);

  snd_pcm_hw_params_alloca(&hwp);

  snd_pcm_hw_params_any(h, hwp);
  snd_pcm_hw_params_set_access(h, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(h, hwp, SND_PCM_FORMAT_S16_LE);

  alsa_rate = 48000;

  if((r = snd_pcm_hw_params_set_rate_near(h, hwp, &alsa_rate, 0)) < 0) {
    fprintf(stderr, "audio: Cannot set rate to %d (%s)\n", 
	    alsa_rate, snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  fprintf(stderr, "audio: rate = %d\n", alsa_rate);


  if((r = snd_pcm_hw_params_set_channels(h, hwp, channels)) < 0) {
    fprintf(stderr, "audio: Cannot set # of channels to %d (%s)\n",
	    alsa_rate, snd_strerror(r));

    snd_pcm_close(h);
    return;
  }
  

  /* Configurue period */

  dir = 0;
  snd_pcm_hw_params_get_period_size_min(hwp, &period_size_min, &dir);
  dir = 0;
  snd_pcm_hw_params_get_period_size_max(hwp, &period_size_max, &dir);

  period_size = 1024;

  fprintf(stderr, "audio: attainable period size %lu - %lu, trying %lu\n",
	  period_size_min, period_size_max, period_size);


  dir = 1;
  r = snd_pcm_hw_params_set_period_size_near(h, hwp, &period_size, &dir);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to set period size %lu (%s)\n",
	    period_size, snd_strerror(r));
    snd_pcm_close(h);
    return;
  }


  dir = 0;
  r = snd_pcm_hw_params_get_period_size(hwp, &period_size, &dir);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to get period size (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  /* Configurue buffer size */

  snd_pcm_hw_params_get_buffer_size_min(hwp, &buffer_size_min);
  snd_pcm_hw_params_get_buffer_size_max(hwp, &buffer_size_max);
  buffer_size = period_size * 3;

  fprintf(stderr, "audio: attainable buffer size %lu - %lu, trying %lu\n",
	  buffer_size_min, buffer_size_max, buffer_size);


  dir = 0;
  r = snd_pcm_hw_params_set_buffer_size_near(h, hwp, &buffer_size);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to set buffer size %lu (%s)\n",
	    buffer_size, snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  r = snd_pcm_hw_params_get_buffer_size(hwp, &buffer_size);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to get buffer size (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }


  /* write the hw params */
  r = snd_pcm_hw_params(h, hwp);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure hardware parameters (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  /*
   * Software parameters
   */

  snd_pcm_sw_params_alloca(&swp);
  snd_pcm_sw_params_current(h, swp);

  
  r = snd_pcm_sw_params_set_avail_min(h, swp, buffer_size / 2);

  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure wakeup threshold (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  r = snd_pcm_sw_params_set_xfer_align(h, swp, 1);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure xfer alignment (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }


  snd_pcm_sw_params_set_start_threshold(h, swp, 0);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure start threshold (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return;
  }
  
  r = snd_pcm_sw_params(h, swp);
  if(r < 0) {
    fprintf(stderr, "audio: Cannot set soft parameters (%s)\n", 
		snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  r = snd_pcm_prepare(h);
  if(r < 0) {
    fprintf(stderr, "audio: Cannot prepare audio for playback (%s)\n", 
		snd_strerror(r));
    snd_pcm_close(h);
    return;
  }

  alsa_channels = channels;
  alsa_handle = h;

  alsa_period_size = period_size;

  printf("audio: period size = %ld\n", period_size);
  printf("audio: buffer size = %ld\n", buffer_size);
  printf("audio:        rate = %dHz\n", alsa_rate);



  switch(alsa_channels) {
  case 0:
    break;

  case 2:
    mixer_output_matrix[MIXER_CHANNEL_LEFT] [0] = 1.0f;
    mixer_output_matrix[MIXER_CHANNEL_RIGHT][1] = 1.0f;

    mixer_output_matrix[MIXER_CHANNEL_SR_LEFT][0] =  0.707f;
    mixer_output_matrix[MIXER_CHANNEL_SR_LEFT][1] = -0.707f;

    mixer_output_matrix[MIXER_CHANNEL_SR_RIGHT][0] = -0.707f;
    mixer_output_matrix[MIXER_CHANNEL_SR_RIGHT][1] =  0.707f;

    mixer_output_matrix[MIXER_CHANNEL_CENTER][0] =  0.707f;
    mixer_output_matrix[MIXER_CHANNEL_CENTER][1] =  0.707f;

    mixer_output_matrix[MIXER_CHANNEL_LFE][0] =  0.707f;
    mixer_output_matrix[MIXER_CHANNEL_LFE][1] =  0.707f;
    break;

  case 6:
    mixer_output_matrix[MIXER_CHANNEL_LEFT] [0] = 1.0f;
    mixer_output_matrix[MIXER_CHANNEL_RIGHT][1] = 1.0f;

    mixer_output_matrix[MIXER_CHANNEL_SR_LEFT][2] = 1.0f;
    mixer_output_matrix[MIXER_CHANNEL_SR_RIGHT][3] = 1.0f;

    mixer_output_matrix[MIXER_CHANNEL_LEFT][5]  = 0.5f;
    mixer_output_matrix[MIXER_CHANNEL_RIGHT][5] = 0.5;
    mixer_output_matrix[MIXER_CHANNEL_LFE][5]   = 1.0f;

    if(config_get_bool("alsa-phantom-center", 0)) {
      mixer_output_matrix[MIXER_CHANNEL_CENTER][0] =  0.707f;
      mixer_output_matrix[MIXER_CHANNEL_CENTER][1] =  0.707f;
    } else {
      mixer_output_matrix[MIXER_CHANNEL_CENTER][4] =  1.0f;
    }
    break;
  }

  audio_mixer_setup_output(alsa_channels, alsa_period_size, alsa_rate);
}






static void *
alsa_thread(void *aux)
{
  int c, x;
  int16_t *outbuf;
  int outlen;
  audio_buf_t *buf;
  snd_pcm_sframes_t delay;
  int current_output_type = AUDIO_OUTPUT_PCM;
  void *iec958buf;
  int f = AUDIO_OUTPUT_PCM;

  iec958buf = calloc(1, IEC958_MAX_FRAME_SIZE);

  if(config_get_str("alsa-ac3-device", NULL)) f |= AUDIO_OUTPUT_AC3;
  if(config_get_str("alsa-dts-device", NULL)) f |= AUDIO_OUTPUT_DTS;

  /* tell mixer which formats we support */

  mixer_hw_output_formats = f;

  while(1) {

    alsa_configure(current_output_type);

    while(1) {

      buf = af_deq(&mixer_output_fifo, 1);

      if(buf->payload_type != current_output_type) {
	current_output_type = buf->payload_type;
	af_free(buf);
	break;
      }

      switch(current_output_type) {

      case AUDIO_OUTPUT_PCM:
	outbuf = ab_dataptr(buf);
	outlen = alsa_period_size;
	break;

      case AUDIO_OUTPUT_AC3:
	outlen = iec958_build_ac3frame(ab_dataptr(buf), buf->size, iec958buf);
	outbuf = iec958buf;
	break;

      case AUDIO_OUTPUT_DTS:
	outlen = iec958_build_dtsframe(ab_dataptr(buf), buf->size, iec958buf);
	outbuf = iec958buf;
	break;

      default:
	outbuf = NULL;
	outlen = 0;
	break;
      }

      if(alsa_handle == NULL) {
	af_free(buf);
	continue;
      }

      c = snd_pcm_wait(alsa_handle, 100);
      if(c >= 0) 
	c = snd_pcm_avail_update(alsa_handle);

      if(c == -EPIPE) {
	snd_pcm_prepare(alsa_handle);
	continue;
      }
 
      if(outlen > 0) {

	x = snd_pcm_writei(alsa_handle, outbuf, outlen);

	if(snd_pcm_delay(alsa_handle, &delay))
	  delay = 0;
      
      /* convert delay from sample rates to µs */
	mixer_hw_output_delay = (delay * 1000 / alsa_rate) * 1000;
      }

      af_free(buf);
    }
  }
}









void
audio_alsa_init(void)
{
  pthread_t ptid;
  pthread_create(&ptid, NULL, alsa_thread, NULL);
  alsa_mixer_setup();
}




static ic_t  alsa_master_volume_input;
static float alsa_master_volume;
static int   alsa_master_mute;
static snd_mixer_elem_t *alsa_mixer_element;



/**
 * Mixer update
 */

static void
mixer_write(float vol, int mute)
{
  snd_mixer_elem_t *e = alsa_mixer_element;

  if(e == NULL)
    return;

  /* Mute flag */

  if(snd_mixer_selem_has_playback_switch_joined(e)) {
    snd_mixer_selem_set_playback_switch_all(e, !mute);
  } else {
    snd_mixer_selem_set_playback_switch(e, SND_MIXER_SCHN_FRONT_LEFT,  !mute);
    snd_mixer_selem_set_playback_switch(e, SND_MIXER_SCHN_FRONT_RIGHT, !mute);
  }

  vol = 100.0f * vol;

  /* Volume */

  snd_mixer_selem_set_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT,  vol);
  snd_mixer_selem_set_playback_volume(e, SND_MIXER_SCHN_FRONT_RIGHT, vol);
}




/**
 *
 */


static void *
alsa_master_volume_thread(void *aux)
{
  int key;
  float v;

  while(1) {
    key = input_getkey(&alsa_master_volume_input, 1);

    v = alsa_master_volume / 10.0;
    if(v < 0.01)
      v = 0.01;

    switch(key) {
    case INPUT_KEY_VOLUME_UP:
      alsa_master_volume += v;
      break;

    case INPUT_KEY_VOLUME_DOWN:
      alsa_master_volume -= v;
      break;

    case INPUT_KEY_VOLUME_MUTE:
      alsa_master_mute = !alsa_master_mute;
      break;
    }

    if(alsa_master_volume > 1.0f)
      alsa_master_volume = 1.0f;
    else if(alsa_master_volume < 0.0f)
      alsa_master_volume = 0.0f;

    mixer_write(alsa_master_volume, alsa_master_mute);
    audio_ui_vol_changed(alsa_master_volume, alsa_master_mute);
  }
}


/**
 *
 */

static int
alsa_mixer_input_event(inputevent_t *ie)
{
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
      input_keystrike(&alsa_master_volume_input, ie->u.key);
      return 1;
    }
    break;
  }
  return 0;
}







static int
alsa_mixer_setup(void)
{
  snd_mixer_t *mixer;
  const char *mixerdev = "hw:0";
  snd_mixer_selem_id_t *sid;
  snd_mixer_elem_t *e;
  long v;
  int r;
  pthread_t ptid;

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


  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_name(sid,
			      config_get_str("alsa-mixer-element", "Master"));

  e = snd_mixer_find_selem(mixer, sid);
  if(e != NULL) {
    snd_mixer_selem_set_playback_volume_range(e, 0, 100);
    snd_mixer_selem_get_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &v);
    alsa_master_volume = v / 100.0;
  }

  input_init(&alsa_master_volume_input);
  pthread_create(&ptid, NULL, &alsa_master_volume_thread, NULL);
  inputhandler_register(200, alsa_mixer_input_event);

  alsa_mixer_element = e;
  return 0;

}
