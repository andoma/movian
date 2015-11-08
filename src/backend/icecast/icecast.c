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

#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>

#include "navigator.h"
#include "backend/backend.h"
#include "media/media.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_proto.h"
#include "fileaccess/fa_libav.h"
#include "misc/str.h"
#include "misc/cancellable.h"
#include "htsmsg/htsmsg_xml.h"
#include "networking/http.h"
#include "usage.h"
#include "misc/minmax.h"

TAILQ_HEAD(icecast_source_queue, icecast_source);

typedef struct icecast_source {
  TAILQ_ENTRY(icecast_source) is_link;
  char *is_url;
  int is_dead;
} icecast_source_t;


/**
 *
 */
typedef struct icecast_play_context {
  const char *ipc_url;
  int ipc_hold; // Set if paused
  media_pipe_t *ipc_mp;
  media_format_t *ipc_mf;
  media_codec_t *ipc_mc;

  struct icecast_source_queue ipc_sources;
  int ipc_nsources;

  struct http_header_list ipc_request_headers;
  struct http_header_list ipc_response_headers;

  prop_t *ipc_radio_info;

  char ipc_streaminfo_set;

} icecast_play_context_t;


static fa_handle_t *icy_meta_parser(icecast_play_context_t *ipc,
                                    fa_handle_t *src, int stride);

/**
 *
 */
static void
flush_sources(icecast_play_context_t *ipc)
{
  icecast_source_t *is, *next;
  for(is = TAILQ_FIRST(&ipc->ipc_sources); is != NULL; is = next) {
    next = TAILQ_NEXT(is, is_link);
    free(is->is_url);
    free(is);
  }
  TAILQ_INIT(&ipc->ipc_sources);
  ipc->ipc_nsources = 0;
}


/**
 *
 */
static void
add_source(icecast_play_context_t *ipc, const char *url)
{
    icecast_source_t *is = malloc(sizeof(icecast_source_t));
    is->is_url = url_resolve_relative_from_base(ipc->ipc_url, url);
    is->is_dead = 0;
    TAILQ_INSERT_TAIL(&ipc->ipc_sources, is, is_link);
    ipc->ipc_nsources++;
}


/**
 *
 */
static void
parse_pls(icecast_play_context_t *ipc, char *buf)
{
  flush_sources(ipc);

  LINEPARSE(line, buf) {
    if(strncasecmp(line, "file", 4))
      continue;
    if((line = strchr(line + 4, '=')) == NULL)
      continue;
    add_source(ipc, line + 1);
  }
}


/**
 *
 */
static void
parse_m3u(icecast_play_context_t *ipc, char *buf)
{
  LINEPARSE(line, buf) {
    if(*line != '#')
      add_source(ipc, line);
  }
}


/**
 *
 */
static void
parse_xspf(icecast_play_context_t *ipc, buf_t *b)
{
  char errbuf[512];
  htsmsg_t *m = htsmsg_xml_deserialize_buf(b, errbuf, sizeof(errbuf));
  if(m == NULL) {
    TRACE(TRACE_ERROR, "Radio", "Unable to parse XSPF -- %s", errbuf);
    return;
  }

  htsmsg_t *list = htsmsg_get_map_multi(m, "playlist", "trackList", NULL);

  if(list != NULL) {
    htsmsg_field_t *f;
    HTSMSG_FOREACH(f, list) {
      htsmsg_t *t;
      if((t = htsmsg_get_map_by_field_if_name(f, "track")) == NULL)
        continue;
      const char *loc = htsmsg_get_str(t, "location");
      if(loc != NULL)
        add_source(ipc, loc);
    }
  }
  htsmsg_release(m);
}


/**
 *
 */
static int
num_deads(const icecast_play_context_t *ipc)
{
  int num_dead = 0;
  const icecast_source_t *is;

  TAILQ_FOREACH(is, &ipc->ipc_sources, is_link)
    if(is->is_dead)
      num_dead++;
  return num_dead;
}

/**
 *
 */
