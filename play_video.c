/*
 *  Playback of video
 *  Copyright (C) 2007-2008 Andreas Öman
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
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>

#include <libavformat/avformat.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "play_video.h"
#include "gl/video_decoder.h"
#include "subtitles.h"
#include "layout/layout_forms.h"
#include "layout/layout_support.h"

#define INPUT_APP_SEEK_DIRECT               INPUT_APP + 0

#define OVERLAY_BUTTON_PLAYPAUSE 1
#define OVERLAY_BUTTON_PREV      2
#define OVERLAY_BUTTON_REW       3
#define OVERLAY_BUTTON_FWD       4
#define OVERLAY_BUTTON_END       5
#define OVERLAY_BUTTON_VSETTINGS 6
#define OVERLAY_BUTTON_ASETTINGS 7
#define OVERLAY_BUTTON_SSETTINGS 8
#define OVERLAY_BUTTON_STOP      9

typedef struct play_video_ctrl {

  int    pvc_widget_status_playstatus;
  glw_t *pvc_status_overlay;

  glw_t *pvc_menu_playfield;

  int    pvc_status_fader;

  appi_t *pvc_ai;

  int pvc_setup_mode;

  ic_t pvc_ic;

  int pvc_display_menu;

  AVFormatContext *pvc_fctx;

  codecwrap_t **pvc_cwvec;

  vd_conf_t pvc_vdc;
  
} play_video_ctrl_t;

/**
 *
 */
static int
overlay_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  play_video_ctrl_t *pvc = opaque;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    if(pvc->pvc_status_fader > 0)
      pvc->pvc_status_fader--;

    w->glw_alpha = GLW_LP(16, w->glw_alpha, pvc->pvc_status_fader ? 1.0 : 0.0);
    break;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
static void
pv_update_playstatus(play_video_ctrl_t *pvc, mp_playstatus_t mps)
{
  const char *model;
  glw_t *w;

  if(pvc->pvc_widget_status_playstatus == mps)
    return;

  pvc->pvc_widget_status_playstatus = mps;

  w = glw_find_by_id(pvc->pvc_status_overlay, "playstatus", 0);
  if(w == NULL)
    return;

  switch(mps) {
  case MP_PAUSE:
    pvc->pvc_status_fader = INT32_MAX;
    model = "videoplayback/pause";
    break;
  case MP_PLAY:
    pvc->pvc_status_fader = 150;
    model = "videoplayback/play";
    break;
  case MP_VIDEOSEEK_PAUSE:
  case MP_VIDEOSEEK_PLAY:
    pvc->pvc_status_fader = INT32_MAX;
    model = "videoplayback/seek";
    break;
  default:
    model = NULL;
    break;
  }

  if(model != NULL) {
    w = glw_create(GLW_MODEL,
		   GLW_ATTRIB_PARENT, w,
		   GLW_ATTRIB_FILENAME, model,
		   NULL);
  } else {
    w = glw_create(GLW_DUMMY,
		   GLW_ATTRIB_PARENT, w,
		   NULL);
  }
}

/**
 * Update text about video/audio stream type
 */
static void
video_player_update_stream_info(play_video_ctrl_t *pvc, media_pipe_t *mp)
{
  int as = mp->mp_audio.mq_stream;
  int vs = mp->mp_video.mq_stream;
  glw_t *w = pvc->pvc_status_overlay;

  if(as >= 0 && pvc->pvc_cwvec[as] != NULL)
    layout_update_codec_info(w, "audioinfo", pvc->pvc_cwvec[as]->codec_ctx);
  else
    layout_update_codec_info(w, "audioinfo", NULL);

  if(vs >= 0 && pvc->pvc_cwvec[vs] != NULL)
    layout_update_codec_info(w, "audioinfo", pvc->pvc_cwvec[vs]->codec_ctx);
  else
    layout_update_codec_info(w, "audioinfo", NULL);
}


/**
 * Add audio track options to the form given
 */
