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

#include <librtmp/rtmp.h>
#include <librtmp/log.h>

#include <libavcodec/avcodec.h>

#include "navigator.h"
#include "backend/backend.h"
#include "media/media.h"
#include "main.h"
#include "i18n.h"
#include "misc/isolang.h"
#include "video/video_playback.h"
#include "video/video_settings.h"
#include "subtitles/subtitles.h"
#include "metadata/playinfo.h"
#include "usage.h"

typedef struct {

  media_pipe_t *mp;

  int vframeduration;
  
  media_codec_t *vcodec;
  media_codec_t *acodec;

  RTMP *r;

  int in_seek_skip;

  int width;
  int height;

  int64_t seekpos_video;
  int64_t seekpos_audio;

  int restartpos_last;

  const char *canonical_url;

  int total_duration;  // in ms

  sub_scanner_t *ss;
  const video_args_t *va;
  const char *url;

  int is_loading;

} rtmp_t;


/**
 *
 */
static int
rtmp_canhandle(const char *url)
{
  return 
    !strncmp(url, "rtmp://", strlen("rtmp://")) ||
    !strncmp(url, "rtmpe://", strlen("rtmpe://"));
}



#define SAVC(x)	static AVal av_##x = { (char *)#x, sizeof(#x)-1};

SAVC(onMetaData);
SAVC(duration);
SAVC(videoframerate)
SAVC(framerate)
SAVC(width);
SAVC(height);

static int
handle_metadata0(rtmp_t *r, AMFObject *obj,
		 media_pipe_t *mp, char *errstr, size_t errlen)
{
  AVal metastring;
  AMFObjectProperty prop;
  prop_t *m = mp->mp_prop_metadata;


  AMFProp_GetString(AMF_GetProp(obj, NULL, 0), &metastring);

  if(!AVMATCH(&metastring, &av_onMetaData)) {
    snprintf(errstr, errlen, "No metadata in metadata packet");
    return -1;
  }
  if(RTMP_FindFirstMatchingProperty(obj, &av_duration, &prop) &&
     prop.p_type == AMF_NUMBER && prop.p_vu.p_number > 0) {
    prop_set_float(prop_create(m, "duration"), prop.p_vu.p_number);
    r->total_duration = prop.p_vu.p_number * 1000;
    mp_set_duration(mp, r->total_duration * 1000LL);
    mp_set_clr_flags(mp, MP_CAN_SEEK, 0);

    if(r->ss == NULL && !(r->va->flags & BACKEND_VIDEO_NO_SUBTITLE_SCAN))
      r->ss = sub_scanner_create(r->url, mp->mp_prop_subtitle_tracks, r->va, 0);

  } else {

    r->total_duration = 0;
    mp_set_duration(mp, AV_NOPTS_VALUE);
    mp_set_clr_flags(mp, 0, MP_CAN_SEEK);
  }

  if((RTMP_FindFirstMatchingProperty(obj, &av_videoframerate, &prop) &&
      RTMP_FindFirstMatchingProperty(obj, &av_framerate, &prop))
     && prop.p_type == AMF_NUMBER) {
    r->vframeduration = 1000000.0 / prop.p_vu.p_number;
    mp->mp_framerate.num = 1000000;
    mp->mp_framerate.den = prop.p_vu.p_number;
  }
  r->width = r->height = 0;
  if(RTMP_FindFirstMatchingProperty(obj, &av_width, &prop) &&
     prop.p_type == AMF_NUMBER)
    r->width = prop.p_vu.p_number;

  if(RTMP_FindFirstMatchingProperty(obj, &av_height, &prop) &&
     prop.p_type == AMF_NUMBER)
    r->height = prop.p_vu.p_number;

  if(r->width && r->height)
    TRACE(TRACE_DEBUG, "RTMP", "Video size %d x %d", r->width, r->height);
  return 0;

}