static int
open_stream(icecast_play_context_t *ipc)
{
  char errbuf[256];
  char pbuf[256];
  AVFormatContext *fctx;
  int num_dead = 0;
  fa_handle_t *fh = NULL;
  icecast_source_t *is;
  const char *url;

  const int flags = FA_STREAMING | FA_NO_RETRIES |
    FA_BUFFERED_NO_PREFETCH |
    FA_BUFFERED_SMALL | FA_NO_PARKING;

 again:
  num_dead = num_deads(ipc);

  assert(num_dead <= ipc->ipc_nsources);

  fa_open_extra_t foe = {
    .foe_request_headers  = &ipc->ipc_request_headers,
    .foe_response_headers = &ipc->ipc_response_headers,
    .foe_cancellable =       ipc->ipc_mp->mp_cancellable,
  };

  if(num_dead == ipc->ipc_nsources) {

    // All sources are dead, or we don't have any sources (yet)

    fh = fa_open_ex(ipc->ipc_url, errbuf, sizeof(errbuf), flags, &foe);

    if(fh == NULL) {

      if(cancellable_is_cancelled(ipc->ipc_mp->mp_cancellable))
        return -1;

      TRACE(TRACE_ERROR, "Radio", "Unable to open %s -- %s",
            ipc->ipc_url, errbuf);
      return -1;
    }

    const char *ct = http_header_get(&ipc->ipc_response_headers,
                                     "content-type");

    int is_m3u = 0;
    int is_pls = 0;
    int is_xspf = 0;

    if(ct != NULL)
      TRACE(TRACE_DEBUG, "Radio", "%s content-type: %s", ipc->ipc_url, ct);

    if(ct != NULL && !strcasecmp(ct, "application/xspf+xml")) {
      is_xspf = 1;
      goto load;
    }

    int r = fa_read(fh, pbuf, sizeof(pbuf)-1);
    if(r < 0) {
      TRACE(TRACE_ERROR, "Radio", "Unable to probe %s",
            ipc->ipc_url);
      return -1;
    }
    pbuf[r] = 0;

    if(ct != NULL &&
       (!strcasecmp(ct, "application/x-mpegurl") ||
        !strcasecmp(ct, "audio/x-mpegurl"))) {
      TRACE(TRACE_DEBUG, "Radio",
            "%s is an .m3u playlist according to content-type", ipc->ipc_url);
      is_m3u = 1;
    }

    if(r > 5 && !memcmp(pbuf, "<?xml", 5)) {
      TRACE(TRACE_DEBUG, "Radio", "%s is a XSPF playlist based on content",
            ipc->ipc_url);
      is_xspf = 1;
    }

    if(r > 10 && !memcmp(pbuf, "[playlist]", 10)) {
      // The URL points to a playlist, parse it
      TRACE(TRACE_DEBUG, "Radio", "%s is a .pls playlist based on content",
            ipc->ipc_url);
      is_pls = 1;
    } else if(strstr(pbuf, "http://") || strstr(pbuf, "https://")) {
      TRACE(TRACE_DEBUG, "Radio", "%s guessed to be an m3u based on content",
            ipc->ipc_url);
      is_m3u = 1;
    }

 load:
    if(is_pls || is_m3u || is_xspf) {

      buf_t *b = fa_load_and_close(fh);
      if(b == NULL) {
        TRACE(TRACE_ERROR, "Radio", "Unable to read playlist %s",
              ipc->ipc_url);
        return -1;
      }

      flush_sources(ipc);

      if(is_xspf) {
        parse_xspf(ipc, b);
        b = NULL;
      } else if(is_pls) {
        parse_pls(ipc, buf_str(b));
      } else {
        parse_m3u(ipc, buf_str(b));
      }
      buf_release(b);


      if(ipc->ipc_nsources == 0) {
        TRACE(TRACE_ERROR, "Radio", "No files found in playlist %s",
              ipc->ipc_url);
        return -1;
      }

      goto again;
    }
    fa_seek(fh, 0, SEEK_SET);
    url = ipc->ipc_url;

  } else {

    int r = rand() % (ipc->ipc_nsources - num_dead);
    TAILQ_FOREACH(is, &ipc->ipc_sources, is_link) {
      if(is->is_dead)
        continue;
      if(!r--)
        break;
    }

    assert(is != NULL);
    url = is->is_url;
    fh = fa_open_ex(url, errbuf, sizeof(errbuf), flags, &foe);

    if(fh == NULL) {
      is->is_dead = 1;
      if(cancellable_is_cancelled(ipc->ipc_mp->mp_cancellable))
        return -1;


      TRACE(TRACE_INFO, "Radio", "Unable to open %s -- %s, trying another URL",
            ipc->ipc_url, errbuf);

      num_dead = num_deads(ipc);
      if(num_dead == ipc->ipc_nsources)
        return -1;

      goto again;
    }
  }

  fa_set_read_timeout(fh, 10000);

  const char *mx = http_header_get(&ipc->ipc_response_headers, "icy-metaint");
  if(mx != NULL) {
    int stride = atoi(mx);
    if(stride)
      fh = icy_meta_parser(ipc, fh, stride);
  }

  AVIOContext *avio = fa_libav_reopen(fh, 1);

  if(avio == NULL) {
    fa_close(fh);
    return -1;
  }

  const char *ct = http_header_get(&ipc->ipc_response_headers, "content-type");

  if((fctx = fa_libav_open_format(avio, url, errbuf, sizeof(errbuf), ct,
                                  4096, 0, -1)) == NULL) {

    if(!cancellable_is_cancelled(ipc->ipc_mp->mp_cancellable)) {
      TRACE(TRACE_ERROR, "Radio", "Unable to open %s -- %s",
            ipc->ipc_url, errbuf);
    }

    fa_libav_close(avio);
    return -1;
  }

  TRACE(TRACE_DEBUG, "Radio", "Starting playback of %s", url);

  mp_configure(ipc->ipc_mp,
               MP_CAN_PAUSE | MP_FLUSH_ON_HOLD | MP_ALWAYS_SATISFIED,
               MP_BUFFER_SHALLOW, 0, "radio");

  ipc->ipc_mp->mp_audio.mq_stream = -1;
  ipc->ipc_mp->mp_video.mq_stream = -1;

  ipc->ipc_mf = media_format_create(fctx);
  ipc->ipc_mc = NULL;

  for(int i = 0; i < fctx->nb_streams; i++) {
    AVCodecContext *ctx = fctx->streams[i]->codec;

    if(ctx->codec_type != AVMEDIA_TYPE_AUDIO)
      continue;

    ipc->ipc_mc = media_codec_create(ctx->codec_id, 0, ipc->ipc_mf, ctx, NULL,
                                     ipc->ipc_mp);
    ipc->ipc_mp->mp_audio.mq_stream = i;
    break;
  }

  if(ipc->ipc_mc == NULL) {
    media_format_deref(ipc->ipc_mf);
    ipc->ipc_mf = NULL;
    TRACE(TRACE_ERROR, "Radio", "Unable to open %s -- No audio stream", url);
    return -1;
  }

  mp_become_primary(ipc->ipc_mp);
  ipc->ipc_streaminfo_set = 0;
  return 0;
}


