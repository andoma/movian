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

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>

#include <libhts/htstv.h>

#include <libglw/glw.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "tv_playback.h"
#include "miw.h"

static int 
ich_entry_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}

/*****************************************************************************
 *****************************************************************************
 *
 *  Media pipe info widget
 *
 */


static int 
iptv_miw_bar_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  iptv_channel_t *ich = opaque;
  switch(signal) {
  case GLW_SIGNAL_PREPARE:
     w->glw_extra = (float)(walltime - ich->ich_event_start_time) / 
      (float)ich->ich_event_duration;
    break;

  default:
    break;
  }
  return 0;
}



static void
iptv_miw_fill(iptv_player_t *iptv, glw_t *parent, iptv_channel_t *ich)
{
  tvheadend_t *tvh = &iptv->iptv_tvh;
  int channel = ich->ich_index;
  glw_t *y, *c;
  tvchannel_t tvc;
  tvevent_t tve;
  char buf[100];
  char *title;

  c = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, parent,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  c = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  tvh_get_channel(tvh, &tvc, channel);



  if(tvc.tvc_icon[0]) {
    glw_create(GLW_BITMAP,
	       GLW_ATTRIB_FILENAME, tvc.tvc_icon,
	       GLW_ATTRIB_WEIGHT, 1.0,
	       GLW_ATTRIB_PARENT, c,
	       GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
	       GLW_ATTRIB_BORDER_WIDTH, 0.05,
	       NULL);
    title = NULL;
  } else {
    title = tvc.tvc_displayname;
  }

  if(tvh_get_event_current(tvh, &tve, channel)) {

    /* Nothing is currently on... */

    if(title == NULL)
      glw_create(GLW_DUMMY,
		 GLW_ATTRIB_PARENT, parent,
		 GLW_ATTRIB_WEIGHT, 24.0f,
		 NULL);
    else
      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_WEIGHT, 24.0f,
		 GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		 GLW_ATTRIB_PARENT, c,
		 GLW_ATTRIB_CAPTION, title,
		 NULL);
    return;
  }


  if(title) {
    snprintf(buf, sizeof(buf), "%s - %s", title, tve.tve_title);
    title = buf;
  } else {
    title = tve.tve_title;
  }

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_WEIGHT, 20.0f,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_PARENT, c,
	     GLW_ATTRIB_CAPTION, title,
	     NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, c,
		 GLW_ATTRIB_WEIGHT, 4.0f,
		 NULL);
  
 
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_RIGHT,
	     GLW_ATTRIB_WEIGHT, 2.0f,
	     GLW_ATTRIB_CAPTION, tve.tve_timetxt,
	     NULL);

  ich->ich_event_start_time = tve.tve_start;
  ich->ich_event_duration = tve.tve_stop - tve.tve_start;
  

  glw_create(GLW_BAR,
	     GLW_ATTRIB_COLOR, GLW_COLOR_LIGHT_GREEN,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_SIGNAL_HANDLER, iptv_miw_bar_callback, ich, 0,
	     NULL);

}



static int 
iptv_miw_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  iptv_channel_t *ich = opaque;

  switch(signal) {
  case GLW_SIGNAL_RETHINK:
    iptv_miw_fill(ich->ich_iptv, w, ich);
    return 0;

  default:
    return 0;
  }
}


static glw_t *
iptv_create_miw(iptv_player_t *iptv, iptv_channel_t *ich, uint32_t tag)
{
  glw_t *c;

  c = glw_create(GLW_XFADER,
		 GLW_ATTRIB_SIGNAL_HANDLER, iptv_miw_callback, ich, 0, 
		 GLW_ATTRIB_TAG, iptv->iptv_tag_hash, tag,
		 NULL);

  iptv_miw_fill(iptv, c, ich);
  return c;
}


static int 
iptv_feed_errors_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  iptv_channel_t *ich = opaque;
  pes_player_t *pp;
  char buf[20];

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    pp = &ich->ich_pp;
    snprintf(buf, sizeof(buf), "%d errors/m", 
	     avgstat_read(&pp->pp_cc_errors, 60, wallclock / 1000000));
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    return 0;
    
  default:
    return 0;
  }
}



