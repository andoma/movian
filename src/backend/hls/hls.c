/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include <string.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>


#include "navigator.h"
#include "backend/backend.h"
#include "media.h"
#include "showtime.h"
#include "i18n.h"
#include "misc/isolang.h"
#include "misc/str.h"
#include "misc/dbl.h"
#include "misc/queue.h"
#include "video/video_playback.h"
#include "video/video_settings.h"
#include "metadata/metadata.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_libav.h"
#include "hls.h"
#include "subtitles/subtitles.h"

/**
 * Relevant docs:
 *
 * http://tools.ietf.org/html/draft-pantos-http-live-streaming-07
 * http://developer.apple.com/library/ios/#technotes/tn2288/
 * http://developer.apple.com/library/ios/#technotes/tn2224/
 *
 * Buffer-Based Rate Adaptation for HTTP Video Streaming
 *    http://conferences.sigcomm.org/sigcomm/2013/papers/fhmn/p9.pdf
 */

#define TESTURL "http://devimages.apple.com.edgekey.net/resources/http-streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8"

#define TESTURL2 "http://svtplay7k-f.akamaihd.net/i/world//open/20130127/1244301-001A/MOLANDERS-001A-7c59babd66879169_,900,348,564,1680,2800,.mp4.csmil/master.m3u8"



TAILQ_HEAD(hls_variant_queue, hls_variant);
TAILQ_HEAD(hls_segment_queue, hls_segment);

#define HLS_CRYPTO_NONE   0
#define HLS_CRYPTO_AES128 1

/**
 *
 */
typedef struct hls_segment {
  TAILQ_ENTRY(hls_segment) hs_link;
  char *hs_url;
  int hs_size;
  int hs_byte_offset;
  int hs_byte_size;
  int64_t hs_duration; // in usec
  int64_t hs_time_offset;
  
  //  int64_t hs_start_ts;
  int hs_seq;
  AVFormatContext *hs_fctx; // Set if open

  int hs_vstream;
  int hs_astream;

  uint8_t hs_crypto;
  uint8_t hs_corrupt;

  rstr_t *hs_key_url;
  uint8_t hs_iv[16];

  struct hls_variant *hs_variant;

  int64_t hs_opened_at;
  int hs_block_cnt;

} hls_segment_t;


/**
 *
 */
typedef struct hls_variant {
  TAILQ_ENTRY(hls_variant) hv_link;
  char *hv_url;

  int hv_last_seq;
  int hv_first_seq;

  struct hls_segment_queue hv_segments;

  char hv_frozen;
  char hv_audio_only;
  int hv_h264_profile;
  int hv_h264_level;
  int hv_target_duration;

  time_t hv_loaded; /* last time it was loaded successfully
                     *  0 means not loaded
                     */

  int hv_program;
  int hv_bitrate;

  int hv_width;
  int hv_height;
  char *hv_subs_group;
  char *hv_audio_group;

  int64_t hv_duration;

  hls_segment_t *hv_current_seg;

  rstr_t *hv_key_url;
  buf_t *hv_key;

} hls_variant_t;


/**
 *
 */
typedef struct hls_demuxer {
  struct hls_variant_queue hd_variants;
  
  int64_t hd_seek_to;
  int hd_seek_initial;
  int hd_seq;

  hls_variant_t *hd_current;
  hls_variant_t *hd_seek;
  hls_variant_t *hd_req;

  int hd_bw;

  int64_t hd_delta_ts;

} hls_demuxer_t;

/**
 *
 */
typedef struct hls {
  const char *h_baseurl;

  int h_debug;

  media_pipe_t *h_mp;

  media_codec_t *h_codec_h264;
  media_codec_t *h_codec_aac;

  AVInputFormat *h_fmt;

  int h_blocked;

  hls_demuxer_t h_primary;

  int h_playback_priority;

  int h_live;

} hls_t;

#define HLS_TRACE(h, x...) do {			\
  if((h)->h_debug)				\
    TRACE(TRACE_DEBUG, "HLS", x);		\
  } while(0)


/**
 *
 */
static char *
get_attrib(char *v, const char **keyp, const char **valuep)
{
  const char *key = v;
  char *value = strchr(key, '=');
  if(value == NULL)
    return NULL;
  *value++ = 0;
  while(*value < 33 && *value)
    value++;
  if(*value == '"') {
    v = ++value;
    while(*v && *v != '"' && v[-1] != '\\')
      v++;
    if(*v)
      *v++ = 0;
  } else {
    v = value;
  }
  while(*v && *v != ',')
    v++;
  if(*v)
    *v++ = 0;
  *keyp = key;
  *valuep = value;
  return v;
}


/**
 *
 */
static void
segment_destroy(hls_segment_t *hs)
{
  assert(hs->hs_fctx == NULL);
  TAILQ_REMOVE(&hs->hs_variant->hv_segments, hs, hs_link);
  free(hs->hs_url);
  rstr_release(hs->hs_key_url);
  free(hs);
}


/**
 *
 */
