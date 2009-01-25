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
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#include <libhts/htssettings.h>

#include <libavformat/avformat.h>

#include "showtime.h"
#include "video_playback.h"
#include "subtitles.h"
#include "event.h"
#include "media.h"
#include "fileaccess/fa_probe.h"


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

  hts_thread_t pvc_thread;

  media_pipe_t *pvc_mp;

  char *pvc_url;

  AVFormatContext *pvc_fctx;

  codecwrap_t **pvc_cwvec;

  int64_t pvc_rcache_last;

  char *pvc_rcache_title;

  int pvc_force_status_display;

  prop_t *pvc_prop_root;
  prop_t *pvc_prop_playstatus;

  prop_t *pvc_prop_videoinfo;
  prop_t *pvc_prop_audioinfo;

} play_video_ctrl_t;



#if 0


/**
 *
 */
static int
video_player_event_handler(event_t *e, void *opaque)
{
#if 0
  play_video_ctrl_t *pvc = opaque;
  appi_t *ai = pvc->pvc_ai;

  if(ai->ai_active == 0)
    return 0;

  switch(ge->ge_type) {

  case EVENT_KEY_SEEK_FAST_BACKWARD:
  case EVENT_KEY_SEEK_BACKWARD:
  case EVENT_KEY_SEEK_FAST_FORWARD:
  case EVENT_KEY_SEEK_FORWARD:
  case EVENT_KEY_PLAYPAUSE:
  case EVENT_KEY_PLAY:
  case EVENT_KEY_PAUSE:
  case EVENT_KEY_STOP:
  case EVENT_KEY_PREV:
  case EVENT_KEY_NEXT:
  case EVENT_KEY_RESTART_TRACK:
    break;
  default:
    return 0;
  }

  glw_event_enqueue(&ai->ai_geq, ge);
  return 1;
#endif
  abort();
}


/**
 * Update text about video/audio stream type
 */
static void
video_player_update_stream_info(play_video_ctrl_t *pvc)
{
  media_pipe_t *mp = pvc->pvc_mp;
  int as = mp->mp_audio.mq_stream;
  int vs = mp->mp_video.mq_stream;
  AVCodecContext *ctx;

  ctx = as >= 0 && pvc->pvc_cwvec[as] != NULL ? 
    pvc->pvc_cwvec[as]->codec_ctx : NULL;
  media_update_codec_info_prop(pvc->pvc_prop_audioinfo, ctx);

  ctx = as >= 0 && pvc->pvc_cwvec[vs] != NULL ? 
    pvc->pvc_cwvec[vs]->codec_ctx : NULL;
  media_update_codec_info_prop(pvc->pvc_prop_videoinfo, ctx);
}

/**
 *
 */
static void
rcache_init(play_video_ctrl_t *pvc, const char *fname)
{
  char *n;
  n = pvc->pvc_rcache_title = strdup(fname);

  while(*n) {
    if(*n == '/' || *n == ':' || *n == '?' || *n == '*' || *n > 127 || *n < 32)
      *n = '_';
    n++;
  }
}

/**
 * Restart cache, store info
 */
static void
rcache_store(play_video_ctrl_t *pvc, int ts)
{
  htsmsg_t *m;

  m = htsmsg_create();
  htsmsg_add_s64(m, "ts", ts);
  hts_settings_save(m, "restartcache/%s", pvc->pvc_rcache_title);
  htsmsg_destroy(m);
}

#if 0

/**
 * Called from GLW when user selecs a different audio track
 */
static void
video_playback_set_audio_track(void *opaque, void *opaque2, int value)
{
  play_video_ctrl_t *pvc = opaque;

  pvc->pvc_mp->mp_audio.mq_stream = value;
  video_player_update_stream_info(pvc);
}
#endif

#if 0
/**
 * Open menu
 */