static int
handle_metadata(rtmp_t *r, char *body, unsigned int len,
		media_pipe_t *mp, char *errstr, size_t errlen)
{
  AMFObject obj;
  int rval;
  if(AMF_Decode(&obj, body, len, 0) < 0) {
    snprintf(errstr, errlen, "Unable to decode metadata AMF");
    return -1;
  }

  rval = handle_metadata0(r, &obj, mp, errstr, errlen);
  AMF_Reset(&obj);
  return rval;
}



/**
 *
 */
static void
video_seek(rtmp_t *r, media_pipe_t *mp, media_buf_t **mbp,
	   int64_t pos, const char *txt)
{
  if(pos < 0)
    pos = 0;

  TRACE(TRACE_DEBUG, "Video", "seek %s to %.2f", txt, pos / 1000000.0);
 
  RTMP_SendSeek(r->r, pos / 1000);

  r->seekpos_video = pos;
  r->seekpos_audio = pos;

  mp->mp_video.mq_seektarget = pos;
  mp->mp_audio.mq_seektarget = pos;

  mp_flush(mp);

  if(mbp != NULL && *mbp != NULL) {
    media_buf_free_unlocked(mp, *mbp);
    *mbp = NULL;
  }

  prop_set_float(prop_create(mp->mp_prop_root, "seektime"), pos / 1000000.0);
}

/**
 *
 */
static event_t *
rtmp_process_event(rtmp_t *r, event_t *e, media_buf_t **mbp)
{
  media_pipe_t *mp = r->mp;

  if(event_is_type(e, EVENT_EXIT) ||
     event_is_type(e, EVENT_PLAY_URL) ||
     event_is_action(e, ACTION_SKIP_FORWARD) ||
     event_is_action(e, ACTION_SKIP_BACKWARD))
    return e;

  if(event_is_type(e, EVENT_CURRENT_TIME)) {
    event_ts_t *ets = (event_ts_t *)e;
    int sec = ets->ts / 1000000;

    if(sec != r->restartpos_last && mp->mp_flags & MP_CAN_SEEK) {
      r->restartpos_last = sec;
      playinfo_set_restartpos(r->canonical_url, mp->mp_seek_base / 1000, 1);
    }

  } else if(mp->mp_flags & MP_CAN_SEEK && event_is_type(e, EVENT_SEEK)) {
    event_ts_t *ets = (event_ts_t *)e;

    video_seek(r, mp, mbp, ets->ts, "direct");

  }
  event_release(e);
  return NULL;
}


/**
 *
 */
static event_t *
sendpkt(rtmp_t *r, media_queue_t *mq, media_codec_t *mc,
	int64_t dts, int64_t pts, const void *data, 
	size_t size, int skip, int dt, int duration, int drive_clock)
{
  event_t *e = NULL;
  media_buf_t *mb = media_buf_alloc_unlocked(r->mp, size);

  if(r->is_loading) {
    r->is_loading = 0;
    prop_set(r->mp->mp_prop_root, "loading", PROP_SET_INT, 0);
  }

  mb->mb_data_type = dt;
  mb->mb_duration = duration;
  mb->mb_cw = media_codec_ref(mc);
  mb->mb_drive_clock = drive_clock;
  mb->mb_dts = dts;
  mb->mb_pts = pts;
  mb->mb_user_time = pts;
  mb->mb_skip = skip;

  memcpy(mb->mb_data, data, size);

  do {

    if(mb == NULL || (e = mb_enqueue_with_events(r->mp, mq, mb)) == NULL) {
      mb = NULL;
      break;
    }

    e = rtmp_process_event(r, e, &mb);

  } while(e == NULL);

  if(mb != NULL)
    media_buf_free_unlocked(r->mp, mb);

  return e;
}


/**
 *
 */