static void
variant_destroy(hls_variant_t *hv)
{
  hls_segment_t *hs;
  while((hs = TAILQ_FIRST(&hv->hv_segments)) != NULL)
    segment_destroy(hs);

  free(hv->hv_subs_group);
  free(hv->hv_audio_group);
  buf_release(hv->hv_key);
  rstr_release(hv->hv_key_url);
  free(hv->hv_url);
}


/**
 *
 */
static void
variants_destroy(struct hls_variant_queue *q)
{
  hls_variant_t *hv;
  while((hv = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, hv, hv_link);
    variant_destroy(hv);
  }
}



/**
 *
 */
static hls_variant_t *
variant_create(void)
{
  hls_variant_t *hv = calloc(1, sizeof(hls_variant_t));
  TAILQ_INIT(&hv->hv_segments);
  return hv;
}


/**
 *
 */
static hls_segment_t *
hv_add_segment(hls_variant_t *hv, const char *url)
{
  hls_segment_t *hs = calloc(1, sizeof(hls_segment_t));
  hs->hs_url = url_resolve_relative_from_base(hv->hv_url, url);
  hs->hs_variant = hv;
  TAILQ_INSERT_TAIL(&hv->hv_segments, hs, hs_link);
  return hs;
}


/**
 *
 */
static hls_segment_t *
hv_find_segment_by_time(const hls_variant_t *hv, int64_t pos)
{
  hls_segment_t *hs;
  TAILQ_FOREACH_REVERSE(hs, &hv->hv_segments, hls_segment_queue, hs_link) {
    if(hs->hs_time_offset <= pos)
      break;
  }
  return hs;
}


/**
 *
 */
static hls_segment_t *
hv_find_segment_by_seq(const hls_variant_t *hv, int seq)
{
  hls_segment_t *hs;
  TAILQ_FOREACH(hs, &hv->hv_segments, hs_link) {
    if(hs->hs_seq == seq)
      break;
  }
  return hs;
}

/**
 *
 */
typedef struct hls_variant_parser {
  rstr_t *hvp_key_url;
  int hvp_crypto;
  int hvp_explicit_iv;
  uint8_t hvp_iv[16];
} hls_variant_parser_t;



/**
 *
 */
static void
hv_parse_key(hls_variant_parser_t *hvp, const char *baseurl, const char *V)
{
  char *v = mystrdupa(V);

  hvp->hvp_crypto = HLS_CRYPTO_NONE;
  hvp->hvp_explicit_iv = 0;

  while(*v) {
    const char *key, *value;
    v = get_attrib(v, &key, &value);
    if(v == NULL)
      break;

    if(!strcmp(key, "METHOD")) {
      if(!strcmp(value, "AES-128"))
	hvp->hvp_crypto = HLS_CRYPTO_AES128;

    } else if(!strcmp(key, "URI")) {
      char *s = url_resolve_relative_from_base(baseurl, value);
      rstr_release(hvp->hvp_key_url);
      hvp->hvp_key_url = rstr_alloc(s);
      free(s);

    } else if(!strcmp(key, "IV")) {
      if(!strncmp(value, "0x", 2) || !strncmp(value, "0X", 2)) {
	hvp->hvp_explicit_iv = 1;
	hex2bin(hvp->hvp_iv, sizeof(hvp->hvp_iv), value + 2);
      }
    }
  }
}


/**
 *
 */
static int
variant_update(hls_variant_t *hv, hls_t *h)
{
  char errbuf[1024];
  if(hv->hv_frozen)
    return 0;

  time_t now;
  time(&now);

  if(hv->hv_loaded == now)
    return 0;

  HLS_TRACE(h, "Updating variant %d", hv->hv_bitrate);

  buf_t *b = fa_load(hv->hv_url, NULL, errbuf, sizeof(errbuf), NULL, 
		     FA_COMPRESSION, NULL, NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "HLS", "Unable to open %s -- %s", hv->hv_url, errbuf);
    return 1;
  }

  hv->hv_loaded = now;

  b = buf_make_writable(b);

  double duration = 0;
  int byte_offset = -1;
  int byte_size = -1;
  int seq = 1;
  hls_variant_parser_t hvp;

  memset(&hvp, 0, sizeof(hvp));

  LINEPARSE(s, buf_str(b)) {
    const char *v;
    if((v = mystrbegins(s, "#EXTINF:")) != NULL) {
      duration = my_str2double(v, NULL);
    } else if((v = mystrbegins(s, "#EXT-X-ENDLIST")) != NULL) {
      hv->hv_frozen = 1;
    } else if((v = mystrbegins(s, "#EXT-X-TARGETDURATION")) != NULL) {
      hv->hv_target_duration = atoi(v);
    } else if((v = mystrbegins(s, "#EXT-X-KEY:")) != NULL) {
      hv_parse_key(&hvp, hv->hv_url, v);
    } else if((v = mystrbegins(s, "#EXT-X-MEDIA-SEQUENCE:")) != NULL) {
      seq = atoi(v);
    } else if((v = mystrbegins(s, "#EXT-X-BYTERANGE:")) != NULL) {
      byte_size = atoi(v);
      const char *o = strchr(v, '@');
      if(o != NULL)
        byte_offset = atoi(o+1);

    } else if(s[0] != '#') {

      if(seq > hv->hv_last_seq) {

	if(hv->hv_first_seq == 0)
	  hv->hv_first_seq = seq;

	hls_segment_t *hs = hv_add_segment(hv, s);
	hs->hs_byte_offset = byte_offset;
	hs->hs_byte_size   = byte_size;
	hs->hs_time_offset = hv->hv_duration;
	hs->hs_duration    = duration * 1000000LL;
	hs->hs_crypto      = hvp.hvp_crypto;
	hs->hs_key_url     = rstr_dup(hvp.hvp_key_url);
	hv->hv_duration   += hs->hs_duration;

	if(hvp.hvp_explicit_iv) {
	  memcpy(hs->hs_iv, hvp.hvp_iv, 16);
	} else {
	  memset(hs->hs_iv, 0, 12);
	  hs->hs_iv[12] = seq >> 24;
	  hs->hs_iv[13] = seq >> 16;
	  hs->hs_iv[14] = seq >> 8;
	  hs->hs_iv[15] = seq;
	}

	if(hv->hv_target_duration == 0)
	  hv->hv_target_duration = duration;

	hs->hs_seq = seq;
	duration = 0;
	byte_offset = -1;
	byte_size = -1;

	hv->hv_last_seq = hs->hs_seq;

	HLS_TRACE(h, "Added new seq %d", hs->hs_seq);
      }
      seq++;
    }
  }

  buf_release(b);
  rstr_release(hvp.hvp_key_url);
  return 0;
}


