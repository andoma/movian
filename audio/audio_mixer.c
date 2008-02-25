/*
 *  Audio mixer
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
#include "audio_mixer.h"
#include "audio_compressor.h"
#include "menu.h"
#include "layout/layout.h"

audio_fifo_t mixer_output_fifo;

int mixer_hw_output_delay;
int mixer_hw_output_formats;

float mixer_output_matrix[AUDIO_MIXER_MAX_CHANNELS]
                         [AUDIO_MIXER_MAX_CHANNELS];

pthread_mutex_t audio_source_lock = PTHREAD_MUTEX_INITIALIZER;

audio_mixer_t mixer_output;

LIST_HEAD(, audio_source) audio_sources;


static void
audio_source_update_delay(audio_source_t *as, audio_fifo_t *dst_fifo)
{
  int d = mixer_hw_output_delay + dst_fifo->avgdelay + as->as_fifo.avgdelay;

  if(as->as_avg_delay == 0) {
    as->as_avg_delay = d;
  } else {
    as->as_avg_delay = (as->as_avg_delay * 31.0 + d) / 32.0f;
  }
}


/*
 * Output mixer thread
 */

static void *
mixer_thread(void *aux)
{
  audio_source_t *as;
  audio_fifo_t *dst_fifo = &mixer_output_fifo;
  audio_buf_t *dst_buf, *passthru;
  int16_t *dst16;
  float *dst, o;
  int i, j, v;

  pthread_mutex_lock(&audio_source_lock);

  LIST_HEAD(, audio_source) mixlist;

  while(1) {

     /* Check which audio sources that actually has something
       to deliver for us */

    LIST_INIT(&mixlist);

    passthru = NULL;

    LIST_FOREACH(as, &audio_sources, as_link) {

      /* Set target gain of currently selected audio source to 1.0 */

      if(as->as_mp == primary_audio) {
	as->as_target_gain = 1.0f;
      } else {
	as->as_target_gain = 0.0f;
      }

      if((as->as_src_buf = af_deq(&as->as_fifo, 0)) != NULL) {

	if(as->as_src_buf->payload_type != AUDIO_OUTPUT_PCM) {
	  /* This source carries non PCM audio */

	  if(as->as_mp == primary_audio) {
	    /* it's primary, send it thru */
	    passthru = as->as_src_buf;
	    audio_source_update_delay(as, dst_fifo);
	  } else {
	    /* nope, free it (we cant mix it anyway), there should not
	       be much of these (non-primary non-pcm) packets. The
	       decoders will not send non-pcm if it is not primary,
	       but there might be transient conditions */
	    af_free(as->as_src_buf);
	  }
	  continue;
	}

	/* It's PCM audio, link source to list of sources that we will
	   mix for this round */

	LIST_INSERT_HEAD(&mixlist, as, as_tmplink);
	as->as_src = ab_dataptr(as->as_src_buf);
      }
    }

    /* Do actual mixing */

    dst = mixer_output.buffer;

    as = LIST_FIRST(&mixlist);

    if(as == NULL) {
      memset(dst, 0, sizeof(float) * mixer_output.words);
    } else {
      for(i = 0; i < mixer_output.period_size; i++) {
	for(j = 0; j < mixer_output.channels; j++) {
	  *dst++ = *as->as_src++ * as->as_gain;
	}
	as->as_gain = (as->as_gain * 999. + as->as_target_gain) / 1000.;
      }

      as = LIST_NEXT(as, as_tmplink);
      for(; as != NULL; as = LIST_NEXT(as, as_tmplink)) {
	dst = mixer_output.buffer;
	for(i = 0; i < mixer_output.period_size; i++) {
	  for(j = 0; j < mixer_output.channels; j++) {
	    *dst++ += *as->as_src++ * as->as_gain;
	  }
	  as->as_gain = (as->as_gain * 1999. + as->as_target_gain) / 2000.;
	}
      }
    }

    /* Compute average buffer delay for each source and free source buffers */

    LIST_FOREACH(as, &mixlist, as_tmplink) {
      audio_source_update_delay(as, dst_fifo);
      af_free(as->as_src_buf);
    }

    pthread_mutex_unlock(&audio_source_lock);

    if(passthru != NULL) {
      af_enq(dst_fifo, passthru);

    } else {
      /* We have PCM that should be delivered */


      /* Do post processing effects */

      if(post_mixer_compressor.mode)
	audio_compressor(mixer_output.buffer, &post_mixer_compressor,
			 &mixer_output);

      dst = mixer_output.buffer;

      dst_buf = af_alloc(dst_fifo);
      dst_buf->payload_type = AUDIO_OUTPUT_PCM;
      dst16 = ab_dataptr(dst_buf);

      for(i = 0; i < mixer_output.words; i++) {
	o = *dst++;
	if(o >= INT16_MAX)
	  v = INT16_MAX;
	else if(o <= INT16_MIN)
	  v = INT16_MIN;
	else
	  v = o;
	*dst16++ = v;
      }

      af_enq(dst_fifo, dst_buf);
    }
    pthread_mutex_lock(&audio_source_lock);
  }
}