static event_t *
get_packet_v(rtmp_t *r, uint8_t *data, int size, int64_t dts,
	     media_pipe_t *mp)
{
  uint8_t flags;
  uint8_t type = 0;
  enum AVCodecID id;
  int d = 0;
  event_t *e;

  if(r->r->m_read.flags & RTMP_READ_SEEKING)
    return NULL; 

  if(size < 2)
    return NULL;

  flags = *data++;
  size--;


  switch(flags & 0xf) {
  case 7:
    type = *data++;
    size--;
    id = AV_CODEC_ID_H264;

    if(size < 3)
      return NULL;
    
    d = (AMF_DecodeInt24((char *)data) + 0xff800000) ^ 0xff800000;
    data += 3;
    size -= 3;
    break;

  case 4:
    type = *data++;
    size--;
    id = AV_CODEC_ID_VP6F;
    break;
  default:
    return NULL;
  }

  if(r->vcodec == NULL) {
    media_codec_params_t mcp = {0};

    switch(id) {
    case AV_CODEC_ID_H264:
      if(type != 0 || size < 0)
	return NULL;

      mcp.extradata      = data;
      mcp.extradata_size = size;
      break;

    case AV_CODEC_ID_VP6F:
      if(size < 1)
	return NULL;
      mcp.extradata      = data;
      mcp.extradata_size = size;
      break;

    default:
      abort();
    }
    mcp.width = r->width;
    mcp.height = r->height;
    r->vcodec = media_codec_create(id, 0, NULL, NULL, &mcp, mp);
    return NULL;
  }


  int skip = 0;

  //  r->last_video_dts = dts;

  int64_t pts = 1000LL * (dts + d);
  dts = 1000LL * dts;

  if(d < 0 || dts <= r->seekpos_video) {
    skip = 1;
    r->in_seek_skip = 1;
  } else if(r->in_seek_skip) {
    skip = 2;
    r->in_seek_skip = 0;
  } else {
    r->seekpos_video = dts;
  }

  e = sendpkt(r, &r->mp->mp_video, r->vcodec, dts, pts,
	      data, size, skip, MB_VIDEO, r->vframeduration, 1);
  return e;
}


/**
 *
 */
static event_t *
get_packet_a(rtmp_t *r, uint8_t *data, int size, int64_t dts, 
	     media_pipe_t *mp)
{
  uint8_t flags;
  uint8_t type = 0;
  enum AVCodecID id;

  if(r->r->m_read.flags & RTMP_READ_SEEKING)
    return NULL; 

  if(size < 2)
    return NULL;

  flags = *data++;
  size--;

  switch(flags & 0xf0) {
  case 0xa0:   id = AV_CODEC_ID_AAC;
    type = *data++;
    size--;
    break;

  case 0x20:
    id = AV_CODEC_ID_MP3;
    break;

  default: 
    return NULL;
  }
    
  if(r->acodec == NULL) {
    media_codec_params_t mcp = {0};
    int parse = 0;
    const char *fmt;

    switch(id) {
      
    case AV_CODEC_ID_AAC:
      if(type != 0 || size < 0)
	return NULL;
	
      mcp.extradata      = data;
      mcp.extradata_size = size;
      fmt = "AAC";
      break;

    case AV_CODEC_ID_MP3:
      parse = 1;
      fmt = "MP3";
      break;

    default:
      abort();
    }

    mp_add_track(mp->mp_prop_audio_tracks,
		 NULL,
		 "rtmp:1",
		 fmt,
		 fmt,
		 NULL, 
		 NULL,
		 NULL,
		 0,
                 1);

    prop_set_string(mp->mp_prop_audio_track_current, "rtmp:1");
    prop_set_int(mp->mp_prop_audio_track_current_manual, 0);

    r->acodec = media_codec_create(id, parse, NULL, NULL, &mcp, mp);
    return NULL;
  }

  media_codec_t *mc = r->acodec;

  dts *= 1000;

  if(dts <= r->seekpos_audio)
    return NULL;

  r->seekpos_audio = dts;
  if(mc->parser_ctx == NULL)
    return sendpkt(r, &mp->mp_audio, mc, 
		   dts, dts, data, size, 0, MB_AUDIO, 0, 0);

  while(size > 0) {
    int outlen;
    uint8_t *outbuf;
    int rlen = av_parser_parse2(mc->parser_ctx,
				mc->fmt_ctx, &outbuf, &outlen, 
				data, size, dts, dts, AV_NOPTS_VALUE);
    if(outlen) {
      event_t *e = sendpkt(r, &mp->mp_audio, mc,
			   mc->parser_ctx->dts,
			   mc->parser_ctx->pts,
			   outbuf, outlen, 0,
			   MB_AUDIO, 0, 0);
      if(e != NULL)
	return e;
    }
    dts = AV_NOPTS_VALUE;
    data += rlen;
    size -= rlen;
  }
  return NULL;
}




