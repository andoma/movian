/*
 *  Ps3 audio output
 *  Copyright (C) 2011 Andreas Öman
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <audio/audio.h>
#include <psl1ght/lv2/timer.h>
#include <sysutil/audio.h>

#include <libavutil/avutil.h>

#include "showtime.h"
#include "audio/audio_defs.h"

static float audio_vol;


static int max_pcm;
static int max_dts;
static int max_ac3;

/**
 * Convert dB to amplitude scale factor (-6dB ~= half volume)
 */
static void
set_mastervol(void *opaque, float value)
{
  float v = pow(10, (value / 20));
  audio_vol = v;
}

static void
fillBuffersilence(float *buf, int channels)
{
  memset(buf, 0, sizeof(float) * channels);
}


static void
copy_buf_int16(float *buf, const audio_buf_t *ab, int channels)
{
  int i;
  const int16_t *src = (const int16_t *)ab->ab_data;

  if(ab->ab_channels == channels) {
    for (i = 0; i < AUDIO_BLOCK_SAMPLES * channels; i++)
      *buf++ = (float)src[i] / 32767.0;

  } else if(ab->ab_channels == 6 && channels == 8) {
    for (i = 0; i < AUDIO_BLOCK_SAMPLES * 6; i+=6) {
      *buf++ = (float)src[i+0] / 32767.0;
      *buf++ = (float)src[i+1] / 32767.0;
      *buf++ = (float)src[i+2] / 32767.0;
      *buf++ = (float)src[i+3] / 32767.0;
      *buf++ = (float)src[i+4] / 32767.0;
      *buf++ = (float)src[i+5] / 32767.0;
      *buf++ = 0;
      *buf++ = 0;
    }
  } else {
    fillBuffersilence(buf, channels);
  }
}


static void
copy_buf_float(float *buf, const audio_buf_t *ab, int channels)
{
  int i;
  const float *src = (const float *)ab->ab_data;

  if(ab->ab_channels == 1 && channels == 2) {
    for (i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      *buf++ = src[i+0];
      *buf++ = src[i+0];
    }
    return;
  }

  if(ab->ab_channels == 5 && channels == 8) {
    for (i = 0; i < AUDIO_BLOCK_SAMPLES * 5; i+=5) {
      *buf++ = src[i+0];
      *buf++ = src[i+1];
      *buf++ = src[i+2];
      *buf++ = 0;
      *buf++ = src[i+3];
      *buf++ = src[i+4];
      *buf++ = 0;
      *buf++ = 0;
    }
    return;
  }

  if(ab->ab_channels == 6 && channels == 8) {
    for (i = 0; i < AUDIO_BLOCK_SAMPLES * 6; i+=6) {
      *buf++ = src[i+0];
      *buf++ = src[i+1];
      *buf++ = src[i+2];
      *buf++ = src[i+3];
      *buf++ = src[i+4];
      *buf++ = src[i+5];
      *buf++ = 0;
      *buf++ = 0;
    }
    return;
  }


  if(ab->ab_channels == 7 && channels == 8) {
    // 5.1 + rear center
    for (i = 0; i < AUDIO_BLOCK_SAMPLES * 7; i+=7) {
      *buf++ = src[i+0];
      *buf++ = src[i+1];
      *buf++ = src[i+2];
      *buf++ = src[i+3];
      *buf++ = src[i+5];
      *buf++ = src[i+6];
      *buf++ = src[i+4] * 0.707;
      *buf++ = src[i+4] * 0.707;
    }
    return;
  }

  if(ab->ab_channels == 8 && channels == 8) {
    for (i = 0; i < AUDIO_BLOCK_SAMPLES * 8; i+=8) {
      *buf++ = src[i+0];
      *buf++ = src[i+1];
      *buf++ = src[i+2];
      *buf++ = src[i+3];
      *buf++ = src[i+6];
      *buf++ = src[i+7];
      *buf++ = src[i+4];
      *buf++ = src[i+5];
    }
    return;
  }

  if(ab->ab_channels == channels) {
    memcpy(buf, src, sizeof(float) * AUDIO_BLOCK_SAMPLES * channels);
  } else {
    fillBuffersilence(buf, channels);
  }
}




static u32
playOneBlock(u64 *readIndex, float *dst,
	     const audio_mode_t *am, const audio_buf_t *ab,
	     sys_event_queue_t snd_queue, int channels)
{
  u32 ret = 0;
  u64 current_block = *readIndex;
  int64_t pts;
  u32 audio_block_index = (current_block + 1) % AUDIO_BLOCK_8;
  
  sys_event_t event;

  ret = sys_event_queue_receive(snd_queue, &event, 20 * 1000);
  
  float *buf = dst + channels * AUDIO_BLOCK_SAMPLES * audio_block_index;

  if(ab == NULL) {
    fillBuffersilence(buf, channels);
  } else {
    
    if(ab->ab_isfloat)
      copy_buf_float(buf, ab, channels);
    else
      copy_buf_int16(buf, ab, channels);

    if((pts = ab->ab_pts) != AV_NOPTS_VALUE && ab->ab_mp != NULL) {
      pts += am->am_audio_delay * 1000;

      pts -= 1000000LL * (AUDIO_BLOCK_SAMPLES * AUDIO_BLOCK_8) / 48000;

      media_pipe_t *mp = ab->ab_mp;

      hts_mutex_lock(&mp->mp_clock_mutex);
      mp->mp_audio_clock = pts;
      mp->mp_audio_clock_realtime = showtime_get_ts();
      mp->mp_audio_clock_epoch = ab->ab_epoch;

      hts_mutex_unlock(&mp->mp_clock_mutex);
    }
  }
  return 0;
}


