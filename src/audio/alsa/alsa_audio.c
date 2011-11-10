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
#include <endian.h>
#include <alsa/asoundlib.h>

#include <libavutil/avutil.h>

#include "showtime.h"
#include "audio/audio_defs.h"
#include "audio/audio_iec958.h"

/* seams to only be defined in modern versions of endian.h */
#if !defined(htobe16) && !defined(htole16)
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define htobe16(x) ((((x) & 0xff00) >> 8) | (((x) & 0xff) << 8))
#  define htole16(x) (x)
# else
#  define htobe16(x) (x)
#  define htole16(x) ((((x) & 0xff00) >> 8) | (((x) & 0xff) << 8))
# endif
#endif

static hts_mutex_t alsa_mutex;

/**
 * Alsa representation of an audio mode
 */
typedef struct alsa_audio_mode {
  audio_mode_t aam_head;

  char *aam_dev;

  int aam_sample_rate;
  int aam_format;

} alsa_audio_mode_t;

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

  TRACE(TRACE_DEBUG, "ALSA", "Opening device %s", dev);

  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0))
    return NULL;

  hwp = alloca(snd_pcm_hw_params_sizeof());
  memset(hwp, 0, snd_pcm_hw_params_sizeof());
  snd_pcm_hw_params_any(h, hwp);

  snd_pcm_hw_params_set_access(h, hwp, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(h, hwp, aam->aam_format);

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

  TRACE(TRACE_DEBUG, "ALSA", "ALSA format S16_%s %dch %dHz, input format %s",
	aam->aam_format == SND_PCM_FORMAT_S16_BE ? "BE" : "LE", ch, rate,
	audio_format_to_string(format));

  snd_pcm_hw_params_set_channels(h, hwp, ch);

  /* Configurue period */

  dir = 0;
  snd_pcm_hw_params_get_period_size_min(hwp, &period_size_min, &dir);
  dir = 0;
  snd_pcm_hw_params_get_period_size_max(hwp, &period_size_max, &dir);

  //  period_size = period_size_max;

  period_size = 1024;

  TRACE(TRACE_DEBUG, "ALSA", "attainable period size %lu - %lu, trying %lu",
	  period_size_min, period_size_max, period_size);


  dir = 0;
  r = snd_pcm_hw_params_set_period_size_near(h, hwp, &period_size, &dir);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Unable to set period size %lu (%s)",
	  dev, period_size, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }


  dir = 0;
  r = snd_pcm_hw_params_get_period_size(hwp, &period_size, &dir);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Unable to get period size (%s)",
	  dev, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  /* Configurue buffer size */

  snd_pcm_hw_params_get_buffer_size_min(hwp, &buffer_size_min);
  snd_pcm_hw_params_get_buffer_size_max(hwp, &buffer_size_max);
  buffer_size = period_size * 4;

  TRACE(TRACE_DEBUG, "ALSA", "attainable buffer size %lu - %lu, trying %lu",
	buffer_size_min, buffer_size_max, buffer_size);

  dir = 0;
  r = snd_pcm_hw_params_set_buffer_size_near(h, hwp, &buffer_size);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Unable to set buffer size %lu (%s)",
	  dev, buffer_size, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  r = snd_pcm_hw_params_get_buffer_size(hwp, &buffer_size);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Unable to get buffer size (%s)",
	  dev, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }


  /* write the hw params */
  r = snd_pcm_hw_params(h, hwp);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", 
	  "%s: Unable to configure hardware parameters (%s)",
	  dev, snd_strerror(r));
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
    TRACE(TRACE_ERROR, "ALSA", "%s: Unable to configure wakeup threshold (%s)",
	  dev, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  r = snd_pcm_sw_params_set_xfer_align(h, swp, 1);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Unable to configure xfer alignment (%s)",
	  dev, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }


  snd_pcm_sw_params_set_start_threshold(h, swp, 0);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Unable to configure start threshold (%s)",
	  dev, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }
  
  r = snd_pcm_sw_params(h, swp);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Cannot set soft parameters (%s)", 
	  dev, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  r = snd_pcm_prepare(h);
  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "%s: Cannot prepare audio for playback (%s)", 
	  dev, snd_strerror(r));
    snd_pcm_close(h);
    return NULL;
  }

  aam->aam_head.am_preferred_size = period_size;
 
  TRACE(TRACE_DEBUG, "ALSA", "period size = %ld", period_size);
  TRACE(TRACE_DEBUG, "ALSA", "buffer size = %ld", buffer_size);

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
 * Convert dB to amplitude scale factor (-6dB ~= half volume)
 */
