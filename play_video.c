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
  int64_t pts4seek = 0;
  int streams;
  int64_t cur_pos_pts = AV_NOPTS_VALUE;
  media_pipe_t *mp = &ai->ai_mp;
  vd_conf_t vdc;
  glw_t *top, *overlay, *w, *vdw;
  struct layout_form_entry_list overlay_lfelist;
  const char *s;
  int curtime = -1;

  /**
   * Create top level stack
   */
  top = glw_create(GLW_ZSTACK,
		   GLW_ATTRIB_PARENT, parent,
		   NULL);

  /**
   * Create video output widget
   */
  vd_conf_init(&vdc);
  vdw = vd_create_widget(top, &ai->ai_mp);
  mp_set_video_conf(mp, &vdc);
 
  /**
   * Control overlay
   */
  overlay = glw_create(GLW_MODEL,
		       GLW_ATTRIB_PARENT, top,
		       GLW_ATTRIB_FILENAME, "videoplayback/overlay",
		       NULL);

  TAILQ_INIT(&overlay_lfelist);

  LFE_ADD_BTN(&overlay_lfelist, "player_play",  OVERLAY_BUTTON_PLAYPAUSE);
  LFE_ADD_BTN(&overlay_lfelist, "player_prev",  OVERLAY_BUTTON_PREV);
  LFE_ADD_BTN(&overlay_lfelist, "player_rew",   OVERLAY_BUTTON_REW);
  LFE_ADD_BTN(&overlay_lfelist, "player_fwd",   OVERLAY_BUTTON_FWD);
  LFE_ADD_BTN(&overlay_lfelist, "player_end",   OVERLAY_BUTTON_END);
  LFE_ADD_BTN(&overlay_lfelist, "player_stop",  OVERLAY_BUTTON_STOP);

  LFE_ADD_BTN(&overlay_lfelist, "player_video_settings", 
	      OVERLAY_BUTTON_VSETTINGS);

  LFE_ADD_BTN(&overlay_lfelist, "player_audio_settings",
	      OVERLAY_BUTTON_ASETTINGS);

  LFE_ADD_BTN(&overlay_lfelist, "player_subtitle_settings",
	      OVERLAY_BUTTON_SSETTINGS);


  layout_form_initialize(&overlay_lfelist, overlay, &ai->ai_gfs, ic, 1);




  



  printf("\nPlaying %s\n", fname);

  if(av_open_input_file(&fctx, fname, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file\n");
    return -1;
  }

  fctx->flags |= AVFMT_FLAG_GENPTS;

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return INPUT_KEY_NEXT;
  }


  /**
   * Set title
   */
  if((w = glw_find_by_id(overlay, "title", 0)) != NULL) {
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
  layout_update_time(overlay, "time_total", fctx->duration / AV_TIME_BASE);

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
  
  mp_set_playstatus(mp, MP_PLAY);
  media_pipe_acquire_audio(mp);

  layout_update_codec_info(overlay, "audioinfo",
			   mp->mp_audio.mq_stream >= 0 ?
			   cwvec[mp->mp_audio.mq_stream]->codec_ctx : NULL);

  layout_update_codec_info(overlay, "videoinfo",
			   mp->mp_video.mq_stream >= 0 ?
			   cwvec[mp->mp_video.mq_stream]->codec_ctx : NULL);

  wrap_unlock_all_codecs(fw);

  while(1) {

    /**
     * If current time feedback (from audio output) changes,
     * update overlay
     */ 
    if(curtime != mp->mp_time_feedback) {
      float v;

      curtime = mp->mp_time_feedback;
      layout_update_time(overlay, "time_current", curtime);

      v = (float)curtime / (float)(fctx->duration / AV_TIME_BASE);
      layout_update_bar(overlay, "durationbar", v);

    }

    if(mp_is_paused(mp)) {
      key = input_getkey(ic, 1);
      media_pipe_acquire_audio(mp);
    } else {
      key = input_getkey(ic, 0);
    }

    switch(key) {

    case INPUT_KEY_SEEK_FAST_BACKWARD:
      av_seek_frame(fctx, -1, pts4seek - 60000000, AVSEEK_FLAG_BACKWARD);
    case INPUT_KEY_SEEK_BACKWARD:
      av_seek_frame(fctx, -1, pts4seek - 15000000, AVSEEK_FLAG_BACKWARD);
      goto seekflush;
    case INPUT_KEY_SEEK_FAST_FORWARD:
      av_seek_frame(fctx, -1, pts4seek + 60000000, 0);
      goto seekflush;
    case INPUT_KEY_SEEK_FORWARD:
      av_seek_frame(fctx, -1, pts4seek + 15000000, 0);
      goto seekflush;

    seekflush:
      mp_flush(mp);
      mp_auto_display(mp);
      input_flush_queue(ic);
      key = 0;
      break;

    case INPUT_KEY_PLAYPAUSE:
    case INPUT_KEY_PLAY:
    case INPUT_KEY_PAUSE:
      mp_playpause(mp, key);
      key = 0;
      break;

    default:
      key = 0;
      break;
    }
    
    if(key)
      break;

    if(mp_is_paused(mp))
      continue;

    mb = media_buf_alloc();

    i = av_read_frame(fctx, &pkt);

    if(i < 0) {
      mp_wait(mp, mp->mp_audio.mq_stream != -1, mp->mp_video.mq_stream != -1);
      key = 0;
      break;
    }

    mb->mb_data = NULL;
    mb->mb_size = pkt.size;

    if(pkt.pts != AV_NOPTS_VALUE) {
      mb->mb_pts = av_rescale_q(pkt.pts,
				fctx->streams[pkt.stream_index]->time_base,
				AV_TIME_BASE_Q);
      pts4seek = mb->mb_pts;
    } else {
      mb->mb_pts = AV_NOPTS_VALUE;
    }
    
    mb->mb_duration = av_rescale_q(pkt.duration,
				   fctx->streams[pkt.stream_index]->time_base,
				   AV_TIME_BASE_Q);

    if(pkt.stream_index == mp->mp_video.mq_stream && 
       cwvec[mp->mp_video.mq_stream] != NULL) {
      ctx = cwvec[mp->mp_video.mq_stream]->codec_ctx;

      mb->mb_cw = wrap_codec_ref(cwvec[mp->mp_video.mq_stream]);
      mb->mb_data_type = MB_VIDEO;
      mb->mb_rate = 0;
      mb->mb_keyframe = 1;
      mb->mb_data = malloc(pkt.size + FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(mb->mb_data, pkt.data, pkt.size);
      memset(mb->mb_data + pkt.size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
      mq = &mp->mp_video;

    } else if(pkt.stream_index == mp->mp_audio.mq_stream && 
	      cwvec[mp->mp_audio.mq_stream] != NULL) {

      ctx = cwvec[mp->mp_audio.mq_stream]->codec_ctx;

      if(mb->mb_pts != AV_NOPTS_VALUE) {
	if(fctx->start_time != AV_NOPTS_VALUE)
	  cur_pos_pts = mb->mb_pts - fctx->start_time;
	else
	  cur_pos_pts = mb->mb_pts;
      }

      if(cur_pos_pts != AV_NOPTS_VALUE)
	mb->mb_time = cur_pos_pts / AV_TIME_BASE;
      else
	mb->mb_time = 0;

      mb->mb_data_type = MB_AUDIO;
      mb->mb_cw = wrap_codec_ref(cwvec[mp->mp_audio.mq_stream]);

      mb->mb_data = malloc(pkt.size +  FF_INPUT_BUFFER_PADDING_SIZE);
      memset(mb->mb_data + pkt.size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
 
      memcpy(mb->mb_data, pkt.data, pkt.size);
      mq = &mp->mp_audio;
    } else {
      mq = NULL;
    }

    if(mq != NULL) {
      mb_enqueue(mp, mq, mb);
      av_free_packet(&pkt);
      continue;
    }
    
    av_free_packet(&pkt);

    if(mb->mb_data != NULL)
      free(mb->mb_data);
    free(mb);

  }

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

  if(vdw != NULL) 
    glw_destroy(vdw);

  wrap_format_wait(fw);

  if(mp->mp_subtitles) {
    subtitles_free(mp->mp_subtitles);
    mp->mp_subtitles = NULL;
  }

  return key;
}