static void
add_audio_tracks(AVFormatContext *fctx, glw_t *t)
{
  AVCodecContext *ctx;
  char buf[100];
  const char *p;
  int i;

  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;
    if(ctx->codec_type != CODEC_TYPE_AUDIO)
      continue;

    avcodec_string(buf, sizeof(buf), ctx, 0);
    p = strncasecmp(buf, "audio: ", 7) ? buf : buf + 7;

    layout_form_add_option(t, "audio_tracks", p, i);
  }
}


/**
 * 
 */
static int
video_start_form(play_video_ctrl_t *pvc, glw_t *parent, appi_t *ai, ic_t *ic,
		 vd_conf_t *vdc, AVFormatContext *fctx,
		 media_pipe_t *mp, int64_t start_time)
{
  glw_t *m;
  struct layout_form_entry_list lfelist;
  inputevent_t ie;

  TAILQ_INIT(&lfelist);


  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, parent,
		 GLW_ATTRIB_FILENAME, "videoplayback/start",
		 NULL);

  start_time -= fctx->start_time;

  layout_update_time(m, "start_time", start_time / AV_TIME_BASE);

  add_audio_tracks(fctx, m);
  LFE_ADD_BTN(&lfelist, "play", 0);
  LFE_ADD_OPTION(&lfelist, "audio_tracks", &mp->mp_audio.mq_stream);
  LFE_ADD_BTN(&lfelist, "start_from_begin", 1);
  LFE_ADD_BTN(&lfelist, "cancel", -1);
  
  layout_form_query(&lfelist, m, &ai->ai_gfs, &ie);
  glw_detach(m);
  return ie.u.u32;
}

/**
 * 
 */
static int
open_rcache(const char *fname)
{
  char buf[512], *n2, *n;

  if(settingsdir == NULL)
    return -1;

  snprintf(buf, sizeof(buf), "%s/restartcache", settingsdir);
  mkdir(buf, 0700);

  n = n2 = strdupa(fname);
  while(*n) {
    if(*n == '/' || *n == ':' || *n == '?' || *n == '*' || *n > 127 || *n < 32)
      *n = '_';
    n++;
  }

  snprintf(buf, sizeof(buf), "%s/restartcache/%s", settingsdir, n2);
  return open(buf, O_CREAT | O_RDWR, 0660);
}


/**
 * Thread for reading from lavf and sending to lavc
 */