/**
 *
 */
static void
hls_dump(const hls_t *h)
{
  const hls_variant_t *hv;
  const char *txt;
  HLS_TRACE(h, "Base URL: %s", h->h_baseurl);
  HLS_TRACE(h, "Variants");
  TAILQ_FOREACH(hv, &h->h_primary.hd_variants, hv_link) {
    HLS_TRACE(h, "  %s", hv->hv_url);
    HLS_TRACE(h, "    bitrate:    %d", hv->hv_bitrate);

    if(hv->hv_audio_only) {
      HLS_TRACE(h, "    Audio only\n");
      continue;
    }

    switch(hv->hv_h264_profile) {
    case 66:
      txt = "h264 Baseline";
      break;
    case 77:
      txt = "h264 Main";
      break;
    default:
      txt = "<unknown>";
      break;
    }

    HLS_TRACE(h, "    Video resolution: %d x %d  Profile: %s  Level: %d.%d",
              hv->hv_width, hv->hv_height, txt,
              hv->hv_h264_level / 10,
              hv->hv_h264_level % 10);
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
static void
variant_close_current_seg(hls_variant_t *hv)
{
  if(hv->hv_current_seg == NULL)
    return;

  hls_segment_t *hs = hv->hv_current_seg;

  if(hs->hs_fctx != NULL)
    fa_libav_close_format(hs->hs_fctx);
  hs->hs_fctx = NULL;

  hv->hv_current_seg = NULL;
}


/**
 *
 */
static void
demuxer_update_bw(hls_t *h, hls_demuxer_t *hd)
{
  hls_variant_t *hv = hd->hd_current;
  hls_segment_t *hs = hv->hv_current_seg;

  if(hs == NULL || !hs->hs_opened_at)
    return;

  int ts = showtime_get_ts() - hs->hs_opened_at;
  if(h->h_blocked != hs->hs_block_cnt)
    return;

  int bw = 8000000LL * hs->hs_size / ts;

  if(hd->hd_bw == 0) {
    hd->hd_bw = bw;
  } else if (bw < hd->hd_bw) {
    hd->hd_bw = bw;
  } else {
    hd->hd_bw = (bw + hd->hd_bw * 3) / 4;
  }
  HLS_TRACE(h, "Estimated bandwidth: %d bps (filtered: %d bps)",
	    bw, hd->hd_bw);
}


typedef enum {
  SEGMENT_OPEN_OK,
  SEGMENT_OPEN_NOT_FOUND,
  SEGMENT_OPEN_CORRUPT,
} segment_open_result_t;

/**
 *
 */
static segment_open_result_t
segment_open(hls_t *h, hls_segment_t *hs, int fast_fail)
{
  int err, j;
  fa_handle_t *fh;
  char errbuf[256];

  assert(hs->hs_fctx == NULL);

  hls_variant_t *hv = hs->hs_variant;

  int flags = FA_STREAMING;

  if(fast_fail)
    flags |= FA_FAST_FAIL;

  if(hs->hs_byte_size != -1 && hs->hs_byte_offset != -1)
    flags |= FA_BUFFERED_SMALL;

  hs->hs_opened_at = showtime_get_ts();
  hs->hs_block_cnt = h->h_blocked;
  
  HLS_TRACE(h, "Open segment %d in %d bps @ %s",
	    hs->hs_seq, hs->hs_variant->hv_bitrate, hs->hs_url);

  fh = fa_open_ex(hs->hs_url, errbuf, sizeof(errbuf), flags, NULL);
  if(fh == NULL) {
    TRACE(TRACE_INFO, "HLS", "Unable to open segment %s -- %s",
	  hs->hs_url, errbuf);
    return SEGMENT_OPEN_NOT_FOUND;
  }
  
  if(hs->hs_byte_size != -1 && hs->hs_byte_offset != -1)
    fh = fa_slice_open(fh, hs->hs_byte_offset, hs->hs_byte_size);


  hs->hs_size = fa_fsize(fh);

  switch(hs->hs_crypto) {
  case HLS_CRYPTO_AES128:

    if(!rstr_eq(hs->hs_key_url, hv->hv_key_url)) {
      buf_release(hv->hv_key);
      hv->hv_key = fa_load(rstr_get(hs->hs_key_url), NULL,
			   errbuf, sizeof(errbuf), NULL, 0, NULL, NULL);
      if(hv->hv_key == NULL) {
	TRACE(TRACE_ERROR, "HLS", "Unable to load key file %s",
	      rstr_get(hs->hs_key_url));
	fa_close(fh);
	return SEGMENT_OPEN_NOT_FOUND;
      }

      rstr_set(&hv->hv_key_url, hs->hs_key_url);
    }
      
    fh = fa_aescbc_open(fh, hs->hs_iv, buf_c8(hv->hv_key));
  }

  AVIOContext *avio = fa_libav_reopen(fh);
  hs->hs_fctx = avformat_alloc_context();
  hs->hs_fctx->pb = avio;

  if((err = avformat_open_input(&hs->hs_fctx, hs->hs_url,
				h->h_fmt, NULL)) != 0) {
    TRACE(TRACE_ERROR, "HLS",
	  "Unable to open TS file (%s) segment seq: %d error %d",
	  hs->hs_url, hs->hs_seq, err);

    fa_libav_close(avio);
    return SEGMENT_OPEN_CORRUPT;
  }

  hs->hs_fctx->flags |= AVFMT_FLAG_NOFILLIN;
  hs->hs_vstream = -1;
  hs->hs_astream = -1;

  for(j = 0; j < hs->hs_fctx->nb_streams; j++) {
    const AVCodecContext *ctx = hs->hs_fctx->streams[j]->codec;

    switch(ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      if(hs->hs_vstream == -1 && ctx->codec_id == CODEC_ID_H264)
	hs->hs_vstream = j;
      break;

    case AVMEDIA_TYPE_AUDIO:
      if(hs->hs_astream == -1 && ctx->codec_id == CODEC_ID_AAC)
	hs->hs_astream = j;
      break;
	
    default:
      break;
    }
  }
  return SEGMENT_OPEN_OK;
}


/**
 *
 */
static void
variant_update_metadata(hls_t *h, hls_variant_t *hv, int availbw)
{
  mp_set_duration(h->h_mp, hv->hv_duration);
  char buf[256];
  snprintf(buf, sizeof(buf),
	   "HLS %d kbps (Avail: %d kbps)", 
	   hv->hv_bitrate / 1000, availbw / 1000);
  prop_set(h->h_mp->mp_prop_metadata, "format", PROP_SET_STRING, buf);
}


#define HLS_SEGMENT_EOF ((hls_segment_t *)-1)
#define HLS_SEGMENT_NYA ((hls_segment_t *)-2) // Not yet available
#define HLS_SEGMENT_ERR ((hls_segment_t *)-3) // Stream broken

/**
 *
 */
static hls_segment_t *
demuxer_get_segment(hls_t *h, hls_demuxer_t *hd)
{
  int retry = 0;

 again:
  // If no variant is requested we switch to the seek (low bw) variant
  // also swtich to seek variant if we want to seek (gasp!)
  if(hd->hd_req == NULL || hd->hd_seek_to != PTS_UNSET)
    hd->hd_req = hd->hd_seek;

  // Need to switch variant? Need to close current segment if so
  if(hd->hd_req != hd->hd_current) {
    
    if(hd->hd_current != NULL)
      variant_close_current_seg(hd->hd_current);

    hd->hd_current = hd->hd_req;
  }

  hls_variant_t *hv = hd->hd_current;

  if(hv->hv_current_seg == NULL ||
     hv->hv_current_seg->hs_seq != hd->hd_seq ||
     hd->hd_seek_to != AV_NOPTS_VALUE) {

    if(variant_update(hv, h)) {

      if(retry)
       	return HLS_SEGMENT_NYA;

      retry = 1;
      hd->hd_req = NULL;
      goto again;
    }

    if(hd->hd_seq == 0) {

      if(hv->hv_frozen) {
	hd->hd_seq = hv->hv_first_seq;
      } else {
	hd->hd_seq = MAX(hv->hv_last_seq - 2, hv->hv_first_seq);
      }
    }

    variant_close_current_seg(hv);

    hls_segment_t *hs;

    h->h_live = !hv->hv_frozen;

    // Initial seek on live streams make no sense, void that
    if(!hv->hv_frozen && hd->hd_seek_to != PTS_UNSET && hd->hd_seek_initial)
      hd->hd_seek_to = PTS_UNSET;

    if(hd->hd_seek_to != PTS_UNSET) {
      hs = hv_find_segment_by_time(hv, hd->hd_seek_to);
    } else {
      hs = hv_find_segment_by_seq(hv, hd->hd_seq);
    }

    if(hs == NULL) {
      if(hv->hv_frozen)
	return HLS_SEGMENT_EOF;
      else
	return HLS_SEGMENT_NYA;
    }

    // If we're not playing from the seek segment (ie. our "fallback" segment)
    // we ask for fast fail so we can retry with another segment

    segment_open_result_t rcode = segment_open(h, hs, hv != hd->hd_seek);

    switch(rcode) {
    case SEGMENT_OPEN_OK:
      break;

    case SEGMENT_OPEN_NOT_FOUND:

      /*
       * Segment can not be found (but since it's listed in the playlist
       * it *should* be there, retry with something directly else unless we're
       * at our seek variant in which case we wait a short while (by
       * returning the special Not-Yet-Available segment)
       */
      if(hv == hd->hd_seek)
	return HLS_SEGMENT_NYA;

      /* 
       * Variants are sorted in bitrate descending order, so just
       * step down one
       */
      hd->hd_req = TAILQ_NEXT(hv, hv_link);
      goto again;

    case SEGMENT_OPEN_CORRUPT:
      /*
       * File is there, but we are not able to parse it correctly
       * (broken key?, not a TS file, whatever)
       * Mark it as broken and try something else
       */
      hs->hs_corrupt = 1;
      if(hv == hd->hd_seek) {
	// We give up now
	return HLS_SEGMENT_ERR;
      }
      hd->hd_req = TAILQ_NEXT(hv, hv_link);
      goto again;
    }

    hd->hd_seek_to = PTS_UNSET;

    hv->hv_current_seg = hs;
    hd->hd_seq = hs->hs_seq;
    variant_update_metadata(h, hv, hd->hd_bw);
  }
  return hv->hv_current_seg;
}


/**
 *
 */
static void
hls_seek(hls_t *h, int64_t pts, int64_t ts, int initial)
{
  hls_demuxer_t *hd = &h->h_primary;
  mp_flush(h->h_mp, 0);

  h->h_mp->mp_video.mq_seektarget = pts;
  h->h_mp->mp_audio.mq_seektarget = pts;
  hd->hd_seek_to = ts;
  hd->hd_seek_initial = initial;
  prop_set(h->h_mp->mp_prop_root, "seektime", PROP_SET_FLOAT, ts / 1000000.0);
}


#if 0
/**
 *
 */
static hls_variant_t *
pick_variant(hls_t *h, int bw, int64_t buffer_depth, int td)
{
  hls_variant_t *hv;
  float f;

  int bd = buffer_depth / 1000000LL;

  printf("-- Estimated bandwidth: %d buffer depth: %d seconds\n", bw, bd);

  TAILQ_FOREACH(hv, &h->h_variants, hv_link) {

    if(bd) {
      f = (0.1 * bd) / (hv->hv_target_duration ?: td);
      printf("\t\tf=%f\n", f);
    } else {
      f = 0.75;
    }

    f = MAX(0.1, MIN(f, 2.0));
    printf("\tBW:%d factor:%f  comparing with:%d\n", hv->hv_bitrate, f,
	   (int)(bw * f));

    if(hv->hv_bitrate < bw * f)
      break;
  }
  if(hv == NULL)
     hv = h->h_seek;
  
  return hv;
}


/**
 *
 */
static hls_variant_t *
pick_variant(hls_t *h, int bw, int64_t buffer_depth, int td)
{
  hls_variant_t *hv;

  //  int bd = buffer_depth / 1000000LL;

  //  printf("-- Estimated bandwidth: %d buffer depth: %d seconds\n", bw, bd);

  TAILQ_FOREACH(hv, &h->h_variants, hv_link) {

    int dltime = (hv->hv_target_duration ?: td) * hv->hv_bitrate / 8;
    //    printf("\tBW: %d estimated download time: %d\n", hv->hv_bitrate, dltime);
	   
    if(3 * dltime < buffer_depth)
      break;
  }
  if(hv == NULL)
     hv = h->h_seek;
  
  return hv;
}


#endif


/**
 *
 */
static void
demuxer_select_variant_simple(hls_t *h, hls_demuxer_t *hd)
{
  hls_variant_t *hv;

  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_bitrate < hd->hd_bw)
      break;
  }
  if(hv == NULL || h->h_playback_priority)
    hv = hd->hd_seek;
  
  hd->hd_req = hv;
}


