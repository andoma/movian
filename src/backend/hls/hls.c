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
#include "media/media.h"
#include "showtime.h"
#include "i18n.h"
#include "misc/isolang.h"
#include "misc/str.h"
#include "misc/dbl.h"
#include "misc/queue.h"
#include "video/video_playback.h"
#include "video/video_settings.h"
#include "metadata/playinfo.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_libav.h"
#include "hls.h"
#include "subtitles/subtitles.h"
#include "usage.h"
#include "misc/minmax.h"

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


#define HLS_QUEUE_MERGE         0x1
#define HLS_QUEUE_KEYFRAME_SEEN 0x2

#define MB_EOF ((void *)-1)


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
  int64_t hs_first_ts;
  int64_t hs_ts_offset;


  int hs_seq;

  uint8_t hs_crypto;
  uint8_t hs_open_error;

  rstr_t *hs_key_url;
  uint8_t hs_iv[16];

  struct hls_variant *hs_variant;

  int64_t hs_opened_at;
  int hs_block_cnt;

  int hs_unavailable;

} hls_segment_t;


typedef enum {
  HLS_ERROR_OK = 0,
  HLS_ERROR_SEGMENT_NOT_FOUND,
  HLS_ERROR_SEGMENT_BROKEN,
  HLS_ERROR_SEGMENT_BAD_KEY,
  HLS_ERROR_SEGMENT_READ_ERROR,
  HLS_ERROR_VARIANT_BAD_FORMAT,
} hls_error_t;


/**
 *
 */
typedef struct hls_variant {
  TAILQ_ENTRY(hls_variant) hv_link;
  char *hv_url;
  struct hls *hv_hls;

  AVFormatContext *hv_fctx; // Set if open
  int hv_vstream;
  int hv_astream;

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

  int hv_corrupt_counter;

#define HV_CORRUPT_LIMIT 3 /* If corrupt_counter >= this, we never consider
			    * this variant again
			    */

  char *hv_subs_group;
  char *hv_audio_group;

  int64_t hv_duration;

  hls_segment_t *hv_current_seg;
  fa_handle_t *hv_current_file;

  int hv_opening_file;

  rstr_t *hv_key_url;
  buf_t *hv_key;


  uint8_t *hv_video_headers;
  int hv_video_headers_size;

  hls_error_t hv_recent_segment_status;

} hls_variant_t;


/**
 *
 */
typedef struct hls_demuxer {
  struct hls_variant_queue hd_variants;

  hls_variant_t *hd_current;
  hls_variant_t *hd_req;

  int hd_bw;

  media_codec_t *hd_audio_codec;

  time_t hd_last_switch;

} hls_demuxer_t;
/**
 *
 */
typedef struct hls {
  const char *h_baseurl;

  int h_debug;

  media_pipe_t *h_mp;

  media_codec_t *h_codec_h264;

  AVInputFormat *h_fmt;

  int h_blocked;

  hls_demuxer_t h_primary;

  int h_playback_priority;

  int h_restartpos_last;
  int64_t h_last_timestamp_presented;
  char h_sub_scanning_done;
  char h_enqueued_something;

  cancellable_t h_cancellable;

  event_t *h_exit_event;

  int64_t h_seek_to;

  hts_mutex_t h_mutex;
  hts_cond_t h_cond;

  int h_current_seq;

} hls_t;

#define HLS_TRACE(h, x, ...) do {                               \
    if((h)->h_debug)                                            \
      TRACE(TRACE_DEBUG, "HLS", x, ##__VA_ARGS__);		\
  } while(0)


/**
 *
 */
static char *
get_attrib(char *v, const char **keyp, const char **valuep)
{
  const char *key = v;
  while(*key == ' ')
    key++;

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
  free(hv);
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
variant_create(hls_t *h)
{
  hls_variant_t *hv = calloc(1, sizeof(hls_variant_t));
  hv->hv_hls = h;
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
  hs->hs_first_ts = PTS_UNSET;
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
    if(hs->hs_time_offset <= pos) {
      if(hs == TAILQ_LAST(&hv->hv_segments, hls_segment_queue)) {
	if(pos > hs->hs_time_offset + hs->hs_duration)
	  return NULL;
      }
      break;
    }
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

#define VARIANT_UNLOADABLE 1
#define VARIANT_EMPTY      2

/**
 *
 */
static int
variant_update(hls_variant_t *hv, time_t now)
{
  char errbuf[1024];
  if(hv->hv_frozen)
    return 0;

  if(hv->hv_loaded == now)
    return 0;

  hls_t *h = hv->hv_hls;

  buf_t *b = fa_load(hv->hv_url,
                      FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                      FA_LOAD_FLAGS(FA_COMPRESSION),
                      NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "HLS", "Unable to open %s -- %s", hv->hv_url, errbuf);
    return VARIANT_UNLOADABLE;
  }

  hv->hv_loaded = now;

  b = buf_make_writable(b);

  double duration = 0;
  int byte_offset = -1;
  int byte_size = -1;
  int seq = 1;
  int items = 0;
  hls_variant_parser_t hvp;


  memset(&hvp, 0, sizeof(hvp));

  int prev_last_seq = hv->hv_last_seq;

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

      items++;

      if(seq > hv->hv_last_seq) {

	if(hv->hv_first_seq == 0) {
	  hv->hv_first_seq = seq;
          prev_last_seq = seq - 1;
        }

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

      }
      seq++;
    }
  }

  if(prev_last_seq != hv->hv_last_seq)
    HLS_TRACE(h, "Added new seq %d ... %d",
              prev_last_seq + 1, hv->hv_last_seq);

  buf_release(b);
  rstr_release(hvp.hvp_key_url);

  if(items == 0)
    return VARIANT_EMPTY;
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
static void
check_audio_only(hls_t *h)
{
  hls_variant_t *hv;
  int streams = 0;
  int audio_only = 0;
  TAILQ_FOREACH(hv, &h->h_primary.hd_variants, hv_link) {
    streams++;
    if(hv->hv_audio_only)
      audio_only++;
  }

  if(streams == audio_only) {
    // Most likely not _all_ variants are audio only, so we clear the flag
    TAILQ_FOREACH(hv, &h->h_primary.hd_variants, hv_link) {
      hv->hv_audio_only = 0;
    }
  }
}



