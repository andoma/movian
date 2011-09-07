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
#include "fa_libav.h"
#include "backend/dvd/dvd.h"
#include "notifications.h"
#include "api/opensubtitles.h"
#include "htsmsg/htsmsg_xml.h"
#include "backend/backend.h"
#include "misc/isolang.h"
#include "text/text.h"

LIST_HEAD(attachment_list, attachment);

typedef struct attachment {
  LIST_ENTRY(attachment) link;
  void (*dtor)(void *opaque);
  void *opaque;
} attachment_t;

static void attachment_load(struct attachment_list *alist,
			    const uint8_t *ptr, size_t len);

static void attachment_unload_all(struct attachment_list *alist);



static event_t *playlist_play(AVIOContext *avio, media_pipe_t *mp, int primary,
			      int flags, char *errbuf, size_t errlen);


/**
 *
 */
typedef struct fs_sub_scanner {
  prop_t *p;
  char *url;
} fs_sub_scanner_t;


/**
 *
 */
static int
fs_sub_match(const char *video, const char *sub)
{
  char *fname = mystrdupa(sub);
  fname = strrchr(fname, '/');
  if(fname == NULL)
    return 0;

  fname++;
  char *dot = strrchr(fname, '.');
  if(dot)
    *dot = 0;
  return !strcmp(fname, video);
}


/**
 *
 */
static void
fs_sub_scan_dir(prop_t *prop, const char *url, const char *video)
{
  char *postfix;
  fa_dir_t *fd;
  fa_dir_entry_t *fde;
  char errbuf[256];

  if((fd = fa_scandir(url, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "Video", "Unable to scan %s for subtitles: %s",
	  url, errbuf);
    return;
  }

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(fde->fde_type == CONTENT_DIR && !strcasecmp(fde->fde_filename, "subs")) {
      fs_sub_scan_dir(prop, fde->fde_url, video);
      continue;
    }

    postfix = strrchr(fde->fde_filename, '.');
    if(postfix != NULL && !strcasecmp(postfix, ".srt")) {
      const char *lang = NULL;
      if(postfix - fde->fde_filename > 4 && postfix[-4] == '.') {
	char b[4];
	memcpy(b, postfix - 3, 3);
	b[3] = 0;
	lang = isolang_iso2lang(b);
      }

      int score = video && fs_sub_match(video, fde->fde_url);

      mp_add_track(prop, fde->fde_filename, fde->fde_url, "SRT", NULL, lang,
		   "External file", score);
    }
  }
  fa_dir_free(fd);
}


/**
 *
 */
static void *
fs_sub_scan_thread(void *aux)
{
  fs_sub_scanner_t *fss = aux;
  char parent[URL_MAX];
  char *fname = mystrdupa(fss->url);

  fname = strrchr(fname, '/');
  if(fname != NULL) {
    fname++;
    char *dot = strrchr(fname, '.');
    if(dot)
      *dot = 0;
  }

  fa_parent(parent, sizeof(parent), fss->url);

  fs_sub_scan_dir(fss->p, parent, fname);

  prop_ref_dec(fss->p);
  free(fss->url);
  free(fss);
  return NULL;
}

/**
 *
 */