static void
iptv_feed_info(glw_t *y, iptv_channel_t *ich)
{
  glw_t *x, *w;
  const float rw = 0.06;

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_WEIGHT, 1.5f,
		 NULL);


  glw_create(GLW_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_FILENAME, "icon://tv.png",
	     NULL);

  /* Status info */

   ich->ich_status_widget = 
    glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_CAPTION, "",
	     GLW_ATTRIB_WEIGHT, 10.0,
	     NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  /* Current transport */
 
   ich->ich_transport_widget = 
    glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_CAPTION, "",
	     GLW_ATTRIB_WEIGHT, 10.0,
	     NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);


  /* Local network errors*/
  
  w = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_WEIGHT, 4.0,
		 NULL);


  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, w,
	     GLW_ATTRIB_CAPTION, "Local network errors",
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_SIGNAL_HANDLER, iptv_feed_errors_callback, ich, 0,
	     GLW_ATTRIB_PARENT, w,
	     GLW_ATTRIB_CAPTION, "",
	     NULL);


  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  /* TV network errors*/

  w = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_WEIGHT, 4.0,
		 NULL);


  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, w,
	     GLW_ATTRIB_CAPTION, "TV network errors",
	     NULL);

  ich->ich_feed_errors_widget = 
    glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, w,
	     GLW_ATTRIB_CAPTION, "",
	     NULL);
}




static glw_t *
iptv_create_extra_miw(iptv_player_t *iptv, iptv_channel_t *ich)
{
  glw_t *y, *c;
  media_pipe_t *mp = &ich->ich_mp;

  c = glw_create(GLW_CONTAINER, NULL);


  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  iptv_feed_info(y, ich);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.15,
	     NULL);
  
  miw_add_queue(y, &mp->mp_video, "icon://video.png");

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.15,
	     NULL);

  miw_add_queue(y, &mp->mp_audio, "icon://audio.png");

  return c;
}



/*****************************************************************************
 *****************************************************************************
 *
 *  Playback functions
 *
 */


static int
iptv_filter_audio(void *aux, uint32_t sc, int codec_id)
{
  iptv_channel_t *ich = aux;

  if(codec_id == CODEC_ID_AC3) {
    ich->ich_ac3_ctd = 10;
    return 1;
  }

  if(ich->ich_ac3_ctd == 0)
    return 1;

  ich->ich_ac3_ctd--;
  return 0;
}



static iptv_channel_t *
ich_create(iptv_player_t *iptv, int channel, uint32_t tag)
{
  iptv_channel_t *ich;
  pes_player_t *pp;
  media_pipe_t *mp;
  char buf[10];

  ich = calloc(1, sizeof(iptv_channel_t));
  ich->ich_iptv = iptv;
  ich->ich_index = channel;

  mp = &ich->ich_mp;
  snprintf(buf, sizeof(buf), "TV-ch-%d", channel);
  mp_init(mp, strdup(buf), iptv->iptv_appi);
  mp_set_video_conf(mp, &iptv->iptv_vd_conf);

  pp = &ich->ich_pp;
  ich->ich_fw = wrap_format_create(NULL, 1);

  pes_init(pp, mp, ich->ich_fw);
  
  pp->pp_video.ps_output = &mp->mp_video;
  pp->pp_audio.ps_output = &mp->mp_audio;

  pp->pp_audio.ps_filter_cb = iptv_filter_audio;
  pp->pp_audio.ps_aux = ich;

  mp->mp_info_widget = iptv_create_miw(iptv, ich, tag);
  mp->mp_info_extra_widget = iptv_create_extra_miw(iptv, ich);
  iptv->iptv_channels[channel] = ich;
  
  return ich;
}

/*****************************************************************************
 *****************************************************************************
 *
 *  IPTV channel list 
 *
 */



