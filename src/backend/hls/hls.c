/*
 *  HLS player
 *  Copyright (C) 2013 Andreas Öman
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
#include "video/video_playback.h"
#include "video/video_settings.h"
#include "metadata/metadata.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_libav.h"

#define TESTURL "https://devimages.apple.com.edgekey.net/resources/http-streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8"
#define TESTURL2 "http://svtplay7k-f.akamaihd.net/i/world//open/20130127/1244301-001A/MOLANDERS-001A-7c59babd66879169_,900,348,564,1680,2800,.mp4.csmil/master.m3u8"


#include "misc/queue.h"

LIST_HEAD(hls_variant_list, hls_variant);
TAILQ_HEAD(hls_segment_queue, hls_segment);

typedef struct hls_segment {
  TAILQ_ENTRY(hls_segment) hs_link;
  char *hs_url;
  int hs_byte_offset;
  int hs_byte_size;
  int64_t hs_duration; // in usec
  int hs_seq;
} hls_segment_t;


typedef struct hls_variant {
  LIST_ENTRY(hls_variant) hv_link;
  char *hv_url;

  struct hls_segment_queue hv_segments;

  int hv_frozen;

  time_t hv_loaded; /* last time it was loaded successfully
                     *  0 means not loaded
                     */

  int hv_program;
  int hv_bitrate;

  int hv_width;
  int hv_height;
  const char *hv_subs_group;
  const char *hv_audio_group;

  int64_t hv_duration;

} hls_variant_t;


typedef struct hls {
  const char *h_baseurl;
  struct hls_variant_list h_variants;

  hls_variant_t *h_pend;

  struct media_codec *acodec;
  int astream;

  struct media_codec *vcodec;
  int vstream;

} hls_t;



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
hv_add_segment(hls_t *h, hls_variant_t *hv, const char *url)
{
  hls_segment_t *hs = calloc(1, sizeof(hls_segment_t));
  hs->hs_url = url_resolve_relative_from_base(hv->hv_url, url);
  TAILQ_INSERT_TAIL(&hv->hv_segments, hs, hs_link);
  return hs;
}


/**
 *
 */
static hls_segment_t *
hv_find_segment(hls_variant_t *hv, int seq)
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
static void
variant_update(hls_t *h, hls_variant_t *hv)
{
  if(hv->hv_frozen)
    return;

  buf_t *b = fa_load(hv->hv_url, NULL, NULL, 0, NULL, 0, NULL, NULL);

  b = buf_make_writable(b);
  char *s = buf_str(b);

  int l;
  double total_duration = 0;
  double duration = 0;
  int byte_offset = -1;
  int byte_size = -1;
  int seq = 1;
  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    s[l] = 0;
    if(l == 0)
      continue;
    const char *v;
    printf("HLS-Variant: %s\n", s);
    if((v = mystrbegins(s, "#EXTINF:")) != NULL) {
      duration = my_str2double(v, NULL);
    } else if((v = mystrbegins(s, "#EXT-X-ENDLIST")) != NULL) {
      hv->hv_frozen = 1;
    } else if((v = mystrbegins(s, "#EXT-X-MEDIA_SEQUENCE:")) != NULL) {
      seq = atoi(v);
    } else if((v = mystrbegins(s, "#EXT-X-BYTERANGE:")) != NULL) {
      byte_size = atoi(v);
      const char *o = strchr(v, '@');
      if(o != NULL)
        byte_offset = atoi(o+1);

    } else if(s[0] != '#') {
      hls_segment_t *hs = hv_add_segment(h, hv, s);
      hs->hs_byte_offset = byte_offset;
      hs->hs_byte_size   = byte_size;
      total_duration += duration;
      hs->hs_duration    = duration * 1000000LL;
      hs->hs_seq = seq++;
      duration = 0;
      byte_offset = -1;
      byte_size = -1;
    }
  }

  hv->hv_duration = total_duration * 1000000LL;


  buf_release(b);
}


/**
 *
 */
static void
hls_ext_x_media(hls_t *h, const char *V)
{
  //  char *v = mystrdupa(V);

}



/**
 *
 */