static void
set_mastervol(void *opaque, float value)
{
  int *ptr = opaque;
  int v;
  v = 65536 * pow(10, (value / 20));
  if(v > 65535)
    v = 65535;
  *ptr = v;
}


/**
 * Set mute flag
 */
static void
set_mastermute(void *opaque, int v)
{
  int *ptr = opaque;
  *ptr = v;
}

/**
 *
 */
static void
conv_s16_be(int16_t *outbuf, int frames, int channels, int mastervol)
{
  int i;
  for(i = 0; i < frames * channels; i++) {
    outbuf[i] = htobe16(CLIP16((outbuf[i] * mastervol) >> 16));
    outbuf[i] = htobe16(CLIP16((outbuf[i] * mastervol) >> 16));
  }
}


/**
 *
 */
static void
conv_s16_le(int16_t *outbuf, int frames, int channels, int mastervol)
{
  int i;
  for(i = 0; i < frames * channels; i++) {
    outbuf[i] = htole16(CLIP16((outbuf[i] * mastervol) >> 16));
    outbuf[i] = htole16(CLIP16((outbuf[i] * mastervol) >> 16));
  }
}


/**
 *
 */
static void
conv_s16_be_6ch(int16_t *outbuf, int frames, int channels, int mastervol)
{
  int i;
  int16_t t1, t2;
  for(i = 0; i < frames * 6; i+=6) {
    outbuf[i+0] = htobe16(CLIP16((outbuf[i+0] * mastervol) >> 16));
    outbuf[i+1] = htobe16(CLIP16((outbuf[i+1] * mastervol) >> 16));
    t1          = htobe16(CLIP16((outbuf[i+2] * mastervol) >> 16));
    t2          = htobe16(CLIP16((outbuf[i+3] * mastervol) >> 16));
    outbuf[i+2] = htobe16(CLIP16((outbuf[i+4] * mastervol) >> 16));
    outbuf[i+3] = htobe16(CLIP16((outbuf[i+5] * mastervol) >> 16));
    outbuf[i+4] = t1;
    outbuf[i+5] = t2;
  }
}


/**
 *
 */
static void
conv_s16_le_6ch(int16_t *outbuf, int frames, int channels, int mastervol)
{
  int i;
  int16_t t1, t2;
  for(i = 0; i < frames * 6; i+=6) {
    outbuf[i+0] = htole16(CLIP16((outbuf[i+0] * mastervol) >> 16));
    outbuf[i+1] = htole16(CLIP16((outbuf[i+1] * mastervol) >> 16));
    t1          = htole16(CLIP16((outbuf[i+2] * mastervol) >> 16));
    t2          = htole16(CLIP16((outbuf[i+3] * mastervol) >> 16));
    outbuf[i+2] = htole16(CLIP16((outbuf[i+4] * mastervol) >> 16));
    outbuf[i+3] = htole16(CLIP16((outbuf[i+5] * mastervol) >> 16));
    outbuf[i+4] = t1;
    outbuf[i+5] = t2;
  }
}


/**
 *
 */
