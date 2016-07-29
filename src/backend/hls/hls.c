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
#include <string.h>
#include <unistd.h>

#include "navigator.h"
#include "backend/backend.h"
#include "media/media.h"
#include "main.h"
#include "i18n.h"
#include "misc/isolang.h"
#include "misc/str.h"
#include "misc/dbl.h"
#include "misc/queue.h"
#include "video/video_playback.h"
#include "video/video_settings.h"
#include "metadata/playinfo.h"
#include "fileaccess/fileaccess.h"
#include "hls.h"
#include "subtitles/subtitles.h"
#include "usage.h"
#include "misc/minmax.h"

#define HLS_CORRUPTION_MEASURE_PERIOD (60 * 1000000)

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


static hls_variant_t *hls_demuxer_select_variant(hls_demuxer_t *hd, int64_t now,
                                                 int bw);


/**
 *
 */
static void
hls_free_mbp(media_pipe_t *mp, media_buf_t **mbp)
{
  media_buf_t *mb = *mbp;
  if(*mbp == NULL)
    return;

  if(mb != HLS_EOF && mb != HLS_DIS)
    media_buf_free_unlocked(mp, mb);
  *mbp = NULL;
}


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
static hls_discontinuity_segment_t *
discontinuity_seq_get(hls_t *h, int discontinuity_seq)
{
  hls_discontinuity_segment_t *hds;
  LIST_FOREACH(hds, &h->h_discontinuity_segments, hds_link) {
    if(hds->hds_seq == discontinuity_seq) {
      hds->hds_refcount++;
      return hds;
    }
  }

  hds = calloc(1, sizeof(hls_discontinuity_segment_t));
  LIST_INSERT_HEAD(&h->h_discontinuity_segments, hds, hds_link);
  if(discontinuity_seq != 0)
    hds->hds_offset = PTS_UNSET;
  hds->hds_refcount = 1;
  hds->hds_seq = discontinuity_seq;
  return hds;
}


/**
 *
 */
static void
discontinuity_seq_release(hls_discontinuity_segment_t *hds)
{
  if(--hds->hds_refcount)
    return;
  LIST_REMOVE(hds, hds_link);
  free(hds);
}


/**
 *
 */
static void
segment_destroy(hls_segment_t *hs)
{
  discontinuity_seq_release(hs->hs_discontinuity_segment);

  if(hs->hs_fh != NULL)
    fa_close(hs->hs_fh);

  TAILQ_REMOVE(&hs->hs_variant->hv_segments, hs, hs_link);
  free(hs->hs_url);
  rstr_release(hs->hs_key_url);
  if(hs == hs->hs_variant->hv_segment_search)
    hs->hs_variant->hv_segment_search = NULL;
  free(hs);
}


/**
 *
 */
static void
variant_destroy(hls_variant_t *hv)
{
  hls_segment_t *hs;
  hls_variant_close(hv);

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
variant_create(hls_demuxer_t *hd)
{
  hls_variant_t *hv = calloc(1, sizeof(hls_variant_t));
  hv->hv_start_time_offset = PTS_UNSET;
  hv->hv_demuxer = hd;
  hv->hv_first_seq = -1;
  hv->hv_last_seq = -1;
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
  hs->hs_ts_offset = PTS_UNSET;
  hs->hs_url = url_resolve_relative_from_base(hv->hv_url, url);
  hs->hs_variant = hv;
  TAILQ_INSERT_TAIL(&hv->hv_segments, hs, hs_link);
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
static void
hv_parse_start(hls_variant_t *hv, const char *V)
{
  char *v = mystrdupa(V);
  while(*v) {
    const char *key, *value;
    v = get_attrib(v, &key, &value);
    if(v == NULL)
      break;

    if(!strcmp(key, "TIME-OFFSET")) {
      hv->hv_start_time_offset = MAX(my_str2double(value, NULL), 0.0f) * 1000000;
    }
  }
}

/**
 *
 */
static const char *hlserrstr[] = {
  [HLS_ERROR_SEGMENT_NOT_FOUND]     = "Segment not found",
  [HLS_ERROR_SEGMENT_BROKEN]        = "Unable to open segment",
  [HLS_ERROR_SEGMENT_ACCESS_DENIED] = "Access denied",
  [HLS_ERROR_SEGMENT_BAD_KEY]       = "Unable to get encryption key",
  [HLS_ERROR_VARIANT_PROBE_ERROR]   = "Probing error",
  [HLS_ERROR_VARIANT_NO_VIDEO]      = "No video",
  [HLS_ERROR_VARIANT_UNKNOWN_AUDIO] = "Unknown audio codec",
  [HLS_ERROR_VARIANT_EMPTY]         = "Variant is empty",
  [HLS_ERROR_VARIANT_NOT_FOUND]     = "Variant not found",
};


/**
 *
 */
void
hls_bad_variant(hls_variant_t *hv, hls_error_t err)
{
  hls_demuxer_t *hd = hv->hv_demuxer;
  hls_t *h = hd->hd_hls;
  int64_t now = arch_get_ts();

  h->h_last_error = err;

  HLS_TRACE(h, "Unable to demux variant %s -- %s",
            hv->hv_name, hlserrstr[err]);
  hv->hv_corrupt_counter++;
  if(hv->hv_corrupt_timer < now - HLS_CORRUPTION_MEASURE_PERIOD) {
    hv->hv_corruptions_last_period = 1;
    hv->hv_corrupt_timer = now;
  } else {
    hv->hv_corruptions_last_period++;
  }

  hls_variant_close(hv);
  usleep(100000);
  hd->hd_req = hls_demuxer_select_variant(hd, now, 0);
  hd->hd_last_switch = now;
}


/**
 *
 */
static hls_error_t attribute_unused_result
hls_variant_update(hls_variant_t *hv, media_pipe_t *mp)
{
  hls_segment_t *hs;
  char errbuf[1024];
  int changed = 0;

  if(hv->hv_frozen)
    return 0;

  hls_t *h = hv->hv_demuxer->hd_hls;

  buf_t *b = fa_load(hv->hv_url,
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     FA_LOAD_FLAGS(FA_COMPRESSION),
                     FA_LOAD_CANCELLABLE(h->h_mp->mp_cancellable),
                     NULL);

  if(b == NULL) {
    if(!cancellable_is_cancelled(h->h_mp->mp_cancellable))
      TRACE(TRACE_ERROR, "HLS", "Unable to open variant %s -- %s",
            hv->hv_url, errbuf);
    return HLS_ERROR_VARIANT_NOT_FOUND;
  }

  hv->hv_loaded = time(NULL);

  b = buf_make_writable(b);

  double duration = 0;
  int byte_offset = -1;
  int byte_size = -1;
  int seq = 1;
  int items = 0;
  hls_variant_parser_t hvp;
  int first_seq = -1;
  int discontinuity_seq = -1;

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
      if(first_seq == -1)
        first_seq = seq;
    } else if((v = mystrbegins(s, "#EXT-X-DISCONTINUITY-SEQUENCE:")) != NULL) {
      discontinuity_seq = atoi(v);
    } else if(!strcmp(s, "#EXT-X-DISCONTINUITY")) {
      if(discontinuity_seq == -1) {
        hs = hv_find_segment_by_seq(hv, seq - 1);
        if(hs != NULL)
          discontinuity_seq = hs->hs_discontinuity_segment->hds_seq;
        else
          discontinuity_seq = 0;
      }
      discontinuity_seq++;
    } else if((v = mystrbegins(s, "#EXT-X-START:")) != NULL) {
      hv_parse_start(hv, v);
    } else if((v = mystrbegins(s, "#EXT-X-BYTERANGE:")) != NULL) {
      byte_size = atoi(v);
      const char *o = strchr(v, '@');
      if(o != NULL)
        byte_offset = atoi(o+1);

    } else if(s[0] != '#') {

      items++;


      if(seq > hv->hv_last_seq) {

        if(discontinuity_seq == -1) {
          hs = hv_find_segment_by_seq(hv, seq - 1);
          if(hs != NULL)
            discontinuity_seq = hs->hs_discontinuity_segment->hds_seq;
          else
            discontinuity_seq = 0;
        }

	if(hv->hv_first_seq == -1) {
	  hv->hv_first_seq = seq;
        }

	hs = hv_add_segment(hv, s);
	hs->hs_byte_offset = byte_offset;
	hs->hs_byte_size   = byte_size;
	hs->hs_time_offset = hv->hv_duration;
	hs->hs_duration    = duration * 1000000LL;
	hs->hs_crypto      = hvp.hvp_crypto;
	hs->hs_key_url     = rstr_dup(hvp.hvp_key_url);
        hs->hs_discontinuity_segment =
          discontinuity_seq_get(h, discontinuity_seq);

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

        changed = 1;

	hs->hs_seq = seq;
	duration = 0;
	byte_offset = -1;
	byte_size = -1;
	hv->hv_last_seq = hs->hs_seq;

      }
      seq++;
    }
  }

  if(first_seq == -1)
    first_seq = 1;

  while((hs = TAILQ_FIRST(&hv->hv_segments)) != NULL) {

    if(hs->hs_seq >= first_seq ||
       hs->hs_fh != NULL ||
       hv->hv_current_seg == hs)
      break;
    segment_destroy(hs);
    changed = 1;
  }

  buf_release(b);
  rstr_release(hvp.hvp_key_url);

  if(TAILQ_FIRST(&hv->hv_segments) == NULL)
    return HLS_ERROR_VARIANT_EMPTY;

  if(changed) {
    int first = TAILQ_FIRST(&hv->hv_segments)->hs_seq;
    int last = TAILQ_LAST(&hv->hv_segments, hls_segment_queue)->hs_seq;
    HLS_TRACE(h, "Loaded segments %d ... %d (%d)", first, last, last - first);
  }

  if(hv->hv_frozen && hv->hv_duration)
    h->h_duration = hv->hv_duration;

  return 0;
}