static void
hls_ext_x_stream_inf(hls_t *h, const char *V)
{
  char *v = mystrdupa(V);

  if(h->h_pend != NULL)
    free(h->h_pend);

  hls_variant_t *hv = h->h_pend = variant_create();

  while(*v) {
    const char *key, *value;
    v = get_attrib(v, &key, &value);
    if(v == NULL)
      break;

    if(!strcmp(key, "BANDWIDTH"))
      hv->hv_bitrate = atoi(value);
    else if(!strcmp(key, "AUDIO"))
      hv->hv_audio_group = value;
    else if(!strcmp(key, "SUBS"))
      hv->hv_subs_group = value;
    else if(!strcmp(key, "PROGRAM-ID"))
      hv->hv_program = atoi(value);
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
static int
variant_cmp(const hls_variant_t *a, const hls_variant_t *b)
{
  return b->hv_bitrate - a->hv_bitrate;
}


/**
 *
 */
static void
hls_add_variant(hls_t *h, const char *url)
{
  if(h->h_pend == NULL)
    h->h_pend = variant_create();

  h->h_pend->hv_url = url_resolve_relative_from_base(h->h_baseurl, url);
  LIST_INSERT_SORTED(&h->h_variants, h->h_pend, hv_link, variant_cmp);
  h->h_pend = NULL;
}


/**
 *
 */
static void
hls_dump(const hls_t *h)
{
  const hls_variant_t *hv;
  printf("HLS DUMP\n");
  printf("  base URL: %s\n", h->h_baseurl);
  printf("\n");
  printf("Variants\n");
  LIST_FOREACH(hv, &h->h_variants, hv_link) {
    printf("    %s\n", hv->hv_url);
    printf("    bitrate:    %d\n", hv->hv_bitrate);
    printf("    resolution: %d x %d\n", hv->hv_width, hv->hv_height);
    printf("\n");
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
static event_t *
play_segment(hls_t *h, media_pipe_t *mp, hls_segment_t *hs)
{
  AVInputFormat *fmt = av_find_input_format("mpegts");
  AVFormatContext *fctx;
  char errbuf[256];
  AVIOContext *avio;
  fa_handle_t *fh;
  int err;
  int retry_cnt = 0;

 again:
  if(retry_cnt == 20) {
    TRACE(TRACE_INFO, "HLS", "Unable to open segment %s -- Too many attempts",
          hs->hs_url);
    return NULL;

  }

  retry_cnt++;

  fh = fa_open_ex(hs->hs_url, errbuf, sizeof(errbuf), FA_BUFFERED_BIG, NULL);
  if(fh == NULL) {
    TRACE(TRACE_INFO, "HLS", "Unable to open segment %s -- %s",
          hs->hs_url, errbuf);
    usleep(200000);
    goto again;
  }

  avio = fa_libav_reopen(fh);

  fctx = avformat_alloc_context();
  fctx->pb = avio;

  if((err = avformat_open_input(&fctx, hs->hs_url, fmt, NULL)) != 0) {
    TRACE(TRACE_ERROR, "HLS", "Unable to open TS file %d", err);
    goto again;
  }
  fctx->flags |= AVFMT_FLAG_NOFILLIN;

  int i;

  media_format_t *fw = media_format_create(fctx);
  h->vstream = -1;
  h->astream = -1;

  for(i = 0; i < fctx->nb_streams; i++) {
    AVCodecContext *ctx = fctx->streams[i]->codec;

    switch(ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      if(h->vstream == -1) {
#if 0
        if(h->vcodec != NULL)
          media_codec_deref(h->vcodec);
#endif
        if(h->vcodec == NULL)
          h->vcodec = media_codec_create(ctx->codec_id, 0, fw, ctx, NULL, mp);
        h->vstream = i;
      }
      break;

    case AVMEDIA_TYPE_AUDIO:
      if(h->astream == -1) {
        if(h->acodec == NULL)
          h->acodec = media_codec_create(ctx->codec_id, 0, fw, ctx, NULL, mp);
        h->astream = i;
      }
      break;

    default:
      break;
    }
  }

  media_queue_t *mq = NULL;
  media_buf_t *mb = NULL;
  event_t *e = NULL;
  while(1) {

    if(mb == NULL) {
      AVPacket pkt;
      int r = av_read_frame(fctx, &pkt);
      if(r == AVERROR(EAGAIN))
        continue;

      if(r) {
        e = event_create_type(EVENT_EOF);
        break;
      }

      int si = pkt.stream_index;

      if(si == h->vstream && h->vcodec) {
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
        mb->mb_cw = media_codec_ref(h->vcodec);
        mb->mb_stream = 0;

      } else if(si == h->astream && h->acodec) {

        mb = media_buf_from_avpkt_unlocked(mp, &pkt);
        mb->mb_data_type = MB_AUDIO;
        mq = &mp->mp_audio;

        mb->mb_cw = media_codec_ref(h->acodec);
        mb->mb_stream = 1;

      } else {
        /* Check event queue ? */
        av_free_packet(&pkt);
        continue;
      }

      mb->mb_pts = rescale(fctx, pkt.pts, si);
      mb->mb_dts = rescale(fctx, pkt.dts, si);

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

      mb->mb_stream = pkt.stream_index;

      if(mb->mb_data_type == MB_VIDEO) {
        mb->mb_drive_clock = 1;
        if(fctx->start_time != AV_NOPTS_VALUE)
          mb->mb_delta = fctx->start_time;
      }

      mb->mb_keyframe = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }
    event_t *e;

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_CURRENT_TIME)) {
#if 0
      event_ts_t *ets = (event_ts_t *)e;
      int sec = ets->ts / 1000000;

      printf("sec=%d %lld\n", sec, ets->ts);

#if 0
      last_timestamp_presented = ets->ts;

      // Update restartpos every 5 seconds
      if(sec < restartpos_last || sec >= restartpos_last + 5) {
	restartpos_last = sec;
	metadb_set_video_restartpos(canonical_url, ets->ts / 1000);
      }
      if(sec != lastsec) {
	lastsec = sec;
	update_seek_index(sidx, sec);
	update_seek_index(cidx, sec);
      }
#endif
#endif
    }

    event_release(e);
  }
  media_format_deref(fw);
  return e;
}



/**
 *
 */
static event_t *
hls_play(hls_t *h, media_pipe_t *mp, char *errbuf, size_t errlen,
         const video_args_t *va)
{
  mp->mp_video.mq_stream = 0;
  mp->mp_audio.mq_stream = 1;

  if(!(va->flags & BACKEND_VIDEO_NO_AUDIO))
    mp_become_primary(mp);

  prop_set_string(mp->mp_prop_type, "video");

  mp_set_playstatus_by_hold(mp, 0, NULL);

  hls_variant_t *hv = LIST_FIRST(&h->h_variants);

  int seq = 1;
  while(1) {
    hls_segment_t *hs;

    variant_update(h, hv);

    mp_configure(mp, MP_PLAY_CAPS_SEEK | MP_PLAY_CAPS_PAUSE,
                 MP_BUFFER_DEEP, hv->hv_duration);

    char buf[256];
    snprintf(buf, sizeof(buf), "HLS %d Kbps",  hv->hv_bitrate / 1000);
    prop_set(mp->mp_prop_metadata, "format", PROP_SET_STRING, buf);

    hs = hv_find_segment(hv, seq);
    if(hs == NULL)
      break;
    seq++;
    play_segment(h, mp, hs);

    hv = LIST_NEXT(hv, hv_link);
    if(hv == NULL)
      hv = LIST_FIRST(&h->h_variants);
  }

  return NULL;
}






/**
 *
 */
static event_t *
hls_playvideo2(buf_t *b, const char *url, media_pipe_t *mp,
               char *errbuf, size_t errlen,
               video_queue_t *vq, struct vsource_list *vsl,
               const video_args_t *va0)
{
  b = buf_make_writable(b);

  char *s = buf_str(b);

  if(!mystrbegins(s, "#EXTM3U")) {
    snprintf(errbuf, errlen, "Not an m3u file");
    return NULL;
  }

  hls_t h;
  memset(&h, 0, sizeof(h));
  h.h_baseurl = url;
  int l;
  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    s[l] = 0;
    if(l == 0)
      continue;
    const char *v;
    printf("HLS: %s\n", s);
    if((v = mystrbegins(s, "#EXT-X-MEDIA:")) != NULL)
      hls_ext_x_media(&h, v);
    else if((v = mystrbegins(s, "#EXT-X-STREAM-INF:")) != NULL)
      hls_ext_x_stream_inf(&h, v);
    else if(s[0] != '#') {
      hls_add_variant(&h, s);
    }
  }
  hls_dump(&h);

  hls_play(&h, mp, errbuf, errlen, va0);


  buf_release(b);
  snprintf(errbuf, errlen, "Not implemented");
  return NULL;

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
  buf = fa_load(url, NULL, errbuf, errlen, NULL, 0, NULL, NULL);

  if(buf == NULL)
    return NULL;

  return hls_playvideo2(buf, url, mp, errbuf, errlen, vq, vsl, va0);
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