/**
 *
 */
static int
variant_open_file(hls_variant_t *hv, hls_t *h)
{
  hls_segment_t *hs = hv->hv_current_seg;
  fa_open_extra_t foe = {0};
  fa_handle_t *fh;
  char errbuf[512];
  const int fast_fail = 1;
  int retry_counter = 0;

  if(fast_fail)
    foe.foe_open_timeout = 2000;

  foe.foe_c = &hv->hv_hls->h_cancellable;

  int flags = FA_STREAMING;

 retry:

  hs->hs_opened_at = showtime_get_ts();
  hs->hs_block_cnt = hv->hv_hls->h_blocked;

  fh = fa_open_ex(hs->hs_url, errbuf, sizeof(errbuf), flags, &foe);

  if(fh == NULL) {
    if(foe.foe_protocol_error == 404) {
      if(retry_counter < 3) {
        HLS_TRACE(h, "Segment %s not found, retrying", hs->hs_url);
        usleep(500000);
        retry_counter++;
        goto retry;
      }
      TRACE(TRACE_ERROR, "HLS", "Segment %s not found", hs->hs_url);
      hv->hv_recent_segment_status = HLS_ERROR_SEGMENT_NOT_FOUND;

    } else {
      TRACE(TRACE_ERROR, "HLS", "Segment %s -- HTTP error %d",
            hs->hs_url, foe.foe_protocol_error);
      hv->hv_recent_segment_status = HLS_ERROR_SEGMENT_BROKEN;
    }
    hs->hs_unavailable = 1;
    return -1;
  }

  if(hs->hs_byte_size != -1 && hs->hs_byte_offset != -1)
    fh = fa_slice_open(fh, hs->hs_byte_offset, hs->hs_byte_size);


  hs->hs_size = fa_fsize(fh);

  switch(hs->hs_crypto) {
  case HLS_CRYPTO_AES128:

    if(!rstr_eq(hs->hs_key_url, hv->hv_key_url)) {
      buf_release(hv->hv_key);
      hv->hv_key = fa_load(rstr_get(hs->hs_key_url),
                            FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                            NULL);
      if(hv->hv_key == NULL) {
	TRACE(TRACE_ERROR, "HLS", "Unable to load key file %s",
	      rstr_get(hs->hs_key_url));
	fa_close(fh);
        hv->hv_recent_segment_status = HLS_ERROR_SEGMENT_BAD_KEY;
        hs->hs_unavailable = 1;
        return -1;
      }

      rstr_set(&hv->hv_key_url, hs->hs_key_url);
    }

    fh = fa_aescbc_open(fh, hs->hs_iv, buf_c8(hv->hv_key));
  }
  hv->hv_current_file = fh;
  HLS_TRACE(hv->hv_hls, "Opened %s OK", hs->hs_url);
  hv->hv_recent_segment_status  = HLS_ERROR_OK;
  return 0;
}




/**
 *
 */
static void
demuxer_update_bw(hls_variant_t *hv)
{
  hls_segment_t *hs = hv->hv_current_seg;
  hls_t *h = hv->hv_hls;
  hls_demuxer_t *hd = &h->h_primary;

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

  HLS_TRACE(h, "Estimated bandwidth: %d bps (filtered: %d bps)", bw, hd->hd_bw);

  prop_set(h->h_mp->mp_prop_io, "bitrate", PROP_SET_INT, hd->hd_bw / 1000);
  prop_set(h->h_mp->mp_prop_io, "bitrateValid", PROP_SET_INT, 1);
}


/**
 *
 */
static void
variant_close_file(hls_variant_t *hv)
{
  fa_close(hv->hv_current_file);
  hv->hv_current_file = NULL;

  demuxer_update_bw(hv);
}




/**
 * Select a low bitrate variant that we hope works ok
 */
static hls_variant_t *
hls_select_default_variant(const hls_t *h, hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  hls_variant_t *best = NULL;
  TAILQ_FOREACH_REVERSE(hv, &hd->hd_variants, hls_variant_queue, hv_link) {
    if(hv->hv_audio_only)
      continue;

    if(best == NULL || best->hv_corrupt_counter > hv->hv_corrupt_counter)
      best = hv;
  }
  return best;
}