/**
 *
 */
static void
hls_dump_demuxer(const hls_demuxer_t *hd, const hls_t *h)
{
  const hls_variant_t *hv;
  const char *txt;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    HLS_TRACE(h, "  %s", hv->hv_url);
    HLS_TRACE(h, "    bitrate:    %d", hv->hv_bitrate);

    if(hv->hv_audio_only) {
      HLS_TRACE(h, "    Audio only\n");
      continue;
    }
    if(hv->hv_initial) {
      HLS_TRACE(h, "    Initial\n");
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
hls_dump(const hls_t *h)
{
  HLS_TRACE(h, "Base URL: %s", h->h_baseurl);
  HLS_TRACE(h, "Primary/Adaptive variants");
  hls_dump_demuxer(&h->h_primary, h);
  HLS_TRACE(h, "Audio variants");
  hls_dump_demuxer(&h->h_audio, h);
}


/**
 *
 */
static void
check_audio_only(hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  int streams = 0;
  int audio_only = 0;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    streams++;
    if(hv->hv_audio_only)
      audio_only++;
  }

  if(streams == audio_only) {
    // Most likely not _all_ variants are audio only, so we clear the flag
    TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
      hv->hv_audio_only = 0;
    }
  }
}



/**
 *
 */
hls_error_t
hls_segment_open(hls_segment_t *hs)
{
  hls_variant_t *hv = hs->hs_variant;
  fa_open_extra_t foe = {0};
  fa_handle_t *fh;
  char errbuf[512];
  hls_demuxer_t *hd = hv->hv_demuxer;
  const hls_t *h = hd->hd_hls;

  assert(hs->hs_fh == NULL);
  hs->hs_open_time = arch_get_ts();
  hs->hs_blocked_counter = h->h_blocked;

  foe.foe_open_timeout = 3000;
  foe.foe_cancellable = hd->hd_cancellable;

  int flags = FA_BUFFERED_BIG | FA_STREAMING;

  if(hs->hs_byte_offset != -1)
    flags &= ~FA_STREAMING;
  
  fh = fa_open_ex(hs->hs_url, errbuf, sizeof(errbuf), flags, &foe);

  if(fh == NULL) {

    if(cancellable_is_cancelled(hd->hd_cancellable))
      return HLS_ERROR_SEGMENT_NOT_FOUND;

    usleep(500000);
    if(foe.foe_protocol_error == 404) {
      return HLS_ERROR_SEGMENT_NOT_FOUND;
    } else if(foe.foe_protocol_error == 403) {
      hs->hs_permanent_error = 1;
      return HLS_ERROR_SEGMENT_ACCESS_DENIED;
    } else {
      return HLS_ERROR_SEGMENT_BROKEN;
    }
  }

  fa_set_read_timeout(fh, 3000);

  if(hs->hs_byte_size != -1 && hs->hs_byte_offset != -1)
    fh = fa_slice_open(fh, hs->hs_byte_offset, hs->hs_byte_size);

  hs->hs_size = fa_fsize(fh);

  switch(hs->hs_crypto) {
  case HLS_CRYPTO_AES128:

    if(!rstr_eq(hs->hs_key_url, hv->hv_key_url)) {
      HLS_TRACE(h, "Loading key %s", rstr_get(hs->hs_key_url));
      buf_release(hv->hv_key);
      hv->hv_key = fa_load(rstr_get(hs->hs_key_url),
                            FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                            NULL);
      if(hv->hv_key == NULL) {
	TRACE(TRACE_ERROR, "HLS", "Unable to load key file %s",
	      rstr_get(hs->hs_key_url));
	fa_close(fh);
        return HLS_ERROR_SEGMENT_BAD_KEY;
      }
      rstr_set(&hv->hv_key_url, hs->hs_key_url);
    }

    fh = fa_aescbc_open(fh, hs->hs_iv, buf_c8(hv->hv_key));
  }
  hs->hs_fh = fh;
  HLS_TRACE(h, "Opened %s (sequence %d) ranges:[%d + %d] OK",
            hs->hs_url, hs->hs_seq, hs->hs_byte_offset, hs->hs_byte_size);
  return 0;
}


