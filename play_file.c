/*
 *  Functions for file/url playback
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
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <ffmpeg/avstring.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "play_file.h"
#include "gl/gl_video.h"
#include "miw.h"
#include "mediaprobe.h"
#include "audio/audio_sched.h"

static glw_t *play_file_menu_audio_setup(glw_t *p, media_pipe_t *mp);


glw_t *
play_file_create_miw(media_pipe_t *mp, mediainfo_t *mi, glw_t **pscp)
{
  glw_t *x, *c;
  char tmp[200];
  const char *s;

  c = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);
  
  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  *pscp = glw_create(GLW_XFADER,
		     GLW_ATTRIB_PARENT, x,
		     NULL);
  

  if(mi->mi_author != NULL) {
    s = tmp;
    snprintf(tmp, sizeof(tmp), "%s - %s", mi->mi_author, mi->mi_title);
  } else {
    s = mi->mi_title;
  }

  /* Title */
  
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_WEIGHT, 20.0f,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_CAPTION, s,
	     NULL);

  miw_audiotime_create(x, mp, 4.0f, GLW_ALIGN_RIGHT);

  return c;
}


static void
play_file_playstatus_widget_update(media_pipe_t *mp)
{
  glw_t *w = mp->mp_playstatus_update_opaque;
  const char *icon;

  switch(mp->mp_playstatus) {
  case MP_STOP:
    icon = "icon://media-playback-stop.png";
    break;
  case MP_PAUSE:
    icon = "icon://media-playback-pause.png";
    break;
  case MP_PLAY:
    icon = "icon://media-playback-start.png";
    break;
  default:
    glw_destroy_childs(w);
    return;
  }

  glw_create(GLW_BITMAP, 
	     GLW_ATTRIB_PARENT, w, 
	     GLW_ATTRIB_FILENAME, icon,
	     NULL);
}



glw_t *
play_file_create_extra_miw(media_pipe_t *mp)
{
  glw_t *x, *y, *c;

  c = glw_create(GLW_CONTAINER, NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);
 
  miw_add_queue(y, &mp->mp_video, "icon://video.png");

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);

  miw_add_queue(y, &mp->mp_audio, "icon://audio.png");

 
  return c;
}





