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
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>

#include "main.h"
#include "fa_video.h"
#include "event.h"
#include "media/media.h"
#include "fileaccess.h"
#include "fa_libav.h"
#include "backend/dvd/dvd.h"
#include "notifications.h"
#include "htsmsg/htsmsg_xml.h"
#include "backend/backend.h"
#include "misc/isolang.h"
#include "text/text.h"
#include "video/video_settings.h"
#include "video/video_playback.h"
#include "subtitles/subtitles.h"
#include "misc/md5.h"
#include "misc/str.h"
#include "i18n.h"
#include "metadata/playinfo.h"
#include "usage.h"

#if ENABLE_METADATA
#include "fa_probe.h"
#endif

#if ENABLE_HLS
#include "backend/hls/hls.h"
#endif

typedef struct seek_item {
  prop_t *si_prop;
  float si_start; // in seconds
} seek_item_t;

typedef struct seek_index {
  prop_t *si_root;
  int si_nitems;
  seek_item_t *si_current;
  seek_item_t si_items[0];
} seek_index_t;



LIST_HEAD(attachment_list, attachment);

typedef struct attachment {
  LIST_ENTRY(attachment) link;
  void (*dtor)(void *opaque);
  void *opaque;
} attachment_t;

static void attachment_load(struct attachment_list *alist,
			    const char *url, int64_t offset, 
			    int size, int font_domain, const char *source);

static void attachment_unload_all(struct attachment_list *alist);

static void compute_hash(fa_handle_t *fh, video_args_t *va);

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
video_seek(AVFormatContext *fctx, media_pipe_t *mp, media_buf_t **mbp,
	   int64_t pos, const char *txt)
{
  pos = FFMAX(0, FFMIN(fctx->duration, pos)) + fctx->start_time;

  TRACE(TRACE_DEBUG, "Video", "seek %s to %.2f (%"PRId64" - %"PRId64")", txt, 
	(pos - fctx->start_time) / 1000000.0,
	pos, fctx->start_time);

  if(av_seek_frame(fctx, -1, pos, AVSEEK_FLAG_BACKWARD)) {
    TRACE(TRACE_ERROR, "Video", "Seek failed");
  }

  mp->mp_video.mq_seektarget = pos;
  mp->mp_audio.mq_seektarget = pos;

  mp_flush(mp);
  
  if(*mbp != NULL && *mbp != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, *mbp);
  *mbp = NULL;

  pos -= fctx->start_time;


  prop_set(mp->mp_prop_root, "seektime", PROP_SET_FLOAT, pos / 1000000.0);
}


/**
 *
 */
static void
update_seek_index(seek_index_t *si, int sec)
{
  if(si != NULL && si->si_nitems) {
    int i, j = 0;
    for(i = 0; i < si->si_nitems; j = i, i++)
      if(si->si_items[i].si_start > sec)
	break;
    
    if(si->si_current != &si->si_items[j]) {
      si->si_current = &si->si_items[j];
      prop_suggest_focus(si->si_current->si_prop);
    }
  }
}



/**
 * Thread for reading from lavf and sending to lavc
 */