/**
 *
 */
void
hls_segment_close(hls_segment_t *hs)
{
  if(hs->hs_fh == NULL)
    return;

  hls_demuxer_t *hd = hs->hs_variant->hv_demuxer;
  hls_t *h = hd->hd_hls;

  if(hs->hs_blocked_counter == h->h_blocked) {
    int64_t ts = arch_get_ts() - hs->hs_open_time;
    if(ts > 1000) {
      int64_t bw = 8000000LL * hs->hs_size / ts;
      bw = MIN(100000000, bw);


      int low_buffer = h->h_mp->mp_buffer_delay < 5000000;
      const char *delta;
      if(hd->hd_bw == 0) {
        hd->hd_bw = bw;
        delta = "Initial";
      } else if(bw < hd->hd_bw) {
        delta = "Decrease";
        if(low_buffer)
          hd->hd_bw = (hd->hd_bw + bw) / 2;
        else
          hd->hd_bw = (hd->hd_bw * 7 + bw) / 8;
      } else {
        delta = "Increase";
        hd->hd_bw = (hd->hd_bw + bw) / 2;
      }
      HLS_TRACE(h, "Estimated bandwidth updated %d bps "
                "(most recent segment %d bps) "
                "buffer: %ds (%s) delta: %s\n",
                hd->hd_bw, (int)bw,
                (int)(h->h_mp->mp_buffer_delay / 1000000),
                low_buffer ? "Low" : "OK",
                delta);
      hd->hd_bw_updated = 1;
    }
  }

  fa_close(hs->hs_fh);
  hs->hs_fh = NULL;
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
static int
get_current_video_seq(media_pipe_t *mp, hls_t *h)
{
  int seq = h->h_last_enqueued_seq;
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = TAILQ_FIRST(&mp->mp_video.mq_q_data);
  if(mb != NULL)
    seq = mb->mb_sequence;
  hts_mutex_unlock(&mp->mp_mutex);
  return seq;
}


/**
 * Most requests are for same sequence so we just keep a cache pointer
 */
hls_segment_t *
hv_find_segment_by_seq(hls_variant_t *hv, int seq)
{
  hls_segment_t *hs = hv->hv_segment_search;
  if(hs != NULL) {
    if(hs->hs_seq == seq)
      return hs;
    hs = TAILQ_NEXT(hs, hs_link);
    if(hs != NULL && hs->hs_seq == seq) {
      hv->hv_segment_search = hs;
      return hs;
    }
  }

  hs = TAILQ_LAST(&hv->hv_segments, hls_segment_queue);
  if(hs == NULL)
    return NULL;
  if(hs->hs_seq == seq)
    return hs;

  TAILQ_FOREACH(hs, &hv->hv_segments, hs_link) {
    if(hs->hs_seq == seq) {
      hv->hv_segment_search = hs;
      break;
    }
  }
  return hs;
}


/**
 *
 */
void
hls_variant_open(hls_variant_t *hv)
{
  hls_demuxer_t *hd = hv->hv_demuxer;
  hls_t *h = hd->hd_hls;

  if(hd == &h->h_primary) {
    media_pipe_t *mp = h->h_mp;
    char info[64];
    if(hv->hv_bitrate) {
      snprintf(info, sizeof(info), "HLS %d kb/s", hv->hv_bitrate / 1000);
    } else {
      snprintf(info, sizeof(info), "HLS");
    }
    prop_set(mp->mp_prop_metadata, "format", PROP_SET_STRING, info);
#if 0
    prop_t *fmt = prop_create_r(mp->mp_prop_metadata, "format");

    if(hv->hv_bitrate) {
      char info[64];
      snprintf(info, sizeof(info), "HLS %d kb/s", hv->hv_bitrate / 1000);
      mp_send_prop_set_string(mp, &mp->mp_audio, fmt, info);
    } else {
      mp_send_prop_set_string(mp, &mp->mp_audio, fmt, "HLS");
    }

    prop_ref_dec(fmt);
#endif

    mp_set_duration(mp, hv->hv_frozen ? hv->hv_duration :  AV_NOPTS_VALUE);
    if(hv->hv_frozen) {
      mp_set_clr_flags(mp, MP_CAN_SEEK, 0);
    } else {
      mp_set_clr_flags(mp, 0, MP_CAN_SEEK);
    }
  }
}


/**
 *
 */
void
hls_variant_close(hls_variant_t *hv)
{
  if(hv->hv_demuxer_close != NULL)
    hv->hv_demuxer_close(hv);

  if(hv->hv_current_seg != NULL) {
    hls_segment_close(hv->hv_current_seg);
    hv->hv_current_seg = NULL;
  }
}


/**
 *
 */
hls_segment_t *
hls_variant_select_next_segment(hls_variant_t *hv)
{
  hls_demuxer_t *hd = hv->hv_demuxer;
  hls_t *h = hd->hd_hls;
  media_pipe_t *mp = h->h_mp;
  hls_segment_t *hs;


  if(hv->hv_loaded == 0) {
    hls_error_t err = hls_variant_update(hv, mp);

    if(err != HLS_ERROR_OK) {
      hls_bad_variant(hv, err);
      return NULL;
    }
  }

 retry:
  if(hv->hv_current_seg == NULL) {

    hs = NULL;
    if(hd->hd_seek_to_segment != PTS_UNSET) {
      hs = hv_find_segment_by_time(hv, hd->hd_seek_to_segment);

      if(hv->hv_frozen && hs == NULL)
        return HLS_EOF;

      if(hs != NULL) {
        HLS_TRACE(h, "%s: Seek to %"PRId64" -- Segment %d", hd->hd_type,
                  hd->hd_seek_to_segment, hs->hs_seq);
      } else {
        HLS_TRACE(h, "%s: Seek to %"PRId64" -- Segment not found", hd->hd_type,
                  hd->hd_seek_to_segment);
      }

      hd->hd_seek_to_segment = PTS_UNSET;
    }

    if(hs == NULL) {
      int seq = get_current_video_seq(h->h_mp, h);
      if(seq == 0 && !hv->hv_frozen) {

        if(hv->hv_start_time_offset != PTS_UNSET) {
          int64_t acc = 0;

          hls_segment_t *x;
          TAILQ_FOREACH(x, &hv->hv_segments, hs_link) {
            acc += x->hs_duration;
            if(hv->hv_start_time_offset < acc) {
              hs = x;
              HLS_TRACE(h, "Live stream starting at segment %d", hs->hs_seq);
              break;
            }
          }
        }

        if(hs == NULL) {
          seq = MAX(hv->hv_last_seq - 5, hv->hv_first_seq);
          HLS_TRACE(h, "Live stream selecting initial segment %d", seq);
        }
      }

      if(hs == NULL && seq) {

        int i;
        for(i = 0; i < 5; i++) {
          hs = hv_find_segment_by_seq(hv, seq + i);
          if(hs != NULL)
            break;
          HLS_TRACE(h, "Lookup of seq %d failed, trying next", seq + i);
        }
      }
    }
    if(hs == NULL)
      hs = TAILQ_FIRST(&hv->hv_segments);

  } else {
    hs = TAILQ_NEXT(hv->hv_current_seg, hs_link);
  }

  if(hs != NULL)
    return hs;

  if(hv->hv_frozen) {
    // We're not a live stream and no segment can be found, this is EOF
    return HLS_EOF;
  }

  if(mp->mp_buffer_delay < 10000000) {

    if(hv->hv_loaded != time(NULL)) {
      hls_error_t err = hls_variant_update(hv, mp);

      if(err != HLS_ERROR_OK) {
        hls_bad_variant(hv, err);
        return NULL;
      }
      goto retry;
    }
  }

  /*
   * Ok, so no segment is availble yet => go to sleep (but wakeup
   * if we need to exit or if we need to seek someplace)
   */

  hts_mutex_lock(&mp->mp_mutex);

  if(TAILQ_FIRST(&mp->mp_eq) == NULL) {
    if(hts_cond_wait_timeout(&mp->mp_backpressure, &mp->mp_mutex, 1000)) {
      hts_mutex_unlock(&mp->mp_mutex);
      return HLS_NYA;
    }
  }
  hts_mutex_unlock(&mp->mp_mutex);
  return NULL;
}


/**
 * Select a low bitrate variant that we hope works ok
 */
hls_variant_t *
hls_select_default_variant(hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  hls_variant_t *best = NULL;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_initial)
      return hv;
  }

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
demuxer_select_variant_simple(hls_demuxer_t *hd, int64_t now, int bw)
{
  hls_variant_t *hv;
  hls_variant_t *best = NULL;

  int lcc = INT32_MAX;

  // Figure out which stream have the lowest corruption counter
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_audio_only)
      continue;
    if(hv->hv_corrupt_timer < now - HLS_CORRUPTION_MEASURE_PERIOD)
      hv->hv_corruptions_last_period = 0;

    lcc = MIN(lcc, hv->hv_corrupt_counter);
  }

  // First, try to select best mathcing bandwidth among streams with
  // lowest corruption

  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_audio_only)
      continue;
    if(hv->hv_corruptions_last_period >= 3)
      continue;

    if(hv->hv_corrupt_counter != lcc)
      continue;

    if(hv->hv_bitrate >= bw)
      continue;

    if(best == NULL)
      best = hv;
  }

  if(best != NULL)
    return best;

  // Try to select something that's not corrupted

  TAILQ_FOREACH_REVERSE(hv, &hd->hd_variants, hls_variant_queue, hv_link) {
    if(hv->hv_audio_only)
      continue;
    if(hv->hv_corruptions_last_period >= 3)
      continue;

    return hv;
  }
  hd->hd_no_functional_streams = 1;
  return NULL;
}