/**
 *
 */
static event_t *
rtmp_loop(rtmp_t *r, media_pipe_t *mp, char *url, char *errbuf, size_t errlen)
{
  RTMPPacket p = {0};
  int pos = -1, ret;
  uint32_t dts;
  event_t *e = NULL;

  while(1) {


    if(pos == -1) {

      mp->mp_eof = 0;
      ret = RTMP_GetNextMediaPacket(r->r, &p);

      if(ret == 2) {
	/* Wait for queues to drain */
	mp->mp_eof = 1;
      again:
	e = mp_wait_for_empty_queues(mp);

	if(e != NULL) {
	  e = rtmp_process_event(r, e, NULL);
	  if(e == NULL)
	    goto again;
	}

	if(e == NULL)
	  e = event_create_type(EVENT_EOF);
	break;
      }

      if(ret == 0) {
	int64_t restartpos = r->seekpos_video;

        if(cancellable_is_cancelled(mp->mp_cancellable)) {
          snprintf(errbuf, errlen, "Cancelled");
          return NULL;
        }

	TRACE(TRACE_ERROR, "RTMP", "Disconnected");
	sleep(1);

	if(restartpos == AV_NOPTS_VALUE) {
	  snprintf(errbuf, errlen,
		   "Giving up restart since nothing was decoded");
	  return NULL;
	}


	RTMP_Close(r->r);

	RTMP_Init(r->r, mp->mp_cancellable);

	memset(&p, 0, sizeof(p));

	TRACE(TRACE_DEBUG, "RTMP", "Reconnecting stream at pos %"PRId64,
	      restartpos);

	if(!RTMP_SetupURL(r->r, url)) {
	  snprintf(errbuf, errlen, "Unable to setup RTMP session");
	  return NULL;
	}

	if(!RTMP_Connect(r->r, NULL, errbuf, errlen, 5000)) {
	  return NULL;
	}

	if(!RTMP_ConnectStream(r->r, 0)) {
	  snprintf(errbuf, errlen, "Unable to stream RTMP session");
	  return NULL;
	}

	if(mp->mp_flags & MP_CAN_SEEK)
	  RTMP_SendSeek(r->r, restartpos / 1000);
	continue;
      }

      dts = p.m_nTimeStamp;

      switch(p.m_packetType) {
      case RTMP_PACKET_TYPE_INFO:
	if(handle_metadata(r, p.m_body, p.m_nBodySize, mp, errbuf, errlen)) {
	  RTMPPacket_Free(&p);
	  return NULL;
	}
	break;

      case RTMP_PACKET_TYPE_VIDEO:
	e = get_packet_v(r, (void *)p.m_body, p.m_nBodySize, dts, mp);
	break;

      case RTMP_PACKET_TYPE_AUDIO:
	e = get_packet_a(r, (void *)p.m_body, p.m_nBodySize, dts, mp);
	break;
	
      case 0x16:
	pos = 0;
	break;
      default:
	TRACE(TRACE_DEBUG, "RTMP", 
	      "Got unknown packet type %d\n", p.m_packetType);
	break;
      }
      if(pos == -1)
	RTMPPacket_Free(&p);
    }

    if(pos != -1) {
      if(pos + 11 < p.m_nBodySize) {
	uint32_t ds = AMF_DecodeInt24(p.m_body + pos + 1);
	  
	if(pos + 11 + ds + 4 > p.m_nBodySize) {
	  snprintf(errbuf, errlen, "Corrupt stream");
	  RTMPPacket_Free(&p);
	  return NULL;
	}

	dts = AMF_DecodeInt24(p.m_body + pos + 4);
	dts |= (p.m_body[pos + 7] << 24);

	if(p.m_body[pos] == RTMP_PACKET_TYPE_INFO) {
	  if(handle_metadata(r, p.m_body, p.m_nBodySize, mp, errbuf, errlen)) {
	    RTMPPacket_Free(&p);
	    return NULL;
	  }
	} else if(p.m_body[pos] == RTMP_PACKET_TYPE_VIDEO) {
	  e = get_packet_v(r, (void *)p.m_body + pos + 11, ds, dts, mp);
	} else if(p.m_body[pos] == RTMP_PACKET_TYPE_AUDIO) {
	  e = get_packet_a(r, (void *)p.m_body + pos + 11, ds, dts, mp);
	} else {
	  TRACE(TRACE_DEBUG, "RTMP", 
		"Got unknown packet type %d\n", p.m_body[pos]);
	}
	pos += 11 + ds + 4;
      } else {
	pos = -1;
	RTMPPacket_Free(&p);
      }
    }
    if(e != NULL)
      break;
  }
  return e;
}