static event_t *
video_player_loop(AVFormatContext *fctx, media_codec_t **cwvec,
		  media_pipe_t *mp, int flags,
		  char *errbuf, size_t errlen,
		  const char *canonical_url,
		  int freetype_context,
		  seek_index_t *sidx, // Minute for minute thumbs
		  seek_index_t *cidx, // Chapters
		  int cwvec_size,
		  fa_handle_t *fh,
                  int resume_mode,
                  const char *title)
{
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  AVPacket pkt;
  int r, si;
  event_t *e;
  event_ts_t *ets;
  int64_t ts;

  int lastsec = -1;
  int restartpos_last = -1;
  int64_t last_timestamp_presented = AV_NOPTS_VALUE;

  mp->mp_seek_base = 0;
  mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;

  if(mp->mp_flags & MP_CAN_SEEK) {
    int64_t start = playinfo_get_restartpos(canonical_url, title, resume_mode) * 1000;
    if(start) {
      TRACE(TRACE_DEBUG, "VIDEO", "Attempting to resume from %.2f seconds",
            start / 1000000.0f);
      mp->mp_seek_base = start;
      video_seek(fctx, mp, &mb, start, "restart position");
    }
  }

  const int64_t offset = fctx->start_time != PTS_UNSET ? fctx->start_time : 0;

  while(1) {
    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      mp->mp_eof = 0;

      fa_deadline(fh, mp->mp_buffer_delay != INT32_MAX ? 
		  mp->mp_buffer_delay / 3 : 0);

      r = av_read_frame(fctx, &pkt);

      if(r == AVERROR(EAGAIN))
	continue;

      if(r) {
	char buf[512];
	if(av_strerror(r, buf, sizeof(buf)))
	  snprintf(buf, sizeof(buf), "Error %d", r);
	TRACE(TRACE_DEBUG, "Video", "Playback reached EOF: %s (%d)", buf, r);
	mb = MB_SPECIAL_EOF;
	mp->mp_eof = 1;
	continue;
      }

      si = pkt.stream_index;
      if(si >= cwvec_size)
	goto bad;

      if(si == mp->mp_video.mq_stream) {
	/* Current video stream */
	mb = media_buf_from_avpkt_unlocked(mp, &pkt);
	mb->mb_data_type = MB_VIDEO;
	mq = &mp->mp_video;

	if(fctx->streams[si]->avg_frame_rate.num) {
	  mb->mb_duration = 1000000LL * fctx->streams[si]->avg_frame_rate.den /
	    fctx->streams[si]->avg_frame_rate.num;
	} else {
	  mb->mb_duration = rescale(fctx, pkt.duration, si);
	}

        mp->mp_framerate = fctx->streams[si]->avg_frame_rate;

      } else if(fctx->streams[si]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {

	mb = media_buf_from_avpkt_unlocked(mp, &pkt);
	mb->mb_data_type = MB_AUDIO;
	mq = &mp->mp_audio;

      } else if(fctx->streams[si]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {

	int duration = pkt.convergence_duration ?: pkt.duration;

	mb = media_buf_from_avpkt_unlocked(mp, &pkt);
	mb->mb_codecid = fctx->streams[si]->codec->codec_id;
	mb->mb_font_context = freetype_context;
	mb->mb_data_type = MB_SUBTITLE;
	mq = &mp->mp_video;

	mb->mb_duration = rescale(fctx, duration, si);

      } else {
	/* Check event queue ? */
      bad:
	av_free_packet(&pkt);
	continue;
      }

      mb->mb_pts      = rescale(fctx, pkt.pts, si);
      mb->mb_dts      = rescale(fctx, pkt.dts, si);

      if(mq->mq_seektarget != AV_NOPTS_VALUE &&
	 mb->mb_data_type != MB_SUBTITLE) {
	ts = mb->mb_pts != AV_NOPTS_VALUE ? mb->mb_pts : mb->mb_dts;
	if(ts < mq->mq_seektarget) {
	  mb->mb_skip = 1;
	} else {
	  mq->mq_seektarget = AV_NOPTS_VALUE;
	}
      }

      mb->mb_cw = cwvec[si] ? media_codec_ref(cwvec[si]) : NULL;

      mb->mb_stream = pkt.stream_index;

      if(mb->mb_data_type == MB_VIDEO) {
	mb->mb_drive_clock = 2;
        mb->mb_user_time = offset;
      }

      mb->mb_keyframe = !!(pkt.flags & AV_PKT_FLAG_KEY);
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
	e = event_create_type(EVENT_EOF);
	break;
      }
    } else if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_CURRENT_TIME)) {

      ets = (event_ts_t *)e;

      if(ets->epoch == mp->mp_epoch && mp->mp_flags & MP_CAN_SEEK) {
	int sec = ets->ts / 1000000;
	last_timestamp_presented = ets->ts;

	// Update restartpos every 5 seconds
	if(sec < restartpos_last || sec >= restartpos_last + 5) {
	  restartpos_last = sec;
	  playinfo_set_restartpos(canonical_url, ets->ts / 1000, 1);
	}
      
	if(sec != lastsec) {
	  lastsec = sec;
	  update_seek_index(sidx, sec);
	  update_seek_index(cidx, sec);
	}
      }


    } else if(event_is_type(e, EVENT_SEEK)) {

      ets = (event_ts_t *)e;
      video_seek(fctx, mp, &mb, ets->ts, "direct");

    } else if(event_is_action(e, ACTION_SKIP_FORWARD) ||
              event_is_action(e, ACTION_SKIP_BACKWARD) ||
              event_is_type(e, EVENT_EXIT) ||
	      event_is_type(e, EVENT_PLAY_URL)) {
      break;
    }
    event_release(e);
  }

  if(mb != NULL && mb != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, mb);

  if(fctx->duration != AV_NOPTS_VALUE) {

    // Compute stop position (in percentage of video length)

    int spp =
      fctx->duration > 0 ? (mp->mp_seek_base * 100 / fctx->duration) : 0;

    if(spp >= video_settings.played_threshold || event_is_type(e, EVENT_EOF)) {
      playinfo_set_restartpos(canonical_url, -1, 0);
      playinfo_register_play(canonical_url, 1);
      TRACE(TRACE_DEBUG, "Video",
            "Playback reached %d%%, counting as played (%s)",
            spp, canonical_url);
    } else if(last_timestamp_presented != PTS_UNSET) {
      playinfo_set_restartpos(canonical_url, last_timestamp_presented / 1000,
                              0);
    }
  } else {
    /*
     * Hack to wipe out bad records from the db. See #2503
     * Remove some time in the future.
     */
    playinfo_set_restartpos(canonical_url, -1, 0);
  }

  return e;
}