static void
fs_sub_scan(prop_t *prop, const char *url)
{
  fs_sub_scanner_t *fss = malloc(sizeof(fs_sub_scanner_t));
  fss->p = prop_ref_inc(prop);
  fss->url = strdup(url);

  hts_thread_create_detached("fs sub scan", fs_sub_scan_thread, fss,
			     THREAD_PRIO_LOW);
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
static int64_t
video_seek(AVFormatContext *fctx, media_pipe_t *mp, media_buf_t **mbp,
	   int64_t pos, const char *txt)
{
  pos = FFMAX(fctx->start_time, FFMIN(fctx->start_time + fctx->duration, pos));

  TRACE(TRACE_DEBUG, "Video", "seek %s to %.2f", txt, 
	(pos - fctx->start_time) / 1000000.0);
 
  av_seek_frame(fctx, -1, pos, AVSEEK_FLAG_BACKWARD);

  mp->mp_video.mq_seektarget = pos;
  mp->mp_audio.mq_seektarget = pos;

  mp_flush(mp, 0);
  
  if(*mbp != NULL && *mbp != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, *mbp);
  *mbp = NULL;

  prop_set_float(prop_create(mp->mp_prop_root, "seektime"), 
		 (pos - fctx->start_time) / 1000000.0);

  return pos;
}


/**
 * Thread for reading from lavf and sending to lavc
 */
static event_t *
video_player_loop(AVFormatContext *fctx, media_codec_t **cwvec,
		  media_pipe_t *mp, int flags,
		  char *errbuf, size_t errlen)
{
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  AVPacket pkt;
  int r, si;
  event_t *e;
  event_ts_t *ets;
  int64_t ts;
  int64_t seekbase = AV_NOPTS_VALUE;

  int hold = 0, lost_focus = 0, epoch = 1;

  mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
  mp_set_playstatus_by_hold(mp, 0, NULL);

  while(1) {
    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      r = av_read_frame(fctx, &pkt);

      if(r == AVERROR(EAGAIN))
	continue;

      if(r == AVERROR_EOF) {
	mb = MB_SPECIAL_EOF;
	continue;
      }

      if(r != 0) {
	fa_ffmpeg_error_to_txt(r, errbuf, errlen);
	e = NULL;
	break;
      }

      si = pkt.stream_index;

      if(si == mp->mp_video.mq_stream) {
	/* Current video stream */
	mb = media_buf_alloc_unlocked(mp, pkt.size);
	mb->mb_data_type = MB_VIDEO;
	mq = &mp->mp_video;

	if(fctx->streams[si]->avg_frame_rate.num) {
	  mb->mb_duration = 1000000 * fctx->streams[si]->avg_frame_rate.den /
	    fctx->streams[si]->avg_frame_rate.num;
	} else {
	  mb->mb_duration = rescale(fctx, pkt.duration, si);
	}

      } else if(fctx->streams[si]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {

	mb = media_buf_alloc_unlocked(mp, pkt.size);
	mb->mb_data_type = MB_AUDIO;
	mq = &mp->mp_audio;

      } else if(fctx->streams[si]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {

	int duration = pkt.convergence_duration ?: pkt.duration;

	mb = media_buf_alloc_unlocked(mp, pkt.size);
	mb->mb_codecid = fctx->streams[si]->codec->codec_id;
	mb->mb_data_type = MB_SUBTITLE;
	mq = &mp->mp_video;

	mb->mb_duration = rescale(fctx, duration, si);

      } else {
	/* Check event queue ? */
	av_free_packet(&pkt);
	continue;
      }

      mb->mb_epoch    = epoch;
      mb->mb_pts      = rescale(fctx, pkt.pts, si);
      mb->mb_dts      = rescale(fctx, pkt.dts, si);

      if(mq->mq_seektarget != AV_NOPTS_VALUE &&
	 mb->mb_data_type != MB_SUBTITLE) {
	ts = mb->mb_pts != AV_NOPTS_VALUE ? mb->mb_pts : mb->mb_dts;
	if(ts < mq->mq_seektarget) {
	  mb->mb_skip = 1;
	} else {
	  mb->mb_skip = 2;
	  mq->mq_seektarget = AV_NOPTS_VALUE;
	}
      }

      mb->mb_cw = cwvec[si] ? media_codec_ref(cwvec[si]) : NULL;

      mb->mb_stream = pkt.stream_index;

      memcpy(mb->mb_data, pkt.data, pkt.size);

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
    if(mb == MB_SPECIAL_EOF) {
      /* Wait for queues to drain */
      e = mp_wait_for_empty_queues(mp);


      if(e == NULL) {
	if(!(flags & BACKEND_VIDEO_NO_AUTOSTOP))
	  mp_set_playstatus_stop(mp);
	
	e = event_create_type(EVENT_EOF);
	break;
      }
    } else if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }


    if(event_is_action(e, ACTION_PLAYPAUSE) ||
       event_is_action(e, ACTION_PLAY) ||
       event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, &mp->mp_video, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_send_cmd_head(mp, &mp->mp_audio, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(mp, hold, NULL);
      lost_focus = 0;
      
    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PAUSE);
      mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PLAY);
	mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold, NULL);

      }

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      lost_focus = 0;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_type(e, EVENT_CURRENT_PTS)) {

      ets = (event_ts_t *)e;
      seekbase = ets->pts;

    } else if(event_is_type(e, EVENT_SEEK)) {

      epoch++;
      ets = (event_ts_t *)e;
      
      ts = ets->pts + fctx->start_time;

      if(ts < fctx->start_time)
	ts = fctx->start_time;

      seekbase = video_seek(fctx, mp, &mb, ts, "direct");

    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase - 60000000, "-60s");

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase - 15000000, "-15s");

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase + 15000000, "+15s");

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      seekbase = video_seek(fctx, mp, &mb, seekbase + 60000000, "+60s");

    } else if(event_is_action(e, ACTION_STOP)) {
      mp_set_playstatus_stop(mp);

    } else if(event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)) {
      event_select_track_t *est = (event_select_track_t *)e;

      TRACE(TRACE_DEBUG, "Video", "Selecting subtitle track %s",
	    est->id);

      mp_send_cmd_head(mp, &mp->mp_video, MB_FLUSH_SUBTITLES);

      if(!strcmp(est->id, "sub:off")) {
	prop_set_string(mp->mp_prop_subtitle_track_current, est->id);
	mp->mp_video.mq_stream2 = -1;

	mp_load_ext_sub(mp, NULL);

      } else if(!strncmp(est->id, "libav:", strlen("libav:"))) {
	unsigned int idx = atoi(est->id + strlen("libav:"));
	if(idx < fctx->nb_streams) {
	  AVCodecContext *ctx = fctx->streams[idx]->codec;
	  if(ctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
	    mp_load_ext_sub(mp, NULL);
	    mp->mp_video.mq_stream2 = idx;
	    prop_set_string(mp->mp_prop_subtitle_track_current, est->id);
	  }
	}
      } else  {

	mp->mp_video.mq_stream2 = -1;
	prop_set_string(mp->mp_prop_subtitle_track_current, est->id);

	mp_load_ext_sub(mp, est->id);
      }


    } else if(event_is_type(e, EVENT_SELECT_AUDIO_TRACK)) {
      event_select_track_t *est = (event_select_track_t *)e;

      TRACE(TRACE_DEBUG, "Video", "Selecting audio track %s",
	    est->id);

      if(!strcmp(est->id, "audio:off")) {
	prop_set_string(mp->mp_prop_audio_track_current, est->id);
	mp->mp_audio.mq_stream = -1;

      } else if(!strncmp(est->id, "libav:", strlen("libav:"))) {
	unsigned int idx = atoi(est->id + strlen("libav:"));
	if(idx < fctx->nb_streams) {
	  AVCodecContext *ctx = fctx->streams[idx]->codec;
	  if(ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
	    mp->mp_audio.mq_stream = idx;
	    prop_set_string(mp->mp_prop_audio_track_current, est->id);
	  }
	}
      }

    } else if(event_is_type(e, EVENT_EXIT) ||
	      event_is_type(e, EVENT_PLAY_URL)) {
      break;
    }
    event_release(e);
  }

  if(mb != NULL && mb != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, mb);
  return e;
}


