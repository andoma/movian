/*
 *  Playback of video
 *  Copyright (C) 2007-2010 Andreas Öman
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
#include "backend/dvd/dvd.h"
#include "notifications.h"
#include "video/subtitles.h"
#include "api/opensubtitles.h"

static void
scan_subtitles(prop_t *prop, const char *url)
{
  char parent[URL_MAX];
  fa_dir_t *fd;
  fa_dir_entry_t *fde;
  char errbuf[256];
  const char *e;

  fa_parent(parent, sizeof(parent), url);

  if((fd = fa_scandir_recursive(parent, errbuf, sizeof(errbuf),
				FA_SCAN_ARCHIVES)) == NULL) {
    TRACE(TRACE_DEBUG, "Video", "Unable to scan %s for subtitles: %s",
	  parent, errbuf);
    return;
  }

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    
    e = strrchr(fde->fde_url, '.');
    if(e != NULL && !strcasecmp(e, ".srt")) {

      prop_t *p = prop_create(prop, NULL);
      prop_set_string(prop_create(p, "id"), fde->fde_url);
      prop_set_string(prop_create(p, "title"), fde->fde_filename);
    }
  }
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



/**
 *
 */
static media_buf_t *
subtitle_decode(AVFormatContext *fctx, AVCodecContext *ctx,
		AVPacket *pkt, int si)
{
  media_buf_t *mb;

  switch(ctx->codec_id) {
  case CODEC_ID_TEXT:
    mb = media_buf_alloc();
    mb->mb_data_type = MB_SUBTITLE;
    
    mb->mb_pts = rescale(fctx, pkt->pts,      si);
    mb->mb_duration = rescale(fctx, pkt->duration, si);
    
    char *s = malloc(pkt->size + 1);
    mb->mb_data = memcpy(s, pkt->data, pkt->size);
    s[pkt->size] = 0;
    break;

  case CODEC_ID_SSA:
    mb = subtitles_ssa_decode_line(pkt->data, pkt->size);
    break;

  default:
#if 0
    printf("Codec type %x not supported\n", ctx->codec_id);

    /* Subtitles */
    printf("%d bytes\n", pkt->size);
    
    int k;
    char *sub = (char *)pkt->data;
    for(k = 0; k < pkt->size; k++)
      printf("%c", sub[k] >= 32 ? sub[k] : '.');
    printf("\n");
#endif
   
    return NULL;
  }
  return mb;
}


/**
 *
 */