static int
ps3_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  prop_sub_t *s_vol;
  audio_buf_t *ab;

  u32 port_num;

  AudioPortConfig config;

  int ret;
  int cur_channels = 0;
  int running = 0;

  sys_event_queue_t snd_queue;
  u64 snd_queue_key;
  int achannels = 0;

  if(audioInit())
    return -1;

  s_vol = prop_subscribe(PROP_SUB_DIRECT_UPDATE,
			 PROP_TAG_CALLBACK_FLOAT, set_mastervol, NULL,
			 PROP_TAG_ROOT, prop_mastervol,
			 NULL);

  TRACE(TRACE_DEBUG, "AUDIO", "PS3 audio system initialized");

  while(1) {
    ab = af_deq2(af, !running, am);
    if(ab == AF_EXIT) {
      ab = NULL;
      break;
    }

    if(ab != NULL) {

      if(ab->ab_channels != cur_channels) {
      
	if(running) {
	  audioPortStop(port_num);
	  audioRemoveNotifyEventQueue(snd_queue_key);
	  audioPortClose(port_num);
	  sys_event_queue_destroy(snd_queue, 0);
	  running = 0;
	}

	cur_channels = ab->ab_channels;

	AudioOutConfiguration conf;
	memset(&conf, 0, sizeof(conf));

	switch(cur_channels) {
	case 1:
	case 2:
	  achannels = 2;
	  conf.channel = 2;
	  conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
	  break;

	case 5:
	case 6:
	  achannels = 8;
	  if(max_pcm >= 6) {
	    conf.channel = 6;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
	  } else if(max_dts == 6) {
	    conf.channel = 6;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_DTS;
	  } else if(max_ac3 == 6) {
	    conf.channel = 6;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_AC3;
	  } else {
	    conf.channel = 2;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
	    conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_A;
	  }
	  break;

	case 7:
	case 8:
	  achannels = 8;
	  if(max_pcm == 8) {
	    conf.channel = 8;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
	  } else if(max_dts == 6) {
	    conf.channel = 6;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_DTS;
	    conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_B;
	  } else if(max_ac3 == 6) {
	    conf.channel = 6;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_AC3;
	    conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_B;
	  } else {
	    conf.channel = 2;
	    conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
	    conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_A;
	  }
	  break;
	}



	int r;
	r = audioOutConfigure(AUDIO_OUT_PRIMARY, &conf, NULL, 1);
	if(r == 0) {
	  int i;
	  for(i = 0; i < 100;i++) {
	    AudioOutState state;
	    r = audioOutGetState(AUDIO_OUT_PRIMARY, 0, &state );
	    if(r != 0)
	      break;
	    TRACE(TRACE_DEBUG, "AUDIO", "The state is %d", state.state);
	    if(state.state == 2)
	      continue;
	    usleep(100);
	    break;
	  }
	}

	AudioPortParam params;

	params.numChannels = achannels;
	params.numBlocks = AUDIO_BLOCK_8;
	params.attr = 0;
	params.level = 1;
	
	ret = audioPortOpen(&params, &port_num);

	TRACE(TRACE_DEBUG, "AUDIO", "PS3 audio port %d opened", port_num);
	
	audioGetPortConfig(port_num, &config);
	audioCreateNotifyEventQueue(&snd_queue, &snd_queue_key);
	audioSetNotifyEventQueue(snd_queue_key);
	sys_event_queue_drain(snd_queue);
	audioPortStart(port_num);
	
	running = 1;
      }
    }
    
    playOneBlock((u64*)(u64)config.readIndex,
		 (float*)(u64)config.audioDataStart,
		 am, ab, snd_queue, achannels);

    
    if(ab != NULL)
      ab_free(ab);
  }
  TRACE(TRACE_DEBUG, "AUDIO", "leaving the loop");

  if(running) {
    audioPortStop(port_num);
    audioRemoveNotifyEventQueue(snd_queue_key);
    audioPortClose(port_num);
    sys_event_queue_destroy(snd_queue, 0);
  }

  audioQuit();
  prop_unsubscribe(s_vol);
  return 0;
}




/**
 *
 */
void
audio_ps3_init(void)
{
  audio_mode_t *am = calloc(1, sizeof(audio_mode_t));

  am->am_formats = 
    AM_FORMAT_PCM_MONO |
    AM_FORMAT_PCM_STEREO | AM_FORMAT_PCM_5DOT1 | 
    AM_FORMAT_PCM_6DOT1  | AM_FORMAT_PCM_7DOT1;
  am->am_sample_rates = AM_SR_48000;

  max_pcm = audioOutGetSoundAvailability(AUDIO_OUT_PRIMARY,
					 AUDIO_OUT_CODING_TYPE_LPCM,
					 AUDIO_OUT_FS_48KHZ,
					 0);
  
  max_dts = audioOutGetSoundAvailability(AUDIO_OUT_PRIMARY,
					 AUDIO_OUT_CODING_TYPE_DTS,
					 AUDIO_OUT_FS_48KHZ,
					 0);

  max_ac3 = audioOutGetSoundAvailability(AUDIO_OUT_PRIMARY,
					 AUDIO_OUT_CODING_TYPE_AC3,
					 AUDIO_OUT_FS_48KHZ,
					 0);

  audioOutSetCopyControl(AUDIO_OUT_PRIMARY, AUDIO_OUT_COPY_CONTROL_FREE);

  /* Absolute minimum requirements */
  am->am_title = strdup("PS3");
  am->am_id = strdup("ps3");
  am->am_preferred_size = AUDIO_BLOCK_SAMPLES;

  am->am_entry = ps3_audio_start;
  am->am_float = 1;

  audio_mode_register(am);
}