/**
 *
 */
static void
rtmp_free(rtmp_t *r)
{
  if(r->vcodec != NULL)
    media_codec_deref(r->vcodec);

  if(r->acodec != NULL)
    media_codec_deref(r->acodec);

  RTMP_Close(r->r);
  RTMP_Free(r->r);
}

static int rtmp_log_level;

/**
 *
 */
static event_t *
rtmp_playvideo(const char *url0, media_pipe_t *mp,
	       char *errbuf, size_t errlen,
	       video_queue_t *vq, struct vsource_list *vsl,
	       const video_args_t *va0)
{
  video_args_t va = *va0;
  rtmp_t r = {0};
  event_t *e;
  char *url = mystrdupa(url0);

  mp_set_url(mp, va0->canonical_url, va0->parent_url, va0->parent_title);

  usage_event("Play video", 1, USAGE_SEG("format", "RTMP"));

  prop_set(mp->mp_prop_metadata, "format", PROP_SET_STRING, "RTMP");
  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);

  va.flags |= BACKEND_VIDEO_NO_FS_SCAN;

  rtmp_log_level = RTMP_LOGINFO;
  RTMP_LogSetLevel(rtmp_log_level);

  r.r = RTMP_Alloc();
  RTMP_Init(r.r, mp->mp_cancellable);

  int64_t start = playinfo_get_restartpos(va.canonical_url, va.title, va.resume_mode);

  if(!RTMP_SetupURL(r.r, url)) {
    snprintf(errbuf, errlen, "Unable to setup RTMP-session");
    rtmp_free(&r);
    return NULL;
  }

  r.r->Link.lFlags |= RTMP_LF_SWFV;

  if(!RTMP_Connect(r.r, NULL, errbuf, errlen, 5000)) {
    rtmp_free(&r);
    return NULL;
  }

  if(!RTMP_ConnectStream(r.r, 0)) {
    snprintf(errbuf, errlen, "Unable to connect RTMP-stream");
    rtmp_free(&r);
    return NULL;
  }

  if(start)
    RTMP_SendSeek(r.r, start);
    
  r.mp = mp;
  
  mp->mp_audio.mq_stream = 0;
  mp->mp_video.mq_stream = 0;

  if(start > 0) {
    r.seekpos_video = start * 1000;
    r.seekpos_audio = start * 1000;
    mp->mp_seek_base = r.seekpos_video;
    mp->mp_video.mq_seektarget = r.seekpos_video;
    mp->mp_audio.mq_seektarget = r.seekpos_video;
  } else {
    mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
    mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
    mp->mp_seek_base = 0;
    r.seekpos_audio = AV_NOPTS_VALUE;
    r.seekpos_video = AV_NOPTS_VALUE;
  }

  mp_configure(mp, MP_CAN_PAUSE, MP_BUFFER_DEEP, 0, "video");
  mp->mp_max_realtime_delay = (r.r->Link.timeout - 1) * 1000000;

  mp_become_primary(mp);

  playinfo_register_play(va.canonical_url, 0);

  r.canonical_url = va.canonical_url;
  r.restartpos_last = -1;

  r.url = url;
  r.va = &va;
  r.is_loading = 1;
  e = rtmp_loop(&r, mp, url, errbuf, errlen);

  if(r.ss)
    sub_scanner_destroy(r.ss);

  if(r.total_duration) {
    int p = mp->mp_seek_base / (r.total_duration * 10);
    if(p >= video_settings.played_threshold) {
      TRACE(TRACE_DEBUG, "RTMP", "Playback reached %d%%, counting as played",
	    p);
      playinfo_register_play(va.canonical_url, 1);
      playinfo_set_restartpos(va.canonical_url, -1, 0);
    } else {
      playinfo_set_restartpos(va.canonical_url, mp->mp_seek_base / 1000, 0);
    }
  }

  mp_shutdown(mp);

  TRACE(TRACE_DEBUG, "RTMP", "End of playback");

  rtmp_free(&r);
  return e;
}


