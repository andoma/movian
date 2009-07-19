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

#include <libavformat/avformat.h>

#include "showtime.h"
#include "fa_video.h"
#include "event.h"
#include "media.h"
#include "fa_probe.h"
#include "fileaccess.h"
#include "dvd/dvd.h"
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
static int64_t
video_seek(AVFormatContext *fctx, media_pipe_t *mp, media_buf_t **mbp,
	   int64_t pos, int backward, const char *txt)
{

  pos = FFMAX(fctx->start_time, FFMIN(fctx->start_time + fctx->duration, pos));

  TRACE(TRACE_DEBUG, "Video", "seek %s to %.2f", txt, 
	(pos - fctx->start_time) / 1000000.0);
 
  av_seek_frame(fctx, -1, pos, backward ? AVSEEK_FLAG_BACKWARD : 0);

  mp->mp_video.mq_seektarget = pos;
  mp->mp_audio.mq_seektarget = pos;

  mp_flush(mp);
  
  if(*mbp != NULL) {
    media_buf_free(*mbp);
    *mbp = NULL;
  }

  prop_set_float(prop_create(mp->mp_prop_root, "seektime"), 
		 (pos - fctx->start_time) / 1000000.0);

  return pos;
}


/**
 * Thread for reading from lavf and sending to lavc
 */
static event_t *
video_player_loop(AVFormatContext *fctx, codecwrap_t **cwvec, media_pipe_t *mp,
		  char *errbuf, size_t errlen)
{
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  AVCodecContext *ctx;
  AVPacket pkt;
  int r, si;
  event_t *e;
  event_ts_t *ets;
  int64_t ts, seekbase = AV_NOPTS_VALUE;

  int hold = 0, lost_focus = 0, epoch = 1;

  mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
  mp_set_playstatus_by_hold(mp, 0);

  while(1) {
    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      if((r = av_read_frame(fctx, &pkt)) < 0) {

	if(r == AVERROR_EOF) {

	  /* Wait for queues to drain */
	  e = mp_wait_for_empty_queues(mp, 0);

	  mp_set_playstatus_stop(mp);

	  if(e == NULL)
	    e = event_create_type(EVENT_EOF);

	} else {
	  snprintf(errbuf, errlen, "%s", fa_ffmpeg_error_to_txt(r));
	  e = NULL;
	}
	break;
      }

      si = pkt.stream_index;

      ctx = cwvec[si] ? cwvec[si]->codec_ctx : NULL;

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

      mb->mb_epoch    = epoch;
      mb->mb_pts      = rescale(fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(fctx, pkt.dts,      si);
      /* pkt.duration is not stable enough, some formats (matroska)
	 seem to assume that it should be multiplied with ticks_per_frame, 
	 and some formats does not. We can just leave it to zero.
	 The code downstream will figure it out by itself */
	
      mb->mb_duration = 0; //rescale(fctx, pkt.duration, si);

      if(mq->mq_seektarget != AV_NOPTS_VALUE) {
	ts = mb->mb_pts != AV_NOPTS_VALUE ? mb->mb_pts : mb->mb_dts;
	if(ts < mq->mq_seektarget) {
	  mb->mb_skip = 1;
	} else {
	  mb->mb_skip = 2;
	  mq->mq_seektarget = AV_NOPTS_VALUE;
	}
      }

      mb->mb_cw = wrap_codec_ref(cwvec[si]);

      mb->mb_stream = pkt.stream_index;

      if(pkt.destruct == av_destruct_packet) {
	/* Move the data pointers from ffmpeg's packet */
	mb->mb_data = pkt.data;
	pkt.data = NULL;

	mb->mb_size = pkt.size;
	pkt.size = 0;

      } else {

	mb->mb_data = malloc(pkt.size +   FF_INPUT_BUFFER_PADDING_SIZE);
	memset(mb->mb_data + pkt.size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
	memcpy(mb->mb_data, pkt.data, pkt.size);
	mb->mb_size = pkt.size;
      }

      if(mb->mb_pts != AV_NOPTS_VALUE && mb->mb_data_type == MB_AUDIO)
	mb->mb_time = mb->mb_pts - fctx->start_time;
      else
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


    if(event_is_action(e, ACTION_PLAYPAUSE) ||
       event_is_action(e, ACTION_PLAY) ||
       event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, &mp->mp_video, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_send_cmd_head(mp, &mp->mp_audio, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(mp, hold);
      lost_focus = 0;
      
    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PAUSE);
      mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PLAY);
	mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold);

      }

    } else if(event_is_type(e, EVENT_CURRENT_PTS)) {

      ets = (event_ts_t *)e;
      seekbase = ets->pts;

    } else if(event_is_type(e, EVENT_SEEK)) {

      epoch++;
      ets = (event_ts_t *)e;
      
      ts = ets->pts + fctx->start_time;

      if(ts < fctx->start_time)
	ts = fctx->start_time;

      seekbase = video_seek(fctx, mp, &mb, ts, 1, "direct");

    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase - 60000000, 1, "-60s");

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase - 15000000, 1, "-15s");

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase + 15000000, 1, "+15s");

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase + 60000000, 1, "+60s");

    } else if(event_is_type(e, EVENT_EXIT) ||
	      event_is_type(e, EVENT_PLAY_URL)) {
      break;
    }
    event_unref(e);
  }

  if(mb != NULL)
    media_buf_free(mb);
  return e;
}

