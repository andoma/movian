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
#include "audio_fifo.h"
#include "menu.h"
#include "layout/layout.h"

media_pipe_t *mixer_primary_audio;

audio_fifo_t mixer_output_fifo;

int mixer_hw_output_delay;

float mixer_output_matrix[AUDIO_MIXER_MAX_CHANNELS]
                         [AUDIO_MIXER_MAX_CHANNELS];

static int mixer_period_size;
static int mixer_output_channels;
static int mixer_output_rate;
static int mixer_words;

pthread_mutex_t audio_source_lock = PTHREAD_MUTEX_INITIALIZER;

LIST_HEAD(, audio_source) audio_sources;



struct compressor_data {
  int enable;
  int holdtime;    /* in ms */
  float thresdb;
  float postgaindb;
  float ratiocfg;


  float lp;

  float ratio;
  float thres;
  float postgain;
  int holdsamples;  /* holdtime, but in mixer samples */
  float hpeak;
  float gain;
  int hold;
};

struct compressor_data final_compressor;

static void
mixer_compressor(float *data, struct compressor_data *comp)
{
  float peak = 0;
  float *d0;

  float g;
  int m, v, c;

  int i = mixer_period_size;

  d0 = data;

  while(i--) {
    m = 0;
    for(c = 0; c < mixer_output_channels; c++) {
      v = data[c];
      if(v < 0)
	v = -v;
      if(v > m)
	m = v;
    }

    peak = (float)m / 32768.;

    if(peak > comp->thres) {
      comp->hpeak = peak;
      comp->hold = comp->holdsamples;
    } else if(comp->hold > 0) {
      comp->hold--;
    } else {
      comp->hpeak = peak;
    }

    if(comp->hpeak > comp->thres) {
      g = comp->postgain * ((comp->hpeak - comp->thres) * 
			    comp->ratio + comp->thres) / comp->hpeak;
    } else {
      g = comp->postgain;
    }
    
    comp->gain = (comp->gain * (comp->lp - 1) + g) / comp->lp;

    for(c = 0; c < mixer_output_channels; c++) {
      *data *= comp->gain;
      data++;
    }
  }
}


static void
compressor_update_config(struct compressor_data *comp)
{
  comp->holdsamples = mixer_output_rate * comp->holdtime / 1000;
  comp->postgain    = pow(10, comp->postgaindb / 10.);
  comp->thres       = pow(10, comp->thresdb / 10.);
  comp->ratio       = 1 / comp->ratiocfg;

  printf("postgain = %f, thres = %f, ratio = %f\n",
	 comp->postgain, comp->thres, comp->ratio);
}