static void
rtmp_log(int level, const char *format, va_list vl)
{
  int mylevel = 0;

  if(level > rtmp_log_level)
    return;

  switch(level) {
  case RTMP_LOGCRIT:
  case RTMP_LOGERROR:
    mylevel = TRACE_ERROR;
    break;
  case RTMP_LOGWARNING:
  case RTMP_LOGINFO:
    mylevel = TRACE_INFO;
    break;
  case RTMP_LOGDEBUG:
  case RTMP_LOGDEBUG2:
  case RTMP_LOGALL:
    mylevel = TRACE_DEBUG;
    break;
  }
  tracev(0, mylevel, "RTMP", format, vl);
}

static int
rtmp_init(void)
{
  RTMP_LogSetCallback(rtmp_log);
  return 0;
}

/**
 *
 */
static int
rtmp_probe(const char *url0, char *errbuf, size_t errlen, int timeout_ms)
{
  RTMP *r;
  char *url = mystrdupa(url0);

  r = RTMP_Alloc();
  RTMP_Init(r, NULL);

  if(!RTMP_SetupURL(r, url)) {
    snprintf(errbuf, errlen, "Unable to setup RTMP-session");
    RTMP_Free(r);
    return BACKEND_PROBE_FAIL;
  }

  if(!RTMP_Connect(r, NULL, errbuf, errlen, timeout_ms)) {
    RTMP_Close(r);
    RTMP_Free(r);
    return BACKEND_PROBE_FAIL;
  }

  RTMP_SetReadTimeout(r, timeout_ms);

  if(!RTMP_ConnectStream(r, 0)) {
    snprintf(errbuf, errlen, "Unable to connect RTMP-stream");
    RTMP_Close(r);
    RTMP_Free(r);
    return BACKEND_PROBE_FAIL;
  }

  RTMP_Close(r);
  RTMP_Free(r);

  return BACKEND_PROBE_OK;
}


/**
 *
 */
static int
rtmp_open(prop_t *page, const char *url, int sync)
{
  usage_page_open(sync, "RTMP");
  return backend_open_video(page, url, sync);
}


/**
 *
 */
static backend_t be_rtmp = {
  .be_init = rtmp_init,
  .be_canhandle = rtmp_canhandle,
  .be_open = rtmp_open,
  .be_play_video = rtmp_playvideo,
  .be_probe = rtmp_probe,
};

BE_REGISTER(rtmp);