/**
 *
 */
static void
close_stream(icecast_play_context_t *ipc)
{
  if(ipc->ipc_mc != NULL)
    media_codec_deref(ipc->ipc_mc);
  if(ipc->ipc_mf != NULL)
    media_format_deref(ipc->ipc_mf);
  ipc->ipc_mc = NULL;
  ipc->ipc_mf = NULL;
  flush_sources(ipc);
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
static event_t *
stream_radio(icecast_play_context_t *ipc, char *errbuf, size_t errlen)
{
  AVPacket pkt;
  int r, si;
  media_buf_t *mb = NULL;
  media_queue_t *mq;
  event_t *e;
  media_pipe_t *mp = ipc->ipc_mp;
  mq = &mp->mp_audio;

  usage_event("Play audio", 1, USAGE_SEG("format", "icecast"));

  ipc->ipc_radio_info = prop_create(mp->mp_prop_root, "radioinfo");

  TAILQ_INIT(&ipc->ipc_sources);

  http_header_add(&ipc->ipc_request_headers, "Icy-MetaData", "1", 1);

  int loading = 0;

  while(1) {

    /**
     * Need to fetch a new packet ?
     */
    if(mb == NULL) {

      if(ipc->ipc_mf == NULL) {

        if(ipc->ipc_hold) {
          e = mp_dequeue_event(mp);
          goto handle_event;
        }

        prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);
        loading = 1;

        if(open_stream(ipc)) {
          e = mp_dequeue_event_deadline(mp, 1000);
          if(e != NULL)
            goto handle_event;
          continue;
        }
      }

      mp->mp_eof = 0;
      r = av_read_frame(ipc->ipc_mf->fctx, &pkt);
      if(r == AVERROR(EAGAIN))
	continue;


      if(r != 0) {
        if(r != AVERROR_EOF) {
          char msg[100];
          fa_libav_error_to_txt(r, msg, sizeof(msg));
          TRACE(TRACE_ERROR, "Radio", "Playback error: %s (%d)", msg, r);
        }
        close_stream(ipc);

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

	if(e == NULL || r == AVERROR_EOF) {
	  e = event_create_type(EVENT_EOF);
          break;
        }
        if(e != NULL)
          break;
        continue;
      }

      si = pkt.stream_index;

      if(si != mp->mp_audio.mq_stream) {
	av_free_packet(&pkt);
	continue;
      }

      mb = media_buf_alloc_unlocked(mp, pkt.size);
      mb->mb_data_type = MB_AUDIO;

      mb->mb_pts      = rescale(ipc->ipc_mf->fctx, pkt.pts,      si);
      mb->mb_dts      = rescale(ipc->ipc_mf->fctx, pkt.dts,      si);
      mb->mb_duration = rescale(ipc->ipc_mf->fctx, pkt.duration, si);

      mb->mb_cw = media_codec_ref(ipc->ipc_mc);

      /* Move the data pointers from libav's packet */

      mb->mb_stream = pkt.stream_index;

      memcpy(mb->mb_data, pkt.data, pkt.size);

      if(mb->mb_pts != AV_NOPTS_VALUE) {
        const int64_t offset = ipc->ipc_mf->fctx->start_time;
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
      if(loading) {
        prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 0);
        loading = 0;
      }
      continue;
    }

  handle_event:
    if(event_is_type(e, EVENT_HOLD)) {

      event_int_t *ei = (event_int_t *)e;

      ipc->ipc_hold = ei->val;
      if(ipc->ipc_hold && ipc->ipc_mf != NULL) {
        close_stream(ipc);

        if(mb != NULL && mb != MB_SPECIAL_EOF) {
          media_buf_free_unlocked(mp, mb);
          mb = NULL;
        }
      }

    } else if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) ||
	     event_is_action(e, ACTION_SKIP_BACKWARD) ||
	     event_is_action(e, ACTION_SKIP_FORWARD) ||
	     event_is_action(e, ACTION_STOP)) {
      mp_flush(mp);
      break;
    }
    event_release(e);
  }

  prop_set_void(ipc->ipc_radio_info);

  if(mb != NULL && mb != MB_SPECIAL_EOF)
    media_buf_free_unlocked(mp, mb);

  close_stream(ipc);
  http_headers_free(&ipc->ipc_request_headers);
  http_headers_free(&ipc->ipc_response_headers);

  return e;
}