static void *
mixer_thread(void *aux)
{
  media_pipe_t *mp, *prim;
  audio_source_t *as;
  audio_fifo_t *dst_fifo = &mixer_output_fifo;
  audio_buf_t *dst_buf;
  int16_t *dst16;
  
  float *dst;

  float *mixbuf;
  int d;
  int64_t pts;
  float o;
  int i, j, v;

  pthread_mutex_lock(&audio_source_lock);

  LIST_HEAD(, audio_source) mixlist;
  
  mixbuf = malloc(sizeof(float) * mixer_output_channels * 
		  mixer_period_size);

  while(1) {

    pts = 0;

    LIST_INIT(&mixlist);

    prim = NULL;

    LIST_FOREACH(as, &audio_sources, as_link) {
      if((as->as_src_buf = af_deq(&as->as_fifo, 0)) != NULL) {
	LIST_INSERT_HEAD(&mixlist, as, as_tmplink);
	as->as_src = ab_dataptr(as->as_src_buf);
	pts = as->as_src_buf->pts;

	if(as->as_mp == NULL) {
	  as->as_target_gain = 1.0f;
	  continue;
	}

	mp = as->as_mp;

	if(mp->mp_playstatus != MP_PLAY)
	  continue;

	if(prim == NULL) {
	  prim = mp;
	  as->as_target_gain = 1.0f;
	} else {
	  as->as_target_gain = 0.0f;
	}
      }
    }

    mixer_primary_audio = prim;

    dst = mixbuf;

    as = LIST_FIRST(&mixlist);

    if(as == NULL) {
      memset(dst, 0, sizeof(float) * mixer_words);
    } else {
      for(i = 0; i < mixer_period_size; i++) {
	for(j = 0; j < mixer_output_channels; j++) {
	  *dst++ = *as->as_src++ * as->as_gain;
	}
	as->as_gain = (as->as_gain * 999. + as->as_target_gain) / 1000.;
      }

      as = LIST_NEXT(as, as_tmplink);
      for(; as != NULL; as = LIST_NEXT(as, as_tmplink)) {
	dst = mixbuf;
	for(i = 0; i < mixer_period_size; i++) {
	  for(j = 0; j < mixer_output_channels; j++) {
	    *dst++ += *as->as_src++ * as->as_gain;
	  }
	  as->as_gain = (as->as_gain * 999. + as->as_target_gain) / 1000.;
	}
      }
    }

    LIST_FOREACH(as, &mixlist, as_tmplink) {
      d = mixer_hw_output_delay + dst_fifo->avgdelay + as->as_fifo.avgdelay;
      if(as->as_avg_delay == 0) {
	as->as_avg_delay = d;
      } else {
	as->as_avg_delay = (as->as_avg_delay * 31.0 + d) / 32.0f;
      }
      af_free(as->as_src_buf);
    }

    pthread_mutex_unlock(&audio_source_lock);

    if(final_compressor.enable)
      mixer_compressor(mixbuf, &final_compressor);

    dst = mixbuf;

    dst_buf = af_alloc(dst_fifo);
    dst_buf->pts = pts;
    dst16 = ab_dataptr(dst_buf);

    for(i = 0; i < mixer_words; i++) {
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
    pthread_mutex_lock(&audio_source_lock);
  }
}




void
audio_source_prio(audio_source_t *as)
{
  pthread_mutex_lock(&audio_source_lock);
  LIST_REMOVE(as, as_link);
  LIST_INSERT_HEAD(&audio_sources, as, as_link);
  pthread_mutex_unlock(&audio_source_lock);
}



audio_source_t *
audio_source_create(media_pipe_t *mp)
{
  audio_source_t *as = calloc(1, sizeof(audio_source_t));

  while(mixer_words == 0)
    sleep(1);

  audio_fifo_init(&as->as_fifo, 10, mixer_words * sizeof(float), 7);

  pthread_mutex_lock(&audio_source_lock);
  LIST_INSERT_HEAD(&audio_sources, as, as_link);
  pthread_mutex_unlock(&audio_source_lock);

  as->as_mp = mp;

  return as;
}







void
audio_source_destroy(audio_source_t *as)
{
  assert(mixer_words != 0);

  if(as->as_mp == mixer_primary_audio)
    mixer_primary_audio = NULL;

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
    av_resample_init(mixer_output_rate, as->as_rate, 16, 10, 0, 1.0);

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

  for(c = 0; c < 8; c++) {
    for(i = 0; i < 8; i++) {
      printf("%f\t", as->as_coeffs[c][i]);
    }
    printf("\n");
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
    for(och = 0; och < mixer_output_channels; och++) {
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
      as->as_dst_buf->pts = pts;
      dst = ab_dataptr(as->as_dst_buf);
    }

    wrmax = mixer_period_size - as->as_fullness;

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

    if(as->as_fullness == mixer_period_size) {
      af_enq(&as->as_fifo, as->as_dst_buf);
      as->as_fullness = 0;
    }
  }

  as->as_saved_dst = dst;
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

  mixer_words = period_size * channels;

  mixer_period_size     = period_size;
  mixer_output_channels = channels;
  mixer_output_rate     = rate;

  audio_fifo_init(&mixer_output_fifo, 1, mixer_words * sizeof(float), 0);

  pthread_create(&ptid, NULL, mixer_thread, NULL);

  final_compressor.enable = 0;
  final_compressor.holdtime = 300; /* ms */
  final_compressor.thresdb = 0;
  final_compressor.ratiocfg = 1;
  final_compressor.lp = 1000;
  final_compressor.postgain = 1.0f;

  compressor_update_config(&final_compressor);
}







static int 
comp_holdtime(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  struct compressor_data *comp = opaque;
  char buf[50];
  inputevent_t *ie;
  va_list ap;
  glw_t *c;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    snprintf(buf, sizeof(buf), "Holdtime: %d ms", comp->holdtime);

    c = glw_find_by_class(w, GLW_TEXT_BITMAP);
    if(c != NULL)
      glw_set(c, GLW_ATTRIB_CAPTION, buf, NULL);

    c = glw_find_by_class(w, GLW_BAR);
    if(c != NULL)
      c->glw_extra = (float)comp->holdtime / 1000.;
    return 0;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    if(ie->type == INPUT_KEY) {
      switch(ie->u.key) {
      default:
	break;
      case INPUT_KEY_LEFT:
	comp->holdtime = GLW_MAX(0, comp->holdtime - 10);
	break;
      case INPUT_KEY_RIGHT:
	comp->holdtime = GLW_MIN(1000, comp->holdtime + 10);
	break;
      }
      compressor_update_config(&final_compressor);
    }
    return 1;

  default:
    return 0;
  }
}