static void
conv_s16_be_8ch(int16_t *outbuf, int frames, int channels, int mastervol)
{
  int i;
  int16_t t1, t2;
  for(i = 0; i < frames * 8; i+=8) {
    outbuf[i+0] = htobe16(CLIP16((outbuf[i+0] * mastervol) >> 16));
    outbuf[i+1] = htobe16(CLIP16((outbuf[i+1] * mastervol) >> 16));
    t1          = htobe16(CLIP16((outbuf[i+2] * mastervol) >> 16));
    t2          = htobe16(CLIP16((outbuf[i+3] * mastervol) >> 16));
    outbuf[i+2] = htobe16(CLIP16((outbuf[i+4] * mastervol) >> 16));
    outbuf[i+3] = htobe16(CLIP16((outbuf[i+5] * mastervol) >> 16));
    outbuf[i+4] = t1;
    outbuf[i+5] = t2;
    outbuf[i+6] = htobe16(CLIP16((outbuf[i+6] * mastervol) >> 16));
    outbuf[i+7] = htobe16(CLIP16((outbuf[i+7] * mastervol) >> 16));
  }
}


/**
 *
 */
static void
conv_s16_le_8ch(int16_t *outbuf, int frames, int channels, int mastervol)
{
  int i;
  int16_t t1, t2;
  for(i = 0; i < frames * 8; i+=8) {
    outbuf[i+0] = htole16(CLIP16((outbuf[i+0] * mastervol) >> 16));
    outbuf[i+1] = htole16(CLIP16((outbuf[i+1] * mastervol) >> 16));
    t1          = htole16(CLIP16((outbuf[i+2] * mastervol) >> 16));
    t2          = htole16(CLIP16((outbuf[i+3] * mastervol) >> 16));
    outbuf[i+2] = htole16(CLIP16((outbuf[i+4] * mastervol) >> 16));
    outbuf[i+3] = htole16(CLIP16((outbuf[i+5] * mastervol) >> 16));
    outbuf[i+4] = t1;
    outbuf[i+5] = t2;
    outbuf[i+6] = htole16(CLIP16((outbuf[i+6] * mastervol) >> 16));
    outbuf[i+7] = htole16(CLIP16((outbuf[i+7] * mastervol) >> 16));
  }
}





/**
 *
 */