static void *
player_thread(void *aux)
{
  play_video_ctrl_t *pvc = aux;
  appi_t *ai = pvc->pvc_ai;
  media_pipe_t *mp = &ai->ai_mp;
  media_buf_t *mb;
  media_queue_t *mq;
  int64_t pts, dts, seek_ref, seek_delta, seek_abs;
  inputevent_t ie;
  AVCodecContext *ctx;
  AVPacket pkt;

  int run, i, gotevent, key;

  pts          = AV_NOPTS_VALUE;
  dts          = AV_NOPTS_VALUE;
  seek_ref     = pvc->pvc_fctx->start_time;
  run          = 1;

  while(run) {
    i = av_read_frame(pvc->pvc_fctx, &pkt);

    if(i < 0) {
      printf("Read error %d\n", i);
      mp_wait(mp, mp->mp_audio.mq_stream != -1, mp->mp_video.mq_stream != -1);
      break;
    }

    ctx = pvc->pvc_cwvec[pkt.stream_index] ? 
      pvc->pvc_cwvec[pkt.stream_index]->codec_ctx : NULL;
    mb = NULL;
    
    /* Rescale PTS / DTS to µsec */
    if(pkt.pts != AV_NOPTS_VALUE) {
      pts = av_rescale_q(pkt.pts, 
			 pvc->pvc_fctx->streams[pkt.stream_index]->time_base,
			 AV_TIME_BASE_Q);
    } else {
      pts = AV_NOPTS_VALUE;
    }
 
    if(pkt.dts != AV_NOPTS_VALUE) {
      dts = av_rescale_q(pkt.dts, 
			 pvc->pvc_fctx->streams[pkt.stream_index]->time_base,
			 AV_TIME_BASE_Q);
    } else {
      dts = AV_NOPTS_VALUE;
    }
 

    if(ctx != NULL) {
      if(pkt.stream_index == mp->mp_video.mq_stream) {
	/* Current video stream */

	mb = media_buf_alloc();
	mb->mb_data_type = MB_VIDEO;
	mq = &mp->mp_video;

      } else if(pkt.stream_index == mp->mp_audio.mq_stream) {
	/* Current audio stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_AUDIO;
	mq = &mp->mp_audio;
      }
    }

    if(mb == NULL) {
      av_free_packet(&pkt);
    } else {

      mb->mb_pts = pts;
      mb->mb_dts = dts;
   
      /* Rescale duration */

      mb->mb_duration =
	av_rescale_q(pkt.duration, 
		     pvc->pvc_fctx->streams[pkt.stream_index]->time_base,
		     AV_TIME_BASE_Q);

      
      mb->mb_cw = wrap_codec_ref(pvc->pvc_cwvec[pkt.stream_index]);

      /* Move the data pointers from ffmpeg's packet */

      mb->mb_stream = pkt.stream_index;

      mb->mb_data = pkt.data;
      pkt.data = NULL;

      mb->mb_size = pkt.size;
      pkt.size = 0;

      /* Enqueue packet */

      mb_enqueue(mp, mq, mb);
    }
    av_free_packet(&pkt);

    pv_update_playstatus(pvc, mp->mp_playstatus);

    if(mp_is_paused(mp)) {
      gotevent = !input_getevent(&pvc->pvc_ic, 1, &ie, NULL);
      media_pipe_acquire_audio(mp);
    } else {
      gotevent = !input_getevent(&pvc->pvc_ic, 0, &ie, NULL);
    }

    ai->ai_req_fullscreen = mp->mp_playstatus == MP_PLAY;

    if(!gotevent)
      continue;

    seek_abs   = 0;
    seek_delta = 0;

    switch(ie.type) {

    default:
      break;

    case INPUT_TS:
      /**
       * Feedback from decoders
       */
      if(ie.u.ts.stream != mp->mp_video.mq_stream)
	break; /* we only deal with the video stream here */

      if(ie.u.ts.dts != AV_NOPTS_VALUE)
	seek_ref = ie.u.ts.dts;

      break;

    case INPUT_APP_SEEK_DIRECT:
      /**
       * Direct seek
       */
      seek_abs = ie.u.ts.dts;
      break;

    case INPUT_KEY:
      /**
       * Keyboard input
       */
      key = ie.u.key;

      switch(key) {
      case INPUT_KEY_STOP:
	run = 0;
	break;
      case INPUT_KEY_RESTART_TRACK:
	seek_abs = 1;
	break;
      case INPUT_KEY_SEEK_FAST_BACKWARD:
	seek_delta = -60000000;
	break;
      case INPUT_KEY_SEEK_BACKWARD:
	seek_delta = -15000000;
	break;
      case INPUT_KEY_SEEK_FAST_FORWARD:
	seek_delta = 60000000;
	break;
      case INPUT_KEY_SEEK_FORWARD:
	seek_delta = 15000000;
	break;

      case INPUT_KEY_PLAYPAUSE:
      case INPUT_KEY_PLAY:
      case INPUT_KEY_PAUSE:
	mp_playpause(mp, key);
	break;

      default:
	break;
      }
    }
    if((seek_delta && seek_ref != AV_NOPTS_VALUE) || seek_abs) {
      /* Seeking requested */

      /* Just make it display the seek widget */
      pv_update_playstatus(pvc, MP_VIDEOSEEK_PLAY);

      if(seek_abs)
	seek_ref = seek_abs;
      else
	seek_ref += seek_delta;

      if(seek_ref < pvc->pvc_fctx->start_time)
	seek_ref = pvc->pvc_fctx->start_time;

      printf("Seeking to %.2f\n", seek_ref / 1000000.0);

      i = av_seek_frame(pvc->pvc_fctx, -1, seek_ref, AVSEEK_FLAG_BACKWARD);

      if(i < 0)
	printf("Seeking failed\n");

      mp_flush(mp);
      
      mp->mp_videoseekdts = seek_ref;

      switch(mp->mp_playstatus) {
      case MP_VIDEOSEEK_PAUSE:
      case MP_PAUSE:
	mp_set_playstatus(mp, MP_VIDEOSEEK_PAUSE);
	break;
      case MP_VIDEOSEEK_PLAY:
      case MP_PLAY:
	mp_set_playstatus(mp, MP_VIDEOSEEK_PLAY);
	break;
      default:
	abort();
      }
    }
  }
  return NULL;
}


/**
 * Seek to the given timestamp
 */
static void
video_seek_abs(play_video_ctrl_t *pvc, int64_t ts)
{
  inputevent_t ie;

  ie.type = INPUT_APP_SEEK_DIRECT;
  ie.u.ts.dts = ts;
  input_postevent(&pvc->pvc_ic, &ie);
}



/**
 * Seek to the given timestamp
 */
static int
video_player_menu(play_video_ctrl_t *pvc, ic_t *ic, media_pipe_t *mp)
{
  glw_t *t, *m;
  appi_t *ai = pvc->pvc_ai;
  struct layout_form_entry_list lfelist;
  inputevent_t ie;
  vd_conf_t *vdc = &pvc->pvc_vdc;
  int r = 1, run = 1;

  layout_form_entry_options_t deilace_options[] = {
    {"Automatic",          VD_DEILACE_AUTO},
    {"Disabled",           VD_DEILACE_NONE},
    {"Simple",             VD_DEILACE_HALF_RES},
    {"YADIF",              VD_DEILACE_YADIF_FIELD}
  };

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, pvc->pvc_menu_playfield,
		 GLW_ATTRIB_FILENAME, "videoplayback/menu",
		 NULL);

  TAILQ_INIT(&lfelist);

  LFE_ADD(&lfelist, "menu");
  
  /**
   * Audio tab
   */
  t = layout_form_add_tab(m,
			  "menu",           "videoplayback/audio-icon",
			  "menu_container", "videoplayback/audio-tab");
  
  add_audio_tracks(pvc->pvc_fctx, t);

  LFE_ADD_OPTION(&lfelist, "audio_tracks", &mp->mp_audio.mq_stream);

  /**
   * Video tab
   */

  t = layout_form_add_tab(m,
			  "menu",           "videoplayback/video-icon",
			  "menu_container", "videoplayback/video-tab");

  layout_form_fill_options(t, "deinterlacer_options",
			   deilace_options, 4);

  LFE_ADD_OPTION(&lfelist, "deinterlacer_options", &vdc->gc_deilace_type);
  LFE_ADD_INT(&lfelist, "avsync",    &vdc->gc_avcomp,"%dms", -2000, 2000, 50);
  LFE_ADD_INT(&lfelist, "videozoom", &vdc->gc_zoom,  "%d%%",   100, 1000, 10);
  

  layout_form_initialize(&lfelist, m, &ai->ai_gfs, ic, 1);

  while(run) {
    input_getevent(ic, 1, &ie, NULL);

    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:

      switch(ie.u.key) {
      default:
	break;

      case INPUT_KEY_STOP:
      case INPUT_KEY_CLOSE:
      case INPUT_KEY_EJECT:
	r = 0;
	run = 0;
	continue;

      case INPUT_KEY_MENU:
      case INPUT_KEY_BACK:
	run = 0;
	continue;
      }
    }
    input_postevent(&pvc->pvc_ic, &ie);
  }

  glw_detach(m);
  return r;
}



