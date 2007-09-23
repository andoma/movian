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
#include <libgen.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <ffmpeg/avstring.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "play_file.h"
#include "gl/video_decoder.h"
#include "miw.h"
#include "mediaprobe.h"
#include "subtitles.h"

static glw_t *play_file_menu_audio_setup(glw_t *p, media_pipe_t *mp);

static int play_file_pre_launch_menu(const char *fname, appi_t *ai,
				     mediainfo_t *mi, AVFormatContext *fctx,
				     media_pipe_t *mp);


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

  *pscp = glw_create(GLW_FLIPPER,
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

  miw_audiotime_create(x, mp, 3.0f, GLW_ALIGN_RIGHT);

  return c;
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


static void
play_file_draw_status(glw_t *xfader, media_pipe_t *mp)
{
  const char *icon;

  switch(mp->mp_playstatus) {
  case MP_PAUSE:
    icon = "icon://media-playback-pause.png";
    break;
  case MP_PLAY:
    icon = "icon://media-playback-start.png";
    break;
  default:
    glw_destroy_childs(xfader);
    return;
  }

  glw_create(GLW_BITMAP, 
	     GLW_ATTRIB_PARENT, xfader,
	     GLW_ATTRIB_FILENAME, icon,
	     NULL);
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
  glw_t *meta, *xmeta, *amenu, *vmenu;
  int streams;
  int64_t cur_pos_pts = AV_NOPTS_VALUE;
  media_pipe_t *mp = &ai->ai_mp;
  vd_conf_t vdc;
  char albumpath[500];
  char *s, *albumart;
  struct stat st;
  glw_t *vdw = NULL;
  glw_t *psc;

  printf("\nPlaying %s\n", fname);

  if(av_open_input_file(&fctx, fname, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file\n");
    return INPUT_KEY_NEXT;
  }

  fctx->flags |= AVFMT_FLAG_GENPTS;

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

  if(mp->mp_video.mq_stream != -1) {

    switch((key = play_file_pre_launch_menu(fname, ai, mi, fctx, mp))) {
    case INPUT_KEY_BACK:
    case INPUT_KEY_STOP:
      goto noplay;
  default:
    break;
    }
  }

  ai->ai_fctx = fctx;
  mp->mp_format = fctx;

  if(mp->mp_video.mq_stream != -1) {
    vd_conf_init(&vdc);
    vdw = vd_create_widget(parent, &ai->ai_mp);
    ai->ai_req_fullscreen = AI_FS_BLANK;
    mp_set_video_conf(mp, &vdc);
    vmenu = vd_menu_setup(appi_menu_top(ai), &vdc);
  } else {
    vmenu = NULL;
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
  amenu = play_file_menu_audio_setup(appi_menu_top(ai), mp);
  
  mp_set_playstatus(mp, MP_PLAY);
  media_pipe_acquire_audio(mp);
  wrap_unlock_all_codecs(fw);

  play_file_draw_status(psc, mp);

  while(1) {

    if(fctx->duration == AV_NOPTS_VALUE) {
      mp->mp_total_time = -1;
    } else {
      mp->mp_total_time = fctx->duration / AV_TIME_BASE;
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

    case INPUT_KEY_RESTART_TRACK:
      av_seek_frame(fctx, av_find_default_stream_index(fctx),
		    0, AVSEEK_FLAG_BYTE);
      key = 0;
      break;

    case INPUT_KEY_STOP:
    case INPUT_KEY_BACK:
    case INPUT_KEY_NEXT:
    case INPUT_KEY_PREV:
    case INPUT_KEY_CLEAR:
    case INPUT_KEY_ENTER:
    case INPUT_KEY_DELETE:
      break;
	
    case INPUT_KEY_PLAYPAUSE:
    case INPUT_KEY_PLAY:
    case INPUT_KEY_PAUSE:
      mp_playpause(mp, key);
      play_file_draw_status(psc, mp);
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
      mb->mb_data = malloc(pkt.size);
      memcpy(mb->mb_data, pkt.data, pkt.size);
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

      mb->mb_data = malloc(pkt.size);
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
  glw_destroy(meta);
  glw_destroy(xmeta);
  glw_destroy(amenu);
  if(vmenu != NULL)
    glw_destroy(vmenu);

  mp->mp_total_time = 0;

  ai->ai_fctx = NULL;
  ai->ai_req_fullscreen = AI_FS_NONE;

 noplay:

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


/******************************************************************************
 *
 * Menus
 *
 */

static int
audio_menu_atrack(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  glw_t *c;
  media_pipe_t *mp = opaque;
  char buf[256], *p;
  AVStream *st;
  AVFormatContext *fctx;
  int stream_index = glw_get_u32(w);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:

    if(walltime == w->glw_holdtime)
      return 0;

    w->glw_holdtime = walltime;
    fctx = mp->mp_format;

    if((c = glw_find_by_class(w, GLW_BITMAP)) != NULL)
      c->glw_alpha = (mp->mp_audio.mq_stream == stream_index) ? 1 : 0;

    if(stream_index < 0)
      return 1;
 
    if(stream_index < fctx->nb_streams) {

      st = fctx->streams[stream_index];
      if(st->codec->codec_type != CODEC_TYPE_AUDIO) {
	glw_set(w, GLW_ATTRIB_HIDDEN, 1, NULL);
      } else {
	glw_set(w, GLW_ATTRIB_HIDDEN, 0, NULL);

	avcodec_string(buf, sizeof(buf), st->codec, 0);
	p = strncasecmp(buf, "audio: ", 7) ? buf : buf + 7;
	if((c = glw_find_by_class(w, GLW_TEXT_BITMAP)) != NULL)
	  glw_set(c, GLW_ATTRIB_CAPTION, p, NULL);
      }
    } else {
      glw_set(w, GLW_ATTRIB_HIDDEN, 1, NULL);
    }
    return 1;

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
  }
  
  menu_create_item(v, "icon://menu-current.png", "(off)",
		   audio_menu_atrack, mp, -1, 0);

  return v;
}



/******************************************************************************
 *
 * Pre launch menu
 *
 */

typedef struct pre_launch {
  glw_t *menu;
  appi_t *ai;

  glw_vertex_anim_t anim;

} pre_launch_t;

static int 
pre_launch_sub_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(opaque);
  default:
    break;
  }

  return 0;
}

static int 
pre_launch_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  pre_launch_t *pla = opaque;
  glw_rctx_t *rc, rc0;
  glw_vertex_t v;
  inputevent_t *ie;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&pla->ai->ai_ic, ie);
    break;

  case GLW_SIGNAL_LAYOUT:
    rc = va_arg(ap, void *);
    glw_vertex_anim_fwd(&pla->anim, 0.05);
    glw_layout(pla->menu, rc);
    break;

  case GLW_SIGNAL_RENDER:
    rc = va_arg(ap, void *);
    glw_vertex_anim_read(&pla->anim, &v);
    rc0 = *rc;
    rc0.rc_alpha = rc->rc_alpha * v.z;
    glw_render(pla->menu, &rc0);
    break;

  default:
    break;
  }

  return 0;
}

