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
#include "gl/gl_video.h"
#include "miw.h"
#include "audio/audio_sched.h"

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



static void
iptv_miw_rethink(iptv_player_t *iptv, glw_t *parent, iptv_channel_t *ich)
{
  tvheadend_t *tvh = &iptv->iptv_tvh;
  int channel = ich->ich_index;
  const float rw = 0.03;
  glw_t *y;
  tvchannel_t tvc;
  tvevent_t tve;

  parent = glw_create(GLW_CONTAINER_X, 
		      GLW_ATTRIB_PARENT, parent, 
		      NULL);

  tvh_get_channel(tvh, &tvc, channel);

  tvh_create_chicon(&tvc, parent, 1.0f);

  if(tvh_get_event_current(tvh, &tve, channel)) {
    glw_create(GLW_DUMMY,
	       GLW_ATTRIB_PARENT, parent,
	       GLW_ATTRIB_WEIGHT, 1.3f,
	       NULL);
    return;
  }


  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, parent,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  /* Playstatus */

  miw_playstatus_create(parent, &ich->ich_mp);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, parent,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  /* Program time */

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, parent,
		 GLW_ATTRIB_WEIGHT, 4.0f,
		 NULL);
  
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tve.tve_timetxt,
	     NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, parent,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  /* Title */

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_WEIGHT, 10.0f,
	     GLW_ATTRIB_PARENT, parent,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tve.tve_title,
	     NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, parent,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, parent,
	     GLW_ATTRIB_WEIGHT, 1.0f,
	     NULL);


}



static int 
iptv_miw_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  iptv_channel_t *ich = opaque;

  switch(signal) {
  case GLW_SIGNAL_RETHINK:
    glw_destroy_childs(w);
    iptv_miw_rethink(ich->ich_iptv, w, ich);
    return 0;

  default:
    return 0;
  }
}


glw_t *
iptv_create_miw(iptv_player_t *iptv, iptv_channel_t *ich, uint32_t tag)
{
  glw_t *c;

  c = glw_create(GLW_XFADER,
		 GLW_ATTRIB_SIGNAL_HANDLER, iptv_miw_callback, ich, 0, 
		 GLW_ATTRIB_TAG, iptv->iptv_tag_hash, tag,
		 NULL);

  iptv_miw_rethink(iptv, c, ich);
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




glw_t *
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
iptv_filter_audio(void *aux, uint32_t sc)
{
  iptv_channel_t *ich = aux;
  media_pipe_t *mp = &ich->ich_mp, *cur;
  cur = audio_sched_mp_get();

  if(mp != cur)
    return 0;

  if(sc == 0x80) {
    ich->ich_ac3_ctd = 10;
    return 1;
  }

  if(ich->ich_ac3_ctd == 0)
    return 1;

  ich->ich_ac3_ctd--;
  return 0;
}




#if 0
static unsigned int
ich_compute_weight(iptv_channel_t *ich)
{
  int a;
  glw_t *w;
  iptv_channel_t *x;
  iptv_player_t *iptv = ich->ich_iptv;

  if(&ich->ich_mp == audio_sched_mp_get())
    return 750;

  w = iptv->iptv_chlist->glw_selected;
  if(w == NULL) {
    a = 0;
  } else {
    x = glw_get_opaque(w);
    a = x->ich_index;
  }

  a = abs(a - ich->ich_index);
#if 1
  if(a == 0) {
    return 500; 
  } else {
    return 0;
  }
#endif
  return 500 - a;
}

#endif

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
  mp_init(mp, strdup(buf), iptv->iptv_appi, MEDIA_PIPE_DONT_INVERVENT);

  pp = &ich->ich_pp;
  ich->ich_fw = wrap_format_create(NULL, 1);

  pes_init(pp, mp, ich->ich_fw);
  
  pp->pp_video.ps_output = &mp->mp_video;
  pp->pp_audio.ps_output = &mp->mp_audio;

  pp->pp_audio.ps_filter_cb = iptv_filter_audio;
  pp->pp_audio.ps_aux = ich;

  mp_set_playstatus(mp, MP_PLAY);

  mp->mp_info_widget = iptv_create_miw(iptv, ich, tag);
  mp->mp_info_extra_widget = iptv_create_extra_miw(iptv, ich);
  iptv->iptv_channels[channel] = ich;
  
  return ich;
}