audio_source_t *
audio_source_create(media_pipe_t *mp)
{
  audio_source_t *as = calloc(1, sizeof(audio_source_t));

  while(mixer_output.words == 0) {
    printf("Waiting for mixer launch\n");
    sleep(1);
  }
  audio_fifo_init(&as->as_fifo, 20, mixer_output.words * sizeof(float), 15);

  pthread_mutex_lock(&audio_source_lock);
  LIST_INSERT_HEAD(&audio_sources, as, as_link);
  pthread_mutex_unlock(&audio_source_lock);

  as->as_mp = mp;

  return as;
}







void
audio_source_destroy(audio_source_t *as)
{
  assert(mixer_output.words != 0);

  pthread_mutex_lock(&audio_source_lock);
  audio_fifo_destroy(&as->as_fifo);
  LIST_REMOVE(as, as_link);
  pthread_mutex_unlock(&audio_source_lock);

  if(as->as_resampler != NULL)
    av_resample_close(as->as_resampler);
}




void
audio_mixer_source_config(audio_source_t *as, int rate, int srcchannels,
			  channel_offset_t *chlayout)
{
  int c, i;

  for(c = 0; c < AUDIO_MIXER_MAX_CHANNELS; c++) {
    free(as->as_spillbuf[c]);
    as->as_spillbuf[c] = NULL;
  }

  as->as_spill = 0;
  as->as_rate = rate;
    
  if(as->as_resampler != NULL)
    av_resample_close(as->as_resampler);
  as->as_resampler =
    av_resample_init(mixer_output.rate, as->as_rate, 16, 10, 0, 1.0);

  as->as_channels = srcchannels;

  memset(as->as_coeffs, 0, sizeof(float) * AUDIO_MIXER_COEFF_MATRIX_SIZE);

  while(chlayout->channel != MIXER_CHANNEL_NONE) {

    c = chlayout->channel;

    for(i = 0; i < AUDIO_MIXER_MAX_CHANNELS; i++) {
      as->as_coeffs[chlayout->offset][i] +=
	mixer_output_matrix[chlayout->channel][i];
    }
    chlayout++;
  }
}


/*
 *
 */

static float *
audio_mixer_matrix(audio_source_t *as, int16_t *src[], int stride,
		   float *output, int frames)
{
  int och, i, j;
  float o;

  for(i = 0; i < frames * stride; i += stride) {
    for(och = 0; och < mixer_output.channels; och++) {
      o = 0;
      
      for(j = 0; j < as->as_channels; j++)
	o += src[j][i] * as->as_coeffs[j][och];
      
      *output++ = o;
    }
  }
  return output;
}