/**
 *
 */
static hls_variant_t *
demuxer_select_variant_simple(hls_t *h, hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  hls_variant_t *best = NULL;

  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_audio_only)
      continue;

    if(hv->hv_bitrate >= hd->hd_bw)
      continue;

    if(best == NULL || best->hv_corrupt_counter > hv->hv_corrupt_counter)
      best = hv;
  }
  return best;
}


/**
 *
 */
static hls_variant_t *
demuxer_select_variant_random(hls_t *h, hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  int cnt = 0;

  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_corrupt_counter >= HV_CORRUPT_LIMIT)
      continue;

    if(!hv->hv_audio_only)
      cnt++;

  }

  int r = rand() % cnt;
  cnt = 0;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_corrupt_counter >= HV_CORRUPT_LIMIT)
      continue;

    if(hv->hv_audio_only)
      continue;
    if(r == cnt)
      break;
    cnt++;
  }

  HLS_TRACE(h, "Randomly selected bitrate %d", hv ? hv->hv_bitrate : 0);
  return hv;
}

/**
 *
 */
static hls_variant_t *
demuxer_select_variant(hls_t *h, hls_demuxer_t *hd)
{
  if(0)
    return demuxer_select_variant_random(h, hd);

  return demuxer_select_variant_simple(h, hd);
}


/**
 *
 */
static int
check_bw_switch(hls_t *h, time_t now)
{
  hls_demuxer_t *hd = &h->h_primary;

  if(hd->hd_bw == 0)
    return 0;

  if(hd->hd_last_switch + 3 > now)
    return 0;

  hls_variant_t *hv = demuxer_select_variant(h, hd);

  if(hv == NULL || hv == hd->hd_current)
    return 0;

  hd->hd_last_switch = now;

  hd->hd_req = hv;
  return -1;
}


/**
 *
 */
static int
get_current_video_seq(media_pipe_t *mp)
{
  int seq = 0;
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = TAILQ_FIRST(&mp->mp_video.mq_q_data);
  if(mb != NULL)
    seq = mb->mb_sequence;
  hts_mutex_unlock(&mp->mp_mutex);
  return seq;
}

/**
 *
 */
static int
hls_libav_read(void *opaque, uint8_t *buf, int size)
{
  hls_variant_t *hv = opaque;
  hls_t *h = hv->hv_hls;
  const int osize = size;

  time_t now;

  while(size > 0) {

    time(&now);

    if(!hv->hv_opening_file) {
      if(check_bw_switch(hv->hv_hls, now))
        return -1;
    }

    while(hv->hv_current_file == NULL) {
      hls_segment_t *hs;

      // Need to open new file

      if(!hv->hv_opening_file) {
        if(check_bw_switch(hv->hv_hls, now))
          return -1;
      }

      variant_update(hv, now);


      if(hv->hv_current_seg == NULL) {

        hs = NULL;

        if(h->h_seek_to != PTS_UNSET) {
          hs = hv_find_segment_by_time(hv, h->h_seek_to);
          HLS_TRACE(h, "Seek to %"PRId64" -- %s", h->h_seek_to,
                    hs ? "Segment found" : "Segment not found");
          h->h_seek_to = PTS_UNSET;
        }

        if(hs == NULL) {
          int seq = get_current_video_seq(h->h_mp);

          if(seq == 0 && !hv->hv_frozen) {
            seq = MAX(hv->hv_last_seq - 3, hv->hv_first_seq);
            HLS_TRACE(h, "Live stream selecting initial segment %d", seq);
          }

          if(seq) {
            hs = hv_find_segment_by_seq(hv, seq);
          }
        }
        if(hs == NULL)
          hs = TAILQ_FIRST(&hv->hv_segments);

      } else {
        hs = TAILQ_NEXT(hv->hv_current_seg, hs_link);
      }

      if(hs == NULL) {

        if(hv->hv_frozen) {
          return 0; // EOF
        }

        hts_mutex_lock(&h->h_mutex);
        while(h->h_exit_event == NULL || h->h_seek_to != PTS_UNSET)
          if(hts_cond_wait_timeout(&h->h_cond, &h->h_mutex, 1000))
            break;

        if(h->h_exit_event != NULL || h->h_seek_to != PTS_UNSET) {
          hts_mutex_unlock(&h->h_mutex);
          return -1;
        }

        hts_mutex_unlock(&h->h_mutex);
        time(&now);
        continue;
      }

      hv->hv_current_seg = hs;
      HLS_TRACE(h, "Opening variant %d sequence %d",
                hv->hv_bitrate, hs->hs_seq);

      int err = variant_open_file(hv, h);
      if(!err) {
        // Open succeeded, continue with streaming1
        h->h_current_seq = hv->hv_current_seg->hs_seq;
        break;
      }

      assert(hv->hv_current_file == NULL);

      if(h->h_exit_event != NULL || h->h_seek_to != PTS_UNSET)
        return -1;

      const hls_segment_t *prev = hv->hv_current_seg;

      hv->hv_current_seg = NULL;

      if(prev->hs_unavailable) {
        hv->hv_corrupt_counter++;
        sleep(1);
        HLS_TRACE(h, "Bad segment %d in bitrate %d trying different variant",
                  prev->hs_seq, hv->hv_bitrate);
        hls_demuxer_t *hd = &h->h_primary;

        hd->hd_req = demuxer_select_variant(h, hd);
        hd->hd_last_switch = now;
      }
      return -1;
    }

    assert(hv->hv_current_file != NULL);

    const int r = fa_read(hv->hv_current_file, buf, size);
    if(r <= 0) {
      if(r < 0)
        hv->hv_recent_segment_status = HLS_ERROR_SEGMENT_READ_ERROR;

      variant_close_file(hv);

      if(hv->hv_hls->h_exit_event != NULL || h->h_seek_to != PTS_UNSET)
        return -1;

    } else {
      size -= r;
      buf += r;
    }
  }
  return osize - size;
}


