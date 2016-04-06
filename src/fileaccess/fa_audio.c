/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/mathematics.h>

#include "main.h"
#include "fa_audio.h"
#include "event.h"
#include "media/media.h"
#include "fileaccess.h"
#include "fa_libav.h"
#include "notifications.h"
#include "metadata/playinfo.h"
#include "misc/minmax.h"
#include "usage.h"

#if ENABLE_LIBGME
#include <gme/gme.h>
#endif

#if ENABLE_XMP
#include <xmp.h>
#endif


/**
 *
 */
static event_t *
audio_play_zipfile(fa_handle_t *fh, media_pipe_t *mp,
		  char *errbuf, size_t errlen, int hold)
{
  event_t *e = NULL;
  buf_t *b = fa_load_and_close(fh);
  char url[64];
  if(b == NULL) {
    snprintf(errbuf, errlen, "Load error");
    return NULL;
  }
  
  int id = memfile_register(b->b_ptr, b->b_size);
  snprintf(url, sizeof(url), "zip://memfile://%d", id);
  fa_dir_t *fd = fa_scandir(url, errbuf, sizeof(errbuf));
  if(fd != NULL) {
    fa_dir_entry_t *fde;
    RB_FOREACH(fde, &fd->fd_entries, fde_link) {
      e = be_file_playaudio(rstr_get(fde->fde_url), mp, errbuf, errlen,
			    hold, NULL);
      if(e != NULL)
	goto out;
    }
    snprintf(errbuf, errlen, "No audio file found in ZIP archive");
  } else {
    snprintf(errbuf, errlen, "Unable to parse ZIP archive");
  }
 out:
  if(fd != NULL)
    fa_dir_free(fd);
  buf_release(b);
  memfile_unregister(id);
  return e;
}


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
  mp_flush(mp);
  
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
  int64_t ts;
  media_codec_t *cw;
  event_t *e;
  int registered_play = 0;
  uint8_t pb[128];
  size_t psiz;

  mp->mp_seek_base = 0;

  fa_handle_t *fh = fa_open_ex(url, errbuf, errlen, FA_BUFFERED_SMALL, NULL);
  if(fh == NULL)
    return NULL;


  psiz = fa_read(fh, pb, sizeof(pb));
  if(psiz < sizeof(pb)) {
    fa_close(fh);
    snprintf(errbuf, errlen, "File too small");
    return NULL;
  }

  if(pb[0] == 0x50 && pb[1] == 0x4b && pb[2] == 0x03 && pb[3] == 0x04)
    // ZIP File
    return audio_play_zipfile(fh, mp, errbuf, errlen, hold);

  // First we need to check for a few other formats
#if ENABLE_LIBGME
  if(*gme_identify_header(pb))
    return fa_gme_playfile(mp, fh, errbuf, errlen, hold, url);
#endif

#if ENABLE_XMP
  FILE *f = fa_fopen(fh, 0);

  if(f != NULL) {
    int r = xmp_test_modulef(f, NULL);
    if(r == 0) {
      e = fa_xmp_playfile(mp, f, errbuf, errlen, hold, url,
                          fa_fsize(fh));
      fclose(f);
      return e;
    }
    fclose(f);
  }
#endif

  AVIOContext *avio = fa_libav_reopen(fh, 0);

  if(avio == NULL) {
    fa_close(fh);
    return NULL;
  }

  if((fctx = fa_libav_open_format(avio, url, errbuf, errlen, mimetype,
                                  FA_LIBAV_OPEN_STRATEGY_AUDIO)) == NULL) {
    fa_libav_close(avio);
    return NULL;
  }

  usage_event("Play audio", 1, USAGE_SEG("format", fctx->iformat->name));

  TRACE(TRACE_DEBUG, "Audio", "Starting playback of %s", url);

  mp_configure(mp, MP_CAN_SEEK | MP_CAN_PAUSE,
	       MP_BUFFER_SHALLOW, fctx->duration, "tracks");

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

  prop_set(mp->mp_prop_root, "format", PROP_SET_STRING,
           fctx->iformat->long_name);

  while(1) {

    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {
      
      mp->mp_eof = 0;
      r = av_read_frame(fctx, &pkt);
      if(r == AVERROR(EAGAIN))
	continue;
      
      if(r == AVERROR_EOF || r == AVERROR(EIO)) {
	mb = MB_SPECIAL_EOF;
	mp->mp_eof = 1;
	continue;
      }
      
      if(r != 0) {
	char msg[100];
	fa_libav_error_to_txt(r, msg, sizeof(msg));
	TRACE(TRACE_ERROR, "Audio", "Playback error: %s", msg);

	while((e = mp_wait_for_empty_queues(mp)) != NULL) {
	  if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) ||
	     event_is_action(e, ACTION_SKIP_BACKWARD) ||
	     event_is_action(e, ACTION_SKIP_FORWARD) ||
	     event_is_action(e, ACTION_STOP)) {
	    mp_flush(mp);
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

      mb = media_buf_from_avpkt_unlocked(mp, &pkt);
      mb->mb_data_type = MB_AUDIO;

      mb->mb_pts      = rescale(fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(fctx, pkt.dts,      si);
      mb->mb_duration = rescale(fctx, pkt.duration, si);

      mb->mb_cw = media_codec_ref(cw);
      mb->mb_stream = pkt.stream_index;

      if(mb->mb_pts != AV_NOPTS_VALUE) {
        const int64_t offset = fctx->start_time;
        mb->mb_user_time = mb->mb_pts + (offset != PTS_UNSET ? offset : 0);
	mb->mb_drive_clock = 1;
      }

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

      mp_flush(mp);
      break;

    } else if(event_is_type(e, EVENT_CURRENT_TIME)) {

      ets = (event_ts_t *)e;

      if(registered_play == 0) {
	if(ets->ts > PLAYINFO_AUDIO_PLAY_THRESHOLD) {
	  registered_play = 1;
	  playinfo_register_play(url, 1);
	}
      }

    } else if(event_is_type(e, EVENT_SEEK)) {

      ets = (event_ts_t *)e;

      if(fctx->start_time != PTS_UNSET) {
	ts = MAX(ets->ts + fctx->start_time, fctx->start_time);
      } else {
	ts = MAX(ets->ts, 0);
      }
      av_seek_frame(fctx, -1, ts, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);
      
    } else if(event_is_action(e, ACTION_SKIP_BACKWARD)) {

      if(mp->mp_seek_base < 1500000)
	goto skip;
      int64_t z = fctx->start_time != PTS_UNSET ? fctx->start_time : 0;
      av_seek_frame(fctx, -1, z, AVSEEK_FLAG_BACKWARD);
      seekflush(mp, &mb);

    } else if(event_is_action(e, ACTION_SKIP_FORWARD) ||
	      event_is_action(e, ACTION_STOP)) {
    skip:
      mp_flush(mp);
      break;
    }
    event_release(e);
  }

  if(mb != NULL && mb != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, mb);

  media_codec_deref(cw);
  media_format_deref(fw);

  return e;
}