static void
iptv_widget_channel_container_fill(iptv_player_t *iptv, glw_t *y, int channel)
{
  tvheadend_t *tvh = &iptv->iptv_tvh;

  //  tvchannel_t tvc;
  tvevent_t tve;

  if(tvh_get_event_current(tvh, &tve, channel) < 0 ) {
     /* -1 == nothing is on */
    return;
  }

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_WEIGHT, 3.0f,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_WEIGHT, 2.0f,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tve.tve_title,
	     NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tve.tve_timetxt,
	     NULL);
}


static int 
iptv_widget_channel_container_callback(glw_t *w, void *opaque, 
				       glw_signal_t signal, ...)
{
  iptv_player_t *iptv = opaque;

  switch(signal) {
  case GLW_SIGNAL_RETHINK:
    iptv_widget_channel_container_fill(iptv, w, w->glw_u32);
    return 0;

  default:
    return 0;
  }
}


/*
 *
 */

static void
iptv_connect(iptv_player_t *iptv)
{
  tvheadend_t *tvh = &iptv->iptv_tvh;
  appi_t *ai = iptv->iptv_appi;
  glw_t *chlist;
  iptv_channel_t *ich;
  int id;
  void *r;
  const char *v, *x;
  glw_t *s, *c, *y, *z, *w;
  tvchannel_t tvc;

  while(1) {

    r = tvh_query(tvh, "channels.list");

    if(r == NULL) {
      sleep(1);
    } else {
      break;
    }
  }


  chlist = glw_create(GLW_ARRAY,
		      GLW_ATTRIB_SIDEKICK, bar_title("TV"),
		      GLW_ATTRIB_SIGNAL_HANDLER, appi_widget_post_key, ai, 0,
		      NULL);

  iptv->iptv_appi->ai_widget = iptv->iptv_chlist = chlist;

  for(x = r; x != NULL; x = nextline(x)) {
    if((v = propcmp(x, "channel")) != NULL) {
      
      id = atoi(v);

      tvh_get_channel(&iptv->iptv_tvh, &tvc, id);

      ich = ich_create(iptv, id, tvc.tvc_tag);

      s = glw_create(GLW_ZOOM_SELECTOR,
		     GLW_ATTRIB_PARENT, chlist,
		     GLW_ATTRIB_SIGNAL_HANDLER, ich_entry_callback, ich, 0,
		     NULL);

      c = glw_create(GLW_BITMAP,
		     GLW_ATTRIB_PARENT, s,
		     GLW_ATTRIB_FILENAME, "icon://plate-titled.png",
		     GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		     NULL);


      y = glw_create(GLW_CONTAINER_Y,
		     GLW_ATTRIB_PARENT, c,
		     NULL);

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		 GLW_ATTRIB_CAPTION, tvc.tvc_displayname,
		 GLW_ATTRIB_WEIGHT, 0.4,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		 NULL);

      glw_create(GLW_DUMMY,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_WEIGHT, 0.1,
		 NULL);

      z = glw_create(GLW_CONTAINER_Z,
		     GLW_ATTRIB_PARENT, y,
		     GLW_ATTRIB_WEIGHT, 2.1,
		     NULL);
		   
      glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, tvc.tvc_icon[0] ? tvc.tvc_icon :
		 "icon://tv.png",
		 GLW_ATTRIB_ALPHA, 0.25,
		 GLW_ATTRIB_PARENT, z,
		 NULL);

      w = glw_create(GLW_XFADER,
		     GLW_ATTRIB_PARENT, z,
		     GLW_ATTRIB_SIGNAL_HANDLER,
		     iptv_widget_channel_container_callback, iptv, 0,
		     GLW_ATTRIB_TAG, iptv->iptv_tag_hash, tvc.tvc_tag,
		     GLW_ATTRIB_U32, id,
		     NULL);

      iptv_widget_channel_container_fill(iptv, w, id);
    
      ich->ich_vd = vd_create_widget(s, &ich->ich_mp);
    }
  }
}

/*****************************************************************************
 *****************************************************************************
 *
 *  Input handling
 *
 */