/**
 *
 */
static hls_error_t
variant_open(hls_t *h, hls_demuxer_t *hd, hls_variant_t *hv)
{
  int err;
  int buf_size = 32768;
  void *buf = av_malloc(buf_size);

  mp_bump_epoch(h->h_mp);

  cancellable_reset(&h->h_cancellable);

  hv->hv_current_seg = NULL;

  h->h_mp->mp_video.mq_demuxer_flags &= ~HLS_QUEUE_KEYFRAME_SEEN;
  h->h_mp->mp_audio.mq_demuxer_flags &= ~HLS_QUEUE_KEYFRAME_SEEN;

  AVIOContext *avio = avio_alloc_context(buf, buf_size, 0, hv, hls_libav_read,
                                         NULL, NULL);

  hv->hv_fctx = avformat_alloc_context();
  hv->hv_fctx->pb = avio;

  hv->hv_opening_file = 1;

  if((err = avformat_open_input(&hv->hv_fctx, hv->hv_url,
				h->h_fmt, NULL)) != 0) {

    hls_error_t rval = 0;

    if(h->h_seek_to == PTS_UNSET && h->h_exit_event == NULL) {

      TRACE(TRACE_ERROR, "HLS",
            "Unable to open stream %d base %s file-error:%d format-error:%d",
            hv->hv_bitrate, hv->hv_url, hv->hv_recent_segment_status, err);


      switch(hv->hv_recent_segment_status) {
      case HLS_ERROR_SEGMENT_NOT_FOUND:
      case HLS_ERROR_SEGMENT_BROKEN:
      case HLS_ERROR_SEGMENT_BAD_KEY:
      case HLS_ERROR_SEGMENT_READ_ERROR:
        rval = hv->hv_recent_segment_status;
        break;

      case HLS_ERROR_OK:
        // If the file streaming layer didn't report any errors it must
        // be some kind of problem with the file format itself
        rval = HLS_ERROR_VARIANT_BAD_FORMAT;
        break;

      default:
        abort();
      }
    }

    av_free(avio->buffer);
    av_free(avio);
    hv->hv_fctx = NULL;
    return rval;
  }

  hv->hv_opening_file = 0;

  hv->hv_fctx->flags |= AVFMT_FLAG_NOFILLIN;
  hv->hv_vstream = -1;
  hv->hv_astream = -1;

  for(int j = 0; j < hv->hv_fctx->nb_streams; j++) {
    char str[256];
    AVCodecContext *ctx = hv->hv_fctx->streams[j]->codec;

    avcodec_string(str, sizeof(str), ctx, 0);
    HLS_TRACE(h, "Stream #%d: %s", j, str);

    switch(ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      if(hv->hv_vstream == -1 && ctx->codec_id == AV_CODEC_ID_H264)
	hv->hv_vstream = j;
      break;

    case AVMEDIA_TYPE_AUDIO:
      if(hv->hv_astream != -1)
        break;

      hv->hv_astream = j;

      if(hd->hd_audio_codec != NULL &&
         hd->hd_audio_codec->codec_id == ctx->codec_id)
        break;


      if(hd->hd_audio_codec != NULL)
        media_codec_deref(hd->hd_audio_codec);

      hd->hd_audio_codec = media_codec_create(ctx->codec_id,
                                              0, NULL, NULL, NULL, h->h_mp);
      break;

    default:
      break;
    }
  }

  media_pipe_t *mp = h->h_mp;

  prop_t *fmt = prop_create_r(h->h_mp->mp_prop_metadata, "format");

  if(hv->hv_bitrate) {
    char info[64];
    snprintf(info, sizeof(info), "HLS %d kb/s", hv->hv_bitrate / 1000);
    mp_send_prop_set_string(h->h_mp, &h->h_mp->mp_audio, fmt, info);
  } else {
    mp_send_prop_set_string(h->h_mp, &h->h_mp->mp_audio, fmt, "HLS");
  }

  prop_ref_dec(fmt);

  mp_set_duration(mp, hv->hv_frozen ? hv->hv_duration :  AV_NOPTS_VALUE);
  if(hv->hv_frozen) {
    mp_set_clr_flags(mp, MP_CAN_SEEK, 0);
  } else {
    mp_set_clr_flags(mp, 0, MP_CAN_SEEK);
  }
  return 0;
}


/**
 *
 */