/**
 *
 */
static void
demuxer_select_variant_random(hls_t *h, hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  int cnt = 0;

  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link)
    if(!hv->hv_audio_only)
      cnt++;
  
  int r = rand() % cnt;
  cnt = 0;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_audio_only)
      continue;
    if(r == cnt)
      break;
    cnt++;
  }

  if(hv == NULL)
    hv = hd->hd_seek;
  HLS_TRACE(h, "Randomly selected bitrate %d", hv->hv_bitrate);
  hd->hd_req = hv;
}

/**
 *
 */
static void
demuxer_select_variant(hls_t *h, hls_demuxer_t *hd)
{
  if(1)
    demuxer_select_variant_simple(h, hd);
  else
    demuxer_select_variant_random(h, hd);
}


#define MB_EOF ((void *)-1)
#define MB_NYA ((void *)-2)



/**
 *
 */
static void
select_subtitle_track(media_pipe_t *mp, const char *id)
{
  TRACE(TRACE_DEBUG, "HLS", "Selecting subtitle track %s", id);

  mp_send_cmd(mp, &mp->mp_video, MB_CTRL_FLUSH_SUBTITLES);

  if(!strcmp(id, "sub:off")) {
    prop_set_string(mp->mp_prop_subtitle_track_current, id);
    mp->mp_video.mq_stream2 = -1;

    mp_load_ext_sub(mp, NULL, NULL);

  } else {

    mp->mp_video.mq_stream2 = -1;
    prop_set_string(mp->mp_prop_subtitle_track_current, id);
    mp_load_ext_sub(mp, id, NULL);
  }
}