static int64_t
video_seek(AVFormatContext *fctx, media_pipe_t *mp, media_buf_t **mbp,
	   int64_t pos, int backward, const char *txt, int64_t *lastsubpts)
{
  *lastsubpts = AV_NOPTS_VALUE;

  pos = FFMAX(fctx->start_time, FFMIN(fctx->start_time + fctx->duration, pos));

  TRACE(TRACE_DEBUG, "Video", "seek %s to %.2f", txt, 
	(pos - fctx->start_time) / 1000000.0);
 
  av_seek_frame(fctx, -1, pos, backward ? AVSEEK_FLAG_BACKWARD : 0);

  mp->mp_video.mq_seektarget = pos;
  mp->mp_audio.mq_seektarget = pos;

  mp_flush(mp, 0);
  
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
video_player_loop(AVFormatContext *fctx, media_codec_t **cwvec, media_pipe_t *mp,
		  subtitles_t *sub, char *errbuf, size_t errlen)
{
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  AVCodecContext *ctx;
  AVPacket pkt;
  int r, si;
  event_t *e;
  event_ts_t *ets;
  int64_t ts;
  int64_t seekbase, subpts, lastsubpts;

  seekbase = subpts = lastsubpts = AV_NOPTS_VALUE;

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

	if(r == AVERROR(EAGAIN))
	  continue;

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

	if(fctx->streams[si]->avg_frame_rate.num) {
	  mb->mb_duration = 1000000 * fctx->streams[si]->avg_frame_rate.den /
	    fctx->streams[si]->avg_frame_rate.num;
	} else {
	  mb->mb_duration = rescale(fctx, pkt.duration, si);
	}

      } else if(ctx != NULL && si == mp->mp_audio.mq_stream) {
	/* Current audio stream */
	mb = media_buf_alloc();
	mb->mb_data_type = MB_AUDIO;
	mq = &mp->mp_audio;

      } else if(si == mp->mp_video.mq_stream2) {

	ctx = fctx->streams[si]->codec;
	mb = ctx != NULL ? subtitle_decode(fctx, ctx, &pkt, si) : mb;
	mq = &mp->mp_video;

	av_free_packet(&pkt);
	if(mb != NULL)
	  goto deliver;
	continue;

      } else {
	/* Check event queue ? */
	av_free_packet(&pkt);
	continue;
      }

      mb->mb_epoch    = epoch;
      mb->mb_pts      = rescale(fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(fctx, pkt.dts,      si);

      if(mb->mb_data_type == MB_VIDEO && mb->mb_pts > lastsubpts)
	lastsubpts = subpts = mb->mb_pts;
      else
	subpts = AV_NOPTS_VALUE;

      if(mq->mq_seektarget != AV_NOPTS_VALUE) {
	ts = mb->mb_pts != AV_NOPTS_VALUE ? mb->mb_pts : mb->mb_dts;
	if(ts < mq->mq_seektarget) {
	  mb->mb_skip = 1;
	} else {
	  mb->mb_skip = 2;
	  mq->mq_seektarget = AV_NOPTS_VALUE;
	}
      }

      mb->mb_cw = media_codec_ref(cwvec[si]);

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
  deliver:
    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */

      if(subpts != AV_NOPTS_VALUE && sub != NULL) {
	subtitle_entry_t *se = subtitles_pick(sub, subpts);
	if(se != NULL) {

	  media_buf_t *mb2 = media_buf_alloc();
	  
	  mb2->mb_pts = se->se_start;
	  mb2->mb_duration = se->se_stop - se->se_start;
	  mb2->mb_data_type = MB_SUBTITLE;

	  mb2->mb_data = strdup(se->se_text);
	  mb2->mb_size = 0;

	  mb_enqueue_always(mp, mq, mb2);
	}
      }
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

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      lost_focus = 0;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold);

    } else if(event_is_type(e, EVENT_CURRENT_PTS)) {

      ets = (event_ts_t *)e;
      seekbase = ets->pts;

    } else if(event_is_type(e, EVENT_SEEK)) {

      epoch++;
      ets = (event_ts_t *)e;
      
      ts = ets->pts + fctx->start_time;

      if(ts < fctx->start_time)
	ts = fctx->start_time;

      seekbase = video_seek(fctx, mp, &mb, ts, 1, "direct", &lastsubpts);

    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase - 60000000, 1, "-60s",
			    &lastsubpts);

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase - 15000000, 1, "-15s",
			    &lastsubpts);

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase + 15000000, 1, "+15s",
			    &lastsubpts);

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase + 60000000, 1, "+60s",
			    &lastsubpts);

    } else if(event_is_action(e, ACTION_STOP)) {
      mp_set_playstatus_stop(mp);

    } else if(event_is_type(e, EVENT_SELECT_TRACK)) {
      event_select_track_t *est = (event_select_track_t *)e;
      const char *id = est->id;

      if(!strcmp(id, "sub:off")) {
	prop_set_string(mp->mp_prop_subtitle_track_current, id);
	mp->mp_video.mq_stream2 = -1;

	if(sub != NULL) {
	  subtitles_destroy(sub);
	  sub = NULL;
	}


      } else if(!strcmp(id, "audio:off")) {
	prop_set_string(mp->mp_prop_audio_track_current, id);
	mp->mp_audio.mq_stream = -1;

      } else if(id[0] >= '0' && id[0] <= '9') {
	unsigned int idx = atoi(est->id);
	if(idx < fctx->nb_streams) {
	  ctx = fctx->streams[idx]->codec;
	  if(ctx->codec_type == CODEC_TYPE_AUDIO) {
	    mp->mp_audio.mq_stream = idx;
	    prop_set_int(mp->mp_prop_audio_track_current, idx);
	  } else if(ctx->codec_type == CODEC_TYPE_SUBTITLE) {

	    if(sub != NULL) {
	      subtitles_destroy(sub);
	      sub = NULL;
	    }

	    mp->mp_video.mq_stream2 = idx;
	    prop_set_int(mp->mp_prop_subtitle_track_current, idx);
	  }
	}
      } else {
	mp->mp_video.mq_stream2 = -1;

	prop_set_string(mp->mp_prop_subtitle_track_current, est->id);

	if(sub != NULL)
	  subtitles_destroy(sub);

	sub = subtitles_load(est->id);
      }

      


    } else if(event_is_type(e, EVENT_EXIT) ||
	      event_is_type(e, EVENT_PLAY_URL)) {
      break;
    }
    event_unref(e);
  }

  if(mb != NULL)
    media_buf_free(mb);
  if(sub != NULL)
    subtitles_destroy(sub);
  return e;
}