/**
 *
 */
static hls_variant_t *
demuxer_select_variant_random(hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  int cnt = 0;

  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(!hv->hv_audio_only)
      cnt++;

  }

  int r = rand() % cnt;
  cnt = 0;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    if(hv->hv_audio_only)
      continue;
    if(r == cnt)
      break;
    cnt++;
  }

  HLS_TRACE(hd->hd_hls, "Randomly selected bitrate %d", hv ? hv->hv_bitrate : 0);
  return hv;
}

/**
 *
 */
static hls_variant_t *
hls_demuxer_select_variant(hls_demuxer_t *hd, int64_t now, int bw)
{
  if(0)
    return demuxer_select_variant_random(hd);

  return demuxer_select_variant_simple(hd, now, bw);
}



/**
 *
 */
void
hls_check_bw_switch(hls_demuxer_t *hd)
{
  hls_t *h = hd->hd_hls;
  media_pipe_t *mp = h->h_mp;

  if(hd->hd_bw_updated == 0)
    return;

  if(hd == &h->h_primary) {
    prop_set(mp->mp_prop_io, "bitrate", PROP_SET_INT, hd->hd_bw / 1000);
    prop_set(mp->mp_prop_io, "bitrateValid", PROP_SET_INT, 1);
  }

  int64_t now = arch_get_ts();
  if(hd->hd_last_switch + 1000000 > now)
    return;

  hd->hd_bw_updated = 0;
  hls_variant_t *hv = hls_demuxer_select_variant(hd, now, hd->hd_bw);

  if(hv == NULL || hv == hd->hd_current)
    return;


  if(hv->hv_bitrate > hd->hd_current->hv_bitrate) {
    // Stepping up, only do it if we have more than 10s worth of buffer
    if(mp->mp_buffer_delay < 10000000) {
      hd->hd_last_switch = now;
      return;
    }
  }


  hd->hd_last_switch = now;
  hd->hd_req = hv;

  hls_free_mbp(mp, &hd->hd_mb);
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

  } else if(event_is_type(e, EVENT_SELECT_AUDIO_TRACK)) {
    event_select_track_t *est = (event_select_track_t *)e;
    const char *id = mystrbegins(est->id, "hls:");
    if(id != NULL) {
      const int streamid = atoi(id);
      assert(streamid != 0);
      h->h_audio.hd_pending_stream = streamid;
    }

  } else if(event_is_action(e, ACTION_SKIP_FORWARD) ||
            event_is_action(e, ACTION_SKIP_BACKWARD) ||
            event_is_type(e, EVENT_EXIT) ||
            event_is_type(e, EVENT_PLAY_URL) ||
            event_is_type(e, EVENT_SEEK)) {

    cancellable_cancel(h->h_primary.hd_cancellable);
    cancellable_cancel(h->h_audio.hd_cancellable);
    return 0; // Continue processing
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
drop_early_audio_packets(media_pipe_t *mp, int64_t dts)
{
  media_queue_t *mq = &mp->mp_audio;
  media_buf_t *mb;

  while((mb = TAILQ_FIRST(&mq->mq_q_data)) != NULL) {
    TAILQ_REMOVE(&mq->mq_q_data, mb, mb_link);
    media_buf_free_locked(mp, mb);
  }
}


/**
 *
 */
static event_t *
enqueue_buffer(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb,
               hls_t *h, hls_variant_t *hv)
{
  const int is_video = mb->mb_data_type == MB_VIDEO;

  hts_mutex_lock(&mp->mp_mutex);

  mp_update_buffer_delay(mp);

  while(1) {

    event_t *e = TAILQ_FIRST(&mp->mp_eq);
    if(e != NULL) {
      TAILQ_REMOVE(&mp->mp_eq, e, e_link);
      hts_mutex_unlock(&mp->mp_mutex);
      return e;
    }

    // Check if buffer is full
    if(mp->mp_buffer_current + mb_buffered_size(mb) < mp->mp_buffer_limit &&
       mp->mp_buffer_delay < 60000000)
      break;

    h->h_blocked++;
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
  }

  if(unlikely(mq->mq_seektarget != AV_NOPTS_VALUE)) {
    int64_t ts = mb->mb_user_time;
    if(ts < mq->mq_seektarget) {
      mb->mb_skip = 1;
    } else {
      mq->mq_seektarget = AV_NOPTS_VALUE;
    }
  }


  int flush = 0;

  if(mq->mq_demuxer_flags & HLS_QUEUE_MERGE) {

    if(mq->mq_last_deq_dts != PTS_UNSET &&
       mb->mb_dts < mq->mq_last_deq_dts) {
      /*
       * This frame has already been dequeued (DTS lower)
       * Drop packet and restart keyframe search
       */

      if(is_video)
        extract_ps(hv, mb);

      media_buf_free_locked(mp, mb);
      mq->mq_demuxer_flags &= ~HLS_QUEUE_KEYFRAME_SEEN;
      hts_mutex_unlock(&mp->mp_mutex);
      return NULL;
    }

    media_buf_t *b;
    TAILQ_FOREACH(b, &mq->mq_q_data, mb_link)
      if(b->mb_dts != PTS_UNSET && b->mb_dts >= mb->mb_dts)
        break;

    while(b != NULL) {
      media_buf_t *next = TAILQ_NEXT(b, mb_link);
      if(b->mb_data_type == MB_AUDIO || b->mb_data_type == MB_VIDEO) {
        TAILQ_REMOVE(&mq->mq_q_data, b, mb_link);
        mq->mq_packets_current--;
        mp->mp_buffer_current -= mb_buffered_size(b);
        media_buf_free_locked(mp, b);
      }
      b = next;
    }
    mq->mq_demuxer_flags &= ~HLS_QUEUE_MERGE;
    flush = 1;
    assert(mb->mb_keyframe == 1);
    HLS_TRACE(h, "%s queue merged at DTS %"PRId64,
              is_video ? "Video" : "Audio", mb->mb_dts);
  }


  if(is_video && !(mq->mq_demuxer_flags & HLS_QUEUE_KEYFRAME_SEEN) &&
     TAILQ_FIRST(&mq->mq_q_data) == NULL) {
    assert(mb->mb_dts != PTS_UNSET);
    drop_early_audio_packets(mp, mb->mb_dts);
  }

  if(mb->mb_keyframe)
    mq->mq_demuxer_flags |= HLS_QUEUE_KEYFRAME_SEEN;

  if(mp->mp_hold_flags & MP_HOLD_SYNC &&
     mp->mp_video.mq_demuxer_flags & HLS_QUEUE_KEYFRAME_SEEN &&
     mp->mp_audio.mq_demuxer_flags & HLS_QUEUE_KEYFRAME_SEEN) {
    mp->mp_hold_flags &= ~MP_HOLD_SYNC;
    mp_set_playstatus_by_hold_locked(mp, NULL);
  }


  if(is_video && hv->hv_video_headers_size) {

    media_buf_t *mbx =
      media_buf_alloc_locked(mp, hv->hv_video_headers_size + mb->mb_size);

    memcpy(mbx->mb_data, hv->hv_video_headers, hv->hv_video_headers_size);
    memcpy(mbx->mb_data + hv->hv_video_headers_size, mb->mb_data, mb->mb_size);

    mbx->mb_keyframe = mb->mb_keyframe;
    mbx->mb_data_type = MB_VIDEO;
    mbx->mb_cw = media_codec_ref(mb->mb_cw);
    mbx->mb_skip = mb->mb_skip;
    mbx->mb_user_time = mb->mb_user_time;

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
    return NULL;
  }

#ifdef DUMP_VIDEO
  if(is_video)
    dump_video(mb);
#endif

  mb->mb_flush = flush;
  mb_enq(mp, mq, mb);
  h->h_last_enqueued_seq = mb->mb_sequence;

  if(!is_video && mq->mq_stream != h->h_audio.hd_current_stream) {
    HLS_TRACE(h, "Telling audio decoder to switch from stream %d to %d",
              mq->mq_stream, h->h_audio.hd_current_stream);
    mq->mq_stream = h->h_audio.hd_current_stream;
  }
  hts_mutex_unlock(&mp->mp_mutex);
  return NULL;
}


/**
 *
 */
static media_buf_t *
hls_demuxer_get(hls_demuxer_t *hd)
{
  if(hd->hd_mb == NULL)
    hd->hd_mb = hls_ts_demuxer_read(hd);

  return hd->hd_mb;
}

/**
 *
 */
static media_buf_t *
hls_demuxer_get_audio(hls_t *h)
{
  hls_demuxer_t *hd = &h->h_audio;

  if(hd->hd_current_stream != hd->hd_pending_stream) {

    hd->hd_current_stream = hd->hd_pending_stream;

    hls_variant_t *hv;
    TAILQ_FOREACH(hv, &hd->hd_variants, hv_link)
      if(hv->hv_audio_stream == hd->hd_current_stream)
        break;

    HLS_TRACE(h, "Audio demuxer checking stream switch to streamid: %d -> %s",
              hd->hd_current_stream,
              hv ? hv->hv_name : "<muxed in primary>");

    if(hd->hd_current != NULL) {
      hls_variant_close(hd->hd_current);
    }


    if(hd->hd_current != NULL && hv == NULL) {
      // Switching to an audio stream in primary mux requires a full
      // resync
      HLS_TRACE(h, "Force primary stream to resynchronize due to audio switch");
      h->h_primary.hd_req = h->h_primary.hd_current;
    } else {

      media_pipe_t *mp = h->h_mp;
      mp->mp_audio.mq_demuxer_flags |= HLS_QUEUE_MERGE;
      mp->mp_audio.mq_demuxer_flags &= ~HLS_QUEUE_KEYFRAME_SEEN;
      HLS_TRACE(h, "Audio queue merge started");
    }

    hd->hd_current = hv;

    hls_free_mbp(h->h_mp, &hd->hd_mb);
  }

  return hls_demuxer_get(hd);
}


/**
 *
 */
static void
hls_demuxer_seek(media_pipe_t *mp, hls_demuxer_t *hd, int64_t pos)
{
  hd->hd_seek_to_segment = pos;

  if(hd->hd_current != NULL && hd->hd_current->hv_demuxer_flush)
    hd->hd_current->hv_demuxer_flush(hd->hd_current);

  hls_free_mbp(mp, &hd->hd_mb);
}


/**
 *
 */
static void __attribute__((unused))
print_ts(media_buf_t *mb)
{
  if(mb == HLS_EOF)
    printf("%5s:%8s:%5s:%-20s %-20s", "", "", "", "EOF", "");
  else if(mb == HLS_DIS)
    printf("%5s:%8s:%5s:%-20s %-20s", "", "", "", "DIS", "");
  else if(mb->mb_dts == PTS_UNSET)
    printf("%5s:%08x:%5d:%-20s %-20s",
           mb->mb_data_type == MB_VIDEO ? "VIDEO" : "AUDIO",
           mb->mb_epoch, mb->mb_sequence, "UNSET", "");
  else
    printf("%5s:%08x:%5d:%-20"PRId64" %-20"PRId64,
           mb->mb_data_type == MB_VIDEO ? "VIDEO" : "AUDIO",
           mb->mb_epoch, mb->mb_sequence, mb->mb_dts, mb->mb_user_time);
}

/**
 *
 */
static hls_demuxer_t *
get_media_buf(hls_t *h)
{
  media_buf_t *b1, *b2;
  hls_demuxer_t *hd;
  media_pipe_t *mp = h->h_mp;

  const int dumpinfo = 0;
 again:
  if((b2 = hls_demuxer_get_audio(h)) == NULL)
    return NULL;
  if((b1 = hls_demuxer_get(&h->h_primary)) == NULL)
    return NULL;

  if(dumpinfo) {
    printf("MUXER: ");
    print_ts(b1);
    printf("   ");
    print_ts(b2);
    printf("   ");
  }

  const char *why;

  if(b1 == HLS_EOF && b2 == HLS_EOF) {
    // All demuxers are EOF
    mp->mp_eof = 1;
    return HLS_EOF;
  }

  mp->mp_eof = 0;

  if(b1 == HLS_DIS) {
    if(b2 == HLS_EOF) {
      h->h_primary.hd_discontinuity = 0;
      h->h_primary.hd_mb = NULL;
      if(dumpinfo)
        printf("Primary discontinuity reset\n");
      goto again;
    }

    if(b2 == HLS_DIS) {
      h->h_primary.hd_discontinuity = 0;
      h->h_audio.hd_discontinuity = 0;
      h->h_primary.hd_mb = NULL;
      h->h_audio.hd_mb = NULL;
      if(dumpinfo)
        printf("Both discontinuity reset\n");
      goto again;
    }
    hd = &h->h_audio;
    why = "primary-discontinuity";
  } else if(b2 == HLS_DIS) {

    if(b1 == HLS_EOF) {
      h->h_audio.hd_discontinuity = 0;
      h->h_audio.hd_mb = NULL;
      if(dumpinfo)
        printf("Audio discontinuity reset\n");
      goto again;
    }

    hd = &h->h_primary;
    why = "audio-discontinuity";
  } else if(b2 == HLS_EOF) {
    hd = &h->h_primary;
    why = "audio-eof";
  } else if(b1 == HLS_EOF) {
    hd = &h->h_audio;
    why = "primary-eof";
  } else if(b1->mb_dts == PTS_UNSET) {
    hd = &h->h_primary;
    why = "primary-no-dts";
  } else if(b2->mb_dts == PTS_UNSET) {
    hd = &h->h_audio;
    why = "audio-no-dts";
  } else if(b1->mb_dts < b2->mb_dts) {
    hd = &h->h_primary;
    why = "primary-lower-dts";
  } else {
    hd = &h->h_audio;
    why = "audio-lower-dts";
  }

  if(dumpinfo) {
    printf("%s\n", why);
  }

  return hd;
}

/**
 *
 */
static void
clear_ts_offsets(hls_demuxer_t *hd)
{
  hls_variant_t *hv;
  hd->hd_last_dts = PTS_UNSET;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link) {
    hls_segment_t *hs;
    TAILQ_FOREACH(hs, &hv->hv_segments, hs_link) {
      hs->hs_ts_offset = PTS_UNSET;
    }
  }
}


