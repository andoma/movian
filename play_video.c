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

#include <sys/stat.h>
#include <sys/time.h>
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

  glw_t *pvc_menu_overlay;

  int    pvc_status_fader;

  appi_t *pvc_ai;

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
static int
menu_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  play_video_ctrl_t *pvc = opaque;
  appi_t *ai = pvc->pvc_ai;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_INPUT_EVENT:
    printf("hej!\n");
    break;

  case GLW_SIGNAL_LAYOUT:
    w->glw_alpha = GLW_LP(16, w->glw_alpha, ai->ai_display_menu ? 1.0 : 0.0);
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
 * 
 */
static void
video_menu_form_initialize(play_video_ctrl_t *pvc, appi_t *ai, ic_t *ic,
			   vd_conf_t *vdc, AVFormatContext *fctx,
			   media_pipe_t *mp)
{
  glw_t *t, *m = pvc->pvc_menu_overlay;
  AVCodecContext *ctx;
  struct layout_form_entry_list lfelist;
  int r, i;
  char buf[100];
  const char *p;

  layout_form_entry_options_t deilace_options[] = {
    {"Automatic",          VD_DEILACE_AUTO},
    {"Disabled",           VD_DEILACE_NONE},
    {"Simple",             VD_DEILACE_HALF_RES},
    {"YADIF",              VD_DEILACE_YADIF_FIELD}
  };


  TAILQ_INIT(&lfelist);

  LFE_ADD(&lfelist, "menu");
  
  /**
   * Audio tab
   */
  t = layout_form_add_tab(m,
			  "menu",           "videoplayback/audio-icon",
			  "menu_container", "videoplayback/audio-tab");
  

  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;
    if(ctx->codec_type != CODEC_TYPE_AUDIO)
      continue;

    avcodec_string(buf, sizeof(buf), ctx, 0);
    p = strncasecmp(buf, "audio: ", 7) ? buf : buf + 7;

    layout_form_add_option(t, "audio_tracks", p, i);
  }

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
  
  r = layout_form_initialize(&lfelist, m, &ai->ai_gfs, ic, 1);

}

/**
 * 
 */