/**
 *
 */
static void *
icecast_thread(void *aux)
{
  icecast_play_context_t *ipc = aux;
  char errbuf[512];
  ipc->ipc_mp = mp_create("radio", MP_PRIMABLE);
  if(stream_radio(ipc, errbuf, sizeof(errbuf)) == NULL) {
    TRACE(TRACE_ERROR, "Radio", "Error: %s", errbuf);
  }
  mp_shutdown(ipc->ipc_mp);
  mp_destroy(ipc->ipc_mp);

  free((void *)ipc->ipc_url);
  free(ipc);
  return NULL;
}


/**
 *
 */
static int
icecast_open(prop_t *page, const char *url, int sync)
{
  icecast_play_context_t *ipc = calloc(1, sizeof(icecast_play_context_t));
  ipc->ipc_url = strdup(url + strlen("icecast:"));
  usage_page_open(sync, "Icecast");
  hts_thread_create_detached("icecast", icecast_thread, ipc,
			     THREAD_PRIO_MODEL);
  return 0;
}


/**
 *
 */
static int
icecast_canhandle(const char *url)
{
  return !strncmp(url, "icecast:", strlen("icecast:"));
}


/**
 *
 */
static event_t *
icecast_play_audio(const char *url, media_pipe_t *mp,
		  char *errbuf, size_t errlen, int hold, const char *mimetype)
{
  icecast_play_context_t ipc = {0};
  ipc.ipc_url = url + strlen("icecast:");
  ipc.ipc_mp = mp;
  return stream_radio(&ipc, errbuf, sizeof(errlen));
}