/**
 *
 */
static prop_t *
build_index(media_pipe_t *mp, AVFormatContext *fctx, const char *url)
{
  int i;
  prop_t *root = prop_create(mp->mp_prop_root, "seekindex");
  prop_t *parent = prop_create(root, "positions");
  char buf[URL_MAX];

  prop_set_int(prop_create(root, "available"), 1);

  for(i = 0; i < fctx->duration / 1000000; i+=60) {
    prop_t *p = prop_create_root(NULL);
    snprintf(buf, sizeof(buf), "%s#%d", url, i);
    prop_set_string(prop_create(p, "image"), buf);
    prop_set_float(prop_create(p, "timestamp"), i);
    if(prop_set_parent(p, parent))
      prop_destroy(p);
  }

  return root;
}

/**
 *
 */
event_t *
be_file_playvideo(const char *url, media_pipe_t *mp,
		  int flags, int priority,
		  char *errbuf, size_t errlen,
		  const char *mimetype)
{
  AVFormatContext *fctx;
  AVIOContext *avio;
  AVCodecContext *ctx;
  media_format_t *fw;
  int i;
  media_codec_t **cwvec;
  event_t *e;
  uint8_t buf[64];

  uint64_t hash;
  int64_t fsize;
  int opensub_hash_rval;

  struct attachment_list alist;
  LIST_INIT(&alist);

  if(mimetype == NULL) {
    struct fa_stat fs;

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
  }

  /**
   * Check file type
   */
  if((avio = fa_libav_open(url, 65536, errbuf, errlen, FA_CACHE,
			   mp->mp_prop_io)) == NULL)
    return NULL;

  if(avio_read(avio, buf, sizeof(buf)) == sizeof(buf)) {
    if(!memcmp(buf, "<showtimeplaylist", strlen("<showtimeplaylist"))) {
      return playlist_play(avio, mp, flags, priority, errbuf, errlen);
    }
  }

  int seek_is_fast = fa_seek_is_fast(avio->opaque);

  if(seek_is_fast && mimetype == NULL) {
    if(fa_probe_iso(NULL, avio) == 0) {
      fa_libav_close(avio);
    isdvd:
#if ENABLE_DVD
      return dvd_play(url, mp, errbuf, errlen, 1);
#else
      snprintf(errbuf, errlen, "DVD playback is not supported");
      return NULL;
#endif
    }
  }

  if(seek_is_fast)
    opensub_hash_rval = opensub_compute_hash(avio, &hash);
  else
    opensub_hash_rval = 1;

  if(opensub_hash_rval == -1)
    TRACE(TRACE_DEBUG, "Video", "Unable to compute opensub hash");

  fsize = avio_size(avio);
  avio_seek(avio, 0, SEEK_SET);

  if((fctx = fa_libav_open_format(avio, url, errbuf, errlen,
				  mimetype)) == NULL) {
    fa_libav_close(avio);
    return NULL;
  }

  if(!strcmp(fctx->iformat->name, "avi"))
    fctx->flags |= AVFMT_FLAG_GENPTS;

  TRACE(TRACE_DEBUG, "Video", "Starting playback of %s (%s)",
	url, fctx->iformat->name);

  /**
   * Update property metadata
   */
  fa_probe_load_metaprop(mp->mp_prop_metadata, fctx, url);

  /**
   * Subtitles from filesystem
   */
  if(!(flags & BACKEND_VIDEO_NO_FS_SCAN))
    fs_sub_scan(mp->mp_prop_subtitle_tracks, url);

  /**
   * Query opensubtitles.org
   */
  if(!opensub_hash_rval && fsize != -1)
    opensub_add_subtitles(mp->mp_prop_subtitle_tracks,
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


  // Scan all streams and select defaults

  for(i = 0; i < fctx->nb_streams; i++) {
    char str[256];
    media_codec_params_t mcp = {0};

    ctx = fctx->streams[i]->codec;

    avcodec_string(str, sizeof(str), ctx, 0);
    TRACE(TRACE_DEBUG, "Video", " Stream #%d: %s", i, str);

    switch(ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      mcp.width = ctx->width;
      mcp.height = ctx->height;
      mcp.profile = ctx->profile;
      mcp.level = ctx->level;
      break;

    case AVMEDIA_TYPE_AUDIO:
      if(flags & BACKEND_VIDEO_NO_AUDIO)
	continue;
      if(ctx->codec_id == CODEC_ID_DTS)
	ctx->channels = 0;
      break;

    case AVMEDIA_TYPE_ATTACHMENT:
      attachment_load(&alist, ctx->extradata, ctx->extradata_size);
      break;

    default:
      break;
    }


    cwvec[i] = media_codec_create(ctx->codec_id, 0, fw, ctx, &mcp, mp);

    if(cwvec[i] != NULL) {
      switch(ctx->codec_type) {
      case AVMEDIA_TYPE_VIDEO:
	if(mp->mp_video.mq_stream == -1) {
	  mp->mp_video.mq_stream = i;
	}
	break;

      case AVMEDIA_TYPE_AUDIO:
	if(mp->mp_audio.mq_stream == -1) {
	  mp->mp_audio.mq_stream = i;
	  prop_set_stringf(mp->mp_prop_audio_track_current, "libav:%d", i);
	}
	break;
      default:
	break;
      }
    }
  }


  // Start it
  mp_configure(mp, (seek_is_fast ? MP_PLAY_CAPS_SEEK : 0) | MP_PLAY_CAPS_PAUSE,
	       MP_BUFFER_DEEP);

  if(!(flags & BACKEND_VIDEO_NO_AUDIO))
    mp_become_primary(mp);

  prop_set_string(mp->mp_prop_type, "video");

  prop_t *seek_index = build_index(mp, fctx, url);

  e = video_player_loop(fctx, cwvec, mp, flags, errbuf, errlen);

  prop_destroy(seek_index);

  TRACE(TRACE_DEBUG, "Video", "Stopped playback of %s", url);

  mp_flush(mp, 0);
  mp_shutdown(mp);

  for(i = 0; i < fctx->nb_streams; i++)
    if(cwvec[i] != NULL)
      media_codec_deref(cwvec[i]);

  attachment_unload_all(&alist);

  media_format_deref(fw);
  
  return e;
}


/**
 *
 */
static event_t *
playlist_play(AVIOContext *avio, media_pipe_t *mp, int flags,
	      int priority, char *errbuf, size_t errlen)
{
  void *mem;
  htsmsg_t *xml, *urls, *c;
  event_t *e;
  const char *s, *url;
  int loop;
  htsmsg_field_t *f;
  int played_something;

  mem = fa_libav_load_and_close(avio, NULL);

  if((xml = htsmsg_xml_deserialize(mem, errbuf, errlen)) == NULL)
    return NULL;

  s = htsmsg_get_str_multi(xml, "tags", "showtimeplaylist",
			   "attrib", "loop", NULL);
  loop = s != NULL && atoi(s);

  urls = htsmsg_get_map_multi(xml, "tags", "showtimeplaylist", "tags", NULL);

  if(urls == NULL) {
    htsmsg_destroy(xml);
    snprintf(errbuf, errlen, "No <url> tags in playlist");
    return NULL;
  }

  do {
    played_something = 0;

    HTSMSG_FOREACH(f, urls) {
      if(strcmp(f->hmf_name, "url") ||
	 (c = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      int flags2 = flags | BACKEND_VIDEO_NO_AUTOSTOP;

      s = htsmsg_get_str_multi(c, "attrib", "noaudio", NULL);

      if(s && atoi(s))
	flags2 |= BACKEND_VIDEO_NO_AUDIO;

      url = htsmsg_get_str(c, "cdata");
      if(url == NULL)
	continue;
      e = backend_play_video(url, mp, flags2, priority, errbuf, errlen, NULL);
      if(!event_is_type(e, EVENT_EOF)) {
	htsmsg_destroy(xml);
	return e;
      }
      played_something = 1;
    }
  } while(played_something && loop);
  
  htsmsg_destroy(xml);
  return event_create_type(EVENT_EOF);
}


/**
 *
 */
static void
attachment_add_dtor(struct attachment_list *alist,
		    void (*fn)(void *), void *opaque)
{
  attachment_t *a = malloc(sizeof(attachment_t));

  a->dtor = fn;
  a->opaque = opaque;
  LIST_INSERT_HEAD(alist, a, link);
}


/**
 *
 */
static void
attachment_load(struct attachment_list *alist, const uint8_t *ptr, size_t len)
{
  if(len < 20)
    return;

#if ENABLE_LIBFREETYPE
  if(!memcmp(ptr, (const uint8_t []){0,1,0,0,0}, 5) ||
     !memcmp(ptr, "OTTO", 4)) {

    void *h = freetype_load_font_from_memory(ptr, len);
    if(h != NULL)
      attachment_add_dtor(alist, freetype_unload_font, h);
    return;
  }
#endif
}

/**
 *
 */
static void
attachment_unload_all(struct attachment_list *alist)
{
  attachment_t *a;

  while((a = LIST_FIRST(alist)) != NULL) {
    LIST_REMOVE(a, link);
    a->dtor(a->opaque);
    free(a);
  }
}