static void
iptv_key_event_unzoomed(iptv_player_t *iptv, int key)
{
  glw_t *w = iptv->iptv_chlist->glw_selected;
  iptv_channel_t *ich;

  if(w == NULL)
    return;

  ich = glw_get_opaque(w, ich_entry_callback);

  switch(key) {

  case INPUT_KEY_UP:
    glw_nav_signal(iptv->iptv_chlist, GLW_SIGNAL_UP);
    break;

  case INPUT_KEY_DOWN:
    glw_nav_signal(iptv->iptv_chlist, GLW_SIGNAL_DOWN);
    break;

  case INPUT_KEY_LEFT:
    glw_nav_signal(iptv->iptv_chlist, GLW_SIGNAL_LEFT);
    break;

  case INPUT_KEY_RIGHT:
    glw_nav_signal(iptv->iptv_chlist, GLW_SIGNAL_RIGHT);
    break;

  case INPUT_KEY_ENTER:
    tvh_int(tvh_query(&iptv->iptv_tvh, "channel.subscribe %d %d", 
		      ich->ich_index, 500));
    
    mp_set_playstatus(&ich->ich_mp, MP_PLAY);
    media_pipe_acquire_audio(&ich->ich_mp);
    
    glw_nav_signal(iptv->iptv_chlist, GLW_SIGNAL_ENTER);
    iptv->iptv_appi->ai_req_fullscreen = AI_FS_BLANK;
    break;

  case INPUT_KEY_BACK:
    layout_hide(iptv->iptv_appi);
    break;

  default:
    break;
  }
}


static void
iptv_key_event_zoomed(iptv_player_t *iptv, int key)
{
  glw_t *w = iptv->iptv_chlist->glw_selected;
  iptv_channel_t *ich;

  if(w == NULL)
    return;

  ich = glw_get_opaque(w, ich_entry_callback);

  switch(key) {

  case INPUT_KEY_BACK:
  case INPUT_KEY_STOP:
    mp_set_playstatus(&ich->ich_mp, MP_STOP);
    media_pipe_release_audio(&ich->ich_mp);

    tvh_int(tvh_query(&iptv->iptv_tvh, "channel.unsubscribe %d",
		      ich->ich_index));
    
    iptv->iptv_chlist->glw_flags &= ~GLW_ZOOMED;
    iptv->iptv_appi->ai_req_fullscreen = 0;
    break;

  case INPUT_KEY_ENTER:
  case INPUT_KEY_PLAY:
  case INPUT_KEY_PLAYPAUSE:
    media_pipe_acquire_audio(&ich->ich_mp);
    break;

  default:
    break;
  }
}


/*****************************************************************************
 *****************************************************************************
 *
 *  Auto refresh
 *
 */

static void
iptv_tag_refresh(iptv_player_t *iptv, int tag)
{
  glw_t *w;
  LIST_FOREACH(w, &iptv->iptv_tag_hash[tag & GLW_TAG_HASH_MASK], glw_tag_link)
    if(w->glw_tag == tag)
      glw_drop_signal(w, GLW_SIGNAL_RETHINK, NULL);
}

/*****************************************************************************
 *****************************************************************************
 *
 *  IPTV transport stream decoder
 *
 */


static void
iptv_demux(iptv_player_t *iptv, uint8_t *buf, int len, int pkttype,
	   int channel)
{
  pes_player_t *pp;
  iptv_channel_t *ich;
  int b, i;
  enum CodecID c;

  ich = channel < IPTV_MAX_CHANNELS ? iptv->iptv_channels[channel] : NULL;

  if(ich == NULL)
    return;

  switch(pkttype) {

  case HTSTV_TRANSPORT_STREAM:
    if((len % 188) != 0)
      return;
  
    pp = &ich->ich_pp;
    b = len / 188;
  
    for(i = 0; i < b; i++) {
      switch(buf[i * 188]) {

      case HTSTV_MPEG2VIDEO:
	c = CODEC_ID_MPEG2VIDEO;
	break;

      case HTSTV_MPEG2AUDIO:
	c = CODEC_ID_MP2;
	break;

      case HTSTV_H264:
	c = CODEC_ID_H264;
	break;

      case HTSTV_AC3:
	c = CODEC_ID_AC3;
	break;

      default:
	continue;
      }
      buf[i * 188] = 0x47;
      ich->ich_errors += pes_do_tsb(pp, &buf[i * 188], 0, c);
    }

    ich->ich_avg_vqlen = (ich->ich_avg_vqlen * 31.0 + 
			  ich->ich_mp.mp_video.mq_len) / 32.0f;

    ich->ich_avg_aqlen = (ich->ich_avg_aqlen * 31.0 + 
			  ich->ich_mp.mp_audio.mq_len) / 32.0f;
    break;

  case HTSTV_EOS:
    break;
  }
}