static void
video_player_open_menu(play_video_ctrl_t *pvc, int toggle)
{
  glw_t *p;
  AVCodecContext *ctx;
  char buf[100];
  //  media_pipe_t *mp = pvc->pvc_mp;
  int i;

  if(pvc->pvc_menu != NULL) {
    if(toggle) {
      pvc->pvc_menu = NULL;
    }
    return;
  }

  pvc->pvc_menu =
    glw_model_create(NULL, "theme://videoplayer/menu.model", pvc->pvc_container,
		     0, pvc->pvc_prop_root);
  
  /**
   * Populate audio tracks
   */
  if((p = glw_find_by_id(pvc->pvc_menu, "audio_tracks", 0)) != NULL) {

    for(i = 0; i < pvc->pvc_fctx->nb_streams; i++) {
      ctx = pvc->pvc_fctx->streams[i]->codec;
      if(ctx->codec_type != CODEC_TYPE_AUDIO)
	continue;
      media_get_codec_info(ctx, buf, sizeof(buf));
      glw_selection_add_text_option(p, buf, video_playback_set_audio_track,
				    pvc, NULL, i, i == mp->mp_audio.mq_stream);
     }
    glw_selection_add_text_option(p, "Off", video_playback_set_audio_track,
				  pvc, NULL, -1, -1 == mp->mp_audio.mq_stream);

  }


  /**
   * Populate video control widgets
   */
  video_menu_attach(pvc->pvc_menu, &pvc->pvc_vdc);

}
#endif




/**
 *
 */
static void
play_video_clock_update(play_video_ctrl_t *pvc, int64_t pts,
			media_pipe_t *mp)
{
  if(pts != AV_NOPTS_VALUE && pts > pvc->pvc_rcache_last) {

    rcache_store(pvc, pts);

    pvc->pvc_rcache_last = pts + AV_TIME_BASE * 5;
  }
}



/**
 * Thread for reading from lavf and sending to lavc
 */
static void
video_player_loop(play_video_ctrl_t *pvc)
{
  //  appi_t *ai = pvc->pvc_ai;
  media_pipe_t *mp = pvc->pvc_mp;
  media_buf_t *mb;
  media_queue_t *mq;
  int64_t pts, dts, seek_ref, seek_delta, seek_abs;
  event_t *e;
  //  glw_event_appmethod_t *gea;
  event_ts_t *et;
  AVCodecContext *ctx;
  AVPacket pkt;
  int run, i;

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

      if(pts != AV_NOPTS_VALUE)
	mb->mb_time = (pts - pvc->pvc_fctx->start_time) / AV_TIME_BASE;
      else
	mb->mb_time = -1;

      /* Enqueue packet */

      mb_enqueue(mp, mq, mb);
    }
    av_free_packet(&pkt);

    media_update_playstatus_prop(mp->mp_prop_playstatus, mp->mp_playstatus);

    if(mp->mp_playstatus == MP_PLAY && mp_is_audio_silenced(mp))
      mp_set_playstatus(mp, MP_PAUSE, 0);

    //    ai->ai_req_fullscreen = mp->mp_playstatus == MP_PLAY && !pvc->pvc_menu;

    e = NULL; // event_get(mp_is_paused(mp) ? -1 : 0, eq);

    seek_abs   = 0;
    seek_delta = 0;

    if(e != NULL) {
      switch(e->e_type) {
#if 0
      case GEV_APPMETHOD:
	gea = (void *)ge;

	if(!strcmp(gea->method, "restart")) {
	  seek_abs = 1;
	}

	if(!strcmp(gea->method, "closeMenu")) {
	  
	  if(pvc->pvc_menu != NULL) {
	    glw_detach(pvc->pvc_menu);
	    pvc->pvc_menu = NULL;
	  }
	}

	break;
#endif

      case EVENT_VIDEO_CLOCK:
	et = (void *)e;
	/**
	 * Feedback from decoders
	 */
	if(et->stream != mp->mp_video.mq_stream)
	  break; /* we only deal with the video stream here */

	if(et->dts != AV_NOPTS_VALUE)
	  seek_ref = et->dts;

	play_video_clock_update(pvc, et->pts, mp);
	break;
#if 0
      case EVENT_KEY_MENU:
	video_player_open_menu(pvc, 1);
	break;

      case EVENT_KEY_STOP:
	run = 0;
	break;
      case EVENT_KEY_RESTART_TRACK:
	seek_abs = 1;
	break;
      case EVENT_KEY_SEEK_FAST_BACKWARD:
	seek_delta = -60000000;
	break;
      case EVENT_KEY_SEEK_BACKWARD:
	seek_delta = -15000000;
	break;
      case EVENT_KEY_SEEK_FAST_FORWARD:
	seek_delta = 60000000;
	break;
      case EVENT_KEY_SEEK_FORWARD:
	seek_delta = 15000000;
	break;

      case EVENT_KEY_PLAYPAUSE:
      case EVENT_KEY_PLAY:
      case EVENT_KEY_PAUSE:
	mp_playpause(mp, e->e_type);
	break;
#endif

      default:
	break;
      }
      event_unref(e);
    }
    if((seek_delta && seek_ref != AV_NOPTS_VALUE) || seek_abs) {
      /* Seeking requested */

      /* Reset restart cache threshold to force writeout */
      pvc->pvc_rcache_last = INT64_MIN;

      /* Just make it display the seek widget */
      media_update_playstatus_prop(mp->mp_prop_playstatus, MP_VIDEOSEEK_PLAY);

      if(seek_abs)
	seek_ref = seek_abs;
      else
	seek_ref += seek_delta;

      if(seek_ref < pvc->pvc_fctx->start_time)
	seek_ref = pvc->pvc_fctx->start_time;

      i = av_seek_frame(pvc->pvc_fctx, -1, seek_ref, AVSEEK_FLAG_BACKWARD);

      if(i < 0)
	printf("Seeking failed\n");

      mp_flush(mp);
      
      mp->mp_videoseekdts = seek_ref;

      switch(mp->mp_playstatus) {
      case MP_VIDEOSEEK_PAUSE:
      case MP_PAUSE:
	mp_set_playstatus(mp, MP_VIDEOSEEK_PAUSE, 0);
	break;
      case MP_VIDEOSEEK_PLAY:
      case MP_PLAY:
	mp_set_playstatus(mp, MP_VIDEOSEEK_PLAY, 0);
	break;
      default:
	abort();
      }
    }
  }
}