static void
variant_close(hls_variant_t *hv)
{
  if(hv->hv_current_file != NULL) {
    fa_close(hv->hv_current_file);
    hv->hv_current_file = NULL;
  }

  free(hv->hv_video_headers);
  hv->hv_video_headers = NULL;
  hv->hv_video_headers_size = 0;

  if(hv->hv_fctx == NULL)
    return;

  AVIOContext *avio = hv->hv_fctx->pb;
  avformat_close_input(&hv->hv_fctx);
  av_free(avio->buffer);
  av_free(avio);
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
__attribute__((unused))  static hls_segment_t *
find_segment_by_pts(hls_variant_t *hv, AVFormatContext *fctx,
                    int64_t base, int64_t last, int si)
{
  hls_segment_t *hs;

  if(base == AV_NOPTS_VALUE || last == AV_NOPTS_VALUE)
    return NULL;

  base = rescale(fctx, base, si);
  last = rescale(fctx, last, si);

  TAILQ_FOREACH(hs, &hv->hv_segments, hs_link) {

    if(base > last)
      return hs;

    base += hs->hs_duration;
  }
  return NULL;
}



/**
 *
 */
static int
hls_event_callback(media_pipe_t *mp, void *aux, event_t *e)
{
  hls_t *h = aux;

  if(event_is_type(e, EVENT_CURRENT_TIME)) {

    event_ts_t *ets = (event_ts_t *)e;

    if(ets->epoch == mp->mp_epoch) {
      int sec = ets->ts / 1000000;
      h->h_last_timestamp_presented = ets->ts;

      // Update restartpos every 5 seconds
      if(mp->mp_flags & MP_CAN_SEEK &&
         (sec < h->h_restartpos_last || sec >= h->h_restartpos_last + 5)) {
        h->h_restartpos_last = sec;

        //	  playinfo_set_restartpos(canonical_url, ets->ts / 1000, 1);
      }
    }

  } else if(event_is_type(e, EVENT_PLAYBACK_PRIORITY)) {
    event_int_t *ei = (event_int_t *)e;
    h->h_playback_priority = ei->val;

  } else if(event_is_type(e, EVENT_SEEK) && mp->mp_flags & MP_CAN_SEEK) {

    event_ts_t *ets = (event_ts_t *)e;

    cancellable_cancel(&h->h_cancellable);

    hts_mutex_lock(&h->h_mutex);
    h->h_seek_to = ets->ts;
    hts_cond_signal(&h->h_cond);
    hts_mutex_unlock(&h->h_mutex);


  } else if(event_is_action(e, ACTION_SKIP_FORWARD) ||
            event_is_action(e, ACTION_SKIP_BACKWARD) ||
            event_is_type(e, EVENT_EXIT) ||
            event_is_type(e, EVENT_PLAY_URL)) {

    cancellable_cancel(&h->h_cancellable);

    hts_mutex_lock(&h->h_mutex);

    if(h->h_exit_event == NULL) {
      hts_cond_signal(&h->h_cond);
      h->h_exit_event = e;
      event_addref(e);
    }

    hts_mutex_unlock(&h->h_mutex);
  }


  return 1;
}


/**
 *
 */
static void
extract_ps_nal(hls_variant_t *hv, const uint8_t *data, int len)
{
  if(len == 0)
    return;

  const int nal_unit_type = data[0] & 0x1f;

  if(nal_unit_type == 7 || nal_unit_type == 8) {

    int newsize = len + 3 + hv->hv_video_headers_size;
    hv->hv_video_headers = realloc(hv->hv_video_headers, newsize);

    memcpy(hv->hv_video_headers + hv->hv_video_headers_size,
           data - 3, len + 3);
    hv->hv_video_headers_size = newsize;
  }
}


/**
 *
 */
static void
extract_ps(hls_variant_t *hv, media_buf_t *mb)
{
  const uint8_t *d = mb->mb_data;
  int len = mb->mb_size;

  const uint8_t *p = NULL;

  while(len > 3) {
    if(!(d[0] == 0 && d[1] == 0 && d[2] == 1)) {
      d++;
      len--;
      continue;
    }

    if(p != NULL)
      extract_ps_nal(hv, p, d - p);

    d += 3;
    len -= 3;
    p = d;
  }
  d += len;

  if(p != NULL)
    extract_ps_nal(hv, p, d - p);
}


// #define DUMP_VIDEO

#ifdef DUMP_VIDEO

#include <fcntl.h>
/**
 *
 */
static void
dump_video(media_buf_t *mb)
{
  static int fd = -2;
  if(fd == -1)
    return;

  if(fd == -2)
    fd = open("/tmp/videodump.h264", O_TRUNC | O_WRONLY | O_CREAT, 0666);

  if(fd == -1)
    return;

  if(write(fd, mb->mb_data, mb->mb_size) != mb->mb_size) {
    close(fd);
    fd = -1;
  }
}
#endif



/**
 *
 */
static void
enqueue_buffer(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb,
               hls_t *h, hls_variant_t *hv)
{
  const int is_video = mb->mb_data_type == MB_VIDEO;

  hts_mutex_lock(&mp->mp_mutex);

  const int vminpkt = mp->mp_video.mq_stream != -1 ? 5 : 0;
  const int aminpkt = mp->mp_audio.mq_stream != -1 ? 5 : 0;

  mp_update_buffer_delay(mp);

  while(1) {

    // Check if we are inside the realtime delay bounds
    if(mp->mp_buffer_delay < mp->mp_max_realtime_delay) {

      // Check if buffer is full
      if(mp->mp_buffer_current + mb->mb_size < mp->mp_buffer_limit)
	break;
    }

    // These two safeguards so we don't run out of packets in any
    // of the queues

    if(mp->mp_video.mq_packets_current < vminpkt)
      break;

    if(mp->mp_audio.mq_packets_current < aminpkt)
      break;

    h->h_blocked++;
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
  }

  int flush = 0;

  if(mq->mq_demuxer_flags & HLS_QUEUE_MERGE) {
    media_buf_t *b;

    TAILQ_FOREACH(b, &mq->mq_q_data, mb_link)
      if(b->mb_dts != PTS_UNSET)
        break;

    if(b != NULL && mb->mb_dts < b->mb_dts) {
      /*
       * This frame has already been dequeued (DTS lower)
       * Drop packet and restart keyframe search
       */

      if(is_video)
        extract_ps(hv, mb);

      media_buf_free_locked(mp, mb);
      mq->mq_demuxer_flags &= ~HLS_QUEUE_KEYFRAME_SEEN;
      hts_mutex_unlock(&mp->mp_mutex);
      return;
    }

    TAILQ_FOREACH(b, &mq->mq_q_data, mb_link)
      if(b->mb_dts != PTS_UNSET && b->mb_dts >= mb->mb_dts)
        break;

    while(b != NULL) {
      media_buf_t *next = TAILQ_NEXT(b, mb_link);
      if(b->mb_data_type == MB_AUDIO || b->mb_data_type == MB_VIDEO) {
        TAILQ_REMOVE(&mq->mq_q_data, b, mb_link);
        mq->mq_packets_current--;
        mp->mp_buffer_current -= b->mb_size;
        media_buf_free_locked(mp, b);
      }
      b = next;
    }
    mq->mq_demuxer_flags &= ~HLS_QUEUE_MERGE;
    flush = 1;
  }

  if(is_video && hv->hv_video_headers_size) {

    media_buf_t *mbx =
      media_buf_alloc_locked(mp, hv->hv_video_headers_size + mb->mb_size);

    memcpy(mbx->mb_data, hv->hv_video_headers, hv->hv_video_headers_size);
    memcpy(mbx->mb_data + hv->hv_video_headers_size, mb->mb_data, mb->mb_size);

    mbx->mb_data_type = MB_VIDEO;
    mbx->mb_cw = media_codec_ref(mb->mb_cw);

#ifdef DUMP_VIDEO
    dump_video(mbx);
#endif
    mbx->mb_flush = flush;
    mb_enq(mp, mq, mbx);

    flush = 0;
    free(hv->hv_video_headers);
    hv->hv_video_headers = NULL;
    hv->hv_video_headers_size = 0;

    media_buf_free_locked(mp, mb);

    hts_mutex_unlock(&mp->mp_mutex);
    return;
  }

#ifdef DUMP_VIDEO
  if(is_video)
    dump_video(mb);
#endif

  mb->mb_flush = flush;
  mb_enq(mp, mq, mb);

  hts_mutex_unlock(&mp->mp_mutex);
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
  sub_scanner_t *ss = NULL;
  int loading = 1;

  h->h_restartpos_last = -1;
  h->h_last_timestamp_presented = AV_NOPTS_VALUE;
  h->h_sub_scanning_done = 0;
  h->h_enqueued_something = 0;

  h->h_playback_priority = va->priority;
  h->h_seek_to = PTS_UNSET;

  mp->mp_video.mq_stream = 0;
  mp->mp_audio.mq_stream = 1;

  if(!(va->flags & BACKEND_VIDEO_NO_AUDIO))
    mp_become_primary(mp);

  mp_configure(mp, MP_CAN_PAUSE, MP_BUFFER_DEEP, 0, "video");

  //  mp->mp_pre_buffer_delay = 10000000;

  hls_demuxer_t *hd = &h->h_primary;

  mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_seek_base = 0;

  mp_event_set_callback(mp, hls_event_callback, h);

  if(va->flags & BACKEND_VIDEO_RESUME) {
    int64_t start = playinfo_get_restartpos(canonical_url) * 1000;
    if(start) {
      TRACE(TRACE_DEBUG, "HLS", "Attempting to resume from %.2f seconds",
            start / 1000000.0f);
      mp->mp_seek_base = start;
      h->h_seek_to = start;
    }
  }

  hd->hd_current = hls_select_default_variant(h, hd);

  while(1) {

    hts_mutex_lock(&h->h_mutex);
    e = h->h_exit_event;
    if(e != NULL)
      h->h_exit_event = NULL;
    hts_mutex_unlock(&h->h_mutex);

    if(e)
      break;

    if(mb == NULL) {
      hls_variant_t *hv = hd->hd_current;
      AVPacket pkt;
      int r = 0;

      mp->mp_eof = 0;

      if(!h->h_sub_scanning_done && hv->hv_frozen) {
	h->h_sub_scanning_done = 1;
	ss = sub_scanner_create(h->h_baseurl, mp->mp_prop_subtitle_tracks, va,
				hv->hv_duration / 1000000LL);
      }

      if(hv->hv_fctx == NULL) {
        r = variant_open(h, hd, hv);

        if(r == HLS_ERROR_VARIANT_BAD_FORMAT) {
          hv->hv_corrupt_counter++;
          HLS_TRACE(h, "Unable to demux variant %d, trying something else",
                    hv->hv_bitrate);
          variant_close(hd->hd_current);
          hd->hd_current = hls_select_default_variant(h, hd);
          continue;
        }

        if(h->h_seek_to != PTS_UNSET || h->h_exit_event != NULL)
          continue;

      }

      if(!r) {
        r = av_read_frame(hv->hv_fctx, &pkt);
        if(r == AVERROR(EAGAIN))
          continue;
      }

      if(hd->hd_req) {

        if(!r)
          av_free_packet(&pkt);

        if(hd->hd_current != NULL)
          variant_close(hd->hd_current);

        hd->hd_current = hd->hd_req;
        hd->hd_req = NULL;
        mp->mp_video.mq_demuxer_flags |= HLS_QUEUE_MERGE;
        mp->mp_audio.mq_demuxer_flags |= HLS_QUEUE_MERGE;
        continue;
      }

      if(h->h_seek_to != PTS_UNSET) {

        if(!r)
          av_free_packet(&pkt);

        if(hd->hd_current != NULL)
          variant_close(hd->hd_current);
        mp_flush(mp, 0);
        continue;
      }

      if(r) {
        char buf[512];
        assert(hd->hd_current != NULL);
        variant_close(hd->hd_current);
        hd->hd_current = NULL;

        if(av_strerror(r, buf, sizeof(buf)))
          snprintf(buf, sizeof(buf), "Error %d", r);
        TRACE(TRACE_DEBUG, "Video", "Playback reached EOF: %s (%d)", buf, r);
        mb = MB_EOF;
        mp->mp_eof = 1;
        continue;
      }


      int si = pkt.stream_index;
      const AVStream *s = hv->hv_fctx->streams[si];
      hls_segment_t *hs = hv->hv_current_seg;
      assert(hs != NULL);

      if(hs->hs_first_ts == PTS_UNSET && pkt.dts != PTS_UNSET) {
        hs->hs_first_ts = rescale(hv->hv_fctx, pkt.dts, si);
        hs->hs_ts_offset = hs->hs_first_ts - hs->hs_time_offset;
      }

      if(si == hv->hv_vstream) {

        mq = &mp->mp_video;

        if(!(mq->mq_demuxer_flags & HLS_QUEUE_KEYFRAME_SEEN)) {
          if(!pkt.flags & AV_PKT_FLAG_KEY) {
            av_free_packet(&pkt);
            continue;
          }

          if(pkt.dts == AV_NOPTS_VALUE) {
            av_free_packet(&pkt);
            continue;
          }

          mq->mq_demuxer_flags |= HLS_QUEUE_KEYFRAME_SEEN;
        }

        mb = media_buf_from_avpkt_unlocked(mp, &pkt);
        mb->mb_data_type = MB_VIDEO;
        if(s->avg_frame_rate.num) {
          mb->mb_duration = 1000000LL * s->avg_frame_rate.den /
            s->avg_frame_rate.num;
        } else {
          mb->mb_duration = rescale(hv->hv_fctx, pkt.duration, si);
        }
        mb->mb_cw = media_codec_ref(h->h_codec_h264);
        mb->mb_stream = 0;

      } else if(si == hv->hv_astream) {

        mq = &mp->mp_audio;

        if(!(mq->mq_demuxer_flags & HLS_QUEUE_KEYFRAME_SEEN)) {
          if(!pkt.flags & AV_PKT_FLAG_KEY || pkt.dts == AV_NOPTS_VALUE) {
            av_free_packet(&pkt);
            continue;
          }


          // If we are not in splice merge mode, drop all audio frames
          // until we've got a video lock
          if(!(mp->mp_video.mq_demuxer_flags & HLS_QUEUE_KEYFRAME_SEEN) &&
             (!(mq->mq_demuxer_flags & HLS_QUEUE_MERGE))) {
            av_free_packet(&pkt);
            continue;
          }

          mq->mq_demuxer_flags |= HLS_QUEUE_KEYFRAME_SEEN;
        }

        mb = media_buf_from_avpkt_unlocked(mp, &pkt);
        mb->mb_data_type = MB_AUDIO;

        mb->mb_cw = media_codec_ref(hd->hd_audio_codec);
        mb->mb_stream = 1;

      } else {
        /* Check event queue ? */
        av_free_packet(&pkt);
        continue;
      }


#if 0
      printf("%s: PTS=%-16ld DTS=%-16ld %-16ld ep:%d\n",
             mb->mb_data_type == MB_VIDEO ? "VIDEO" : "AUDIO",
             pkt.pts, pkt.dts, pkt.pts - pkt.dts, mp->mp_epoch);
#endif

      /**
       * Some HLS live servers (FlashCom/3.5.5) can send broken PTS
       * timestamps.  Try to detect those and filter them out. Code
       * downstream will repair them.
       */

      if(pkt.pts != AV_NOPTS_VALUE && pkt.dts != AV_NOPTS_VALUE &&
         (pkt.pts - pkt.dts > 900000 || pkt.pts < pkt.dts))
        pkt.pts = AV_NOPTS_VALUE;

      mb->mb_pts = rescale(hv->hv_fctx, pkt.pts, si);
      mb->mb_dts = rescale(hv->hv_fctx, pkt.dts, si);

      if(mb->mb_data_type == MB_VIDEO) {

	mb->mb_drive_clock = 1;
	mb->mb_delta = hs->hs_ts_offset;
      }

      mb->mb_sequence = h->h_current_seq;
      
      mb->mb_keyframe = !!(pkt.flags & AV_PKT_FLAG_KEY);
      av_free_packet(&pkt);

    }

    if(mb == MB_EOF) {

      /* Wait for queues to drain */
      e = mp_wait_for_empty_queues(mp);
      if(e == NULL) {
	e = event_create_type(EVENT_EOF);
	break;
      }
    } else {
      enqueue_buffer(mp, mq, mb, h, hd->hd_current);
      mb = NULL;
      if(loading) {
        prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 0);
        loading = 0;
      }
    }
  }

  if(mb != NULL && mb != MB_EOF)
    media_buf_free_unlocked(mp, mb);

  if(mp->mp_flags & MP_CAN_SEEK) {

    // Compute stop position (in percentage of video length)

    int spp = mp->mp_duration ? mp->mp_seek_base * 100 / mp->mp_duration : 0;

    if(spp >= video_settings.played_threshold || event_is_type(e, EVENT_EOF)) {
      playinfo_set_restartpos(canonical_url, -1, 0);
      playinfo_register_play(canonical_url, 1);
      TRACE(TRACE_DEBUG, "Video",
	    "Playback reached %d%%, counting as played (%s)",
	    spp, canonical_url);
    } else if(h->h_last_timestamp_presented != PTS_UNSET) {
      playinfo_set_restartpos(canonical_url,
                              h->h_last_timestamp_presented / 1000,
			      0);
    }
  }
  // Shutdown

  mp_event_set_callback(mp, NULL, NULL);

  mp_flush(mp, 0);
  mp_shutdown(mp);

  sub_scanner_destroy(ss);

  if(hd->hd_current != NULL)
    variant_close(hd->hd_current);

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
    *hvp = variant_create(h);

  int bitrate = (*hvp)->hv_bitrate;

  if(bitrate) {
    hls_variant_t *hv;

    TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
      if(hv->hv_bitrate == bitrate) {
        HLS_TRACE(h, "Skipping duplicate bitrate %d via %s",
                  bitrate, url);
        variant_destroy(*hvp);
        *hvp = NULL;
        return;
      }
    }
  }

  hls_variant_t *hv = *hvp;

  hv->hv_url = url_resolve_relative_from_base(h->h_baseurl, url);
  TAILQ_INSERT_SORTED(&hd->hd_variants, hv, hv_link, variant_cmp,
                      hls_variant_t);
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

  hls_variant_t *hv = *hvp = variant_create(h);

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
}