/**
 *
 */
static seek_index_t *
build_index(media_pipe_t *mp, AVFormatContext *fctx, const char *url)
{
  if(fctx->duration == AV_NOPTS_VALUE)
    return NULL;

  char buf[URL_MAX];

  int items = 1 + fctx->duration / 60000000;

  seek_index_t *si = mymalloc(sizeof(seek_index_t) +
			      sizeof(seek_item_t) * items);
  if(si == NULL) 
    return NULL;

  si->si_root = prop_create(mp->mp_prop_root, "seekindex");
  prop_t *parent = prop_create(si->si_root, "positions");

  si->si_current = NULL;
  si->si_nitems = items;

  prop_set_int(prop_create(si->si_root, "available"), 1);

  int i;
  for(i = 0; i < items; i++) {
    seek_item_t *item = &si->si_items[i];

    prop_t *p = prop_create_root(NULL);

    snprintf(buf, sizeof(buf), "%s#%d", url, i * 60);
    prop_set(p, "image", PROP_SET_STRING, buf);
    prop_set_float(prop_create(p, "timestamp"), i * 60);

    item->si_prop = p;
    item->si_start = i * 60;

    if(prop_set_parent(p, parent))
      abort();
  }
  return si;
}

/**
 *
 */
static seek_index_t *
build_chapters(media_pipe_t *mp, AVFormatContext *fctx, const char *url)
{
  if(fctx->nb_chapters == 0)
    return NULL;

  char buf[URL_MAX];

  int items = fctx->nb_chapters;

  TRACE(TRACE_DEBUG, "Video", "%d chapters", items);

  seek_index_t *si = mymalloc(sizeof(seek_index_t) +
			      sizeof(seek_item_t) * items);
  if(si == NULL)
    return NULL;

  si->si_root = prop_create(mp->mp_prop_root, "chapterindex");
  prop_t *parent = prop_create(si->si_root, "positions");

  si->si_current = NULL;
  si->si_nitems = items;

  prop_set_int(prop_create(si->si_root, "available"), 1);

  int i;
  for(i = 0; i < items; i++) {
    const AVChapter *avc = fctx->chapters[i];
  
    seek_item_t *item = &si->si_items[i];

    prop_t *p = prop_create_root(NULL);

    double start = av_rescale_q(avc->start, avc->time_base, AV_TIME_BASE_Q);
    double end   = av_rescale_q(avc->end, avc->time_base, AV_TIME_BASE_Q);

    item->si_start = start / 1000000.0;

    snprintf(buf, sizeof(buf), "%s#%d", url, (int)item->si_start);
    prop_set(p, "image", PROP_SET_STRING, buf);

    prop_set_float(prop_create(p, "timestamp"), item->si_start);
    prop_set_float(prop_create(p, "end"), end / 1000000.0);

    AVDictionaryEntry *tag;
    
    tag = av_dict_get(avc->metadata, "title", NULL, AV_DICT_IGNORE_SUFFIX);
    if(tag != NULL && utf8_verify(tag->value))
      prop_set(p, "title", PROP_SET_STRING, tag->value);

    item->si_prop = p;

    if(prop_set_parent(p, parent))
      abort();
  }
  return si;
}