/**
 *
 */
static void
clear_hds_offset(hls_t *h)
{
  hls_discontinuity_segment_t *hds;

  LIST_FOREACH(hds, &h->h_discontinuity_segments, hds_link)
    if(hds->hds_seq != 0)
      hds->hds_offset = PTS_UNSET;
}

/**
 *
 */
static void
hls_seek(hls_t *h, int64_t ts)
{
  media_pipe_t *mp = h->h_mp;

  HLS_TRACE(h, "Seeking to %"PRId64, ts);

  hls_demuxer_seek(mp, &h->h_primary, ts);
  hls_demuxer_seek(mp, &h->h_audio,   ts);
  clear_hds_offset(h);
  clear_ts_offsets(&h->h_primary);
  clear_ts_offsets(&h->h_audio);
  cancellable_reset(h->h_primary.hd_cancellable);
  cancellable_reset(h->h_audio.hd_cancellable);

  mp->mp_video.mq_seektarget = ts;
  mp->mp_audio.mq_seektarget = ts;
  mp_flush(mp);

  mp->mp_video.mq_demuxer_flags &= ~HLS_QUEUE_KEYFRAME_SEEN;
  mp->mp_audio.mq_demuxer_flags &= ~HLS_QUEUE_KEYFRAME_SEEN;
}


