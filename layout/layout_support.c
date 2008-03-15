/*
 *  Layout support functions
 *  Copyright (C) 2008 Andreas Öman
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

#include <string.h>
#include <math.h>
#include <unistd.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"
#include "layout_support.h"

/**
 * Update codec info in text widgets
 */ 
void
layout_update_codec_info(glw_t *w, const char *id, AVCodecContext *ctx)
{
  char tmp1[100];

  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  if(ctx == NULL) {
    glw_set(w, GLW_ATTRIB_CAPTION, "", NULL);
    return;
  }

  snprintf(tmp1, sizeof(tmp1), "%s", ctx->codec->name);
  
  if(ctx->codec_type == CODEC_TYPE_AUDIO) {
    snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
	     ", %d Hz, %d chanels", ctx->sample_rate, ctx->channels);
  }

  if(ctx->width)
    snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
	     ", %dx%d",
	     ctx->width, ctx->height);
  
  if(ctx->bit_rate > 2000000)
    snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
	     ", %.1f Mb/s", (float)ctx->bit_rate / 1000000);
  else if(ctx->bit_rate)
    snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
	     ", %d kb/s", ctx->bit_rate / 1000);
  
  glw_set(w, GLW_ATTRIB_CAPTION, tmp1, NULL);
}

/**
 * Update current time info in text widget
 */ 
void
layout_update_time(glw_t *w, const char *id, int s)
{
  char tmp[100];
  int m ,h;

  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  m = s / 60;
  h = s / 3600;
  
  if(h > 0) {
    snprintf(tmp, sizeof(tmp), "%d:%02d:%02d", h, m % 60, s % 60);
  } else {
    snprintf(tmp, sizeof(tmp), "%d:%02d", m % 60, s % 60);
  }

  glw_set(w, GLW_ATTRIB_CAPTION, tmp, NULL);
}

/**
 * Update a GLW_BAR
 */ 
void
layout_update_bar(glw_t *w, const char *id, float v)
{
  if((w = glw_find_by_id(w, id, 0)) == NULL)
    return;

  glw_set(w, GLW_ATTRIB_EXTRA, v, NULL);
}