/**
 *
 */
static void
seek_index_destroy(seek_index_t *si)
{
  if(si == NULL)
    return;
  prop_destroy(si->si_root);
  free(si);
}




/**
 *
 */
event_t *
be_file_playvideo(const char *url, media_pipe_t *mp,
		  char *errbuf, size_t errlen,
		  video_queue_t *vq, struct vsource_list *vsl,
		  const video_args_t *va0)
{
  rstr_t *title = NULL;
  video_args_t va = *va0;
  mp_set_url(mp, va0->canonical_url, va0->parent_url, va0->parent_title);

  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);

  if(!(va.flags & BACKEND_VIDEO_NO_AUDIO))
    mp_become_primary(mp);

  prop_set(mp->mp_prop_root, "type", PROP_SET_STRING, "video");

#if ENABLE_DVD && ENABLE_METADATA
  if(va.mimetype == NULL) {
    struct fa_stat fs;

    if(fa_stat(url, &fs, errbuf, errlen))
      return NULL;

    /**
     * Is it a DVD ?
     */
    if(fs.fs_type == CONTENT_DIR) {

      metadata_t *md = fa_probe_dir(url);
      int is_dvd = md->md_contenttype == CONTENT_DVD;
      metadata_destroy(md);

      if(is_dvd)
	goto isdvd;
      return NULL;
    }
  }
#endif

  /**
   * Check file type
   */
  fa_handle_t *fh;

  fa_open_extra_t foe = {
    .foe_stats = mp->mp_prop_io,
    .foe_cancellable = mp->mp_cancellable,
  };

  fh = fa_open_ex(url, errbuf, errlen, FA_BUFFERED_BIG, &foe);
  if(fh == NULL)
    return NULL;

  if(va.flags & BACKEND_VIDEO_SET_TITLE) {
    char tmp[1024];

    fa_url_get_last_component(tmp, sizeof(tmp), va.canonical_url);
    char *x = strrchr(tmp, '.');
    if(x)
      *x = 0;

    title = rstr_from_bytes(tmp, NULL, 0);
    va.title = rstr_get(title);

    prop_set(mp->mp_prop_metadata, "title", PROP_SET_RSTRING, title);
  }


#if ENABLE_METADATA
  if(va.mimetype == NULL) {
    if(fa_probe_iso(NULL, fh) == 0) {
      fa_close(fh);
#if ENABLE_DVD
    isdvd:
      prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 0);
      return dvd_play(url, mp, errbuf, errlen, 1);
#else
      snprintf(errbuf, errlen, "DVD playback is not supported");
      return NULL;
#endif
    }
  }
#endif