/**
 *
 */
event_t *
be_file_playvideo(const char *url, media_pipe_t *mp,
		  char *errbuf, size_t errlen)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  formatwrap_t *fw;
  int i;
  char faurl[1000];
  codecwrap_t **cwvec;
  event_t *e;
  struct stat buf;
  fa_handle_t *fh;

  if(fa_stat(url, &buf, errbuf, errlen))
    return NULL;
  
  /**
   * Is it a DVD ?
   */

  if(S_ISDIR(buf.st_mode)) {
    
    if(fa_probe_dir(NULL, url) == CONTENT_DVD)
      goto isdvd;

    return NULL;
  }

  if((fh = fa_open(url, errbuf, errlen)) == NULL)
    return NULL;

  if(fa_probe_iso(NULL, fh) == 0) {
    fa_close(fh);
  isdvd:
#if ENABLE_DVD
    return dvd_play(url, mp, errbuf, errlen);
#else
    snprintf(errbuf, errlen, "DVD playback is not supported");
    return NULL;
#endif
  }
  fa_close(fh);


  /**
   * Open input file
   */
  snprintf(faurl, sizeof(faurl), "showtime:%s", url);
  if(av_open_input_file(&fctx, faurl, NULL, 0, NULL) != 0) {
    snprintf(errbuf, errlen, "Unable to open input file %s", url);
    return NULL;
  }

  // fctx->flags |= AVFMT_FLAG_GENPTS;

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    snprintf(errbuf, errlen, "Unable to find stream info");
    return NULL;
  }

  TRACE(TRACE_DEBUG, "Video", "Starting playback of %s", url);

  /**
   * Update property metadata
   */

  fa_lavf_load_meta(mp->mp_prop_metadata, fctx, faurl);

  /**
   * Init codec contexts
   */
  cwvec = alloca(fctx->nb_streams * sizeof(void *));
  memset(cwvec, 0, sizeof(void *) * fctx->nb_streams);
  
  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(fctx);

  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(mp->mp_video.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_VIDEO)
      mp->mp_video.mq_stream = i;

    if(mp->mp_audio.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_AUDIO)
      mp->mp_audio.mq_stream = i;

    cwvec[i] = wrap_codec_create(ctx->codec_id,
					  ctx->codec_type, 0, fw, ctx, 0);
  }

  mp_become_primary(mp);

  e = video_player_loop(fctx, cwvec, mp, errbuf, errlen);

  mp_flush(mp);
  mp_shutdown(mp);

  for(i = 0; i < fctx->nb_streams; i++)
    if(cwvec[i] != NULL)
      wrap_codec_deref(cwvec[i]);

  wrap_format_deref(fw);
  return e;
}