/**
 *
 */
static event_t *
hls_play(hls_t *h, media_pipe_t *mp, char *errbuf, size_t errlen,
         const video_args_t *va)
{
  event_t *e = NULL;
  const char *canonical_url = va->canonical_url;
  sub_scanner_t *ss = NULL;
  int loading = 1;

  h->h_restartpos_last = -1;
  h->h_last_timestamp_presented = AV_NOPTS_VALUE;
  h->h_sub_scanning_done = 0;
  h->h_enqueued_something = 0;

  h->h_playback_priority = va->priority;

  mp->mp_video.mq_stream = 0;
  mp->mp_audio.mq_stream = 1;

  if(!(va->flags & BACKEND_VIDEO_NO_AUDIO))
    mp_become_primary(mp);

  mp_hold(mp, MP_HOLD_SYNC, NULL);

  mp_configure(mp, MP_CAN_PAUSE, MP_BUFFER_DEEP, 0, "video");

  // mp->mp_pre_buffer_delay = 10000000;

  mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_seek_base = 0;

  mp_event_set_callback(mp, hls_event_callback, h);

  int64_t start = playinfo_get_restartpos(canonical_url,
                                          va->title, va->resume_mode) * 1000;
  if(start) {
    TRACE(TRACE_DEBUG, "HLS", "Attempting to resume from %.2f seconds",
          start / 1000000.0f);
    mp->mp_seek_base = start;
    hls_seek(h, start);
  }

  h->h_primary.hd_current = hls_select_default_variant(&h->h_primary);
  h->h_audio.  hd_current = hls_select_default_variant(&h->h_audio);

  h->h_audio.hd_pending_stream = 1;

  if(va->flags & BACKEND_VIDEO_NO_SUBTITLE_SCAN)
    h->h_sub_scanning_done = 1;

  while(1) {

    if(h->h_primary.hd_no_functional_streams ||
       h->h_audio.hd_no_functional_streams) {
      if(h->h_last_error)
        snprintf(errbuf, errlen, "No playable streams -- %s",
                 hlserrstr[h->h_last_error]);
      else
        snprintf(errbuf, errlen, "No playable streams");
      e = NULL;
      break;
    }
    if(e == NULL)
      e = mp_dequeue_event_deadline(mp, 0);

    if(e != NULL) {
      if(event_is_action(e, ACTION_SKIP_FORWARD) ||
         event_is_action(e, ACTION_SKIP_BACKWARD) ||
         event_is_type(e, EVENT_EXIT) ||
         event_is_type(e, EVENT_PLAY_URL)) {
        break;
      }

      if(event_is_type(e, EVENT_SEEK)) {
        event_ts_t *ets = (event_ts_t *)e;
        hls_seek(h, ets->ts);
      }
      event_release(e);
      e = NULL;
    }

    if(!h->h_sub_scanning_done && h->h_duration) {
      h->h_sub_scanning_done = 1;
      ss = sub_scanner_create(h->h_baseurl, mp->mp_prop_subtitle_tracks, va,
                              h->h_duration / 1000000LL);
    }

    hls_demuxer_t *hd = get_media_buf(h);
    if(hd == NULL)
      continue;

    if(hd == HLS_EOF) {
      int is_empty = 0;
      hts_mutex_lock(&mp->mp_mutex);

      is_empty =
        TAILQ_FIRST(&mp->mp_audio.mq_q_data) == NULL &&
        TAILQ_FIRST(&mp->mp_video.mq_q_data) == NULL;
      hts_mutex_unlock(&mp->mp_mutex);

      if(!is_empty) {
        usleep(100000);
        continue;
      }
      e = event_create_type(EVENT_EOF);
      break;

    } else {

      media_buf_t *mb = hd->hd_mb;

      media_queue_t *mq;
      if(mb->mb_data_type == MB_VIDEO)
        mq = &mp->mp_video;
      else
        mq = &mp->mp_audio;

      e = enqueue_buffer(mp, mq, mb, h, hd->hd_current);
      if(e == NULL) {
        hd->hd_mb = NULL;
        if(loading) {
          prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 0);
          loading = 0;
        }
      }
    }
  }

  if(mp->mp_flags & MP_CAN_SEEK) {

    // Compute stop position (in percentage of video length)

    int spp = mp->mp_duration ? mp->mp_seek_base * 100 / mp->mp_duration : 0;

    if(spp >= video_settings.played_threshold || event_is_type(e, EVENT_EOF)) {
      playinfo_set_restartpos(canonical_url, -1, 0);
      playinfo_register_play(canonical_url, 1);
      TRACE(TRACE_DEBUG, "Video",
	    "Playback reached %d%%%s, counting as played (%s)",
	    spp, event_is_type(e, EVENT_EOF) ? ", EOF detected" : "",
            canonical_url);
    } else if(h->h_last_timestamp_presented != PTS_UNSET) {
      playinfo_set_restartpos(canonical_url,
                              h->h_last_timestamp_presented / 1000,
			      0);
    }
  }
  // Shutdown

  mp_event_set_callback(mp, NULL, NULL);

  mp_shutdown(mp);

  sub_scanner_destroy(ss);

  return e;
}


