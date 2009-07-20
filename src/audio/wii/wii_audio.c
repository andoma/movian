/*
 *  Wii audio output
 *  Copyright (C) 2009 Andreas Öman
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

#include "showtime.h"
#include "audio/audio.h"

#include <ogc/irq.h>
#include <ogcsys.h>

#define ADMA_FRAMES       2048
#define ADMA_BUFFER_SIZE (ADMA_FRAMES * 4) /* 2048 stereo 16bit samples */

static uint8_t buffer[2][ADMA_BUFFER_SIZE] ATTRIBUTE_ALIGN(32);
static int buffer_size[2] = {ADMA_BUFFER_SIZE, ADMA_BUFFER_SIZE};
static int cur_buffer;
static lwpq_t audio_queue;

static void 
switch_buffers(void)
{
  AUDIO_StopDMA();
  
  cur_buffer ^= 1;

  AUDIO_InitDMA((u32)buffer[cur_buffer], buffer_size[cur_buffer]);
  AUDIO_StartDMA();

  LWP_ThreadSignal(audio_queue);
}



static int
wii_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  audio_buf_t *ab;
  int tbuf = 0;
  uint32_t level;

  LWP_InitQueue(&audio_queue);

  AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
  AUDIO_RegisterDMACallback(switch_buffers);

  AUDIO_StartDMA();

  while(1) {
    
    level = IRQ_Disable();
    while(tbuf == cur_buffer)
      LWP_ThreadSleep(audio_queue);
    tbuf = cur_buffer;
    IRQ_Restore(level);

    ab = af_deq(af, 0);
    
    if(am != audio_mode_current) {
      /* We're not the selected audio output anymore, return. */
      ab_free(ab);
      break;
    }

    if(ab != NULL) {
      memcpy(buffer[!tbuf], ab->ab_data, ADMA_BUFFER_SIZE);
    ab_free(ab);
    } else {
      memset(buffer[!tbuf], 0, ADMA_BUFFER_SIZE);
    }
    DCFlushRange(buffer[!tbuf], ADMA_BUFFER_SIZE);

  }

  AUDIO_StopDMA();
  AUDIO_RegisterDMACallback(NULL);
  LWP_CloseQueue(audio_queue);

  return 0;
}



/**
 *
 */
void audio_wii_init(void);

void
audio_wii_init(void)
{
  audio_mode_t *am;

  am = calloc(1, sizeof(audio_mode_t));
  /* Absolute minimum requirements */
  am->am_formats = AM_FORMAT_PCM_STEREO;
  am->am_sample_rates = AM_SR_48000;
  am->am_title = strdup("Nintendo Wii");
  am->am_id = strdup("wii");
  am->am_preferred_size = ADMA_FRAMES;

  am->am_entry = wii_audio_start;

  audio_mode_register(am);
}