static void
iptv_data_input(tvheadend_t *tvh, uint8_t *buf, unsigned int len)
{
  iptv_player_t *iptv = (iptv_player_t *)tvh;
  
  iptv_demux(iptv, buf + 2, len - 2, buf[0], buf[1]);
  free(buf);
}


static void
iptv_status_input(tvheadend_t *tvh, int channel, tvstatus_t *tvs)
{
  iptv_player_t *iptv = (iptv_player_t *)tvh;
  iptv_channel_t *ich;
  char buf[20];

  ich = channel < IPTV_MAX_CHANNELS ? iptv->iptv_channels[channel] : NULL;
  if(ich == NULL)
    return;

  glw_set(ich->ich_status_widget, 
	  GLW_ATTRIB_CAPTION, tvs->tvs_info, 
	  NULL);
  
  glw_set(ich->ich_transport_widget,
	  GLW_ATTRIB_CAPTION, tvs->tvs_transport,
	  NULL);

  snprintf(buf, sizeof(buf), "%d errors/m", tvs->tvs_cc_errors);

  glw_set(ich->ich_feed_errors_widget,
	  GLW_ATTRIB_CAPTION, buf,
	  NULL);
  

  memcpy(&ich->ich_status, tvs, sizeof(tvstatus_t));
}




/*****************************************************************************
 *****************************************************************************
 *
 *  IPTV main loop
 *
 */


static void *
iptv_loop(void *aux)
{
  iptv_player_t *iptv = aux;
  tvheadend_t *tvh = &iptv->iptv_tvh;
  appi_t *ai = iptv->iptv_appi;
  inputevent_t ie;

  iptv->iptv_tag_hash = glw_taghash_create();
  vd_conf_init(&iptv->iptv_vd_conf);

  
  tvh_init(tvh, &ai->ai_ic);
  iptv->iptv_tvh.tvh_data_callback = iptv_data_input;
  iptv->iptv_tvh.tvh_status_callback = iptv_status_input;

  vd_menu_setup(appi_menu_top(ai), &iptv->iptv_vd_conf);

  while(1) {

    iptv_connect(iptv);
    
    while(tvh->tvh_fp != NULL) {

      input_getevent(&ai->ai_ic, 1, &ie, NULL);

      switch(ie.type) {
      default:
	break;

      case INPUT_KEY:
	if(iptv->iptv_chlist->glw_flags & GLW_ZOOMED) {
	  iptv_key_event_zoomed(iptv, ie.u.key);
	} else {
	  iptv_key_event_unzoomed(iptv, ie.u.key);
	}
	break;

      case INPUT_SPECIAL:
	iptv_tag_refresh(iptv, ie.u.u32);
	break;
      }
    }

    glw_destroy(iptv->iptv_chlist);
  }
}




void
iptv_spawn(void)
{
  appi_t *ai = appi_spawn("TV", "icon://tv.png");
  iptv_player_t *iptv = calloc(1, sizeof(iptv_player_t));

  pthread_mutex_init(&iptv->iptv_mutex, NULL);

  pthread_mutex_init(&iptv->iptv_weight_update_mutex, NULL);
  pthread_cond_init(&iptv->iptv_weight_update_cond, NULL);

  iptv->iptv_appi = ai;
  pthread_create(&ai->ai_tid, NULL, iptv_loop, iptv);
}