/**
 *
 */
static event_t *
hls_play(hls_t *h, media_pipe_t *mp, char *errbuf, size_t errlen,
         const video_args_t *va)
{
  media_queue_t *mq = NULL;
  media_buf_t *mb = NULL;
  event_t *e = NULL;
  const char *canonical_url = va->canonical_url;
  int restartpos_last = -1;
  int64_t last_timestamp_presented = AV_NOPTS_VALUE;
  sub_scanner_t *ss = NULL;
  h->h_playback_priority = va->priority;

  mp->mp_video.mq_stream = 0;
  mp->mp_audio.mq_stream = 1;

  if(!(va->flags & BACKEND_VIDEO_NO_AUDIO))
    mp_become_primary(mp);

  prop_set_string(mp->mp_prop_type, "video");

  mp_set_playstatus_by_hold(mp, 0, NULL);

  mp_configure(mp, MP_PLAY_CAPS_SEEK | MP_PLAY_CAPS_PAUSE, MP_BUFFER_DEEP, 0);

  hls_variant_t *hv;
  hls_demuxer_t *hd = &h->h_primary;

  TAILQ_FOREACH_REVERSE(hv, &hd->hd_variants, hls_variant_queue, hv_link) {
    if(hv->hv_audio_only)
      continue;
    hd->hd_seek = hv;
    break;
  }

  if(va->flags & BACKEND_VIDEO_RESUME ||
     (video_settings.resume_mode == VIDEO_RESUME_YES &&
      !(va->flags & BACKEND_VIDEO_START_FROM_BEGINNING))) {
    int64_t start = video_get_restartpos(canonical_url) * 1000;
    if(start) {
      hls_seek(h, start, start, 1);
    }
  }


  hd->hd_current = hd->hd_seek;

  while(1) {

    if(mb == NULL) {
      mp->mp_eof = 0;

      hls_segment_t *hs = demuxer_get_segment(h, hd);

      if(hs == HLS_SEGMENT_EOF || hs == HLS_SEGMENT_ERR) {
	mb = MB_EOF;
	mp->mp_eof = 1;
	continue;
      }

      if(hs == HLS_SEGMENT_NYA) {
	mb = MB_NYA;
	continue;
      }

      if(ss == NULL && hs->hs_variant->hv_frozen)
	ss = sub_scanner_create(h->h_baseurl, mp->mp_prop_subtitle_tracks, va,
				hs->hs_variant->hv_duration / 1000000LL);

      AVPacket pkt;
      int r = av_read_frame(hs->hs_fctx, &pkt);
      if(r == AVERROR(EAGAIN))
        continue;

      if(r) {
	hd->hd_seq++;
	demuxer_update_bw(h, hd);
	demuxer_select_variant(h, hd);
	continue;
      }

      int si = pkt.stream_index;
      const AVStream *s = hs->hs_fctx->streams[si];

      if(si == hs->hs_vstream) {
        /* Current video stream */
        mb = media_buf_from_avpkt_unlocked(mp, &pkt);
        mb->mb_data_type = MB_VIDEO;
        mq = &mp->mp_video;
        if(s->avg_frame_rate.num) {
          mb->mb_duration = 1000000LL * s->avg_frame_rate.den /
            s->avg_frame_rate.num;
        } else {
          mb->mb_duration = rescale(hs->hs_fctx, pkt.duration, si);
        }
        mb->mb_cw = media_codec_ref(h->h_codec_h264);
        mb->mb_stream = 0;

      } else if(si == hs->hs_astream) {

        mb = media_buf_from_avpkt_unlocked(mp, &pkt);
        mb->mb_data_type = MB_AUDIO;
        mq = &mp->mp_audio;

        mb->mb_cw = media_codec_ref(h->h_codec_aac);
        mb->mb_stream = 1;

      } else {
        /* Check event queue ? */
        av_free_packet(&pkt);
        continue;
      }

      mb->mb_pts = rescale(hs->hs_fctx, pkt.pts, si);
      mb->mb_dts = rescale(hs->hs_fctx, pkt.dts, si);

      if(mq->mq_seektarget != AV_NOPTS_VALUE &&
         mb->mb_data_type != MB_SUBTITLE) {
        int64_t ts;
        ts = mb->mb_pts != AV_NOPTS_VALUE ? mb->mb_pts : mb->mb_dts;
        if(ts < mq->mq_seektarget) {
          mb->mb_skip = 1;
        } else {
          mb->mb_skip = 2;
          mq->mq_seektarget = AV_NOPTS_VALUE;
        }
      }

      if(mb->mb_data_type == MB_VIDEO) {
	if(hd->hd_delta_ts == PTS_UNSET && mb->mb_pts != PTS_UNSET)
	  hd->hd_delta_ts = mb->mb_pts - hs->hs_time_offset;

	mb->mb_drive_clock = 1;
	mb->mb_delta = hd->hd_delta_ts;
      }

      mb->mb_keyframe = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }

    if(mb == MB_NYA || mb == NULL) {
      // Nothing to send yet
      e = mp_dequeue_event_deadline(mp, 1000);
      mb = NULL;
      if(e == NULL)
	continue;
    } else if(mb == MB_EOF) {

      /* Wait for queues to drain */
      e = mp_wait_for_empty_queues(mp);
      if(e == NULL) {
	e = event_create_type(EVENT_EOF);
	break;
      }
    } else if((e = mb_enqueue_with_events_ex(mp, mq, mb,
					     &h->h_blocked)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_CURRENT_TIME)) {

      event_ts_t *ets = (event_ts_t *)e;

      if(ets->epoch == mp->mp_epoch) {
	int sec = ets->ts / 1000000;
	last_timestamp_presented = ets->ts;

	// Update restartpos every 5 seconds
	if(!h->h_live &&
           (sec < restartpos_last || sec >= restartpos_last + 5)) {
	  restartpos_last = sec;
	  metadb_set_video_restartpos(canonical_url, ets->ts / 1000);
	}
      }

    } else if(event_is_type(e, EVENT_PLAYBACK_PRIORITY)) {
      event_int_t *ei = (event_int_t *)e;
      h->h_playback_priority = ei->val;

    } else if(event_is_type(e, EVENT_SEEK)) {

      if(mb != NULL && mb != MB_EOF && mb != MB_NYA)
	media_buf_free_unlocked(mp, mb);

      mb = NULL;

      event_ts_t *ets = (event_ts_t *)e;

      hls_seek(h, ets->ts + hd->hd_delta_ts, ets->ts, 0);

    } else if(event_is_action(e, ACTION_STOP)) {
      mp_set_playstatus_stop(mp);

    } else if(event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)) {
      event_select_track_t *est = (event_select_track_t *)e;
      select_subtitle_track(mp, est->id);

    } else if(event_is_type(e, EVENT_EXIT) ||
	      event_is_type(e, EVENT_PLAY_URL)) {
      break;
    }

    event_release(e);
  }

  if(mb != NULL && mb != MB_EOF && mb != MB_NYA)
    media_buf_free_unlocked(mp, mb);

  if(!h->h_live) {

    // Compute stop position (in percentage of video length)

    int spp = mp->mp_duration ? mp->mp_seek_base * 100 / mp->mp_duration : 0;

    if(spp >= video_settings.played_threshold || event_is_type(e, EVENT_EOF)) {
      metadb_set_video_restartpos(canonical_url, -1);
      metadb_register_play(canonical_url, 1, CONTENT_VIDEO);
      TRACE(TRACE_DEBUG, "Video",
	    "Playback reached %d%%, counting as played (%s)",
	    spp, canonical_url);
    } else if(last_timestamp_presented != PTS_UNSET) {
      metadb_set_video_restartpos(canonical_url,
				  last_timestamp_presented / 1000);
    }
  }
  // Shutdown

  mp_flush(mp, 0);
  mp_shutdown(mp);

  if(hd->hd_current != NULL)
    variant_close_current_seg(hd->hd_current);

  sub_scanner_destroy(ss);

  return e;
}