int
play_video(const char *fname, appi_t *ai, ic_t *ic, glw_t *parent)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  AVPacket pkt;
  codecwrap_t **cwvec;
  formatwrap_t *fw;
  int i, key;
  media_buf_t *mb;
  media_queue_t *mq;
  int streams;
  //  int64_t cur_pos_pts = AV_NOPTS_VALUE;
  media_pipe_t *mp = &ai->ai_mp;
  vd_conf_t vdc;
  glw_t *top, *vdw;

  int64_t pts, dts, seek_ref, seek_delta, seek_abs;
  inputevent_t ie;
  int gotevent, run;

  play_video_ctrl_t pvc;
  glw_t *w;
  const char *s;

  memset(&pvc, 0, sizeof(play_video_ctrl_t));
  pvc.pvc_ai = ai;

  /**
   * Open input file
   */
  if(av_open_input_file(&fctx, fname, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file\n");
    return -1;
  }

  fctx->flags |= AVFMT_FLAG_GENPTS;

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return -1;
  }

  /**
   * Create top level stack
   */
  top = glw_create(GLW_CONTAINER_Z,
		   GLW_ATTRIB_DISPLACEMENT, 0.0, 0.0, 1.0,
		   GLW_ATTRIB_PARENT, parent,
		   NULL);
  /**
   * Create video output widget
   */
  vd_conf_init(&vdc);
  vdw = vd_create_widget(top, &ai->ai_mp);
  mp_set_video_conf(mp, &vdc);


  /**
   * Status overlay
   */
  pvc.pvc_status_overlay = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_PARENT, top,
	       GLW_ATTRIB_ALPHA, 0.0f,
	       GLW_ATTRIB_SIGNAL_HANDLER, overlay_callback, &pvc, 100,
	       GLW_ATTRIB_FILENAME, "videoplayback/overlay",
	       NULL);

  /**
   * Menu overlay
   */
  pvc.pvc_menu_overlay = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_PARENT, top,
	       GLW_ATTRIB_SIGNAL_HANDLER, menu_callback, &pvc, 100,
	       GLW_ATTRIB_FILENAME, "videoplayback/menu",
	       NULL);

  video_menu_form_initialize(&pvc, ai, ic, &vdc, fctx, mp);

  /**
   * Set title
   */
  if((w = glw_find_by_id(pvc.pvc_status_overlay, "title", 0)) != NULL) {
    s = fctx->title;
    if(*s == 0) {
      /* No stored title */
      s = strrchr(fname, '/');
      s = s ? s + 1 : fname;
    }
    glw_set(w, GLW_ATTRIB_CAPTION, s, NULL);
  }

  /**
   * Set total duration
   */
  layout_update_time(pvc.pvc_status_overlay,
		     "time_total", fctx->duration / AV_TIME_BASE);

  cwvec = alloca(fctx->nb_streams * sizeof(void *));
  memset(cwvec, 0, sizeof(void *) * fctx->nb_streams);
  
  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(fctx, 1);

  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(mp->mp_video.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_VIDEO) {
      mp->mp_video.mq_stream = i;
    }

    if(mp->mp_audio.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_AUDIO) {
      mp->mp_audio.mq_stream = i;
    }

    cwvec[i] = wrap_codec_create(ctx->codec_id, ctx->codec_type, 0, fw, ctx);
  }

  ai->ai_fctx = fctx;
  mp->mp_format = fctx;

  wrap_lock_all_codecs(fw);
  
  mp_set_playstatus(mp, MP_VIDEOSEEK_PLAY);
  mp->mp_videoseekdts = 0;

  media_pipe_acquire_audio(mp);
  mp->mp_feedback = ic;

  if(mp->mp_audio.mq_stream >= 0 && cwvec[mp->mp_audio.mq_stream] != NULL)
    layout_update_codec_info(pvc.pvc_status_overlay, "audioinfo",
			     cwvec[mp->mp_audio.mq_stream]->codec_ctx);
  else
    layout_update_codec_info(pvc.pvc_status_overlay, "audioinfo", NULL);

  if(mp->mp_video.mq_stream >= 0 && cwvec[mp->mp_video.mq_stream] != NULL)
    layout_update_codec_info(pvc.pvc_status_overlay, "videoinfo",
			     cwvec[mp->mp_video.mq_stream]->codec_ctx);
  else
    layout_update_codec_info(pvc.pvc_status_overlay, "videoinfo", NULL);


  wrap_unlock_all_codecs(fw);

  pts         = AV_NOPTS_VALUE;
  dts         = AV_NOPTS_VALUE;
  seek_ref    = AV_NOPTS_VALUE;
  run       = 1;

  glw_focus_stack_deactivate(&ai->ai_gfs);

  while(run) {
    i = av_read_frame(fctx, &pkt);

    if(i < 0) {
      printf("Read error %d\n", i);
      mp_wait(mp, mp->mp_audio.mq_stream != -1, mp->mp_video.mq_stream != -1);
      break;
    }

    ctx = cwvec[pkt.stream_index] ? cwvec[pkt.stream_index]->codec_ctx : NULL;
    mb = NULL;
    
    /* Rescale PTS / DTS to µsec */
    if(pkt.pts != AV_NOPTS_VALUE) {
      pts = av_rescale_q(pkt.pts, fctx->streams[pkt.stream_index]->time_base,
			 AV_TIME_BASE_Q);
    } else {
      pts = AV_NOPTS_VALUE;
    }
 
    if(pkt.dts != AV_NOPTS_VALUE) {
      dts = av_rescale_q(pkt.dts, fctx->streams[pkt.stream_index]->time_base,
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
	av_rescale_q(pkt.duration, fctx->streams[pkt.stream_index]->time_base,
		     AV_TIME_BASE_Q);

      
      mb->mb_cw = wrap_codec_ref(cwvec[pkt.stream_index]);

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

    pv_update_playstatus(&pvc, mp->mp_playstatus);

    if(mp_is_paused(mp)) {
      gotevent = !input_getevent(ic, 1, &ie, NULL);
      media_pipe_acquire_audio(mp);
    } else {
      gotevent = !input_getevent(ic, 0, &ie, NULL);
    }

    if(!gotevent)
      continue;

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
      if(pts != AV_NOPTS_VALUE) {
	pts -= fctx->start_time;

	layout_update_time(pvc.pvc_status_overlay,
			   "time_current",pts / AV_TIME_BASE);
	layout_update_bar(pvc.pvc_status_overlay, "durationbar", 
			  (double)pts / (double)(fctx->duration));
      }
      break;

      /**
       * Keyboard input
       */
    case INPUT_KEY:

      key = ie.u.key;

      seek_abs   = 0;
      seek_delta = 0;

      switch(key) {
      case INPUT_KEY_MENU:
	if(ai->ai_display_menu)
	  glw_focus_stack_activate(&ai->ai_gfs);
	else
	  glw_focus_stack_deactivate(&ai->ai_gfs);
	break;
      case INPUT_KEY_BACK:
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

      if((seek_delta && seek_ref != AV_NOPTS_VALUE) || seek_abs) {
	/* Seeking requested */

	/* Just make it display the seek widget */
	pv_update_playstatus(&pvc, MP_VIDEOSEEK_PLAY);

	if(seek_abs)
	  seek_ref = seek_abs;
	else
	  seek_ref += seek_delta;

	if(seek_ref < fctx->start_time)
	  seek_ref = fctx->start_time;

	printf("Seeking to %.2f\n", seek_ref / 1000000.0);

	i = av_seek_frame(fctx, -1, seek_ref, AVSEEK_FLAG_BACKWARD);

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
  }

  glw_focus_stack_activate(&ai->ai_gfs);

  mp_set_playstatus(mp, MP_STOP);

  wrap_lock_all_codecs(fw);

  mp->mp_info_widget = NULL;
  mp->mp_info_extra_widget = NULL;

  mp->mp_total_time = 0;

  ai->ai_fctx = NULL;
  ai->ai_req_fullscreen = AI_FS_NONE;

  streams = fctx->nb_streams;

  for(i = 0; i < streams; i++) if(cwvec[i] != NULL)
    wrap_codec_deref(cwvec[i], 0);

  glw_destroy(top);

  wrap_format_wait(fw);

  if(mp->mp_subtitles) {
    subtitles_free(mp->mp_subtitles);
    mp->mp_subtitles = NULL;
  }
  input_flush_queue(ic);

  return 0;
}