/**
 *
 */
int
hls_get_audio_track(hls_t *h, int pid, const char *mux_id, const char *language,
                    const char *fmt, int autosel)
{
  hls_audio_track_t *hat;
  char trackuri[256];

  rstr_t *rformat     = rstr_alloc(fmt);
  rstr_t *rlanguage   = rstr_alloc(language);

  LIST_FOREACH(hat, &h->h_audio_tracks, hat_link) {
    if(mux_id == NULL && hat->hat_mux_id == NULL && hat->hat_pid == pid)
      break;
    if(mux_id != NULL && hat->hat_mux_id != NULL &&
       !strcmp(hat->hat_mux_id, mux_id))
      break;
  }

  if(hat != NULL) {

    prop_set(hat->hat_trackprop, "format", PROP_SET_RSTRING, rformat);

  } else {

    hat = LIST_FIRST(&h->h_audio_tracks);
    int newid = hat ? hat->hat_stream_id + 1 : 1;

    hat = calloc(1, sizeof(hls_audio_track_t));
    hat->hat_stream_id = newid;
    hat->hat_pid = pid;
    hat->hat_mux_id = mux_id ? strdup(mux_id) : NULL;
    LIST_INSERT_HEAD(&h->h_audio_tracks, hat, hat_link);
    snprintf(trackuri, sizeof(trackuri), "hls:%d", newid);

    prop_t *s;
    int score = 1000;

    if(mux_id) {
      s = _p("Supplementary");
    } else {
      s = _p("Primary");
      score += 10;
    }

    hat->hat_trackprop =
      mp_add_trackr(h->h_mp->mp_prop_audio_tracks,
                    NULL,
                    trackuri,
                    rformat,
                    rformat,
                    rlanguage,
                    NULL,
                    s,
                    score,
                    autosel);
  }
  rstr_release(rformat);
  rstr_release(rlanguage);

  return hat->hat_stream_id;
}

/**
 *
 */