/**
 *
 */
static int
variant_cmp(const hls_variant_t *a, const hls_variant_t *b)
{
  return b->hv_bitrate - a->hv_bitrate;
}


/**
 *
 */
static void
hls_add_variant(hls_t *h, const char *url, hls_variant_t **hvp,
		hls_demuxer_t *hd)
{
  if(*hvp == NULL)
    *hvp = variant_create();

  hls_variant_t *hv = *hvp;

  hv->hv_url = url_resolve_relative_from_base(h->h_baseurl, url);
  TAILQ_INSERT_SORTED(&hd->hd_variants, hv, hv_link, variant_cmp);
  *hvp = NULL;
}



/**
 *
 */
static void
hls_ext_x_media(hls_t *h, const char *V)
{
  //  char *v = mystrdupa(V);

}


// from https://developer.apple.com/library/ios/documentation/networkinginternet/conceptual/streamingmediaguide/StreamingMediaGuide.pdf

static const struct {
  const char *name;
  int profile;
  int level;
} AVC_h264_codecs[] = {

  // Baseline
  { "avc1.42001e",   66, 30},
  { "avc1.66.30",    66, 30},
  { "avc1.42001f",   66, 31},

  // Main
  { "avc1.4d001e",   77, 30},
  { "avc1.77.30",    77, 30},
  { "avc1.4d001f",   77, 31},
  { "avc1.4d0028",   77, 40},

  // High
  { "avc1.64001f",   100, 31},
  { "avc1.640028",   100, 40},
  { "avc1.640029",   100, 41}, // Spec says 4.0 but that must be a typo
};