/**
 *
 */
static backend_t be_icecast = {
  .be_canhandle  = icecast_canhandle,
  .be_open       = icecast_open,
  .be_play_audio = icecast_play_audio,
};

BE_REGISTER(icecast);


/**
 *
 */
typedef struct icymeta {
  fa_handle_t h;
  fa_handle_t *s_src;
  int stride;
  int remain;
  icecast_play_context_t *ipc;
} icymeta_t;


/**
 *
 */
static void
icymeta_close(fa_handle_t *h)
{
  icymeta_t *s = (icymeta_t *)h;
  fa_close(s->s_src);
  free(s);
}


/**
 *
 */
static int64_t
icymeta_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  return -1;
}


/**
 *
 */
static int64_t
icymeta_fsize(fa_handle_t *handle)
{
  return -1;
}



/**
 *
 */

static void
icymeta_parse(icecast_play_context_t *ipc, const char *buf)
{
  if(gconf.enable_icecast_debug)
    hexdump("icymeta", buf, strlen(buf));

  const char *title = mystrstr(buf, "StreamTitle='");
  if(title != NULL) {
    title += strlen("StreamTitle='");
    const char *end = strstr(title, "';");
    if(end != NULL) {
      char how[128];
      int tlen = end - title;
      rstr_t *t = rstr_from_bytes_len(title, tlen, how, sizeof(how));

      if(gconf.enable_icecast_debug)
        TRACE(TRACE_DEBUG, "Radio", "Title decoded as '%s' to '%s'",
              how, rstr_get(t));

      const char *title_tag = strstr(rstr_get(t), "<mus_sng_title>");
      if(title_tag != NULL) {
        title_tag += strlen("<mus_sng_title>");
        const char *title_tag_end = strstr(title_tag, "</mus_sng_title>");
        if(title_tag_end != NULL) {
          rstr_t *n = rstr_allocl(title_tag, title_tag_end - title_tag);
          rstr_release(t);
          t = n;
        }
      }

      if(!ipc->ipc_streaminfo_set) {
        prop_set_rstring(ipc->ipc_radio_info, t);
        ipc->ipc_streaminfo_set = 1;
      } else {
        mp_send_prop_set_string(ipc->ipc_mp, &ipc->ipc_mp->mp_audio,
                                ipc->ipc_radio_info, rstr_get(t));
      }
      rstr_release(t);
    }
  }
}


/**
 *
 */
static int
icymeta_read(fa_handle_t *handle, void *buf, size_t size)
{
  icymeta_t *s = (icymeta_t *)handle;
  int offset = 0;

  while(size) {

    if(s->remain == 0) {
      char length;
      if(fa_read(s->s_src, &length, 1) != 1)
        return -1;

      if(length) {
        int mlen = length * 16;
        char *buf = malloc(mlen + 1);
        buf[mlen] = 0;
        if(fa_read(s->s_src, buf, mlen) != mlen)
          return -1;

        icymeta_parse(s->ipc, buf);
        free(buf);
      }
      s->remain = s->stride;
    }

    int to_read = MIN(size, s->remain);

    int r = fa_read(s->s_src, buf + offset, to_read);
    if(r != to_read)
      return -1;

    offset += r;
    size -= r;
    s->remain -= r;
  }
  return offset;
}


/**
 *
 */
static fa_protocol_t fa_protocol_icymeta = {
  .fap_name  = "icymeta",
  .fap_close = icymeta_close,
  .fap_read  = icymeta_read,
  .fap_seek  = icymeta_seek,
  .fap_fsize = icymeta_fsize,
};


static fa_handle_t *
icy_meta_parser(icecast_play_context_t *ipc, fa_handle_t *src, int stride)
{
  icymeta_t *s = calloc(1, sizeof(icymeta_t));
  s->h.fh_proto = &fa_protocol_icymeta;
  s->remain = stride;
  s->stride = stride;
  s->s_src = src;
  s->ipc = ipc;
  return &s->h;

}