static void
hls_free_audio_tracks(hls_t *h)
{
  hls_audio_track_t *hat;

  while((hat = LIST_FIRST(&h->h_audio_tracks)) != NULL) {
    LIST_REMOVE(hat, hat_link);
    prop_ref_dec(hat->hat_trackprop);
    free(hat->hat_mux_id);
    free(hat);
  }
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
static int
check_if_bitrate_exist(const hls_demuxer_t *hd, int bitrate)
{
  const hls_variant_t *hv;
  TAILQ_FOREACH(hv, &hd->hd_variants, hv_link)
    if(hv->hv_bitrate == bitrate)
      return 1;
  return 0;
}


/**
 *
 */
static hls_variant_t *
hls_add_variant(const hls_t *h, const char *url, hls_variant_t *hv,
		hls_demuxer_t *hd, const char *name)
{
  if(hv == NULL)
    hv = variant_create(hd);


  if(hv->hv_bitrate && check_if_bitrate_exist(hd, hv->hv_bitrate)) {
    HLS_TRACE(h, "Skipping duplicate bitrate %d via %s",
              hv->hv_bitrate, url);
    variant_destroy(hv);
    return NULL;
  }

  if(name != NULL)
    snprintf(hv->hv_name, sizeof(hv->hv_name), "%s", name);
  else
    snprintf(hv->hv_name, sizeof(hv->hv_name), "bitrate %d",
             hv->hv_bitrate);

  hv->hv_url = url_resolve_relative_from_base(h->h_baseurl, url);
  TAILQ_INSERT_SORTED(&hd->hd_variants, hv, hv_link, variant_cmp,
                      hls_variant_t);
  return hv;
}



/**
 *
 */
static void
hls_ext_x_media(hls_t *h, const char *V)
{
  char *v = mystrdupa(V);

  const char *type = NULL;
  const char *group = NULL;
  const char *name = NULL;
  const char *lang = NULL;
  const char *uri = NULL;
  int def = 0;
  int autosel = 0;

  while(*v) {
    const char *key, *value;
    v = get_attrib(v, &key, &value);
    if(v == NULL)
      break;
    if(!strcmp(key, "TYPE"))
      type = value;
    else if(!strcmp(key, "GROUP-ID"))
      group = value;
    else if(!strcmp(key, "NAME"))
      name = value;
    else if(!strcmp(key, "LANGUAGE"))
      lang = value;
    else if(!strcmp(key, "AUTOSELECT"))
      autosel = !strcmp(value, "YES");
    else if(!strcmp(key, "DEFAULT"))
      def = !strcmp(value, "YES");
    else if(!strcmp(key, "URI"))
      uri = value;
  }
  if(uri == NULL || type == NULL)
    return;


  HLS_TRACE(h, "Secondary stream %s "
            "(type=%s group=%s name=%s language=%s autoselect=%s default=%s)",
            uri,
            type,
            group ? group : "<unset>",
            name  ? name  : "<unset>",
            lang  ? lang  : "<unset>",
            autosel ? "YES" : "NO",
            def ? "YES" : "NO");

  if(!strcmp(type, "AUDIO")) {
    hls_variant_t *hv =
      hls_add_variant(h, uri, NULL, &h->h_audio, name ? name : "audio");

    if(hv != NULL) {
      hv->hv_audio_stream = hls_get_audio_track(h, 0, hv->hv_url, lang,
                                                NULL, autosel);
    }
  }
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
hls_ext_x_stream_inf(hls_t *h, const char *V, hls_variant_t **hvp,
                     hls_demuxer_t *hd)
{
  char *v = mystrdupa(V);

  if(*hvp != NULL)
    free(*hvp);

  hls_variant_t *hv = *hvp = variant_create(hd);

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
hls_demuxer_init(hls_demuxer_t *hd, hls_t *h, const char *type)
{
  TAILQ_INIT(&hd->hd_variants);
  hd->hd_hls = h;
  hd->hd_type = type;
  hd->hd_seek_to_segment = PTS_UNSET;
  hd->hd_last_dts = PTS_UNSET;
  hd->hd_cancellable = cancellable_create();
}


/**
 *
 */
static void
hls_demuxer_close(media_pipe_t *mp, hls_demuxer_t *hd)
{
  variants_destroy(&hd->hd_variants);
  if(hd->hd_audio_codec != NULL)
    media_codec_deref(hd->hd_audio_codec);
  hls_free_mbp(mp, &hd->hd_mb);
  cancellable_release(hd->hd_cancellable);
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

  usage_event("Play video", 1, USAGE_SEG("format", "HLS"));

  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);

  mp->mp_audio.mq_demuxer_flags = 0;
  mp->mp_video.mq_demuxer_flags = 0;

  hls_t h;
  memset(&h, 0, sizeof(h));
  hls_demuxer_init(&h.h_primary, &h, "primary");
  hls_demuxer_init(&h.h_audio, &h, "audio");
  h.h_mp = mp;
  h.h_baseurl = url;
  h.h_codec_h264 = media_codec_create(AV_CODEC_ID_H264, 1, NULL, NULL, NULL, mp);
  h.h_debug = gconf.enable_hls_debug;
  if(strstr(buf, "#EXT-X-STREAM-INF:")) {

    hls_variant_t *hv = NULL;
    int first_variant = 1;

    LINEPARSE(s, buf) {
      const char *v;
      if((v = mystrbegins(s, "#EXT-X-MEDIA:")) != NULL)
        hls_ext_x_media(&h, v);
      else if((v = mystrbegins(s, "#EXT-X-STREAM-INF:")) != NULL)
        hls_ext_x_stream_inf(&h, v, &hv, &h.h_primary);
      else if(s[0] != '#') {
        hv->hv_initial = first_variant;
        first_variant = 0;
        hls_add_variant(&h, s, hv, &h.h_primary, NULL);
        hv = NULL;
      }
    }

    if(hv != NULL)
      variant_destroy(hv);

  } else {
    hls_add_variant(&h, h.h_baseurl, NULL, &h.h_primary, "single");
  }


  check_audio_only(&h.h_primary);

  hls_dump(&h);

  event_t *e = hls_play(&h, mp, errbuf, errlen, va0);

  hls_demuxer_close(mp, &h.h_primary);
  hls_demuxer_close(mp, &h.h_audio);

  media_codec_deref(h.h_codec_h264);

  hls_free_audio_tracks(&h);
  assert(LIST_FIRST(&h.h_discontinuity_segments) == NULL);
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

  mp_set_url(mp, va0->canonical_url, va0->parent_url, va0->parent_title);

  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);

  url += strlen("hls:");
  if(!strcmp(url, "test"))
    url = TESTURL;

  char *baseurl = NULL;

  buf = fa_load(url,
                FA_LOAD_ERRBUF(errbuf, errlen),
                FA_LOAD_FLAGS(FA_COMPRESSION),
                FA_LOAD_CANCELLABLE(mp->mp_cancellable),
                FA_LOAD_LOCATION(&baseurl),
                NULL);
  if(buf == NULL) {
    free(baseurl);
    return NULL;
  }
  buf = buf_make_writable(buf);
  char *s = buf_str(buf);
  event_t *e;

  mp->mp_reset_time = va0->load_request_timestamp;
  mp->mp_reset_epoch = mp->mp_epoch;

  if(s == NULL) {
    snprintf(errbuf, errlen, "Playlist contains no data");
    e = NULL;
  } else {
    e = hls_play_extm3u(s, baseurl, mp, errbuf, errlen, vq, vsl, va0);
  }
  buf_release(buf);
  free(baseurl);
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
static int
hls_open(prop_t *page, const char *url, int sync)
{
  usage_page_open(sync, "HLS");
  return backend_open_video(page, url, sync);
}


/**
 *
 */
static backend_t be_hls = {
  .be_canhandle = hls_canhandle,
  .be_open = hls_open,
  .be_play_video = hls_playvideo,
};

BE_REGISTER(hls);