/**
 *
 */
static void
hls_ext_x_stream_inf(hls_t *h, const char *V, hls_variant_t **hvp)
{
  char *v = mystrdupa(V);

  if(*hvp != NULL)
    free(*hvp);

  hls_variant_t *hv = *hvp = variant_create();

  while(*v) {
    const char *key, *value;
    v = get_attrib(v, &key, &value);
    if(v == NULL)
      break;

    if(!strcmp(key, "BANDWIDTH"))
      hv->hv_bitrate = atoi(value);
    else if(!strcmp(key, "AUDIO"))
      hv->hv_audio_group = strdup(value);
    else if(!strcmp(key, "SUBS"))
      hv->hv_subs_group = strdup(value);
    else if(!strcmp(key, "PROGRAM-ID"))
      hv->hv_program = atoi(value);
    else if(!strcmp(key, "CODECS")) {

      if(!strstr(value, "avc1"))
        hv->hv_audio_only = 1;

      for(int i = 0; i < ARRAYSIZE(AVC_h264_codecs); i++) {
        if(strstr(value, AVC_h264_codecs[i].name)) {
          hv->hv_h264_profile = AVC_h264_codecs[i].profile;
          hv->hv_h264_level = AVC_h264_codecs[i].level;
          break;
        }
      }
    }
    else if(!strcmp(key, "RESOLUTION")) {
      const char *h = strchr(value, 'x');
      if(h != NULL) {
        hv->hv_width  = atoi(value);
        hv->hv_height = atoi(h+1);
      }
    }
  }
}