static glw_t *
menu_entry_header(glw_t *p, const char *title)
{
  glw_t *y, *r;

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, p,
		 NULL);
		 
		 
  r = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 GLW_ATTRIB_WEIGHT, 0.35,
		 NULL);
  
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, r,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, title,
	     NULL);
  return y;
}



static glw_t *
pre_launch_audio_select(glw_t *parent, AVFormatContext *fctx)
{
  AVCodecContext *ctx;
  int i, nstreams = 0;
  glw_t *hlist = NULL, *y = NULL;
  char *p, buf[50];

  for(i = 0; i < fctx->nb_streams; i++)
    if(fctx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO)
      nstreams++;

  if(nstreams < 2)
    return NULL;


  y = menu_entry_header(parent, "Select audio track");
  hlist = glw_create(GLW_HLIST,
		     GLW_ATTRIB_PARENT, y,
		     NULL);

  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;
    if(ctx->codec_type != CODEC_TYPE_AUDIO)
      continue;

    avcodec_string(buf, sizeof(buf), ctx, 0);
    p = strncasecmp(buf, "audio: ", 7) ? buf : buf + 7;

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_PARENT, hlist,
	       GLW_ATTRIB_U32, i,
	       GLW_ATTRIB_CAPTION, p,
	       GLW_ATTRIB_ASPECT, 30.0f,
	       NULL);
  }
  return y;
}




static glw_t *
pre_launch_subtitles_select(glw_t *parent, const char *filename)
{
  glw_t *hlist = NULL, *y = NULL;
  char *c, *dname, *p, buf[500];
  DIR *dir;
  struct dirent *d;
  subtitle_format_t type;

  c = strdup(filename);
  dname = dirname(c);

  if((dir = opendir(dname)) != NULL) {
    while((d = readdir(dir)) != NULL) {
      p = d->d_name;
      if(*p == '.')
	continue;
	
      snprintf(buf, sizeof(buf), "%s/%s", dname, d->d_name);

      type = subtitle_probe_file(buf);

      if(type == SUBTITLE_FORMAT_UNKNOWN)
	continue;

      if(y == NULL) {
	y = menu_entry_header(parent, "Select subtitles");
	hlist = glw_create(GLW_HLIST,
			   GLW_ATTRIB_PARENT, y,
			   NULL);
      }

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		 GLW_ATTRIB_PARENT, hlist,
		 GLW_ATTRIB_CAPTION, p,
		 GLW_ATTRIB_SIGNAL_HANDLER, pre_launch_sub_callback, 
		 strdup(buf), 0,
		 GLW_ATTRIB_ASPECT, 30.0f,
		 NULL);
    }
  }
  if(y != NULL) {
    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_PARENT_HEAD, hlist,
	       GLW_ATTRIB_CAPTION, "No subtitles",
	       GLW_ATTRIB_SIGNAL_HANDLER, pre_launch_sub_callback, NULL, 0,
	       GLW_ATTRIB_ASPECT, 30.0f,
	       NULL);
  }
  free(c);

  return y;
}