#if ENABLE_HLS
  // See if this is an HLS playlist

  if(fa_seek(fh, 0, SEEK_SET) == 0) {
    char buf[1024];
    int l = fa_read(fh, buf, sizeof(buf) - 1);
    if(l > 10) {
      buf[l] = 0;
      if(mystrbegins(buf, "#EXTM3U") &&
         (strstr(buf, "#EXT-X-STREAM-INF:") ||
          strstr(buf, "#EXTINF"))) {
        htsbuf_queue_t hq;
        htsbuf_queue_init(&hq, 0);
        htsbuf_append(&hq, buf, l);
        if(l == sizeof(buf) - 1 && fa_read_to_htsbuf(&hq, fh, 100000)) {
          htsbuf_queue_flush(&hq);
          snprintf(errbuf, errlen, "Unable to read HLS playlist file");
        }
        fa_close(fh);

        char *hlslist = htsbuf_to_string(&hq);
	event_t *e = hls_play_extm3u(hlslist, url, mp, errbuf, errlen,
				     vq, vsl, &va);
        free(hlslist);
	rstr_release(title);
	return e;
      }
    }
  }
#endif

  event_t *e = be_file_playvideo_fh(url, mp,  errbuf, errlen, vq, fh, &va);
  rstr_release(title);
  return e;
}

/**
 *
 */