int
play_file(const char *fname, appi_t *ai, ic_t *ic, mediainfo_t *mi, 
	  glw_t *extrainfo, glw_t *parent)
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
  int seekdur = 50000;
  glw_t *meta, *xmeta;
  int streams;
  media_pipe_t *mp = &ai->ai_mp;
  gvp_conf_t gc;
  char albumpath[500];
  char menutitle[32];

  char *s, *albumart;
  struct stat st;
  glw_t *gvpw = NULL;
  glw_t *psc;

  printf("\nPlaying %s\n", fname);

  if(av_open_input_file(&fctx, fname, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file\n");
    return INPUT_KEY_NEXT;
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return INPUT_KEY_NEXT;
  }

  dump_format(fctx, 0, fname, 0);

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

  
  av_strlcpy(menutitle, mi->mi_title, sizeof(menutitle));

  memcpy(menutitle + sizeof(menutitle) - 4, "...", 4);

  menu_push_top_menu(ai, menutitle);

  ai->ai_fctx = fctx;
  mp->mp_format = fctx;

  audio_sched_mp_activate(mp);

  if(mp->mp_video.mq_stream != -1) {
    gvp_conf_init(&gc);
    gvpw = gvp_create(parent, &ai->ai_mp, &gc, 0);
    ai->ai_req_fullscreen = AI_FS_BLANK;
    gvp_menu_setup(appi_menu_top(ai), &gc);
  }

  wrap_lock_all_codecs(fw);

  albumart = NULL;
  snprintf(albumpath, sizeof(albumpath), "%s", fname);
  s = strrchr(albumpath, '/');
  if(s != NULL) {
    s++;
    strcpy(s, "Folder.jpg");
    if(stat(albumpath, &st) == 0) {
      albumart = albumpath;
    }
  }
  

  meta = mp->mp_info_widget = play_file_create_miw(mp, mi, &psc);

  xmeta = mp->mp_info_extra_widget = play_file_create_extra_miw(mp);

  play_file_menu_audio_setup(appi_menu_top(ai), mp);
  
  mp->mp_playstatus_update_callback = play_file_playstatus_widget_update;
  mp->mp_playstatus_update_opaque = psc;

  mp_set_playstatus(mp, MP_PLAY);

  while(1) {
    mp->mp_total_time = fctx->duration / AV_TIME_BASE;


    if(mp_is_paused(mp)) {
      wrap_unlock_all_codecs(fw);
      key = input_getkey(ic, 1);
      wrap_lock_all_codecs(fw);
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
      wrap_unlock_all_codecs(fw);
      mp_flush(mp, 0);
      wrap_lock_all_codecs(fw);
      printf("mp flush completed\n");
      mp_auto_display(mp);
      input_flush_queue(ic);
      key = 0;
      break;

    case INPUT_KEY_RESTART_TRACK:
      av_seek_frame(fctx, av_find_default_stream_index(fctx),
		    0, AVSEEK_FLAG_BYTE);
      key = 0;
      break;

    case INPUT_KEY_STOP:
      mp_set_playstatus(mp, MP_STOP);
      /* FALLTHRU */
    case INPUT_KEY_BACK:
    case INPUT_KEY_NEXT:
    case INPUT_KEY_PREV:
    case INPUT_KEY_CLEAR:
    case INPUT_KEY_ENTER:
    case INPUT_KEY_DELETE:
      wrap_unlock_all_codecs(fw);
      printf("Flushing..\n");
      mp_flush(mp, 1);
      printf("Flushing done..\n");
      wrap_lock_all_codecs(fw);
      break;
	
    case INPUT_KEY_PLAYPAUSE:
    case INPUT_KEY_PLAY:
    case INPUT_KEY_PAUSE:
      mp_playpause(mp, key);
      key = 0;
      break;

    case INPUT_KEY_DECR:
      //      ai->ai_gvp->gvp_scale -= 0.1;
      key = 0;
      break;

    case INPUT_KEY_INCR:
      //      ai->ai_gvp->gvp_scale += 0.1;
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
      printf("readframe = %d\n", i);
      wrap_unlock_all_codecs(fw);
      printf("waiting...\n");
      mp_wait(mp, mp->mp_audio.mq_stream != -1, mp->mp_video.mq_stream != -1);
      printf("relocking...\n");
      wrap_lock_all_codecs(fw);
      key = 0;
      break;
    }

    mb->mb_data = NULL;
    mb->mb_size = pkt.size;

    mb->mb_pts = pkt.pts * 1000000LL *
      fctx->streams[pkt.stream_index]->time_base.num /
      fctx->streams[pkt.stream_index]->time_base.den;
    
    pts4seek = mb->mb_pts;

    
    if(pkt.stream_index == mp->mp_video.mq_stream && 
       cwvec[mp->mp_video.mq_stream] != NULL) {
      ctx = cwvec[mp->mp_video.mq_stream]->codec_ctx;

      seekdur = mb->mb_duration = 1000000LL / 
	av_q2d(fctx->streams[mp->mp_video.mq_stream]->r_frame_rate);

      mb->mb_cw = wrap_codec_ref(cwvec[mp->mp_video.mq_stream]);
      mb->mb_data_type = MB_VIDEO;
      mb->mb_rate = 0;
      mb->mb_keyframe = 1;
      mb->mb_data = malloc(pkt.size);
      memcpy(mb->mb_data, pkt.data, pkt.size);
      mq = &mp->mp_video;

    } else if(pkt.stream_index == mp->mp_audio.mq_stream && 
	      cwvec[mp->mp_audio.mq_stream] != NULL) {

      ctx = cwvec[mp->mp_audio.mq_stream]->codec_ctx;

      mb->mb_time = (pkt.pts * 
		       av_q2d(fctx->streams[pkt.stream_index]->time_base)) -
	  fctx->start_time / AV_TIME_BASE;
	

      mb->mb_data_type = MB_AUDIO;
      mb->mb_cw = wrap_codec_ref(cwvec[mp->mp_audio.mq_stream]);

      mb->mb_data = malloc(pkt.size);
      memcpy(mb->mb_data, pkt.data, pkt.size);
      mq = &mp->mp_audio;
    } else {
      mq = NULL;
    }

    if(mq != NULL) {
      audio_sched_mp_activate(mp);
      wrap_unlock_all_codecs(fw);
      mb_enqueue(mp, mq, mb);
      av_free_packet(&pkt);
      wrap_lock_all_codecs(fw);
      continue;
    }
    
    av_free_packet(&pkt);

    if(mb->mb_data != NULL)
      free(mb->mb_data);
    free(mb);

  }

  mp->mp_playstatus_update_callback = NULL;
  mp->mp_info_widget = NULL;
  mp->mp_info_extra_widget = NULL;
  glw_destroy(meta);
  glw_destroy(xmeta);


  menu_pop_top_menu(ai);

  mp_set_playstatus(mp, MP_PLAY);

  mp->mp_total_time = 0;

  ai->ai_fctx = NULL;
  ai->ai_req_fullscreen = AI_FS_NONE;

  streams = fctx->nb_streams;

  for(i = 0; i < streams; i++) {
    printf("Closing stream %d ...\n (%p)", i, cwvec[i]);
    if(cwvec[i] != NULL) {
      printf("\t%s\n", cwvec[i]->codec->name);
      wrap_codec_deref(cwvec[i], 0);
    } else {
      printf("\tnot open\n");
    }
  }

  wrap_format_wait(fw);

  printf("fileplay deactivate\n");
  audio_sched_mp_deactivate(mp, 1);
  printf("fileplay deactivate done\n");

  printf("video deactivate\n");

  if(gvpw != NULL)
    glw_destroy(gvpw);

  mp_set_playstatus(mp, MP_STOP);
  return key;
}


