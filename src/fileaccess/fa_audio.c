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
#include <libavformat/avio.h>

#include "showtime.h"
#include "fa_audio.h"
#include "event.h"
#include "media.h"
#include "fileaccess.h"
#include "fa_libav.h"
#include "notifications.h"
#include "metadata.h"

#if ENABLE_LIBGME
#include <gme/gme.h>
#endif

#if CONFIG_LIBOPENSPC
#include <openspc.h>

/**
 *
 */
static event_t *
openspc_play(media_pipe_t *mp, AVIOContext *avio, char *errbuf, size_t errlen)
{
  media_queue_t *mq = &mp->mp_audio;
#error fa_fsize can return -1 .. deal with it
  size_t r, siz = fa_fsize(fh);
  uint8_t *buf = malloc(siz);
  media_buf_t *mb = NULL;
  event_t *e;
  int hold = 0, lost_focus = 0;
  int sample = 0;
  unsigned int duration = INT32_MAX;

  mp_set_playstatus_by_hold(mp, hold, NULL);

  mp->mp_audio.mq_stream = 0;

  fa_seek(fh, 0, SEEK_SET);
  r = fa_read(fh, buf, siz);
  fa_close(fh);
  
  if(r != siz) {
    free(buf);
    snprintf(errbuf, errlen, "openspc: Unable to read file");
    return NULL;
  }

  if(OSPC_Init(buf, siz)) {
    free(buf);
    snprintf(errbuf, errlen, "openspc: Unable to initialize file");
    return NULL;
  }

  if(!memcmp("v0.30", buf + 0x1c, 4) && buf[0x23] == 0x1a) {
    char str[4];
    memcpy(str, buf + 0xa9, 3);
    str[3] = 0;
    duration = atoi(str) * 32000;
  }

  mp_set_play_caps(mp, MP_PLAY_CAPS_PAUSE);

  mp_become_primary(mp);

  while(1) {

    if(mb == NULL) {

      if(sample > duration) {
	while((e = mp_wait_for_empty_queues(mp, 0)) != NULL) {
	  if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) ||
	     event_is_action(e, ACTION_PREV_TRACK) ||
	     event_is_action(e, ACTION_NEXT_TRACK) ||
	     event_is_action(e, ACTION_STOP)) {
	    mp_flush(mp, 0);
	    break;
	  }
	  event_release(e);
	}
	if(e == NULL)
	  e = event_create_type(EVENT_EOF);
	break;
       }

      mb = media_buf_alloc();
      mb->mb_data_type = MB_AUDIO;
      mb->mb_size = sizeof(int16_t) * 2048 * 2;
      mb->mb_data = malloc(mb->mb_size);
      mb->mb_size = OSPC_Run(-1, mb->mb_data, mb->mb_size);

      mb->mb_channels = 2;
      mb->mb_rate = 32000;

      mb->mb_time = sample * 1000000LL / mb->mb_rate;
      sample += 2048;
    }

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
      mp_flush(mp, 0);
      break;
    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      lost_focus = 0;
      mp_set_playstatus_by_hold(mp, hold, NULL);

    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	mp_send_cmd_head(mp, mq, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold, NULL);
      }

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      lost_focus = 0;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_action(e, ACTION_PREV_TRACK) ||
	      event_is_action(e, ACTION_NEXT_TRACK) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp, 0);
      break;
    }
    event_release(e);
  }

  free(buf);
  return e;
}

#endif


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


#define MB_SPECIAL_EOF ((void *)-1)

/**
 *
 */
static void
seekflush(media_pipe_t *mp, media_buf_t **mbp)
{
  mp_flush(mp, 0);
  
  if(*mbp != NULL && *mbp != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, *mbp);
  *mbp = NULL;
}

/**
 *
 */
event_t *
be_file_playaudio(const char *url, media_pipe_t *mp,
		  char *errbuf, size_t errlen, int hold, const char *mimetype)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  AVPacket pkt;
  media_format_t *fw;
  int i, r, si;
  media_buf_t *mb = NULL;
  media_queue_t *mq;
  event_ts_t *ets;
  int64_t ts, seekbase = 0;
  media_codec_t *cw;
  event_t *e;
  int lost_focus = 0;
  int registered_play = 0;

  mp_set_playstatus_by_hold(mp, hold, NULL);

  fa_handle_t *fh = fa_open_ex(url, errbuf, errlen, FA_BUFFERED, NULL);
  if(fh == NULL)
    return NULL;

  // First we need to check for a few other formats
#if ENABLE_LIBOPENSPC || ENABLE_LIBGME

  uint8_t pb[128];
  size_t psiz;
  
  psiz = fa_read(fh, pb, sizeof(pb));
  if(psiz < sizeof(pb)) {
    fa_close(fh);
    snprintf(errbuf, errlen, "Fill too small");
    return NULL;
  }

#if ENABLE_LIBGME
  if(*gme_identify_header(pb))
    return fa_gme_playfile(mp, fh, errbuf, errlen, hold, url);
#endif

#if ENABLE_LIBOPENSPC
  if(!memcmp(pb, "SNES-SPC700 Sound File Data", 27))
    return openspc_play(mp, fh, errbuf, errlen);
#endif

