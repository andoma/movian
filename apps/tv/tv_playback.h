/*
 *  Live TV playback
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

#ifndef TV_PLAYBACK_H
#define TV_PLAYBACK_H

#include "mpeg_support.h"
#include "media.h"
#include "tv_headend.h"
#include "app.h"
#include "gl/video_decoder.h"


typedef struct iptv_channel {
  struct iptv_player *ich_iptv;

  int ich_index;
  int ich_visible;

  pes_player_t ich_pp;
  formatwrap_t *ich_fw;

  media_pipe_t ich_mp;

  int ich_ac3_ctd;

  LIST_ENTRY(iptv_channel) ich_sort_link;

  unsigned int ich_weight;
  unsigned int ich_final_weight;   /* Final computed weight */
  unsigned int ich_sent_weight;    /* Last weight sent to head end */

  glw_t *ich_vd;
  glw_t *ich_vd_parent;

  float ich_avg_vqlen;
  float ich_avg_aqlen;

  int ich_errors;

  tvstatus_t ich_status;

  glw_t *ich_status_widget;
  glw_t *ich_transport_widget;
  glw_t *ich_feed_errors_widget;

  time_t ich_event_start_time;
  int ich_event_duration;

} iptv_channel_t;


typedef struct iptv_player {

  tvheadend_t iptv_tvh; /* must be first */

  appi_t *iptv_appi;

  pthread_mutex_t iptv_mutex;

  int iptv_num_chan;
  vd_conf_t iptv_vd_conf;


  /* render widgets */

  float iptv_zoom;

  glw_t *iptv_chlist;
  glw_t *iptv_chlist_last_selected;

  /* widget tag hash */
  
  struct glw_head *iptv_tag_hash;

  /* Array of channels */

#define IPTV_MAX_CHANNELS 256

  iptv_channel_t *iptv_channels[IPTV_MAX_CHANNELS];

  pthread_mutex_t iptv_weight_update_mutex;
  pthread_cond_t iptv_weight_update_cond;

} iptv_player_t;

void iptv_spawn(void);

#endif /* TV_PLAYBACK_H */