/******************************************************************************
 *
 * Menus
 *
 */



static int
audio_menu_atrack(glw_t *w, glw_signal_t signal, ...)
{
  time_t t;
  glw_t *c;
  media_pipe_t *mp = glw_get_opaque(w);
  AVFormatContext *fctx = mp->mp_format;
  AVStream *st;
  char buf[256], *p;

  switch(signal) {
  case GLW_SIGNAL_PRE_LAYOUT:
    time(&t);

    if(t == w->glw_holdtime)
      return w->glw_flags & GLW_HIDDEN ? 1 : 0;
    w->glw_holdtime = t;

    if((c = glw_find_by_class(w, GLW_BITMAP)) != NULL)
      c->glw_alpha = (mp->mp_audio.mq_stream == glw_get_u32(w)) ? 1 : 0;

    if(glw_get_u32(w) == -1)
      return 0;

    if(glw_get_u32(w) >= fctx->nb_streams)
      return 1;

    st = fctx->streams[glw_get_u32(w)];
    if(st->codec->codec_type != CODEC_TYPE_AUDIO)
      return 1;
 
    avcodec_string(buf, sizeof(buf), st->codec, 0);
    p = strncasecmp(buf, "audio: ", 7) ? buf : buf + 7;
    if((c = glw_find_by_class(w, GLW_TEXT_BITMAP)) != NULL)
      glw_set(c, GLW_ATTRIB_CAPTION, p, NULL);

    return 0;

  case GLW_SIGNAL_CLICK:
    mp->mp_audio.mq_stream = glw_get_u32(w);
    return 1;

  default:
    return 0;
  }
}



static glw_t *
play_file_menu_audio_setup(glw_t *p, media_pipe_t *mp)
{
  glw_t *v, *w;
  int i;

  v = menu_create_submenu(p, "icon://audio.png", "Audio tracks", 1);

  for(i = 0; i < 16; i++) {
    w = menu_create_item(v, "icon://menu-current.png", "",
			 audio_menu_atrack, mp, i, 0);
    w->glw_flags |= GLW_HIDDEN;
  }
  
  menu_create_item(v, "icon://menu-current.png", "(off)",
		   audio_menu_atrack, mp, -1, 0);

  return v;
}


