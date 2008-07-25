/*
 *  TV application
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

#ifndef TV_H_
#define TV_H_

#include <libglw/glw.h>
#include "app.h"

#include "video/video_playback.h"
#include "video/video_decoder.h"
#include "video/video_menu.h"

TAILQ_HEAD(tv_ch_group_queue, tv_ch_group);
TAILQ_HEAD(tv_channel_queue,  tv_channel);
LIST_HEAD(tv_channel_stream_list, tv_channel_stream);

/**
 *
 */
typedef struct tv_ch_group {
  char *tcg_name;
  struct tv_channel_queue tcg_channels;
  glw_t *tcg_widget, *tcg_tab, *tcg_channel_list;

  TAILQ_ENTRY(tv_ch_group) tcg_link;
} tv_ch_group_t;


/**
 * Defines a channel
 */
typedef struct tv_channel {
  char *ch_name;
  uint32_t ch_tag;
  
  glw_t *ch_widget;

  struct tv *ch_tv;
  TAILQ_ENTRY(tv_channel) ch_link;

  /**
   *
   */

  int ch_running;
  TAILQ_ENTRY(tv_channel) ch_running_link;

  /**
   *
   */
  glw_t *ch_video_widget;			  
  vd_conf_t ch_vdc;            /* Video Display Configuration */
  media_pipe_t *ch_mp;
  formatwrap_t *ch_fw;

  struct tv_channel_stream_list ch_streams;

} tv_channel_t;




/**
 * Defines a component (audio, video, etc) inside a tv stream
 */
typedef struct tv_channel_stream {
  int tcs_index;
  codecwrap_t *tcs_cw;
  media_queue_t *tcs_mq;
  int tcs_data_type;

  LIST_ENTRY(tv_channel_stream) tcs_link;

} tv_channel_stream_t;





/**
 * Main TV
 */
typedef struct tv {
  appi_t *tv_ai;
  glw_t *tv_miniature;
  glw_t *tv_stack;

  hts_mutex_t tv_ch_mutex;
  struct tv_ch_group_queue tv_groups;

  struct tv_channel_queue tv_running_channels;

  glw_t *tv_root;

  glw_t *tv_fatal_error;
  const char *tv_last_err;

  struct tvconfig *tv_cfg;

} tv_t;

tv_ch_group_t *tv_channel_group_find(tv_t *tv, const char *name, int create);

tv_channel_t *tv_channel_find(tv_t *tv, tv_ch_group_t *tcg, 
			      const char *name, int create);

void tv_channel_set_icon(tv_channel_t *ch, const char *icon);

void tv_channel_set_current_event(tv_channel_t *ch, int index, 
				  const char *title, time_t start, time_t stop);

tv_channel_t *tv_channel_find_by_tag(tv_t *tv, uint32_t tag);

void tv_fatal_error(tv_t *tv, const char *err);

void tv_remove_all(tv_t *tv);

#endif /* TV_H_ */
