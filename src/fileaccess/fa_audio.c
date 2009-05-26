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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "showtime.h"
#include "fa_audio.h"
#include "event.h"
#include "media.h"
#include "fa_probe.h"
#include "fileaccess.h"
#include "notifications.h"



/**
 *
 */
static int64_t
rescale(AVFormatContext *fctx, int64_t ts, int si)
{
  if(ts == AV_NOPTS_VALUE)
    return AV_NOPTS_VALUE;

  return av_rescale_q(ts, fctx->streams[si]->time_base, AV_TIME_BASE_Q);
}


/**
 *
 */
static void
seekflush(media_pipe_t *mp, media_buf_t **mbp)
{
  mp_flush(mp);
  
  if(*mbp != NULL) {
    media_buf_free(*mbp);
    *mbp = NULL;
  }
}

/**
 *
 */
event_t *
be_file_playaudio(const char *url, media_pipe_t *mp,
		  char *errbuf, size_t errlen)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  AVPacket pkt;
  formatwrap_t *fw;
  int i, r, si;
  media_buf_t *mb = NULL;
  media_queue_t *mq;
  event_seek_t *es;
  int64_t ts, pts4seek = 0;
  codecwrap_t *cw;
  char faurl[1000];
  event_t *e;
  int hold = 0, lost_focus = 0;

  mp_set_playstatus_by_hold(mp, hold);

  snprintf(faurl, sizeof(faurl), "showtime:%s", url);

  if(av_open_input_file(&fctx, faurl, NULL, 0, NULL) != 0) {
    snprintf(errbuf, errlen, "Unable to open input file %s\n", url);
    return NULL;
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    snprintf(errbuf, errlen, "Unable to find stream info");
    return NULL;
  }

  TRACE(TRACE_DEBUG, "Audio", "Starting playback of %s", url);

  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(fctx);

  cw = NULL;
  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(ctx->codec_type != CODEC_TYPE_AUDIO)
      continue;

    cw = wrap_codec_create(ctx->codec_id, ctx->codec_type, 0, fw, ctx, 0);
    mp->mp_audio.mq_stream = i;
    break;
  }

  mp_become_primary(mp);
  mq = &mp->mp_audio;

  while(1) {

    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      if((r = av_read_frame(fctx, &pkt)) < 0) {
	e = event_create_type(EVENT_EOF);
	break;
      }

      si = pkt.stream_index;

      if(si == mp->mp_audio.mq_stream) {
	/* Current audio stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_AUDIO;

      } else {
	/* Check event queue ? */
	av_free_packet(&pkt);
	continue;
      }


      mb->mb_pts      = rescale(fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(fctx, pkt.dts,      si);
      mb->mb_duration = rescale(fctx, pkt.duration, si);

      mb->mb_cw = wrap_codec_ref(cw);

      /* Move the data pointers from ffmpeg's packet */

      mb->mb_stream = pkt.stream_index;

      av_dup_packet(&pkt);

      mb->mb_data = pkt.data;
      pkt.data = NULL;

      mb->mb_size = pkt.size;
      pkt.size = 0;

      if(mb->mb_pts != AV_NOPTS_VALUE) {
	mb->mb_time = mb->mb_pts - fctx->start_time;
	pts4seek = mb->mb_pts;
      } else
	mb->mb_time = AV_NOPTS_VALUE;


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

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {

      mp_flush(mp);
      break;

    } else if(event_is_type(e, EVENT_SEEK)) {

      es = (event_seek_t *)e;
      ts = es->ts + fctx->start_time;
      if(ts < fctx->start_time)
	ts = fctx->start_time;
      av_seek_frame(fctx, -1, ts, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);
      
    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      av_seek_frame(fctx, -1, pts4seek - 60000000, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      av_seek_frame(fctx, -1, pts4seek - 15000000, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      av_seek_frame(fctx, -1, pts4seek + 60000000, 0);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      av_seek_frame(fctx, -1, pts4seek + 15000000, 0);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_RESTART_TRACK)) {

      av_seek_frame(fctx, -1, 0, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      lost_focus = 0;
      mp_set_playstatus_by_hold(mp, hold);

    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	mp_send_cmd_head(mp, mq, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold);
      }

    } else if(event_is_action(e, ACTION_PREV_TRACK) ||
	      event_is_action(e, ACTION_NEXT_TRACK) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp);
      break;
    }
    event_unref(e);
  }

  if(mb != NULL)
    media_buf_free(mb);

  wrap_codec_deref(cw);
  wrap_format_deref(fw);

  if(hold) { 
    // If we were paused, release playback again.
    mp_send_cmd(mp, mq, MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, 0);
  }

  return e;
}