/**
 *
 */
static void
hls_demuxer_close(hls_demuxer_t *hd)
{
  variants_destroy(&hd->hd_variants);
  if(hd->hd_audio_codec != NULL)
    media_codec_deref(hd->hd_audio_codec);
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

  usage_inc_counter("playvideohls", 1);

  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);

  hls_t h;
  memset(&h, 0, sizeof(h));
  hts_mutex_init(&h.h_mutex);
  hts_cond_init(&h.h_cond, &h.h_mutex);
  hls_demuxer_init(&h.h_primary);
  h.h_mp = mp;
  h.h_baseurl = url;
  h.h_fmt = av_find_input_format("mpegts");
  h.h_codec_h264 = media_codec_create(AV_CODEC_ID_H264, 0, NULL, NULL, NULL, mp);
  h.h_debug = gconf.enable_hls_debug;

  hls_variant_t *hv = NULL;

  if(strstr(buf, "#EXT-X-STREAM-INF:")) {

    LINEPARSE(s, buf) {
      const char *v;
      if((v = mystrbegins(s, "#EXT-X-MEDIA:")) != NULL)
        hls_ext_x_media(&h, v);
      else if((v = mystrbegins(s, "#EXT-X-STREAM-INF:")) != NULL)
        hls_ext_x_stream_inf(&h, v, &hv);
      else if(s[0] != '#')
        hls_add_variant(&h, s, &hv, &h.h_primary);
    }
  } else {
    hls_add_variant(&h, h.h_baseurl, &hv, &h.h_primary);
  }

  free(hv);

  check_audio_only(&h);

  hls_dump(&h);

  event_t *e = hls_play(&h, mp, errbuf, errlen, va0);

  hls_demuxer_close(&h.h_primary);

  media_codec_deref(h.h_codec_h264);

  hts_cond_destroy(&h.h_cond);
  hts_mutex_destroy(&h.h_mutex);

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

  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);

  url += strlen("hls:");
  if(!strcmp(url, "test"))
    url = TESTURL;
  if(!strcmp(url, "test2"))
    url = TESTURL2;
  buf = fa_load(url,
                 FA_LOAD_ERRBUF(errbuf, errlen),
                 FA_LOAD_FLAGS(FA_COMPRESSION),
                 NULL);

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