/**
 *
 */
static void
hls_demuxer_init(hls_demuxer_t *hd)
{
  TAILQ_INIT(&hd->hd_variants);
  hd->hd_seek_to = PTS_UNSET;
  hd->hd_seq = 0;
  hd->hd_delta_ts = PTS_UNSET;
}


/**
 *
 */
event_t *
hls_play_extm3u(char *buf, const char *url, media_pipe_t *mp,
		char *errbuf, size_t errlen,
		video_queue_t *vq, struct vsource_list *vsl,
		const video_args_t *va0)
{
  if(!mystrbegins(buf, "#EXTM3U")) {
    snprintf(errbuf, errlen, "Not an m3u file");
    return NULL;
  }

  hls_t h;
  memset(&h, 0, sizeof(h));
  hls_demuxer_init(&h.h_primary);
  h.h_mp = mp;
  h.h_baseurl = url;
  h.h_fmt = av_find_input_format("mpegts");
  h.h_codec_h264 = media_codec_create(CODEC_ID_H264, 0, NULL, NULL, NULL, mp);
  h.h_codec_aac  = media_codec_create(CODEC_ID_AAC,  0, NULL, NULL, NULL, mp);
  h.h_debug = gconf.enable_hls_debug;

  hls_variant_t *hv = NULL;

  if(strstr(buf, "#EXT-X-STREAM-INF:")) {

    LINEPARSE(s, buf) {
      const char *v;
      if((v = mystrbegins(s, "#EXT-X-MEDIA:")) != NULL)
        hls_ext_x_media(&h, v);
      else if((v = mystrbegins(s, "#EXT-X-STREAM-INF:")) != NULL)
        hls_ext_x_stream_inf(&h, v, &hv);
      else if(s[0] != '#') {
        hls_add_variant(&h, s, &hv, &h.h_primary);
      }
    }
  } else {
    hls_add_variant(&h, h.h_baseurl, &hv, &h.h_primary);
  }

  hls_dump(&h);

  event_t *e = hls_play(&h, mp, errbuf, errlen, va0);

  variants_destroy(&h.h_primary.hd_variants);

  media_codec_deref(h.h_codec_h264);
  media_codec_deref(h.h_codec_aac);

  HLS_TRACE(&h, "HLS player done");

  return e;
}


/**
 *
 */
static event_t *
hls_playvideo(const char *url, media_pipe_t *mp,
              char *errbuf, size_t errlen,
              video_queue_t *vq, struct vsource_list *vsl,
              const video_args_t *va0)
{
  buf_t *buf;
  url += strlen("hls:");
  if(!strcmp(url, "test"))
    url = TESTURL;
  if(!strcmp(url, "test2"))
    url = TESTURL2;
  buf = fa_load(url, NULL, errbuf, errlen, NULL, FA_COMPRESSION, NULL, NULL);

  if(buf == NULL)
    return NULL;

  buf = buf_make_writable(buf);
  char *s = buf_str(buf);

  event_t *e = hls_play_extm3u(s, url, mp, errbuf, errlen, vq, vsl, va0);
  buf_release(buf);
  return e;
}


/**
 *
 */
static int
hls_canhandle(const char *url)
{
  return !strncmp(url, "hls:", strlen("hls:"));
}


/**
 *
 */
static backend_t be_hls = {
  .be_canhandle = hls_canhandle,
  .be_open = backend_open_video,
  .be_play_video = hls_playvideo,
};

BE_REGISTER(hls);