static int
play_file_pre_launch_menu(const char *fname, appi_t *ai, mediainfo_t *mi,
			  AVFormatContext *fctx, media_pipe_t *mp)
{
  pre_launch_t pla;
  inputevent_t ie;
  int done = 0;
  char title[100];
  glw_t *sel, *wsave;
  glw_t *amenu, *play, *smenu;
  glw_vertex_t v;
  int fadeout = 0;
  const char *subtitles;

  snprintf(title, sizeof(title), "%s - %d:%02d",
	   mi->mi_title, mi->mi_duration / 60, mi->mi_duration % 60);

  glw_vertex_anim_init(&pla.anim, 1.0f, 0.0f, 0.0f, 
		       GLW_VERTEX_ANIM_SIN_LERP);
  glw_lock();

  pla.ai = ai;
  wsave = ai->ai_widget;

  ai->ai_widget = 
    glw_create(GLW_EXT,
	       GLW_ATTRIB_SIGNAL_HANDLER, pre_launch_widget_callback, &pla, 0,
	       NULL);

  pla.menu = glw_create(GLW_ARRAY,
			GLW_ATTRIB_X_SLICES, 1,
			GLW_ATTRIB_Y_SLICES, 5,
			GLW_ATTRIB_SIDEKICK, bar_title(title),
			NULL);
  glw_unlock();

  glw_vertex_anim_set3f(&pla.anim, 0.2, 0.0, 1.0);

  amenu = pre_launch_audio_select(pla.menu, fctx);
  smenu = pre_launch_subtitles_select(pla.menu, fname);

  

  if(amenu != NULL || smenu != NULL) {

    play = glw_create(GLW_BITMAP,
		      GLW_ATTRIB_FILENAME, "icon://media-playback-start.png",
		      GLW_ATTRIB_PARENT, pla.menu,
		      NULL);

    while(!done) {
 
      input_getevent(&ai->ai_ic, 1, &ie, NULL);

      sel = pla.menu->glw_selected;
      if(sel)
	sel = glw_find_by_class(sel, GLW_HLIST);

      switch(ie.type) {
      default:
	break;

      case INPUT_KEY:

	switch(ie.u.key) {

	default:
	  break;

	case INPUT_KEY_UP:
	  glw_nav_signal(pla.menu, GLW_SIGNAL_UP);
	  break;

	case INPUT_KEY_DOWN:
	  glw_nav_signal(pla.menu, GLW_SIGNAL_DOWN);
	  break;

	case INPUT_KEY_LEFT:
	  if(sel != NULL)
	    glw_nav_signal(sel, GLW_SIGNAL_LEFT);
	  break;
	
	case INPUT_KEY_RIGHT:
	  if(sel != NULL)
	    glw_nav_signal(sel, GLW_SIGNAL_RIGHT);
	  break;

	case INPUT_KEY_ENTER:
	  if(pla.menu->glw_selected == play)
	    done = ie.u.key;
	  break;
	    
	case INPUT_KEY_BACK:
	  fadeout = 1;
	case INPUT_KEY_PLAY:
	  done = ie.u.key;
	  break;
	}
      }
    }
    
    glw_vertex_anim_set3f(&pla.anim, 0.0, 0.0, 0.0);
    
    if(fadeout) while(1) {
      usleep(100000);
      glw_vertex_anim_read(&pla.anim, &v);
      if(v.z < 0.01)
	break;
    }
  }

  if(amenu != NULL) {
    sel = glw_find_by_class(amenu, GLW_HLIST);
    sel = sel ? sel->glw_selected : NULL;
    if(sel)
      mp->mp_audio.mq_stream = glw_get_u32(sel);
  }

  if(smenu != NULL) {
    sel = glw_find_by_class(smenu, GLW_HLIST);
    sel = sel ? sel->glw_selected : NULL;
    if(sel) {
      subtitles = (char *)glw_get_opaque(sel, pre_launch_sub_callback);
      if(subtitles)
	mp->mp_subtitles = subtitles_load(subtitles);
    }
  }

  ai->ai_widget = wsave;
  glw_destroy(pla.menu);

  return done;
}