static int
alsa_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  snd_pcm_t *h = NULL; 
  media_pipe_t *mp;
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
  int mastervol = 0;
  int mastermute = 0;

  prop_sub_t *s_vol;
  prop_sub_t *s_mute;

  typedef void (conv_fn_t)(int16_t *buf, int frames, int channels, int mastervol);
  conv_fn_t *fn = NULL;

  hts_mutex_lock(&alsa_mutex);
  
  s_vol =
    prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_CALLBACK_FLOAT, set_mastervol, &mastervol,
		   PROP_TAG_ROOT, prop_mastervol,
		   PROP_TAG_MUTEX, &alsa_mutex,
		   NULL);

  s_mute =
    prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_CALLBACK_INT, set_mastermute, &mastermute,
		   PROP_TAG_ROOT, prop_mastermute,
		   PROP_TAG_MUTEX, &alsa_mutex,
		   NULL);

  hts_mutex_unlock(&alsa_mutex);

  tmpbuf = calloc(1, IEC958_MAX_FRAME_SIZE);

  while(1) {

    ab = af_deq2(af, h == NULL, am); /* wait if PCM device is not open */

    if(ab == AF_EXIT) {
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

	TRACE(TRACE_DEBUG, "ALSA", "No output, closing device");
	snd_pcm_close(h);
	h = NULL;
      }
      continue;
    }

    if(h == NULL ||
       ab->ab_format != cur_format ||
       ab->ab_samplerate != cur_rate) {

      if(!(ab->ab_format & am->am_formats)) {
	/* Rate / format is not supported by this mode */
	ab_free(ab);
	if(h == NULL)
	  continue;

	goto silence;
      }

      if(h != NULL)
	snd_pcm_close(h);

      cur_format = ab->ab_format;
      cur_rate   = ab->ab_samplerate;

      pts = AV_NOPTS_VALUE;

      if((h = alsa_open(aam, cur_format, cur_rate)) == NULL) {
	ret = -1;
	break;
      }

      if(aam->aam_format == SND_PCM_FORMAT_S16_BE) {
	if(cur_format == AM_FORMAT_PCM_5DOT1)
	  fn = conv_s16_be_6ch;
	else if(cur_format == AM_FORMAT_PCM_7DOT1)
	  fn = conv_s16_be_8ch;
	else
	  fn = conv_s16_be;
      } else {
	if(cur_format == AM_FORMAT_PCM_5DOT1)
	  fn = conv_s16_le_6ch;
	else if(cur_format == AM_FORMAT_PCM_7DOT1)
	  fn = conv_s16_le_8ch;
	else
	  fn = conv_s16_le;
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

      if(mastermute) {
	memset(outbuf, 0, ab->ab_frames * ab->ab_channels * sizeof(int16_t));
      } else {
	fn(outbuf, ab->ab_frames, ab->ab_channels, mastervol);
      }
      break;
    }

    silence_threshold = 500; /* About 5 seconds */
    
    c = snd_pcm_wait(h, 100);
    if(c >= 0) 
      c = snd_pcm_avail_update(h);

    if(c == -EPIPE)
      snd_pcm_prepare(h);


    if(outlen > 0) {

      snd_pcm_writei(h, outbuf, outlen);

      /* PTS is the time of the first frame of this audio packet */

      if((pts = ab->ab_pts) != AV_NOPTS_VALUE && ab->ab_mp != NULL) {

	/* snd_pcm_delay returns number of frames between the software
	   pointer and to the start of the hardware pointer.
	   Ie. the current delay in the soundcard */


	if(snd_pcm_delay(h, &fr))
	  fr = 0; /* failed */

	/* Convert the frame delay into micro seconds */

	d = (fr * 1000 / aam->aam_sample_rate) * 1000;

	/* Subtract it from our timestamp, this will yield
	   the PTS for the sample currently played */

	pts -= d;

	/* Offset with user configure delay */

	pts += am->am_audio_delay * 1000;

	mp = ab->ab_mp;

	hts_mutex_lock(&mp->mp_clock_mutex);
	mp->mp_audio_clock = pts;
	mp->mp_audio_clock_realtime = showtime_get_ts();
	mp->mp_audio_clock_epoch = ab->ab_epoch;

	hts_mutex_unlock(&mp->mp_clock_mutex);
      }
    }
    ab_free(ab);
  }

  hts_mutex_lock(&alsa_mutex);
  prop_unsubscribe(s_vol);
  prop_unsubscribe(s_mute);
  hts_mutex_unlock(&alsa_mutex);

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
#ifdef __BIG_ENDIAN__
  int alsa_formats[] = {SND_PCM_FORMAT_S16_BE, SND_PCM_FORMAT_S16_LE};
#else
  int alsa_formats[] = {SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S16_BE};