/**
 *
 */
static void
add_off_stream(prop_t *prop, const char *id)
{
  prop_t *p = prop_create(prop, NULL);
  
  prop_set_string(prop_create(p, "id"), id);
  prop_set_string(prop_create(p, "title"), "Off");
}


/**
 *
 */
event_t *
be_file_playvideo(struct backend *be, const char *url, media_pipe_t *mp,
		  int primary, int priority,
		  char *errbuf, size_t errlen)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  media_format_t *fw;
  int i;
  char faurl[URL_MAX];
  media_codec_t **cwvec;
  event_t *e;
  struct fa_stat fs;
  fa_handle_t *fh;

  uint64_t hash;
  uint64_t fsize;
  int valid_hash = 0;

  if(fa_stat(url, &fs, errbuf, errlen))
    return NULL;
  
  /**
   * Is it a DVD ?
   */

  if(fs.fs_type == CONTENT_DIR) {
    
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
    return dvd_play(url, mp, errbuf, errlen, 1);
#else
    snprintf(errbuf, errlen, "DVD playback is not supported");
    return NULL;
#endif
  }


  valid_hash = !opensub_compute_hash(fh, &hash);
  fsize = fa_fsize(fh);
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

  prop_t *subs = prop_create(mp->mp_prop_metadata, "subtitlestreams");

  add_off_stream(subs, "sub:off");

  add_off_stream(prop_create(mp->mp_prop_metadata, "audiostreams"),
		 "audio:off");

  /**
   * Update property metadata
   */
  fa_probe_load_metaprop(mp->mp_prop_metadata, fctx, faurl);

  /**
   * Subtitles from filesystem
   */
  scan_subtitles(subs, url);

  /**
   * Query opensubtitles.org
   */
  opensub_add_subtitles(subs,
			opensub_build_query(NULL, hash, fsize, NULL, NULL));

  /**
   * Init codec contexts
   */
  cwvec = alloca(fctx->nb_streams * sizeof(void *));
  memset(cwvec, 0, sizeof(void *) * fctx->nb_streams);
  
  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;
  mp->mp_video.mq_stream2 = -1;

  fw = media_format_create(fctx);

  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    media_codec_params_t mcp = {0};

    if(ctx->codec_type == CODEC_TYPE_VIDEO) {
      
      mcp.width = ctx->width;
      mcp.height = ctx->height;

      if(mp->mp_video.mq_stream == -1)
	mp->mp_video.mq_stream = i;
    }

    if(mp->mp_audio.mq_stream == -1 && ctx->codec_type == CODEC_TYPE_AUDIO)
      mp->mp_audio.mq_stream = i;

    if(ctx->codec_id == CODEC_ID_DTS)
      ctx->channels = 0;

    cwvec[i] = media_codec_create(ctx->codec_id,
				  ctx->codec_type, 0, fw, ctx, &mcp, mp);
  }

  prop_set_int(mp->mp_prop_audio_track_current, mp->mp_audio.mq_stream);

  prop_set_string(mp->mp_prop_subtitle_track_current, "sub:off");

  mp_set_play_caps(mp, MP_PLAY_CAPS_SEEK | MP_PLAY_CAPS_PAUSE);

  mp_become_primary(mp);

  e = video_player_loop(fctx, cwvec, mp, NULL, errbuf, errlen);

  TRACE(TRACE_DEBUG, "Video", "Stopped playback of %s", url);

  mp_flush(mp, 0);
  mp_shutdown(mp);

  for(i = 0; i < fctx->nb_streams; i++)
    if(cwvec[i] != NULL)
      media_codec_deref(cwvec[i]);

  media_format_deref(fw);
  return e;
}