#if 0

static void
ich_destroy(iptv_channel_t *ich)
{
  iptv_player_t *iptv = ich->ich_iptv;

  printf("Freein' channel %d\n", ich->ich_index);

  iptv->iptv_channels[ich->ich_index] = NULL;

  mp_deinit(&ich->ich_mp);

  if(ich->ich_gvp != NULL)
    glw_destroy(ich->ich_gvp);
  
  pes_deinit(&ich->ich_pp);

  wrap_format_wait(ich->ich_fw);

  free(ich);
}

#endif


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


#if 0

static int 
ich_callback(glw_t *w, glw_signal_t signal, ...)
{
  iptv_channel_t *ich = glw_get_opaque(w);
  iptv_player_t *iptv = ich->ich_iptv;
  unsigned int v;

  switch(signal) {
  case GLW_SIGNAL_PRE_LAYOUT:
    v = ich_compute_weight(ich);
    if(v != ich->ich_weight) {

      ich->ich_weight = v;

      pthread_mutex_lock(&iptv->iptv_weight_update_mutex);
      pthread_cond_signal(&iptv->iptv_weight_update_cond);
      pthread_mutex_unlock(&iptv->iptv_weight_update_mutex);
    }

    return 0;

  case GLW_SIGNAL_DESTROY:

    ich_destroy(ich);
    return 0;

  default:
    return 0;
  }
}

#endif




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
    
      /* video widget */

      ich->ich_gvp = gvp_create(s, &ich->ich_mp, &iptv->iptv_gvp_conf,
				GVPF_AUTO_FLUSH);
      

    }
  }
}

/*****************************************************************************
 *****************************************************************************
 *
 *  Input handling
 *
 */

void
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
    
    glw_nav_signal(iptv->iptv_chlist, GLW_SIGNAL_ENTER);
    iptv->iptv_appi->ai_req_fullscreen = AI_FS_BLANK;
    audio_sched_mp_activate(&ich->ich_mp);
    break;

  case INPUT_KEY_BACK:
    layout_hide(iptv->iptv_appi);
    break;

  default:
    break;
  }
}


void
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
    tvh_int(tvh_query(&iptv->iptv_tvh, "channel.unsubscribe %d",
		      ich->ich_index));
    
    iptv->iptv_chlist->glw_flags &= ~GLW_ZOOMED;
    iptv->iptv_appi->ai_req_fullscreen = 0;
    audio_sched_mp_deactivate(&ich->ich_mp, 0);
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


    if(audio_sched_mp_get() != &ich->ich_mp) {

      if(ich->ich_avg_vqlen < 10)
	ich->ich_mp.mp_speed_gain = 1.0f;
      else
	ich->ich_mp.mp_speed_gain = ich->ich_avg_vqlen / 10.0f;
    } else {
      ich->ich_mp.mp_speed_gain = 1.0f;
    }

    break;

  case HTSTV_EOS:
    
    printf("GOT END OF STREAM FOR CHANNEL %d, stopping playback\n",
	   ich->ich_index);

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
  gvp_conf_init(&iptv->iptv_gvp_conf);

  
  tvh_init(tvh, &ai->ai_ic);
  iptv->iptv_tvh.tvh_data_callback = iptv_data_input;
  iptv->iptv_tvh.tvh_status_callback = iptv_status_input;

  gvp_menu_setup(appi_menu_top(ai), &iptv->iptv_gvp_conf);

  while(1) {

    iptv_connect(iptv);
    
    ai->ai_visible = 1;

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

    ai->ai_visible = 0;

    glw_destroy(iptv->iptv_chlist);
  }
}




static void
iptv_spawn(appi_t *ai)
{
  iptv_player_t *iptv = calloc(1, sizeof(iptv_player_t));

  pthread_mutex_init(&iptv->iptv_mutex, NULL);

  pthread_mutex_init(&iptv->iptv_weight_update_mutex, NULL);
  pthread_cond_init(&iptv->iptv_weight_update_cond, NULL);

  iptv->iptv_appi = ai;
  pthread_create(&ai->ai_tid, NULL, iptv_loop, iptv);
}





/*
 *
 */

app_t app_iptv = {
  .app_name = "TV",
  .app_icon = "icon://tv.png",
  .app_spawn = iptv_spawn,
};
