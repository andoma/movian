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
#include "audio/audio_defs.h"

#include <ogc/irq.h>
#include <ogcsys.h>

#define ADMA_FRAMES       2048
#define ADMA_BUFFER_SIZE (ADMA_FRAMES * 4) /* 2048 stereo 16bit samples */

static uint8_t buffer[2][ADMA_BUFFER_SIZE] ATTRIBUTE_ALIGN(32);
static int buffer_size[2] = {ADMA_BUFFER_SIZE, ADMA_BUFFER_SIZE};
static int cur_buffer;
static lwpq_t audio_queue;
static int audio_vol;

static void 
switch_buffers(void)
{
  AUDIO_StopDMA();
  
  cur_buffer ^= 1;

  AUDIO_InitDMA((u32)buffer[cur_buffer], buffer_size[cur_buffer]);
  AUDIO_StartDMA();

  LWP_ThreadSignal(audio_queue);
}




/**
 * Convert dB to amplitude scale factor (-6dB ~= half volume)
 */
static void
set_mastervol(void *opaque, float value)
{
  int v;
  v = 65536 * pow(10, (value / 20));
  if(v > 65535)
    v = 65535;
  audio_vol = v;
}


static int
wii_audio_start(audio_mode_t *am, audio_fifo_t *af)
{
  audio_buf_t *ab;
  int tbuf = 0, i;
  uint32_t level;
  int64_t pts;
  media_pipe_t *mp;

  prop_sub_t *s_vol;

  s_vol =
    prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_CALLBACK_FLOAT, set_mastervol, NULL,
		   PROP_TAG_ROOT, prop_mastervol,
		   NULL);

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
      if(ab != NULL)
	ab_free(ab);
      break;
    }

    if(ab != NULL) {
      const int16_t *src = (const int16_t *)ab->ab_data;
      int16_t *dst = (int16_t *)buffer[!tbuf];
      for(i = 0; i < ADMA_BUFFER_SIZE / 2; i++)
	*dst++ = (*src++ * audio_vol) >> 16;

      /* PTS is the time of the first frame of this audio packet */

      if((pts = ab->ab_pts) != AV_NOPTS_VALUE && ab->ab_mp != NULL) {

#if 0
	/* Convert the frame delay into micro seconds */

	d = (fr * 1000 / aam->aam_sample_rate) * 1000;

	/* Subtract it from our timestamp, this will yield
	   the PTS for the sample currently played */

	pts -= d;

	/* Offset with user configure delay */
#endif

	pts += am->am_audio_delay * 1000;

	mp = ab->ab_mp;

	hts_mutex_lock(&mp->mp_clock_mutex);
	mp->mp_audio_clock = pts;
	mp->mp_audio_clock_realtime = showtime_get_ts();
	mp->mp_audio_clock_epoch = ab->ab_epoch;

	hts_mutex_unlock(&mp->mp_clock_mutex);

      }
      ab_free(ab);

    } else {
      memset(buffer[!tbuf], 0, ADMA_BUFFER_SIZE);
    }
    DCFlushRange(buffer[!tbuf], ADMA_BUFFER_SIZE);

  }

  AUDIO_StopDMA();
  AUDIO_RegisterDMACallback(NULL);
  LWP_CloseQueue(audio_queue);

  prop_unsubscribe(s_vol);

  return 0;
}



/**
 *
 */
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
