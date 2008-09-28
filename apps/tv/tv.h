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
#include <libhts/avg.h>
#include "app.h"

#include "video/video_playback.h"
#include "video/video_decoder.h"
#include "video/video_menu.h"

TAILQ_HEAD(tv_tag_queue,             tv_tag);
TAILQ_HEAD(tv_channel_queue,         tv_channel);
LIST_HEAD(tv_channel_list,           tv_channel);
TAILQ_HEAD(tv_channel_tag_map_queue, tv_channel_tag_map);

LIST_HEAD(tv_channel_stream_list,    tv_channel_stream);

/**
 * A tag (or group of channels)
 */
typedef struct tv_tag {
  char *tt_identifier;
  TAILQ_ENTRY(tv_tag) tt_tv_link;

  struct tv_channel_tag_map_queue tt_ctms;
  int tt_nctms;

  glw_prop_t *tt_prop_root;
  glw_prop_t *tt_prop_title;
  glw_prop_t *tt_prop_icon;
  glw_prop_t *tt_prop_titled_icon;
  glw_prop_t *tt_prop_nchannels;

  glw_t *tt_widget;
  glw_t *tt_tab;
  glw_t *tt_chlist;

} tv_tag_t;



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
 * Defines a channel
 */
typedef struct tv_channel {
  uint32_t ch_id;
  
  glw_prop_t *ch_prop_root;
  glw_prop_t *ch_prop_title;
  glw_prop_t *ch_prop_icon;

  glw_prop_t *ch_prop_fullscreen;

  glw_prop_t *ch_prop_epg_start[3];
  glw_prop_t *ch_prop_epg_stop[3];
  glw_prop_t *ch_prop_epg_title[3];


  glw_prop_t *ch_prop_sub_status;
  glw_prop_t *ch_prop_sub_backend_queuesize;
  glw_prop_t *ch_prop_sub_backend_queuedrops;
  glw_prop_t *ch_prop_sub_backend_queuedelay;
  glw_prop_t *ch_prop_sub_bitrate;

  avgstat_t ch_avg_bitrate;

  int ch_playstatus_start_flags;  /* Control flags passed to
				     mp_set_playstatus() upon stream
				     start */

  struct tv *ch_tv;
  TAILQ_ENTRY(tv_channel) ch_tv_link;


  struct tv_channel_tag_map_queue ch_ctms;

  /**
   * A subscribed channel is one that has subscribed to a backend.
   *
   * It does not necessarily mean that any video is actually displayed
   * (backend may not be able to deliver content for whatever reason)
   */
  int ch_subscribed;

  glw_t *ch_subscribe_widget;
  glw_t *ch_video_widget;
  vd_conf_t ch_vdc;
  media_pipe_t *ch_mp;


  /**
   * A channel is running once the backend has acknowledged that it
   * (at least will try to) deliver data.
   */
  int ch_running;
  LIST_ENTRY(tv_channel) ch_running_link;

  formatwrap_t *ch_fw;
  struct tv_channel_stream_list ch_streams;

} tv_channel_t;


/**
 * Mapping between channels and maps (since there is an M:M relation)
 */
typedef struct tv_channel_tag_map {
  TAILQ_ENTRY(tv_channel_tag_map) ctm_channel_link;
  tv_channel_t *ctm_channel;
  TAILQ_ENTRY(tv_channel_tag_map) ctm_tag_link;
  tv_tag_t *ctm_tag;

  int ctm_delete_me;

  glw_t *ctm_widget;

} tv_channel_tag_map_t;



/**
 * Main TV
 */
typedef struct tv {

  appi_t *tv_ai;

  hts_mutex_t tv_ch_mutex; /* Must never be acquired while holding
			      tv_running_mutex (will deadlock) */

  struct tv_tag_queue tv_tags;
  struct tv_channel_queue tv_channels;

  hts_mutex_t tv_running_mutex;
  struct tv_channel_list tv_running_channels;

  glw_t *tv_stack;
  glw_t *tv_rootwidget;
  glw_t *tv_subscription_container;
  glw_t *tv_fullscreen_container;

  glw_prop_t *tv_prop_root;
  glw_prop_t *tv_prop_url;
  glw_prop_t *tv_prop_show_channels;

  glw_prop_t *tv_prop_backend_error;
  glw_prop_t *tv_prop_backend_name;

  enum {
    TV_RS_RUN,
    TV_RS_RECONFIGURE,
    TV_RS_STOP,
  } tv_runstatus;

  /* Backend support */

  void *tv_be_opaque;
  int (*tv_be_subscribe)(void *opaque, tv_channel_t *ch, char *errbuf,
			 size_t errsize);
  void (*tv_be_unsubscribe)(void *opaque, tv_channel_t *ch);

} tv_t;



/**
 * Transformed events from libglw
 */
typedef struct tv_ctrl_event {
  glw_event_t h;
  enum {
    TV_CTRL_START,
    TV_CTRL_CLEAR_AND_START,
    TV_CTRL_CLEAR,
    TV_CTRL_STOP,
    TV_CTRL_FULLSCREEN,
  } cmd;

  int key; /* ChannelId, etc */

} tv_ctrl_event_t;



/**
 * Tags
 */
tv_tag_t *tv_tag_find(tv_t *tv, const char *identifier, int create);

void tv_tag_destroy(tv_t *tv, tv_tag_t *tt);

void tv_tag_set_title(tv_tag_t *tt, const char *title);

void tv_tag_set_icon(tv_tag_t *tt, const char *icon);

void tv_tag_set_titled_icon(tv_tag_t *tt, int v);

void tv_tag_mark_ctms(tv_tag_t *tt);

void tv_tag_delete_marked_ctms(tv_tag_t *tt);

void tv_tag_map_channel(tv_t *tv, tv_tag_t *tt, tv_channel_t *ch);

/**
 * Channels
 */
tv_channel_t *tv_channel_find(tv_t *tv, uint32_t id, int create);

void tv_channel_destroy(tv_t *tv, tv_channel_t *ch);

void tv_channel_set_title(tv_channel_t *ch, const char *title);

void tv_channel_set_icon(tv_channel_t *ch, const char *icon);

void tv_channel_set_current_event(tv_channel_t *ch, int index, 
				  const char *title,
				  time_t start, time_t stop);

void tv_channel_stop(tv_t *tv, tv_channel_t *ch);

/**
 * Misc
 */
void tv_fatal_error(tv_t *tv, const char *err);

void tv_remove_all(tv_t *tv);


#endif /* TV_H_ */