#endif

  
  AVIOContext *avio = fa_libav_reopen(fh);

  if(avio == NULL) {
    fa_close(fh);
    return NULL;
  }

  if((fctx = fa_libav_open_format(avio, url, 
				  errbuf, errlen, mimetype)) == NULL) {
    fa_libav_close(avio);
    return NULL;
  }

  TRACE(TRACE_DEBUG, "Audio", "Starting playback of %s", url);

  mp_configure(mp, MP_PLAY_CAPS_SEEK | MP_PLAY_CAPS_PAUSE,
	       MP_BUFFER_SHALLOW);

  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = media_format_create(fctx);

  cw = NULL;
  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(ctx->codec_type != AVMEDIA_TYPE_AUDIO)
      continue;

    cw = media_codec_create(ctx->codec_id, 0, fw, ctx, NULL, mp);
    mp->mp_audio.mq_stream = i;
    break;
  }
  
  if(cw == NULL) {
    media_format_deref(fw);
    snprintf(errbuf, errlen, "Unable to open codec");
    return NULL;
  }

  mp_become_primary(mp);
  mq = &mp->mp_audio;

  while(1) {

    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {
      
      r = av_read_frame(fctx, &pkt);
      if(r == AVERROR(EAGAIN))
	continue;
      
      if(r == AVERROR_EOF || r == AVERROR(EIO)) {
	mb = MB_SPECIAL_EOF;
	continue;
      }
      
      if(r != 0) {
	char msg[100];
	fa_ffmpeg_error_to_txt(r, msg, sizeof(msg));
	TRACE(TRACE_ERROR, "Audio", "Playback error: %s", msg);

	while((e = mp_wait_for_empty_queues(mp)) != NULL) {
	  if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) ||
	     event_is_action(e, ACTION_PREV_TRACK) ||
	     event_is_action(e, ACTION_NEXT_TRACK) ||
	     event_is_action(e, ACTION_STOP)) {
	    mp_flush(mp, 0);
	    break;
	  }
	  event_release(e);
	}
	if(e == NULL)
	  e = event_create_type(EVENT_EOF);
	break;
      }

      si = pkt.stream_index;

      if(si != mp->mp_audio.mq_stream) {
	av_free_packet(&pkt);
	continue;
      }



      mb = media_buf_alloc_unlocked(mp, pkt.size);
      mb->mb_data_type = MB_AUDIO;

      mb->mb_pts      = rescale(fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(fctx, pkt.dts,      si);
      mb->mb_duration = rescale(fctx, pkt.duration, si);

      mb->mb_cw = media_codec_ref(cw);

      /* Move the data pointers from ffmpeg's packet */

      mb->mb_stream = pkt.stream_index;

      memcpy(mb->mb_data, pkt.data, pkt.size);

      if(mb->mb_pts != AV_NOPTS_VALUE) {
	if(fctx->start_time == AV_NOPTS_VALUE)
	  mb->mb_time = mb->mb_pts;
	else
	  mb->mb_time = mb->mb_pts - fctx->start_time;
      } else
	mb->mb_time = AV_NOPTS_VALUE;

      mb->mb_send_pts = 1;

      av_free_packet(&pkt);
    }

    /*
     * Try to send the buffer.  If mb_enqueue() returns something we
     * catched an event instead of enqueueing the buffer. In this case
     * 'mb' will be left untouched.
     */

    if(mb == MB_SPECIAL_EOF) {
      // We have reached EOF, drain queues
      e = mp_wait_for_empty_queues(mp);
      
      if(e == NULL) {
	e = event_create_type(EVENT_EOF);
	break;
      }

    } else if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }      

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {

      mp_flush(mp, 0);
      break;

    } else if(event_is_type(e, EVENT_CURRENT_PTS)) {

      ets = (event_ts_t *)e;
      seekbase = ets->ts;

      if(registered_play == 0) {
	if(ets->ts - fctx->start_time > METADB_AUDIO_PLAY_THRESHOLD) {
	  registered_play = 1;
	  metadb_register_play(url, 1, CONTENT_AUDIO);
	}
      }

    } else if(event_is_type(e, EVENT_SEEK)) {

      ets = (event_ts_t *)e;
      ts = ets->ts + fctx->start_time;
      if(ts < fctx->start_time)
	ts = fctx->start_time;
      av_seek_frame(fctx, -1, ts, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);
      
    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      av_seek_frame(fctx, -1, seekbase - 60000000, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      av_seek_frame(fctx, -1, seekbase - 15000000, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      av_seek_frame(fctx, -1, seekbase + 60000000, 0);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      av_seek_frame(fctx, -1, seekbase + 15000000, 0);
      seekflush(mp, &mb);
#if 0
    } else if(event_is_action(e, ACTION_RESTART_TRACK)) {

      av_seek_frame(fctx, -1, 0, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);
#endif
    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      lost_focus = 0;
      mp_set_playstatus_by_hold(mp, hold, NULL);

    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	mp_send_cmd_head(mp, mq, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold, NULL);
      }

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      lost_focus = 0;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_action(e, ACTION_PREV_TRACK) ||
	      event_is_action(e, ACTION_NEXT_TRACK) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp, 0);
      break;
    }
    event_release(e);
  }

  if(mb != NULL && mb != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, mb);

  media_codec_deref(cw);
  media_format_deref(fw);

  if(hold) { 
    // If we were paused, release playback again.
    mp_send_cmd(mp, mq, MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, 0, NULL);
  }

  return e;
}