#endif
  int alsa_format;
  int i;
  char multich_controls = 0;

  info = alloca(snd_pcm_info_sizeof());

  TRACE(TRACE_DEBUG, "ALSA", "Probing device %s", dev);

  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
    TRACE(TRACE_DEBUG, "ALSA", "Probing unable to open %s (%s)", 
	  dev, snd_strerror(r));
    return -1;
  }

  if(snd_pcm_info(h, info) < 0) {
    TRACE(TRACE_DEBUG, "ALSA", 
	  "%s: Unable to obtain info (%s)", dev, snd_strerror(r));
    snd_pcm_close(h);
    return -1;
  }

  snprintf(id, sizeof(id), "alsa.%s", dev);

  name = snd_pcm_info_get_name(info);

  snprintf(longtitle, sizeof(longtitle), "Alsa - %s", name);

  TRACE(TRACE_DEBUG, "ALSA", "%s: Device name: \"%s\"", dev, name);

  hwp = alloca(snd_pcm_hw_params_sizeof());
  memset(hwp, 0, snd_pcm_hw_params_sizeof());

  if((r = snd_pcm_hw_params_any(h, hwp)) < 0) {
    TRACE(TRACE_DEBUG, "ALSA", 
	  "%s: Unable to query hw params (%s)", dev, snd_strerror(r));
    snd_pcm_close(h);
    return -1;
  }

  if((r = snd_pcm_hw_params_set_access(h, hwp,
				       SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    TRACE(TRACE_DEBUG, "ALSA", 
	  "%s: No interleaved support (%s)", dev, snd_strerror(r));
    snd_pcm_close(h);
    return -1;
  }

  for(i = 0; i < sizeof(alsa_formats)/sizeof(alsa_formats[0]); i++)
    if((r = snd_pcm_hw_params_set_format(h, hwp, alsa_formats[i])) == 0)
      break;
  if(r < 0) {
    TRACE(TRACE_DEBUG, "ALSA", 
	  "%s: No 16bit support (%s)", dev, snd_strerror(r));
    snd_pcm_close(h);

    return -1;
  }
  alsa_format = alsa_formats[i]; 

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

  if(!(rates & AM_SR_48000)) {
    TRACE(TRACE_DEBUG, "ALSA", "%s: No 48kHz support", dev);
    snd_pcm_close(h);
    return -1;
  }

  if(!snd_pcm_hw_params_test_channels(h, hwp, 2))
    formats |= AM_FORMAT_PCM_STEREO;
  
  if(!snd_pcm_hw_params_test_channels(h, hwp, 6)) {
    formats |= AM_FORMAT_PCM_5DOT1;
    multich_controls = 1;
  }
  if(!snd_pcm_hw_params_test_channels(h, hwp, 8)) {
    formats |= AM_FORMAT_PCM_7DOT1;
    multich_controls = 1;
  }

  if(formats == 0) {
    TRACE(TRACE_DEBUG, "ALSA", "%s: No usable channel configuration", dev);
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

    TRACE(TRACE_DEBUG, "ALSA", 
	  "%s: Seems to be IEC859 (SPDIF), verifying passthru", dev);


    snprintf(buf, sizeof(buf), "%s:AES0=0x2,AES1=0x82,AES2=0x0,AES3=0x2",
	     dev);

    if((r = snd_pcm_open(&h, buf, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
      TRACE(TRACE_DEBUG, "ALSA", 
	    "%s: SPDIF passthru not working", dev);
      return 0;
    } else {
      snd_pcm_close(h);
      formats |= AM_FORMAT_DTS | AM_FORMAT_AC3;
    }
  }

  TRACE(TRACE_DEBUG, "ALSA", 
	"%s: Ok%s", dev, is_iec958 ? ", SPDIF" : "");

  aam = calloc(1, sizeof(alsa_audio_mode_t));
  aam->aam_head.am_formats = formats;
  aam->aam_head.am_multich_controls = multich_controls;
  aam->aam_head.am_sample_rates = rates;
  aam->aam_head.am_title = strdup(name);
  aam->aam_head.am_id = strdup(id);

  aam->aam_head.am_entry = alsa_audio_start;

  aam->aam_dev = strdup(dev);
  aam->aam_format = alsa_format;

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
      TRACE(TRACE_DEBUG, "ALSA", "Can't get the next card number: %s",
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
      TRACE(TRACE_DEBUG, "ALSA", 
	    "Can't open card %i: %s", cardNum, snd_strerror(err));
      continue;
    }


    // Start with the first wave device on this card
    devNum = -1;
    
    while(1) {

      // Get the number of the next wave device on this card
      if((err = snd_ctl_pcm_next_device(cardHandle, &devNum)) < 0) {
	TRACE(TRACE_DEBUG, "ALSA", 
	      "Can't get next wave device number: %s",
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
void
audio_alsa_init(int have_pulse_audio)
{
  hts_mutex_init(&alsa_mutex);

  if(have_pulse_audio)
    alsa_probe("default", "default");
  alsa_probe(NULL, "iec958");

  alsa_probe_devices();
}