/**
 *  Main function for video playback
 */
int
play_video(const char *url, media_pipe_t *mp)
{
  AVCodecContext *ctx;
  formatwrap_t *fw;
  int64_t ts;
  int streams, i;
  play_video_ctrl_t pvc;
  const char *s;
  htsmsg_t *m;
  char faurl[1000];
  void *eh;

  memset(&pvc, 0, sizeof(play_video_ctrl_t));
  //  pvc.pvc_ai = ai;

  /**
   * Open input file
   */

  snprintf(faurl, sizeof(faurl), "showtime:%s", url);
  if(av_open_input_file(&pvc.pvc_fctx, faurl, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file %s\n", url);
    return -1;
  }

  pvc.pvc_fctx->flags |= AVFMT_FLAG_GENPTS;

  if(av_find_stream_info(pvc.pvc_fctx) < 0) {
    av_close_input_file(pvc.pvc_fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return -1;
  }

  pvc.pvc_mp = mp;

  /**
   * Create property tree
   */ 

  pvc.pvc_prop_root = prop_create(NULL, "media");

  pvc.pvc_prop_playstatus = prop_create(pvc.pvc_prop_root,
					     "playstatus");

  prop_set_int(prop_create(pvc.pvc_prop_root, "totaltime"),
		    pvc.pvc_fctx->duration / AV_TIME_BASE);

  pvc.pvc_prop_videoinfo = prop_create(pvc.pvc_prop_root, "videoinfo");

  pvc.pvc_prop_audioinfo = prop_create(pvc.pvc_prop_root, "audioinfo");

  s = pvc.pvc_fctx->title;
  if(*s == 0) {
    /* No stored title */
    s = strrchr(url, '/');
    s = s ? s + 1 : url;
  }
  
  prop_set_string(prop_create(pvc.pvc_prop_root, "title"), s);

  media_set_metatree(mp, pvc.pvc_prop_root);


  /**
   * Init codec contexts
   */
  pvc.pvc_cwvec = alloca(pvc.pvc_fctx->nb_streams * sizeof(void *));
  memset(pvc.pvc_cwvec, 0, sizeof(void *) * pvc.pvc_fctx->nb_streams);
  
  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(pvc.pvc_fctx);

  for(i = 0; i < pvc.pvc_fctx->nb_streams; i++) {
    ctx = pvc.pvc_fctx->streams[i]->codec;

    if(mp->mp_video.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_VIDEO) {
      mp->mp_video.mq_stream = i;
    }

    if(mp->mp_audio.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_AUDIO) {
      mp->mp_audio.mq_stream = i;
    }

    pvc.pvc_cwvec[i] = wrap_codec_create(ctx->codec_id,
					 ctx->codec_type, 0, fw, ctx, 0);
  }

  //  ai->ai_fctx   = pvc.pvc_fctx;
  mp->mp_format = pvc.pvc_fctx;

  /**
   * Restart playback at last position
   */

  mp->mp_videoseekdts = 0;

  rcache_init(&pvc, url);

  m = hts_settings_load("restartcache/%s", pvc.pvc_rcache_title);
  if(m != NULL) {
    if(!htsmsg_get_s64(m, "ts", &ts)) {
      i = av_seek_frame(pvc.pvc_fctx, -1, ts, AVSEEK_FLAG_BACKWARD);
      if(i >= 0)
	mp->mp_videoseekdts = ts;
    }
  }

  mp_set_playstatus(mp, MP_VIDEOSEEK_PAUSE, 0); 

  //  mp->mp_feedback = eq;

  video_player_update_stream_info(&pvc);

  pvc.pvc_rcache_last = INT64_MIN;

  eh = event_handler_register("videoplayer", video_player_event_handler,
			      EVENTPRI_MEDIACONTROLS_VIDEOPLAYBACK, &pvc);

  video_player_loop(&pvc);

  media_clear_metatree(mp);
  
  event_handler_unregister(eh);

  //  ai->ai_req_fullscreen = 0;

  mp_set_playstatus(mp, MP_STOP, 0);

  mp->mp_total_time = 0;

  //  ai->ai_fctx = NULL;

  streams = pvc.pvc_fctx->nb_streams;

  for(i = 0; i < streams; i++)
    if(pvc.pvc_cwvec[i] != NULL)
      wrap_codec_deref(pvc.pvc_cwvec[i]);

  wrap_format_destroy(fw);

  if(mp->mp_subtitles) {
    fprintf(stderr, "subtitles_free(mp->mp_subtitles);\n");
    mp->mp_subtitles = NULL;
  }
  //  event_flushqueue(eq);

  mp_unref(pvc.pvc_mp);
  return 0;
}
#endif


/**
 *
 */
static int64_t
rescale(play_video_ctrl_t *pvc, int64_t ts, int si)
{
  if(ts == AV_NOPTS_VALUE)
    return AV_NOPTS_VALUE;

  return av_rescale_q(ts, pvc->pvc_fctx->streams[si]->time_base,
		      AV_TIME_BASE_Q);
}


/**
 * Thread for reading from lavf and sending to lavc
 */
static void
video_player_loop(play_video_ctrl_t *pvc)
{
  media_pipe_t *mp = pvc->pvc_mp;
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  AVCodecContext *ctx;
  AVPacket pkt;
  int run = 1, r, si;
  event_t *e;
  event_seek_t *es;
  int64_t ts;
  int hold = 0;

  while(run) {
    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      if((r = av_read_frame(pvc->pvc_fctx, &pkt)) < 0) {

	printf("READ error %d\n", r);
	mp_wait(mp, mp->mp_audio.mq_stream != -1, 
		mp->mp_video.mq_stream != -1);
	printf("All fifos empty\n");
	break;
      }

      si = pkt.stream_index;

      ctx = pvc->pvc_cwvec[si] ? pvc->pvc_cwvec[si]->codec_ctx : NULL;

      if(ctx != NULL && si == mp->mp_video.mq_stream) {
	/* Current video stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_VIDEO;
	mq = &mp->mp_video;
	
      } else if(ctx != NULL && si == mp->mp_audio.mq_stream) {
	/* Current audio stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_AUDIO;
	mq = &mp->mp_audio;

      } else {
	/* Check event queue ? */
	av_free_packet(&pkt);
	continue;
      }

      mb->mb_pts      = rescale(pvc, pkt.pts,      si);
      mb->mb_dts      = rescale(pvc, pkt.dts,      si);
      mb->mb_duration = rescale(pvc, pkt.duration, si);

      mb->mb_cw = wrap_codec_ref(pvc->pvc_cwvec[si]);

      /* Move the data pointers from ffmpeg's packet */

      mb->mb_stream = pkt.stream_index;

      mb->mb_data = pkt.data;
      pkt.data = NULL;

      mb->mb_size = pkt.size;
      pkt.size = 0;

      if(mb->mb_pts != AV_NOPTS_VALUE && mb->mb_data_type == MB_AUDIO)
	mb->mb_mts = (mb->mb_pts - pvc->pvc_fctx->start_time) / 1000;
      else
	mb->mb_mts = -1;

      av_free_packet(&pkt);
    }

    /*
     * Try to send the buffer.  If mb_enqueue() returns something we
     * catched an event instead of enqueueing the buffer. In this case
     * 'mb' will be left untouched.
     */

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    switch(e->e_type) {

    case EVENT_PLAYPAUSE:
    case EVENT_PLAY:
    case EVENT_PAUSE:
      hold = mp_update_hold_by_event(hold, e->e_type);
      mp_send_cmd_head(mp, &mp->mp_video, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_send_cmd_head(mp, &mp->mp_audio, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      break;

    case EVENT_SEEK:
      es = (event_seek_t *)e;
      
      ts = es->ts;

      if(ts < pvc->pvc_fctx->start_time)
	ts = pvc->pvc_fctx->start_time;

      r = av_seek_frame(pvc->pvc_fctx, -1, ts, AVSEEK_FLAG_BACKWARD);

      if(r < 0)
	printf("Seeking failed\n");

      mp_flush(mp);

      if(mb != NULL) {
	media_buf_free(mb);
	mb = NULL;
      }
#if 0      
      switch(mp->mp_playstatus) {
      case MP_VIDEOSEEK_PAUSE:
      case MP_PAUSE:
	mp_set_playstatus(mp, MP_VIDEOSEEK_PAUSE, 0);
	break;
      case MP_VIDEOSEEK_PLAY:
      case MP_PLAY:
	mp_set_playstatus(mp, MP_VIDEOSEEK_PLAY, 0);
	break;
      default:
	abort();
      }
#endif
      break;

    default:
      break;
    }

    event_unref(e);
  }

  if(mb != NULL)
    media_buf_free(mb);
}

/**
 *
 */
static void *
video_play_thread(void *aux)
{
  play_video_ctrl_t *pvc = aux;
  AVCodecContext *ctx;
  formatwrap_t *fw;
  int i ;
  media_pipe_t *mp = pvc->pvc_mp;
  char faurl[1000];

  /**
   * Open input file
   */
  snprintf(faurl, sizeof(faurl), "showtime:%s", pvc->pvc_url);
  if(av_open_input_file(&pvc->pvc_fctx, faurl, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file %s\n", pvc->pvc_url);
    return NULL;
  }

  pvc->pvc_fctx->flags |= AVFMT_FLAG_GENPTS;

  if(av_find_stream_info(pvc->pvc_fctx) < 0) {
    av_close_input_file(pvc->pvc_fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return NULL;
  }

  /**
   * Update property metadata
   */

  fa_lavf_load_meta(mp->mp_prop_meta, pvc->pvc_fctx, faurl);

  /**
   * Init codec contexts
   */
  pvc->pvc_cwvec = alloca(pvc->pvc_fctx->nb_streams * sizeof(void *));
  memset(pvc->pvc_cwvec, 0, sizeof(void *) * pvc->pvc_fctx->nb_streams);
  
  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(pvc->pvc_fctx);

  for(i = 0; i < pvc->pvc_fctx->nb_streams; i++) {
    ctx = pvc->pvc_fctx->streams[i]->codec;

    if(mp->mp_video.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_VIDEO)
      mp->mp_video.mq_stream = i;

    if(mp->mp_audio.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_AUDIO)
      mp->mp_audio.mq_stream = i;

    pvc->pvc_cwvec[i] = wrap_codec_create(ctx->codec_id,
					  ctx->codec_type, 0, fw, ctx, 0);
  }

  /**
   * Restart playback at last position
   */

  mp_prepare(mp, MP_GRAB_AUDIO);

  video_player_loop(pvc);

  for(i = 0; i < pvc->pvc_fctx->nb_streams; i++)
    if(pvc->pvc_cwvec[i] != NULL)
      wrap_codec_deref(pvc->pvc_cwvec[i]);

  wrap_format_destroy(fw);

  if(mp->mp_subtitles) {
    fprintf(stderr, "subtitles_free(mp->mp_subtitles);\n");
    mp->mp_subtitles = NULL;
  }
  return NULL;
}




int
play_video(const char *url, media_pipe_t *mp)
{
  play_video_ctrl_t *pvc = calloc(1, sizeof(play_video_ctrl_t));

  pvc->pvc_url = strdup(url);
  pvc->pvc_mp = mp;


  


  hts_thread_create(&pvc->pvc_thread, video_play_thread, pvc);
  return 0;
}




