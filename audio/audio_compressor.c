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
#include "audio_compressor.h"
#include "menu.h"
#include "layout/layout.h"

struct compressor_data post_mixer_compressor;

void
audio_compressor(float *data, struct compressor_data *comp, audio_mixer_t *mi)
{
  float peak = 0;
  float *d0;

  float g;
  int m, v, c;

  int i = mi->period_size;

  d0 = data;

  while(i--) {
    m = 0;
    for(c = 0; c < mi->channels; c++) {
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

    for(c = 0; c < mi->channels; c++) {
      *data *= comp->gain;
      data++;
    }
  }
}


void
audio_compressor_update_config(struct compressor_data *comp, audio_mixer_t *mi)
{
  comp->holdsamples = mi->rate * comp->holdtime / 1000;
  comp->postgain    = pow(10, comp->postgaindb / 10.);
  comp->thres       = pow(10, comp->thresdb / 10.);
  comp->ratio       = 1 / comp->ratiocfg;
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
      audio_compressor_update_config(&post_mixer_compressor, &mixer_output);
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
      audio_compressor_update_config(&post_mixer_compressor, &mixer_output);
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
      audio_compressor_update_config(&post_mixer_compressor, &mixer_output);
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
      audio_compressor_update_config(&post_mixer_compressor, &mixer_output);
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
audio_compressor_menu_setup(glw_t *a)
{
  glw_t *c;

  c = menu_create_submenu(a, "icon://audio.png", "Range Compressor", 0);

  add_audio_mixer_control(c, comp_holdtime,  &post_mixer_compressor);
  add_audio_mixer_control(c, comp_postgain,  &post_mixer_compressor);
  add_audio_mixer_control(c, comp_threshold, &post_mixer_compressor);
  add_audio_mixer_control(c, comp_ratio,     &post_mixer_compressor);
}
