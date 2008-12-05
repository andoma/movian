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

#include <math.h>
#include <stdio.h>
#define  __USE_XOPEN
#include <unistd.h>
#include <stdlib.h>

#include <errno.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>

#include "showtime.h"
#include "audio/audio.h"
#include "audio/audio_ui.h"
#include "audio/audio_iec958.h"

/**
 * Alsa representation of an audio mode
 */
typedef struct alsa_audio_mode {
  audio_mode_t aam_head;

  char *aam_dev;

  int aam_sample_rate;

} alsa_audio_mode_t;

/**
 * Alsa representation of a mixer controller
 */
typedef struct alsa_mixer_controller {
  mixer_controller_t h;
  snd_mixer_elem_t *amc_elem;
  int amc_joined;

} alsa_mixer_controller_t;


static int alsa_probe_mixer(const char *device, audio_mode_t *am);

#define hts_alsa_debug(fmt...) fprintf(stderr, fmt)

//static int alsa_mixer_setup(void);

/**
 *
 */
static snd_pcm_t *
alsa_open(alsa_audio_mode_t *aam, int format, int rate)
{
  snd_pcm_hw_params_t *hwp;
  snd_pcm_sw_params_t *swp;
  snd_pcm_t *h;
  char buf[64];
  char *dev = aam->aam_dev;
  int r, ch;
  int dir;
  snd_pcm_uframes_t period_size_min;
  snd_pcm_uframes_t period_size_max;
  snd_pcm_uframes_t buffer_size_min;
  snd_pcm_uframes_t buffer_size_max;
  snd_pcm_uframes_t period_size;
  snd_pcm_uframes_t buffer_size;

  if(format == AM_FORMAT_AC3 || format == AM_FORMAT_DTS) {
    snprintf(buf, sizeof(buf), "%s:AES0=0x2,AES1=0x82,AES2=0x0,AES3=0x2", dev);
    dev = buf;
  } 

  fprintf(stderr, "ALSA: opening device \"%s\"\n", dev);
  
  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0))
    return NULL;

  hwp = alloca(snd_pcm_hw_params_sizeof());
  memset(hwp, 0, snd_pcm_hw_params_sizeof());
  snd_pcm_hw_params_any(h, hwp);

  snd_pcm_hw_params_set_access(h, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(h, hwp, SND_PCM_FORMAT_S16_LE);

  switch(rate) {
  default:
  case AM_SR_96000: rate = 96000; break;
  case AM_SR_48000: rate = 48000; break;
  case AM_SR_44100: rate = 44100; break;
  case AM_SR_32000: rate = 32000; break;
  case AM_SR_24000: rate = 24000; break;
  }

  snd_pcm_hw_params_set_rate(h, hwp, rate, 0);
  
  aam->aam_sample_rate = rate;

  switch(format) {
  case AM_FORMAT_PCM_STEREO:
  case AM_FORMAT_AC3:
  case AM_FORMAT_DTS:
    ch = 2;
    break;

  case AM_FORMAT_PCM_5DOT1:
    ch = 6;
    break;

  case AM_FORMAT_PCM_7DOT1:
    ch = 8;
    break;

  default:
    snd_pcm_close(h);
    return NULL;
  }

  snd_pcm_hw_params_set_channels(h, hwp, ch);

  fprintf(stderr, "audio: %d channels\n", ch);


  /* Configurue period */

  dir = 0;
  snd_pcm_hw_params_get_period_size_min(hwp, &period_size_min, &dir);
  dir = 0;
  snd_pcm_hw_params_get_period_size_max(hwp, &period_size_max, &dir);

  //  period_size = period_size_max;

  period_size = 1024;

  fprintf(stderr, "audio: attainable period size %lu - %lu, trying %lu\n",
	  period_size_min, period_size_max, period_size);


  dir = 0;
  r = snd_pcm_hw_params_set_period_size_near(h, hwp, &period_size, &dir);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to set period size %lu (%s)\n",
	    period_size, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }


  dir = 0;
  r = snd_pcm_hw_params_get_period_size(hwp, &period_size, &dir);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to get period size (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  /* Configurue buffer size */

  snd_pcm_hw_params_get_buffer_size_min(hwp, &buffer_size_min);
  snd_pcm_hw_params_get_buffer_size_max(hwp, &buffer_size_max);
  buffer_size = period_size * 4;

  fprintf(stderr, "audio: attainable buffer size %lu - %lu, trying %lu\n",
	  buffer_size_min, buffer_size_max, buffer_size);


  dir = 0;
  r = snd_pcm_hw_params_set_buffer_size_near(h, hwp, &buffer_size);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to set buffer size %lu (%s)\n",
	    buffer_size, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  r = snd_pcm_hw_params_get_buffer_size(hwp, &buffer_size);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to get buffer size (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }


  /* write the hw params */
  r = snd_pcm_hw_params(h, hwp);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure hardware parameters (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  /*
   * Software parameters
   */

  swp = alloca(snd_pcm_sw_params_sizeof());
  memset(hwp, 0, snd_pcm_sw_params_sizeof());

  snd_pcm_sw_params_current(h, swp);


  r = snd_pcm_sw_params_set_avail_min(h, swp,  period_size);

  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure wakeup threshold (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  r = snd_pcm_sw_params_set_xfer_align(h, swp, 1);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure xfer alignment (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }


  snd_pcm_sw_params_set_start_threshold(h, swp, 0);
  if(r < 0) {
    fprintf(stderr, "audio: Unable to configure start threshold (%s)\n",
	    snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }
  
  r = snd_pcm_sw_params(h, swp);
  if(r < 0) {
    fprintf(stderr, "audio: Cannot set soft parameters (%s)\n", 
		snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  r = snd_pcm_prepare(h);
  if(r < 0) {
    fprintf(stderr, "audio: Cannot prepare audio for playback (%s)\n", 
		snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  aam->aam_head.am_preferred_size = period_size;

 
  printf("audio: period size = %ld\n", period_size);
  printf("audio: buffer size = %ld\n", buffer_size);

  return h;
}

/**
 *
 */
static void
alsa_silence(snd_pcm_t *h, int format, int rate, void *tmpbuf)
{
  int frames;

  if(format == AM_FORMAT_AC3) {
    frames = iec958_build_ac3frame(NULL, 0, tmpbuf);
  } else {
    memset(tmpbuf, 0, 500 * sizeof(int16_t) * 8);
    frames = 500;
  }

  snd_pcm_writei(h, tmpbuf, frames);
}


/**
 *
 */
static int
alsa_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  snd_pcm_t *h = NULL; 

  alsa_audio_mode_t *aam = (void *)am;
  audio_buf_t *ab;
  int16_t *outbuf;
  int outlen;
  int c;
  int cur_format = 0;
  int cur_rate = 0;
  int silence_threshold = 0; 
  snd_pcm_sframes_t fr;
  int d;
  int64_t pts = AV_NOPTS_VALUE;
  int ret = 0;
  void *tmpbuf;

  tmpbuf = calloc(1, IEC958_MAX_FRAME_SIZE);

  while(1) {

    ab = af_deq(af, h == NULL); /* wait if PCM device is not open */

    if(am != audio_mode_current) {
      /* We're not the selected audio output anymore, return.
	 We will lose the current audio block, but who cares ? */
      ab_free(ab);

      if(h != NULL)
	snd_pcm_close(h);

      break;
    }

    if(ab == NULL) {
      assert(h != NULL);
    silence:
      pts = AV_NOPTS_VALUE;
      alsa_silence(h, cur_format, cur_rate, tmpbuf);
      silence_threshold--;
      if(silence_threshold < 0) {
	/* We've been silent for a while, close output device */
	printf("Closing PCM device due to idling\n");
	snd_pcm_close(h);
	h = NULL;
      }
      continue;
    }

    if(h == NULL || ab->ab_format != cur_format || ab->ab_rate != cur_rate) {

      if(!(ab->ab_format & am->am_formats) || 
	 !(ab->ab_rate & am->am_sample_rates)) {
	/* Rate / format is not supported by this mode */
	ab_free(ab);
	if(h == NULL)
	  continue;

	goto silence;
      }

      if(h != NULL)
	snd_pcm_close(h);

      cur_format = ab->ab_format;
      cur_rate   = ab->ab_rate;

      pts = AV_NOPTS_VALUE;

      if((h = alsa_open(aam, cur_format, cur_rate)) == NULL) {
	ret = -1;
	break;
      }
    }


    switch(cur_format) {

    case AM_FORMAT_AC3:
      outlen = iec958_build_ac3frame((void *)ab->ab_data, 
				     ab->ab_frames, tmpbuf);
      outbuf = tmpbuf;
      break;

    case AM_FORMAT_DTS:
      outlen = iec958_build_dtsframe((void *)ab->ab_data,
				     ab->ab_frames, tmpbuf);
      outbuf = tmpbuf;
      break;

    default:
      outbuf = (void *)ab->ab_data;
      outlen = ab->ab_frames;
      break;

    }

    silence_threshold = 500; /* About 5 seconds */
    
    c = snd_pcm_wait(h, 100);
    if(c >= 0) 
      c = snd_pcm_avail_update(h);

    if(c == -EPIPE)
      snd_pcm_prepare(h);


    if(outlen > 0) {

      /* PTS is the time of the first frame of this audio packet */

      if((pts = ab->ab_pts) != AV_NOPTS_VALUE && ab->ab_mp != NULL) {

	/* snd_pcm_delay returns number of frames between the software
	   pointer and to the start of the hardware pointer.
	   Ie. the current delay in the soundcard */


	if(snd_pcm_delay(h, &fr))
	  fr = 0; /* failed */

	/* Convert the frame delay into micro seconds */

	d = (fr * 1000 / aam->aam_sample_rate) * 1000;

	/* Add it to our timestamp */
	pts += d;

	ab->ab_mp->mp_audio_clock = pts;
	ab->ab_mp->mp_audio_clock_valid = 1;
      }

      snd_pcm_writei(h, outbuf, outlen);

    }
    ab_free(ab);
  }

  free(tmpbuf);
  return ret;
}

/**
 *
 */
static int
alsa_probe(const char *card, const char *dev)
{
  snd_pcm_hw_params_t *hwp;
  snd_pcm_t *h;
  int r, is_iec958;
  snd_pcm_info_t *info;
  int formats = 0, rates = 0;
  const char *name;
  char buf[128];
  char longtitle[128];
  char id[128];
  alsa_audio_mode_t *aam;
  int i;

  info = alloca(snd_pcm_info_sizeof());

  fprintf(stderr, "\n===============================================\n"
	  "ALSA: probing device \"%s\"\n", dev);

  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
    printf("Unable to open -- %s\n", snd_strerror(r));
    return -1;
  }

  if(snd_pcm_info(h, info) < 0) {
    fprintf(stderr, "Unable to obtain info -- %s\n", snd_strerror(r));
    snd_pcm_close(h);
    return -1;
  }

  snprintf(id, sizeof(id), "alsa.%s", dev);

  name = snd_pcm_info_get_name(info);

  snprintf(longtitle, sizeof(longtitle), "Alsa - %s", name);

  fprintf(stderr, "Device name: \"%s\"\n", name);

  hwp = alloca(snd_pcm_hw_params_sizeof());
  memset(hwp, 0, snd_pcm_hw_params_sizeof());

  if((r = snd_pcm_hw_params_any(h, hwp)) < 0) {
    fprintf(stderr, "Unable to query hw params -- %s\n", snd_strerror(r));
    snd_pcm_close(h);
    return -1;
  }

  if((r = snd_pcm_hw_params_set_access(h, hwp,
				       SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    fprintf(stderr, "No interleaved support -- %s\n", snd_strerror(r));
    snd_pcm_close(h);
    return -1;
  }

  if((r = snd_pcm_hw_params_set_format(h, hwp,
				       SND_PCM_FORMAT_S16_LE)) < 0) {
    fprintf(stderr, "No 16bit LE support -- %s\n", snd_strerror(r));
    snd_pcm_close(h);
    return -1;
  }

  if(!snd_pcm_hw_params_test_rate(h, hwp, 96000, 0))
    rates |= AM_SR_96000;

  if(!snd_pcm_hw_params_test_rate(h, hwp, 48000, 0))
    rates |= AM_SR_48000;

  if(!snd_pcm_hw_params_test_rate(h, hwp, 44100, 0))
    rates |= AM_SR_44100;

  if(!snd_pcm_hw_params_test_rate(h, hwp, 32000, 0))
    rates |= AM_SR_32000;

  if(!snd_pcm_hw_params_test_rate(h, hwp, 24000, 0))
    rates |= AM_SR_24000;

  if(rates == 0) {
    fprintf(stderr, "No 48kHz support\n");
    snd_pcm_close(h);
    return -1;
  }

  if(!snd_pcm_hw_params_test_channels(h, hwp, 2))
    formats |= AM_FORMAT_PCM_STEREO;
  
  if(!snd_pcm_hw_params_test_channels(h, hwp, 6))
    formats |= AM_FORMAT_PCM_5DOT1;
  
  if(!snd_pcm_hw_params_test_channels(h, hwp, 8))
    formats |= AM_FORMAT_PCM_7DOT1;

  if(formats == 0) {
    fprintf(stderr, "No usable channel configuration\n");
    snd_pcm_close(h);
    return -1;
  }

  is_iec958 =
    (strstr(dev, "iec958")  || strstr(dev, "IEC958") ||
     strstr(name, "iec958") || strstr(name, "IEC958")) &&
    formats == AM_FORMAT_PCM_STEREO && rates & AM_SR_48000;

  snd_pcm_close(h);

  if(is_iec958) {
    /* Test if we can output passthru as well*/

    fprintf(stderr, "Seems to be IEC859 (SPDIF), verifying passthru\n");


    snprintf(buf, sizeof(buf), "%s:AES0=0x2,AES1=0x82,AES2=0x0,AES3=0x2",
	     dev);

    if((r = snd_pcm_open(&h, buf, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
      fprintf(stderr, "SPDIF passthru not working\n");
      return 0;
    } else {
      snd_pcm_close(h);
      formats |= AM_FORMAT_DTS | AM_FORMAT_AC3;
    }
  }

  fprintf(stderr, "Ok%s\n", is_iec958 ? ", SPDIF" : "");

  aam = calloc(1, sizeof(alsa_audio_mode_t));
  aam->aam_head.am_formats = formats;
  aam->aam_head.am_sample_rates = rates;
  aam->aam_head.am_title = strdup(name);
  aam->aam_head.am_id = strdup(id);
  aam->aam_head.am_icon = strdup("icon://alsa.png");

  aam->aam_head.am_entry = alsa_audio_start;

  aam->aam_dev = strdup(dev);

  for(i = 0; i < 8; i++)
    aam->aam_head.am_swizzle[i] = i;

  TAILQ_INIT(&aam->aam_head.am_mixer_controllers);
  if(card != NULL)
    alsa_probe_mixer(card, &aam->aam_head);

  audio_mode_register(&aam->aam_head);

  return 0;
}


/**
 *
 */
static void
alsa_probe_devices(void)
{
  int err, cardNum = -1, devNum, subDevCount, i;
  snd_ctl_t *cardHandle;
  snd_pcm_info_t  *pcmInfo;
  char devname[64];
  char cardname[64];

  while(1) {

    // Get next sound card's card number. When "cardNum" == -1, then ALSA
    // fetches the first card
    if((err = snd_card_next(&cardNum)) < 0) {
      fprintf(stderr, "ALSA: Can't get the next card number: %s\n",
	      snd_strerror(err));
      break;
    }

    // No more cards? ALSA sets "cardNum" to -1 if so
    if (cardNum < 0)
      break;

    /* Open this card's control interface. We specify only the card
       number -- not any device nor sub-device too */

    snprintf(cardname, sizeof(cardname), "hw:%i", cardNum);
    if((err = snd_ctl_open(&cardHandle, cardname, 0)) < 0) {
      printf("ALSA: Can't open card %i: %s\n", cardNum, snd_strerror(err));
      continue;
    }


    // Start with the first wave device on this card
    devNum = -1;
    
    while(1) {

      // Get the number of the next wave device on this card
      if((err = snd_ctl_pcm_next_device(cardHandle, &devNum)) < 0) {
	fprintf(stderr, "ALSA: Can't get next wave device number: %s\n",
		snd_strerror(err));
	break;
      }

      /* No more wave devices on this card? ALSA sets "devNum" to -1
       * if so.  NOTE: It's possible that this sound card may have no
       * wave devices on it at all, for example if it's only a MIDI
       * card
       */
      if (devNum < 0)
	break;

      /* To get some info about the subdevices of this wave device (on
       * the card), we need a snd_pcm_info_t, so let's allocate one on
       * the stack
       */

      pcmInfo = alloca(snd_pcm_info_sizeof());
      memset(pcmInfo, 0, snd_pcm_info_sizeof());

      // Tell ALSA which device (number) we want info about
      snd_pcm_info_set_device(pcmInfo, devNum);
      
      // Get info on the wave outs of this device
      snd_pcm_info_set_stream(pcmInfo, SND_PCM_STREAM_PLAYBACK);
      
      i = -1;
      subDevCount = 1;

      // More subdevices?
      while (++i < subDevCount) {
	// Tell ALSA to fill in our snd_pcm_info_t with info on this subdevice
	snd_pcm_info_set_subdevice(pcmInfo, i);
	if ((err = snd_ctl_pcm_info(cardHandle, pcmInfo)) < 0)
	  continue;
	
	// Print out how many subdevices (once only)
	if(!i) {
	  subDevCount = snd_pcm_info_get_subdevices_count(pcmInfo);
	}

	if(subDevCount > 1)
	  snprintf(devname, sizeof(devname), "hw:%i,%i,%i", cardNum, devNum, i);
	else
	  snprintf(devname, sizeof(devname), "hw:%i,%i", cardNum, devNum);

	alsa_probe(cardname, devname);
      }
    }
    snd_ctl_close(cardHandle);
  }
  snd_config_update_free_global();
}

/**
 *
 */
void audio_alsa_init(void); /* Avoid warning */

void
audio_alsa_init(void)
{
  alsa_probe("default", "default");
  alsa_probe(NULL, "iec958");

  alsa_probe_devices();
}




/**
 *
 */
static float
alsa_mixer_volume_set(struct mixer_controller *mc, float in)
{
  alsa_mixer_controller_t *amc = (void *)mc;
  long v;
  float cur;

  /* Some audiocards seem to behave a bit strange at the bottom of its
     defined volume range, so we don't allow values lower than 
     'min + 3dB' */

#define ALSA_MIXER_BOTTOM_MARGIN 3 /* dB */

  if(in < mc->mc_min + ALSA_MIXER_BOTTOM_MARGIN) {
    v = (mc->mc_min + ALSA_MIXER_BOTTOM_MARGIN) * 100.0;
  } else if(in > mc->mc_max) {
    v = mc->mc_max * 100.0;
  } else {
    v = in * 100.0;
  }

  if(snd_mixer_selem_set_playback_dB_all(amc->amc_elem, v, 0) < 0)
    return in;
  
  snd_mixer_selem_get_playback_dB(amc->amc_elem, 0, &v);
  cur = (float)v / 100.0;
  return in - cur;
}


/**
 *
 */
static int
alsa_mixer_mute_set(struct mixer_controller *mc, int mute)
{
  alsa_mixer_controller_t *amc = (void *)mc;

  return snd_mixer_selem_set_playback_switch_all(amc->amc_elem, !mute);
}

/**
 *
 */
static void
alsa_mixer_add_controller(audio_mode_t *am, snd_mixer_elem_t *elem)
{
  alsa_mixer_controller_t *amc;
  mixer_controller_t *mc;
  long min, max;
  char buf[30];
  snd_mixer_selem_id_t *sid = alloca(snd_mixer_selem_id_sizeof());
  
  snd_mixer_selem_get_id(elem, sid);

  amc = calloc(1, sizeof(alsa_mixer_controller_t));
  mc = &amc->h;

  snprintf(buf, sizeof(buf), "Alsa: %s", snd_mixer_selem_id_get_name(sid));
  mc->mc_title = strdup(buf);

  amc->amc_elem = elem;
  
  if(snd_mixer_selem_has_common_volume(elem) ||
     snd_mixer_selem_has_playback_volume(elem)) {

    amc->amc_joined = snd_mixer_selem_has_playback_volume_joined(elem);

    /* Is a volume controller */

    snd_mixer_selem_get_playback_dB_range(elem, &min, &max);

    mc->mc_min = (float)min / 100.0f;
    mc->mc_max = (float)max / 100.0f;

    snd_mixer_selem_get_playback_dB(elem, 0, &min);

    mc->mc_set_volume = alsa_mixer_volume_set;

    if(snd_mixer_selem_has_common_switch(elem) || 
       snd_mixer_selem_has_playback_switch(elem)) {
      mc->mc_set_mute = alsa_mixer_mute_set;
    }
    mc->mc_type = MC_TYPE_SLIDER;

    if(!strcmp("Master", snd_mixer_selem_id_get_name(sid))) {
      am->am_mixers[AM_MIXER_MASTER] = mc;
    }
  } else {
    return;
  } 

  TAILQ_INSERT_TAIL(&am->am_mixer_controllers, mc, mc_link);
}



/**
 *
 */
static int
alsa_probe_mixer(const char *device, audio_mode_t *am)
{
  int err;
  snd_mixer_t *handle;
  snd_mixer_elem_t *elem;
	
  if((err = snd_mixer_open(&handle, 0)) < 0) {
    fprintf(stderr, 
	    "Mixer %s open error: %s\n", device, snd_strerror(err));
    return err;
  }

  if((err = snd_mixer_attach(handle, device)) < 0) {
    fprintf(stderr, "Mixer attach %s error: %s",
	    device, snd_strerror(err));
    snd_mixer_close(handle);
    return err;
  }

  if((err = snd_mixer_selem_register(handle, NULL, NULL)) < 0) {
    fprintf(stderr, 
	    "Mixer %s register error: %s", device, snd_strerror(err));
    snd_mixer_close(handle);
    return err;
  }

  if((err = snd_mixer_load(handle)) < 0) {
    fprintf(stderr, 
	    "Mixer %s load error: %s", device, snd_strerror(err));
    snd_mixer_close(handle);
    return err;
  }

  for(elem = snd_mixer_first_elem(handle); elem; 
      elem = snd_mixer_elem_next(elem)) {

    alsa_mixer_add_controller(am, elem);
  }
  return 0;
}