event_t *
be_file_playvideo_fh(const char *url, media_pipe_t *mp,
                     char *errbuf, size_t errlen,
                     video_queue_t *vq, fa_handle_t *fh,
		     const video_args_t *va0)
{
  sub_scanner_t *ss = NULL;
  video_args_t va = *va0;

  if(!(va.flags & BACKEND_VIDEO_NO_SUBTITLE_SCAN)) {
    compute_hash(fh, &va);
    if(!va.hash_valid)
      TRACE(TRACE_DEBUG, "Video",
            "Unable to compute opensub hash, stream probably not seekable");
  }

  AVIOContext *avio = fa_libav_reopen(fh, 0);
  va.filesize = avio_size(avio);

  AVFormatContext *fctx;
  if((fctx = fa_libav_open_format(avio, url, errbuf, errlen,
				  va.mimetype, 0, -1, 2)) == NULL) {
    fa_libav_close(avio);
    return NULL;
  }

  usage_event("Play video", 1, USAGE_SEG("format", fctx->iformat->name));

  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;
  mp->mp_video.mq_stream2 = -1;

  TRACE(TRACE_DEBUG, "Video", "Starting playback of %s (%s)",
	url, fctx->iformat->name);

#if ENABLE_METADATA
  /**
   * Update property metadata
   */
  metadata_t *md = fa_metadata_from_fctx(fctx);
  if(md != NULL) {
    metadata_to_proptree(md, mp->mp_prop_metadata, 0);
    metadata_destroy(md);
  }

  // We're gonna change/release it further down so claim a reference
  /**
   * Overwrite with data from database if we have something which
   * is not dsid == 1 (the file itself)
   */
  md = metadata_get_video_data(url);
  if(md != NULL && md->md_dsid != 1) {
    metadata_to_proptree(md, mp->mp_prop_metadata, 0);

    // Some hard coded stuff for subtitle scanner

    if(md->md_parent &&
       md->md_parent->md_type == METADATA_TYPE_SEASON &&
       md->md_parent->md_parent && 
       md->md_parent->md_parent->md_type == METADATA_TYPE_SERIES) {

      va.episode = md->md_idx;
      va.season = md->md_parent->md_idx;
      if(md->md_parent->md_parent->md_title != NULL)
	va.title = rstr_get(md->md_parent->md_parent->md_title);

    } else {

      if(md->md_title)
	va.title = rstr_get(md->md_title);
      if(md->md_imdb_id)
	va.imdb = rstr_get(md->md_imdb_id);
    }
  }
#endif

  if(!(va.flags & BACKEND_VIDEO_NO_SUBTITLE_SCAN)) {
    /**
     * Create subtitle scanner.
     * Only scan for subs if we can compute hash or if duration is valid
     */
    if(fctx->duration != AV_NOPTS_VALUE || va.hash_valid) {
      ss = sub_scanner_create(url, mp->mp_prop_subtitle_tracks, &va,
                              fctx->duration != AV_NOPTS_VALUE ?
                              fctx->duration / 1000000 : 0);
    }
  }

  htsmsg_t *vpi = video_playback_info_create(&va);

  /**
   * Init codec contexts
   */
  media_codec_t **cwvec = alloca(fctx->nb_streams * sizeof(void *));
  memset(cwvec, 0, sizeof(void *) * fctx->nb_streams);
  
  int cwvec_size = fctx->nb_streams;
  media_format_t *fw = media_format_create(fctx);


  // Scan all streams and select defaults
  int freetype_context = freetype_get_context();
  struct attachment_list alist;
  int i;
  LIST_INIT(&alist);


  for(i = 0; i < fctx->nb_streams; i++) {
    char str[256];
    media_codec_params_t mcp = {0};
    AVStream *st = fctx->streams[i];
    AVCodecContext *ctx = st->codec;
    AVDictionaryEntry *fn, *mt;
    
    avcodec_string(str, sizeof(str), ctx, 0);
    TRACE(TRACE_DEBUG, "Video", " Stream #%d: %s", i, str);

    switch(ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      mcp.width = ctx->width;
      mcp.height = ctx->height;
      mcp.profile = ctx->profile;
      mcp.level = ctx->level;
      mcp.sar_num = st->sample_aspect_ratio.num;
      mcp.sar_den = st->sample_aspect_ratio.den;

      mcp.frame_rate_num = st->avg_frame_rate.num;
      mcp.frame_rate_den = st->avg_frame_rate.den;

      if(!mcp.frame_rate_num  || !mcp.frame_rate_num) {
	mcp.frame_rate_num = ctx->time_base.den;
	mcp.frame_rate_den = ctx->time_base.num;
      }
      break;

    case AVMEDIA_TYPE_AUDIO:
      if(va.flags & BACKEND_VIDEO_NO_AUDIO)
	continue;
      if(ctx->codec_id == AV_CODEC_ID_DTS)
	ctx->channels = 0;
      break;

    case AVMEDIA_TYPE_ATTACHMENT:
      fn = av_dict_get(st->metadata, "filename", NULL, AV_DICT_IGNORE_SUFFIX);
      mt = av_dict_get(st->metadata, "mimetype", NULL, AV_DICT_IGNORE_SUFFIX);

      TRACE(TRACE_DEBUG, "Video", "         filename: %s mimetype: %s size: %d",
	    fn ? fn->value : "<unknown>",
	    mt ? mt->value : "<unknown>",
	    st->attached_size);

      if(st->attached_size)
	attachment_load(&alist, url, st->attached_offset, st->attached_size,
			freetype_context, fn ? fn->value : "<unknown>");
      break;

    default:
      break;
    }


    if(ctx->codec_type == AVMEDIA_TYPE_VIDEO && mp->mp_video.mq_stream != -1)
      continue;

    mcp.extradata      = ctx->extradata;
    mcp.extradata_size = ctx->extradata_size;

    cwvec[i] = media_codec_create(ctx->codec_id, 0, fw, ctx, &mcp, mp);

    if(cwvec[i] != NULL) {
      TRACE(TRACE_DEBUG, "Video", " Stream #%d: Codec created", i);
      switch(ctx->codec_type) {
      case AVMEDIA_TYPE_VIDEO:
	if(mp->mp_video.mq_stream == -1) {
	  mp->mp_video.mq_stream = i;
          if(mcp.frame_rate_num && mcp.frame_rate_den) {
            htsmsg_add_dbl(vpi, "framerate",
                           (double)mcp.frame_rate_num / mcp.frame_rate_den);
          }
          if(mcp.width)
            htsmsg_add_u32(vpi, "width", mcp.width);
          if(mcp.height)
            htsmsg_add_u32(vpi, "height", mcp.height);
	}
	break;

      case AVMEDIA_TYPE_AUDIO:
	if(mp->mp_audio.mq_stream == -1) {
	  mp->mp_audio.mq_stream = i;
	  prop_set_stringf(mp->mp_prop_audio_track_current, "libav:%d", i);
          prop_set_int(mp->mp_prop_audio_track_current_manual, 0);
	}
	break;
      default:
	break;
      }
    }
  }

  int flags = MP_CAN_PAUSE;

  if(fctx->duration != PTS_UNSET)
    flags |= MP_CAN_SEEK;

  video_playback_info_invoke(VPI_START, vpi, mp->mp_prop_root);
  // Start it
  mp_configure(mp, flags, MP_BUFFER_DEEP, fctx->duration, "video");

  seek_index_t *si = build_index(mp, fctx, url);
  seek_index_t *ci = build_chapters(mp, fctx, url);

  playinfo_register_play(va.canonical_url, 0);
  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 0);

  event_t *e;
  e = video_player_loop(fctx, cwvec, mp, va.flags, errbuf, errlen,
			va.canonical_url, freetype_context, si, ci,
			cwvec_size, fh, va.resume_mode, va.title);

  video_playback_info_invoke(VPI_STOP, vpi, mp->mp_prop_root);
  htsmsg_release(vpi);

  seek_index_destroy(si);
  seek_index_destroy(ci);

  TRACE(TRACE_DEBUG, "Video", "Stopped playback of %s", url);

  mp_shutdown(mp);

  for(i = 0; i < cwvec_size; i++)
    if(cwvec[i] != NULL)
      media_codec_deref(cwvec[i]);

  attachment_unload_all(&alist);

  media_format_deref(fw);

  if(ss != NULL)
    sub_scanner_destroy(ss);

