/*
 *  TV Playback
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

#define _GNU_SOURCE
#include <pthread.h>

#include <assert.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"
#include "layout/layout_support.h"

#include "htsp.h"

#include "tv_playback.h"

/**
 *
 */
void
tv_playback_init(tv_channel_t *ch)
{
  tv_t *tv = ch->ch_tv;

  assert(ch->ch_video_widget == NULL);

  vd_conf_init(&ch->ch_vdc);
  
  ch->ch_mp = mp_create(ch->ch_name, tv->tv_ai);

  ch->ch_video_widget = vd_create_widget(tv->tv_stack, ch->ch_mp, 1.0);

  mp_set_video_conf(ch->ch_mp, &ch->ch_vdc);

  ch->ch_fw = wrap_format_create(NULL, 1);
}


/**
 *
 */
void
tv_channel_stream_destroy(tv_channel_t *ch, tv_channel_stream_t *tcs, int lock)
{
  LIST_REMOVE(tcs, tcs_link);
  wrap_codec_deref(tcs->tcs_cw, lock);

}


/**
 *
 */
void
tv_playback_deinit(tv_channel_t *ch)
{
  tv_t *tv = ch->ch_tv;
  tv_channel_stream_t *tcs;
  media_pipe_t *mp = ch->ch_mp;

  assert(ch->ch_video_widget != NULL);

  TAILQ_REMOVE(&tv->tv_running_channels, ch, ch_running_link);

  mp_set_playstatus(mp, MP_STOP);

  wrap_lock_all_codecs(ch->ch_fw);

  while((tcs = LIST_FIRST(&ch->ch_streams)) != NULL)
    tv_channel_stream_destroy(ch, tcs, 0);

  glw_destroy(ch->ch_video_widget);

  wrap_format_wait(ch->ch_fw);
}


/**
 *
 */
tv_channel_t *
tv_channel_by_tag(tv_t *tv, uint32_t tag)
{
  tv_channel_t *ch;
  TAILQ_FOREACH(ch, &tv->tv_running_channels, ch_running_link)
    if(ch->ch_tag == tag)
      return ch;
  return NULL;
}