static int 
comp_postgain(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  struct compressor_data *comp = opaque;
  char buf[50];
  inputevent_t *ie;
  va_list ap;
  glw_t *c;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    snprintf(buf, sizeof(buf), "Postgain: %.2f dB", comp->postgaindb);

    c = glw_find_by_class(w, GLW_TEXT_BITMAP);
    if(c != NULL)
      glw_set(c, GLW_ATTRIB_CAPTION, buf, NULL);

    c = glw_find_by_class(w, GLW_BAR);
    if(c != NULL)
      c->glw_extra = (comp->postgaindb + 50.) / 100.;
    return 0;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    if(ie->type == INPUT_KEY) {
      switch(ie->u.key) {
      default:
	break;
      case INPUT_KEY_LEFT:
	comp->postgaindb = GLW_MAX(-50, comp->postgaindb - 1);
	break;
      case INPUT_KEY_RIGHT:
	comp->postgaindb = GLW_MIN(50,  comp->postgaindb + 1);
	break;
      }
      compressor_update_config(&final_compressor);
    }
    return 1;

  default:
    return 0;
  }
}





static int 
comp_threshold(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  struct compressor_data *comp = opaque;
  char buf[50];
  inputevent_t *ie;
  va_list ap;
  glw_t *c;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    snprintf(buf, sizeof(buf), "Threshold: %.2f dB", comp->thresdb);

    c = glw_find_by_class(w, GLW_TEXT_BITMAP);
    if(c != NULL)
      glw_set(c, GLW_ATTRIB_CAPTION, buf, NULL);

    c = glw_find_by_class(w, GLW_BAR);
    if(c != NULL)
      c->glw_extra = (comp->thresdb + 50.) / 50.;
    return 0;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    if(ie->type == INPUT_KEY) {
      switch(ie->u.key) {
      default:
	break;
      case INPUT_KEY_LEFT:
	comp->thresdb = GLW_MAX(-50, comp->thresdb - 1);
	break;
      case INPUT_KEY_RIGHT:
	comp->thresdb = GLW_MIN(0,   comp->thresdb + 1);
	break;
      }
      compressor_update_config(&final_compressor);
    }
    return 1;

  default:
    return 0;
  }
}






static int 
comp_ratio(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  struct compressor_data *comp = opaque;
  char buf[50];
  inputevent_t *ie;
  va_list ap;
  glw_t *c;

  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    snprintf(buf, sizeof(buf), "Ratio: 1:%.1f", comp->ratiocfg);

    c = glw_find_by_class(w, GLW_TEXT_BITMAP);
    if(c != NULL)
      glw_set(c, GLW_ATTRIB_CAPTION, buf, NULL);

    c = glw_find_by_class(w, GLW_BAR);
    if(c != NULL)
      c->glw_extra = comp->ratiocfg / 10.;
    return 0;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    if(ie->type == INPUT_KEY) {
      switch(ie->u.key) {
      default:
	break;
      case INPUT_KEY_LEFT:
	comp->ratiocfg = GLW_MAX(1,  comp->ratiocfg - 0.5f);
	break;
      case INPUT_KEY_RIGHT:
	comp->ratiocfg = GLW_MIN(10, comp->ratiocfg + 0.5f);
	break;
      }
      compressor_update_config(&final_compressor);
    }
    return 1;

  default:
    return 0;
  }
}





void
add_audio_mixer_control(glw_t *parent, glw_callback_t *cb, void *opaque)
{
  glw_t *y;

  y = menu_create_container(parent, cb, opaque, 0, 0);
  
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

  glw_create(GLW_BAR,
	     GLW_ATTRIB_COLOR, GLW_COLOR_LIGHT_BLUE,
	     GLW_ATTRIB_PARENT, y,
	     NULL);
}

void
audio_mixer_menu_setup(glw_t *parent)
{
  glw_t *a, *c;

  a = menu_create_submenu(parent, "icon://audio.png", 
			  "Audio settings...", 0);

  c = menu_create_submenu(a, "icon://audio.png", 
			  "Range Compressor...", 0);
 

  add_audio_mixer_control(c, comp_holdtime,  &final_compressor);
  add_audio_mixer_control(c, comp_postgain,  &final_compressor);
  add_audio_mixer_control(c, comp_threshold, &final_compressor);
  add_audio_mixer_control(c, comp_ratio,     &final_compressor);
}
