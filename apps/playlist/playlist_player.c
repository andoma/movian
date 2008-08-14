/*
 *  Playback of audio for playlist
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
#include "event.h"
#include "playlist.h"
#include <layout/layout.h>

static void
playlist_update_playstatus(playlist_entry_t *ple, int status)
{
  const char *s;

  switch(status) {
  case MP_PLAY:
    s = "play";
    break;

  case MP_PAUSE:
    s = "pause";
    break;

  default:
    s = "stop";
    break;
  }
  glw_prop_set_string(ple->ple_prop_playstatus, s);
}

static void
playlist_status_update_next(playlist_entry_t *cur)
{
  playlist_t *pl;
  playlist_entry_t *ple;
  const char *t, *s;

  ple = playlist_advance(cur, 0);

  hts_mutex_lock(&playlistlock);
  pl = cur->ple_pl;
  if(pl != NULL) {

    if(ple != NULL) {
      t = filetag_get_str2(&ple->ple_ftags, FTAG_TITLE);
      s = strrchr(ple->ple_url, '/');
      s = s ? s + 1 : ple->ple_url;
      if(t == NULL)
	t = s;
    } else {
      t = "";
    }
    glw_prop_set_string(pl->pl_prop_next_track_title, t);
  }
  hts_mutex_unlock(&playlistlock);

  if(ple != NULL)
    playlist_entry_unref(ple);

}


static playlist_entry_t *
playlist_play(playlist_entry_t *ple, media_pipe_t *mp, glw_event_queue_t *geq,
	      glw_t *status)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  AVPacket pkt;
  formatwrap_t *fw;
  int i;
  media_buf_t *mb;
  media_queue_t *mq;
  int64_t pts4seek = 0;
  int streams;
  int64_t cur_pos_pts = AV_NOPTS_VALUE;
  int curtime;
  codecwrap_t *cw;
  playlist_entry_t *next = NULL;
  glw_event_t *ge;
  playlist_event_t *pe;
  event_ts_t *et;
  int64_t pts;
  char faurl[1000];

  snprintf(faurl, sizeof(faurl), "showtime:%s", ple->ple_url);

  if(av_open_input_file(&fctx, faurl, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file %s\n", ple->ple_url);
    return playlist_advance(ple, 0);
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return playlist_advance(ple, 0);
  }

  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(fctx, 1);

  cw = NULL;
  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(ctx->codec_type != CODEC_TYPE_AUDIO)
      continue;

    cw = wrap_codec_create(ctx->codec_id, ctx->codec_type, 0, fw, ctx);
    mp->mp_audio.mq_stream = i;
    break;
  }

  mp->mp_format = fctx;

  curtime = -1;

  mp->mp_feedback = geq;

  while(1) {

    if(mp->mp_playstatus == MP_PLAY && mp_is_audio_silenced(mp)) {
      mp_set_playstatus(mp, MP_PAUSE);
      playlist_update_playstatus(ple, MP_PAUSE);
    }


    ge = glw_event_get(mp_is_paused(mp) ? -1 : 0, geq);

    if(ge != NULL) {
      switch(ge->ge_type) {
	
      default:
	break;

      case EVENT_AUDIO_CLOCK:
	et = (void *)ge;
	if(et->pts != AV_NOPTS_VALUE) {

	  pts = et->pts - fctx->start_time;
	  pts /= AV_TIME_BASE;

	  if(curtime != pts) {
	    curtime = pts;

	    hts_mutex_lock(&playlistlock);
	  
	    if(ple->ple_pl != NULL)
	      glw_prop_set_time(ple->ple_pl->pl_prop_time_current,
				ple->ple_time_offset + pts);
	  
	    hts_mutex_unlock(&playlistlock);
	  
	    glw_prop_set_time(ple->ple_prop_time_current, pts);
	  }
	}
	break;

      case EVENT_PLAYLIST:
	pe = (void *)ge;
	playlist_status_update_next(ple);
	switch(pe->type) {
	case PLAYLIST_EVENT_NEWENTRY:
	  /**
	   * A new entry has been added, we don't care about this while
	   * playing, just unref ptr
	   */
	  playlist_entry_unref(pe->ple);
	  break;

	case PLAYLIST_EVENT_PLAYENTRY:
	  /**
	   * User wants us to jump to this entry, do it
	   */
	  next = pe->ple;
	  mp_flush(mp);
	  glw_event_unref(ge);
	  goto out;

	default:
	  abort();
	}
	break;

      case EVENT_KEY_SEEK_FAST_BACKWARD:
	av_seek_frame(fctx, -1, pts4seek - 60000000, AVSEEK_FLAG_BACKWARD);
	goto seekflush;

      case EVENT_KEY_SEEK_BACKWARD:
	av_seek_frame(fctx, -1, pts4seek - 15000000, AVSEEK_FLAG_BACKWARD);
	goto seekflush;

      case EVENT_KEY_SEEK_FAST_FORWARD:
	av_seek_frame(fctx, -1, pts4seek + 60000000, 0);
	goto seekflush;

      case EVENT_KEY_SEEK_FORWARD:
	av_seek_frame(fctx, -1, pts4seek + 15000000, 0);
	goto seekflush;

      case EVENT_KEY_RESTART_TRACK:
	av_seek_frame(fctx, -1, 0, AVSEEK_FLAG_BACKWARD);

      seekflush:
	mp_flush(mp);
	glw_event_flushqueue(geq);
	break;
	
      case EVENT_KEY_PLAYPAUSE:
      case EVENT_KEY_PLAY:
      case EVENT_KEY_PAUSE:
	mp_playpause(mp, ge->ge_type);

	playlist_update_playstatus(ple, mp->mp_playstatus);
	break;

      case EVENT_KEY_PREV:
	next = playlist_advance(ple, 1);
	mp_flush(mp);
	glw_event_unref(ge);
	goto out;
      
      case EVENT_KEY_NEXT:
	next = playlist_advance(ple, 0);
	mp_flush(mp);
	glw_event_unref(ge);
	goto out;

      case EVENT_KEY_STOP:
	next = NULL;
	mp_flush(mp);
	glw_event_unref(ge);
	goto out;
      }
      glw_event_unref(ge);
    }
    
    if(mp_is_paused(mp))
      continue;

    mb = media_buf_alloc();

    i = av_read_frame(fctx, &pkt);

    if(i < 0) {
      /* End of stream (or some other type of error), next track */
      next = playlist_advance(ple, 0);
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

    if(pkt.stream_index == mp->mp_audio.mq_stream) {
      ctx = cw->codec_ctx;

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
      mb->mb_cw = wrap_codec_ref(cw);

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

 out:
  wrap_lock_all_codecs(fw);

  mp->mp_total_time = 0;

  streams = fctx->nb_streams;

  wrap_codec_deref(cw, 0);

  wrap_format_wait(fw);

  playlist_update_playstatus(ple, MP_STOP);

  return next;
}





/**
 *
 */
void *
playlist_player(void *aux)
{
  playlist_player_t *plp = aux;
  media_pipe_t *mp = plp->plp_mp;
  playlist_entry_t *ple = NULL, *next;
  glw_t *status = NULL;
  glw_event_t *ge;
  playlist_event_t *pe;

  while(1) {
 
    while(ple == NULL) {

      /* Got nothing to play, enter STOP mode */

      if(status != NULL) {
	glw_destroy(status);
	status = NULL;
      }

      mp_set_playstatus(mp, MP_STOP);
      ge = glw_event_get(-1, &plp->plp_geq);
      switch(ge->ge_type) {
      default:
	glw_event_unref(ge);
	continue;

      case EVENT_PLAYLIST:
	/**
	 * A new entry has been enqueued, a reference is held for us
	 */
	pe = (void *)ge;
	ple = pe->ple;
	glw_event_unref(ge);
	break;
      }
    }

    mp_set_playstatus(mp, MP_PLAY);

    hts_mutex_lock(&playlistlock);


    /**
     * Create status widget
     */
    status = glw_model_create("theme://playlist/status.model",
			      mp->mp_status_xfader, 0,
			      ple->ple_prop_root,
			      prop_global,
			      ple->ple_pl ? ple->ple_pl->pl_prop_root : NULL, 
			      NULL);
    /**
     * Update playlist widget
     */
    if(ple->ple_pl != NULL)
      glw_prop_set_float(ple->ple_pl->pl_prop_track_current, ple->ple_track);
    hts_mutex_unlock(&playlistlock);


    /**
     * Update track widget
     */

    playlist_update_playstatus(ple, MP_PLAY);
    playlist_status_update_next(ple);

    /**
     * Start playback of track
     */
    next = playlist_play(ple, mp, &plp->plp_geq, status);

    playlist_entry_unref(ple);
    ple = next;
  }
}