#if ENABLE_METADATA
  if(md != NULL)
    metadata_destroy(md);
#endif

  return e;
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
attachment_load(struct attachment_list *alist, const char *url, int64_t offset, 
		int size, int font_domain, const char *source)
{
  char errbuf[256];
  if(size < 20)
    return;

  fa_handle_t *fh = fa_open(url, errbuf, sizeof(errbuf));
  if(fh == NULL) {
    TRACE(TRACE_ERROR, "Video", "Unable to open attachement -- %s", errbuf);
    return;
  }

  fh = fa_slice_open(fh, offset, size);

  void *h = freetype_load_dynamic_font_fh(fh, url, font_domain, NULL, 0);
  if(h != NULL)
     attachment_add_dtor(alist, freetype_unload_font, h);
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



/**
 * Compute hash for the given file
 *
 * opensubhash
 *   http://trac.opensubtitles.org/projects/opensubtitles/wiki/HashSourceCodes
 *
 * subdbhash
 *   http://thesubdb.com/api/
 */
static void
compute_hash(fa_handle_t *fh, video_args_t *va)
{
  int i;
  uint64_t hash;
  int64_t *mem;

  int64_t size = fa_fsize(fh);
  md5_decl(md5ctx);

  if(size < 65536)
    return;

  hash = size;

  if(fa_seek(fh, 0, SEEK_SET) != 0)
    return;

  mem = malloc(sizeof(int64_t) * 8192);

  if(fa_read(fh, mem, 65536) != 65536) {
    free(mem);
    return;
  }

  md5_init(md5ctx);
  md5_update(md5ctx, (void *)mem, 65536);

  for(i = 0; i < 8192; i++) {
#if defined(__BIG_ENDIAN__)
    hash += __builtin_bswap64(mem[i]);
#else
    hash += mem[i];
#endif
  }

  if(fa_seek_lazy(fh, -65536, SEEK_END) == -1 ||
     fa_read(fh, mem, 65536) != 65536) {
    free(mem);
    md5_final(md5ctx, va->subdbhash); // need to free()
    return;
  }

  md5_update(md5ctx, (void *)mem, 65536);

  for(i = 0; i < 8192; i++) {
#if defined(__BIG_ENDIAN__)
    hash += __builtin_bswap64(mem[i]);
#else
    hash += mem[i];
#endif
  }
  free(mem);
  md5_final(md5ctx, va->subdbhash);
  va->opensubhash = hash;
  va->hash_valid = 1;
}
