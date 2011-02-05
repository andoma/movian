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

#include "showtime.h"
#include "audio/audio_defs.h"

#define SHW64(X) (u32)(((u64)X)>>32), (u32)(((u64)X)&0xFFFFFFFF)

static float audio_vol;
static sys_event_queue_t snd_queue;
static u64	snd_queue_key;


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
fillBuffer(float *buf, audio_buf_t *ab)
{
  int i;
  const int16_t *src = (const int16_t *)ab->ab_data;

  for (i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    buf[i * 2 + 0] = (float)src[i * 2 + 0] / 32767.0;
    buf[i * 2 + 1] = (float)src[i * 2 + 1] / 32767.0;
  }
}


static void
fillBuffersilence(float *buf)
{
  int i;
  for (i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    buf[i * 2 + 0] = 0;
    buf[i * 2 + 1] = 0;
  }
}


static u32
playOneBlock(u64 *readIndex, float *audioDataStart, audio_buf_t *ab)
{
  u32 ret = 0;
  //get position of the hardware
  u64 current_block = *readIndex;
  
  u32 audio_block_index = (current_block + 1) % AUDIO_BLOCK_8;
  
  sys_event_t event;
  ret = sys_event_queue_receive( snd_queue, &event, 20 * 1000);
  
  //get position of the block to write
  float *buf = audioDataStart + 2 /*channelcount*/ * AUDIO_BLOCK_SAMPLES * audio_block_index;
  if(ab == NULL) {
    fillBuffersilence(buf);
  } else {
    fillBuffer(buf, ab);
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
  int running = 0;

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

    if(!running) {
      AudioPortParam params;

      params.numChannels = AUDIO_PORT_2CH;
      params.numBlocks = AUDIO_BLOCK_8;
      params.attr = 0;
      params.level = 1;
  
      ret = audioPortOpen(&params, &port_num);

      TRACE(TRACE_DEBUG, "AUDIO", "PS3 audio port %d opened", port_num);

      ret = audioGetPortConfig(port_num, &config);
      TRACE(TRACE_DEBUG, "AUDIO", "audioGetPortConfig: %d\n",ret);
      TRACE(TRACE_DEBUG, "AUDIO", "  readIndex: 0x%8X\n",config.readIndex);
      TRACE(TRACE_DEBUG, "AUDIO", "  status: %d\n",config.status);
      TRACE(TRACE_DEBUG, "AUDIO", "  channelCount: %ld\n",config.channelCount);
      TRACE(TRACE_DEBUG, "AUDIO", "  numBlocks: %ld\n",config.numBlocks);
      TRACE(TRACE_DEBUG, "AUDIO", "  portSize: %d\n",config.portSize);
      TRACE(TRACE_DEBUG, "AUDIO", "  audioDataStart: 0x%8X\n",config.audioDataStart);

      // create an event queue that will tell when a block is read
      ret = audioCreateNotifyEventQueue(&snd_queue, &snd_queue_key);
      TRACE(TRACE_DEBUG, "AUDIO", "audioCreateNotifyEventQueue: %d\n",ret);
      TRACE(TRACE_DEBUG, "AUDIO", "  snd_queue: 0x%08X.%08X\n",SHW64(snd_queue));
      TRACE(TRACE_DEBUG, "AUDIO", "  snd_queue_key: 0x%08X.%08X\n", SHW64(snd_queue_key));
  
      // Set it to the sprx
      ret = audioSetNotifyEventQueue(snd_queue_key);
      TRACE(TRACE_DEBUG, "AUDIO", "audioSetNotifyEventQueue: %d\n",ret);
      TRACE(TRACE_DEBUG, "AUDIO", "  snd_queue_key: 0x%08X.%08X\n",SHW64(snd_queue_key));
  
      // clears the event queue
      ret = sys_event_queue_drain(snd_queue);
      TRACE(TRACE_DEBUG, "AUDIO", "sys_event_queue_drain: %d\n",ret);


      ret=audioPortStart(port_num);
      TRACE(TRACE_DEBUG, "AUDIO", "audioPortStart: %d\n",ret);

      running = 1;
    }
    
    playOneBlock((u64*)(u64)config.readIndex,
		 (float*)(u64)config.audioDataStart,
		 ab);


    if(ab != NULL)
      ab_free(ab);
  }
  TRACE(TRACE_DEBUG, "AUDIO", "leaving the loop");

  if(running) {

    //shutdown in reverse order
    ret=audioPortStop(port_num);
    TRACE(TRACE_DEBUG, "AUDIO", "audioPortStop: %d\n",ret);
    ret=audioRemoveNotifyEventQueue(snd_queue_key);
    TRACE(TRACE_DEBUG, "AUDIO", "audioRemoveNotifyEventQueue: %d\n",ret);
    ret=audioPortClose(port_num);
    TRACE(TRACE_DEBUG, "AUDIO", "audioPortClose: %d\n",ret);
    ret=sys_event_queue_destroy(snd_queue, 0);
    TRACE(TRACE_DEBUG, "AUDIO", "sys_event_queue_destroy: %d\n",ret);
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
  audio_mode_t *am;


  am = calloc(1, sizeof(audio_mode_t));
  /* Absolute minimum requirements */
  am->am_formats = AM_FORMAT_PCM_STEREO;
  am->am_sample_rates = AM_SR_48000;
  am->am_title = strdup("PS3");
  am->am_id = strdup("ps3");
  am->am_preferred_size = AUDIO_BLOCK_SAMPLES;

  am->am_entry = ps3_audio_start;

  audio_mode_register(am);
}