void
audio_mixer_source_int16(audio_source_t *as, int16_t *data, int frames,
			 int64_t pts)
{
  int w, wrmax, i, srcframes, c, j, consumed, srcsize;
  float *dst;
  int16_t *src16;
  int16_t *dst16[AUDIO_MIXER_MAX_CHANNELS];

  dst = as->as_saved_dst;

  while(frames > 0) {

    if(as->as_fullness == 0) {
      as->as_dst_buf = af_alloc(&as->as_fifo);
      dst = ab_dataptr(as->as_dst_buf);
    }

    wrmax = mixer_output.period_size - as->as_fullness;

    if(as->as_resampler != NULL) {
      
      /* resample */

      srcframes = as->as_spill > frames ? 0 : frames;
      w = 0;

      for(c = 0; c < as->as_channels; c++) {

	dst16[c] = malloc(wrmax * sizeof(uint16_t));

	if(as->as_spillbuf[c] != NULL) {
	  srcsize = as->as_spill + srcframes;
	  src16 = malloc(srcsize * sizeof(uint16_t));
	  j = as->as_spill;
	  memcpy(src16, as->as_spillbuf[c], j * sizeof(uint16_t));
	  for(i = 0; i < srcframes; i++)
	    src16[j++] = data[i * as->as_channels + c];
	  free(as->as_spillbuf[c]);
	  as->as_spillbuf[c] = NULL;
	} else {
	  srcsize = srcframes;
	  src16 = malloc(srcsize * sizeof(uint16_t));
	  for(i = 0; i < srcframes; i++)
	    src16[i] = data[i * as->as_channels + c];
	}
	w = av_resample(as->as_resampler, dst16[c], src16, &consumed, 
			srcsize, wrmax, c == as->as_channels - 1);
	if(consumed != srcsize) {
	  j = srcsize - consumed;
	  as->as_spillbuf[c] = malloc(j * sizeof(uint16_t));
	  memcpy(as->as_spillbuf[c], src16 + consumed, j * sizeof(uint16_t));
	  if(c == as->as_channels - 1)
	    as->as_spill = j;
	}

	free(src16);
      }

      frames -= srcframes;
      as->as_fullness += w;

      dst = audio_mixer_matrix(as, dst16, 1, dst, w);

      for(c = 0; c < as->as_channels; c++)
	free(dst16[c]);

    } else {

      w = FFMIN(frames, wrmax);
      as->as_fullness += w;
      frames -= w;

      for(c = 0; c < as->as_channels; c++)
	dst16[c] = data + c;

      dst = audio_mixer_matrix(as, dst16, as->as_channels, dst, w);
      data += w * as->as_channels;
    }

    if(as->as_fullness == mixer_output.period_size) {
      as->as_dst_buf->payload_type = AUDIO_OUTPUT_PCM;
      af_enq(&as->as_fifo, as->as_dst_buf);
      as->as_fullness = 0;
    }
  }

  as->as_saved_dst = dst;
}


void
audio_mixer_source_flush(audio_source_t *as)
{
  audio_fifo_purge(&as->as_fifo);
}

/*****************************************************************************
 *
 * Mixer setup
 *
 */



void
audio_mixer_setup_output(int channels, int period_size, int rate)
{
  pthread_t ptid;
  static int inited;

  pthread_mutex_lock(&audio_source_lock);
  mixer_output.words       = period_size * channels;
  mixer_output.period_size = period_size;
  mixer_output.channels    = channels;
  mixer_output.rate        = rate;
  mixer_output.buffer      = realloc(mixer_output.buffer,
				     mixer_output.words * sizeof(float));
  pthread_mutex_unlock(&audio_source_lock);

  if(!inited) {
    inited = 1;

    audio_fifo_init(&mixer_output_fifo, 1,
		    mixer_output.words * sizeof(float), 0);
    pthread_create(&ptid, NULL, mixer_thread, NULL);
    audio_compressor_setup();
  }

  mixer_output_fifo.bufsize = mixer_output.words * sizeof(float);
}



void
audio_mixer_menu_setup(glw_t *parent)
{
#if 0
  glw_t *a;

  a = menu_create_submenu(parent, "icon://audio.png", "Audio settings", 0);

  audio_compressor_menu_setup(a);
#endif
}