/**
 *  Main function for video playback
 */
int
play_video(const char *url, appi_t *ai, ic_t *ic, glw_t *parent)
{
  AVCodecContext *ctx;
  formatwrap_t *fw;
  media_pipe_t *mp = &ai->ai_mp;
  glw_t *vdw, *zstack, *top, *w;
  int64_t ts, rcache_thres, seek_ref, pts;
  inputevent_t ie;
  int run, rcache_fd, streams, i, key;
  play_video_ctrl_t pvc;
  const char *s;
  pthread_t player_tid;

  memset(&pvc, 0, sizeof(play_video_ctrl_t));
  pvc.pvc_ai = ai;

  /**
   * Open input file
   */
  if(av_open_input_file(&pvc.pvc_fctx, url, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file\n");
    return -1;
  }

  pvc.pvc_fctx->flags |= AVFMT_FLAG_GENPTS;

  if(av_find_stream_info(pvc.pvc_fctx) < 0) {
    av_close_input_file(pvc.pvc_fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return -1;
  }

  /**
   * Create top level stack
   */
  top = glw_create(GLW_CONTAINER_Z,
		   GLW_ATTRIB_PARENT, parent,
		   NULL);

  /**
   * Create playfield for video + menu + status, etc
   */
  zstack = glw_create(GLW_ZSTACK,
		      GLW_ATTRIB_PARENT, top,
		      NULL);
  /**
   * Create video output widget
   */
  vd_conf_init(&pvc.pvc_vdc);
  vdw = vd_create_widget(zstack, &ai->ai_mp, 1.0);
  mp_set_video_conf(mp, &pvc.pvc_vdc);


  /**
   * Status overlay
   */
  pvc.pvc_status_overlay = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_PARENT, top,
	       GLW_ATTRIB_SIGNAL_HANDLER, overlay_callback, &pvc, 100,
	       GLW_ATTRIB_FILENAME, "videoplayback/overlay",
	       NULL);

  /**
   * Menu cubestack
   */

  pvc.pvc_menu_playfield = 
    glw_create(GLW_CUBESTACK,
	       GLW_ATTRIB_PARENT, top,
	       NULL);

  //  video_menu_form_initialize(&pvc, ai, ic, &vdc, fctx, mp);

  /**
   * Set title
   */
  if((w = glw_find_by_id(pvc.pvc_status_overlay, "title", 0)) != NULL) {
    s = pvc.pvc_fctx->title;
    if(*s == 0) {
      /* No stored title */
      s = strrchr(url, '/');
      s = s ? s + 1 : url;
    }
    glw_set(w, GLW_ATTRIB_CAPTION, s, NULL);
  }

  /**
   * Set total duration
   */
  layout_update_time(pvc.pvc_status_overlay,
		     "time_total", pvc.pvc_fctx->duration / AV_TIME_BASE);

  /**
   * Init codec contexts
   */

  pvc.pvc_cwvec = alloca(pvc.pvc_fctx->nb_streams * sizeof(void *));
  memset(pvc.pvc_cwvec, 0, sizeof(void *) * pvc.pvc_fctx->nb_streams);
  
  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(pvc.pvc_fctx, 1);

  for(i = 0; i < pvc.pvc_fctx->nb_streams; i++) {
    ctx = pvc.pvc_fctx->streams[i]->codec;

    if(mp->mp_video.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_VIDEO) {
      mp->mp_video.mq_stream = i;
    }

    if(mp->mp_audio.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_AUDIO) {
      mp->mp_audio.mq_stream = i;
    }

    pvc.pvc_cwvec[i] = wrap_codec_create(ctx->codec_id,
					 ctx->codec_type, 0, fw, ctx);
  }

  ai->ai_fctx   = pvc.pvc_fctx;
  mp->mp_format = pvc.pvc_fctx;

  wrap_lock_all_codecs(fw);
  
  /**
   * Restart playback at last position
   */

  mp->mp_videoseekdts = 0;

  rcache_fd = open_rcache(url);
  if(rcache_fd != -1 && read(rcache_fd, &ts, sizeof(ts)) == sizeof(ts)) {
    i = av_seek_frame(pvc.pvc_fctx, -1, ts, AVSEEK_FLAG_BACKWARD);
    if(i >= 0)
      mp->mp_videoseekdts = ts;
  }
  mp_set_playstatus(mp, MP_VIDEOSEEK_PAUSE);

  media_pipe_acquire_audio(mp);

  input_init(&pvc.pvc_ic);

  mp->mp_feedback = ic;

  video_player_update_stream_info(&pvc, mp);

  wrap_unlock_all_codecs(fw);


  pvc.pvc_setup_mode = 1;

  rcache_thres = AV_NOPTS_VALUE;
  seek_ref     = pvc.pvc_fctx->start_time;

  pthread_create(&player_tid, NULL, player_thread, &pvc);
  run = 1;

  while(run) {
    input_getevent(ic, 1, &ie, NULL);

    switch(ie.type) {

    default:
      break;

      /**
       * Feedback from decoders
       */
    case INPUT_TS:
      if(ie.u.ts.stream != mp->mp_video.mq_stream)
	break; /* we only deal with the video stream here */

      if(ie.u.ts.dts != AV_NOPTS_VALUE)
	seek_ref = ie.u.ts.dts;

      pts = ie.u.ts.pts;

      if(rcache_fd != -1 && 
	 pts != AV_NOPTS_VALUE && pts > rcache_thres) {

	/* Write timestamp into restart cache */

	lseek(rcache_fd, 0, SEEK_SET);
	write(rcache_fd, &pts, sizeof(pts));
	fsync(rcache_fd);

	rcache_thres = pts + AV_TIME_BASE * 5;
      }

      if(pts != AV_NOPTS_VALUE) {
	pts -= pvc.pvc_fctx->start_time;

	layout_update_time(pvc.pvc_status_overlay,
			   "time_current",pts / AV_TIME_BASE);
	layout_update_bar(pvc.pvc_status_overlay, "durationbar", 
			  (double)pts / (double)pvc.pvc_fctx->duration);
      }

      if(pvc.pvc_setup_mode) {
	/* First frame displayed */
	i = video_start_form(&pvc, zstack, ai, ic, &pvc.pvc_vdc,
			     pvc.pvc_fctx, mp, pts);

	switch(i) {

	case -1: /* cancel */
	  run = 0;
	  break;

	case 0: /* play */
	  glw_focus_stack_deactivate(&ai->ai_gfs);
	  mp_set_playstatus(mp, MP_PLAY);
	  pvc.pvc_setup_mode = 0;
	  break;

	case 1: /* reset */
	  video_seek_abs(&pvc, 1);
	  break;
	}
      }

      break;

      /**
       * Keyboard input
       */

    case INPUT_KEY:
      key = ie.u.key;

      switch(key) {
      case INPUT_KEY_STOP:
      case INPUT_KEY_CLOSE:
      case INPUT_KEY_BACK:
      case INPUT_KEY_EJECT:
	ie.u.key = INPUT_KEY_STOP;
	run = 0;
	break;

      case INPUT_KEY_MENU:
	if(pvc.pvc_setup_mode == 0)
	  video_player_menu(&pvc, ic, mp);
	continue;
      }
    }
    input_postevent(&pvc.pvc_ic, &ie);
  }

  pthread_join(player_tid, NULL);

  close(rcache_fd);

  ai->ai_req_fullscreen = 0;

  mp_set_playstatus(mp, MP_STOP);

  wrap_lock_all_codecs(fw);

  mp->mp_info_widget = NULL;
  mp->mp_info_extra_widget = NULL;

  mp->mp_total_time = 0;

  ai->ai_fctx = NULL;

  streams = pvc.pvc_fctx->nb_streams;

  for(i = 0; i < streams; i++)
    if(pvc.pvc_cwvec[i] != NULL)
      wrap_codec_deref(pvc.pvc_cwvec[i], 0);

  glw_destroy(top);

  wrap_format_wait(fw);

  if(mp->mp_subtitles) {
    subtitles_free(mp->mp_subtitles);
    mp->mp_subtitles = NULL;
  }
  input_flush_queue(ic);

  glw_focus_stack_activate(&ai->ai_gfs);

  return 0;
}
