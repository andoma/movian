/*
 *  Backend using librtmp
 *  Copyright (C) 2010 Andreas Öman
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

#include <librtmp/rtmp.h>
#include <librtmp/log.h>

#include <libavcodec/avcodec.h>

#include "navigator.h"
#include "backend/backend.h"
#include "media.h"
#include "showtime.h"
#include "i18n.h"
#include "misc/isolang.h"
#include "video/video_playback.h"
#include "video/video_settings.h"
#include "metadata.h"

typedef struct {

  media_pipe_t *mp;

  int vframeduration;
  
  media_codec_t *vcodec;
  media_codec_t *acodec;

  RTMP *r;

  int lastdts;

  int in_seek_skip;

  int width;
  int height;

  int hold;
  int lost_focus;

  int64_t seekbase;
  int epoch;
  int64_t seekpos;

  int can_seek;

  int restartpos_last;

  const char *canonical_url;

  int seek_origin;
  int total_duration;
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
    r->can_seek = 1;
  } else {
    r->can_seek = 0;
    r->total_duration = 0;
  }
  prop_set_int(mp->mp_prop_canSeek, r->can_seek);

  if((RTMP_FindFirstMatchingProperty(obj, &av_videoframerate, &prop) &&
      RTMP_FindFirstMatchingProperty(obj, &av_framerate, &prop))
     && prop.p_type == AMF_NUMBER)
    r->vframeduration = 1000000.0 / prop.p_vu.p_number;

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
static int64_t
video_seek(rtmp_t *r, media_pipe_t *mp, media_buf_t **mbp,
	   int64_t pos, int backward, const char *txt)
{
  if(pos < 0)
    pos = 0;

  TRACE(TRACE_DEBUG, "Video", "seek %s to %.2f", txt, pos / 1000000.0);
 
  RTMP_SendSeek(r->r, (pos / 1000) - r->seek_origin);

  r->seekpos = pos;

  mp->mp_video.mq_seektarget = pos;
  mp->mp_audio.mq_seektarget = pos;

  mp_flush(mp, 0);
  
  if(mbp != NULL && *mbp != NULL) {
    media_buf_free_unlocked(mp, *mbp);
    *mbp = NULL;
  }

  prop_set_float(prop_create(mp->mp_prop_root, "seektime"), pos / 1000000.0);
  return pos;
}

/**
 *
 */
static event_t *
rtmp_process_event(rtmp_t *r, event_t *e, media_buf_t **mbp)
{
  media_pipe_t *mp = r->mp;

  if(event_is_type(e, EVENT_EXIT) ||
     event_is_type(e, EVENT_PLAY_URL))
    return e;

  if(event_is_action(e, ACTION_PLAYPAUSE) ||
     event_is_action(e, ACTION_PLAY) ||
     event_is_action(e, ACTION_PAUSE)) {
    
    r->hold = action_update_hold_by_event(r->hold, e);
    mp_send_cmd_head(mp, &mp->mp_video, r->hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_send_cmd_head(mp, &mp->mp_audio, r->hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, r->hold, NULL);
    r->lost_focus = 0;
    

  } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

    r->hold = 1;
    r->lost_focus = 1;
    mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PAUSE);
    mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PAUSE);
    mp_set_playstatus_by_hold(mp, r->hold, e->e_payload);
    
  } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {
    
    if(r->lost_focus) {
      r->hold = 0;
      r->lost_focus = 0;
      mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PLAY);
      mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(mp, r->hold, NULL);
    }
    
  } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {
    
    r->hold = 1;
    r->lost_focus = 0;
    mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PAUSE);
    mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PAUSE);
    mp_set_playstatus_by_hold(mp, r->hold, e->e_payload);

  } else if(event_is_type(e, EVENT_CURRENT_PTS)) {
    event_ts_t *ets = (event_ts_t *)e;
    
    r->seekbase = ets->ts;
    
    int sec = r->seekbase / 1000000;

    if(sec != r->restartpos_last && r->can_seek) {
      r->restartpos_last = sec;
      metadb_set_video_restartpos(r->canonical_url, r->seekbase / 1000);
    }

  } else if(r->can_seek && event_is_type(e, EVENT_SEEK)) {
    event_ts_t *ets = (event_ts_t *)e;

    r->epoch++;
      
    r->seekbase = video_seek(r, mp, mbp, ets->ts, 1, "direct");

  } else if(r->can_seek && event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

    r->seekbase = video_seek(r, mp, mbp, r->seekbase - 60000000, 1, "-60s");

  } else if(r->can_seek && event_is_action(e, ACTION_SEEK_BACKWARD)) {

    r->seekbase = video_seek(r, mp, mbp, r->seekbase - 15000000, 1, "-15s");

  } else if(r->can_seek && event_is_action(e, ACTION_SEEK_FORWARD)) {

    r->seekbase = video_seek(r, mp, mbp, r->seekbase + 15000000, 1, "+15s");

  } else if(r->can_seek && event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

    r->seekbase = video_seek(r, mp, mbp, r->seekbase + 60000000, 1, "+60s");

  } else if(event_is_action(e, ACTION_STOP)) {
    mp_set_playstatus_stop(mp);
  } else if(event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)) {
    event_select_track_t *est = (event_select_track_t *)e;
    prop_set_string(mp->mp_prop_subtitle_track_current, est->id);
    if(!strcmp(est->id, "sub:off")) {
      mp_load_ext_sub(mp, NULL);
      } else {
      mp_load_ext_sub(mp, est->id);
    }

  }
  event_release(e);
  return NULL;
}


/**
 *
 */
static event_t *
sendpkt(rtmp_t *r, media_queue_t *mq, media_codec_t *mc,
	int64_t dts, int64_t pts, int64_t time, const void *data, 
	size_t size, int skip, int dt, int duration)
{
  event_t *e = NULL;
  media_buf_t *mb = media_buf_alloc_unlocked(r->mp, size);

  mb->mb_data_type = dt;
  mb->mb_duration = duration;
  mb->mb_cw = media_codec_ref(mc);
  mb->mb_time = time;
  mb->mb_dts = dts;
  mb->mb_pts = pts;
  mb->mb_skip = skip;
  mb->mb_send_pts = dt == MB_VIDEO;
	
  memcpy(mb->mb_data, data, size);
  mb->mb_epoch = r->epoch;

  do {

    if(mb == NULL || (e = mb_enqueue_with_events(r->mp, mq, mb)) == NULL)
      break;
    
    e = rtmp_process_event(r, e, &mb);

  } while(e == NULL);

  return e;
}


/**
 *
 */
static event_t *
get_packet_v(rtmp_t *r, uint8_t *data, size_t size, int64_t dts,
	     media_pipe_t *mp)
{
  uint8_t flags;
  uint8_t type = 0;
  enum CodecID id;
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
    id = CODEC_ID_H264;

    if(size < 3)
      return NULL;
    
    d = (AMF_DecodeInt24((char *)data) + 0xff800000) ^ 0xff800000;
    data += 3;
    size -= 3;
    break;

  case 4:
    type = *data++;
    size--;
    id = CODEC_ID_VP6F;
    break;
  default:
    return NULL;
  }

  if(r->vcodec == NULL) {
    AVCodecContext *ctx;
    media_codec_params_t mcp = {0};

    switch(id) {
    case CODEC_ID_H264:
      if(type != 0 || size < 0)
	return NULL;
	
      ctx = avcodec_alloc_context();
      ctx->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(ctx->extradata, data, size);
      ctx->extradata_size =  size;
      break;

    case CODEC_ID_VP6F:
      if(size < 1)
	return NULL;

      ctx = avcodec_alloc_context();
      ctx->extradata = av_mallocz(1 + FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(ctx->extradata, &type, 1);
      ctx->extradata_size =  1;
      break;

    default:
      abort();
    }
    mcp.width = r->width;
    mcp.height = r->height;
    r->vcodec = media_codec_create(id, 0, NULL, ctx, &mcp, mp);
    return NULL;
  }


  int skip = 0;

  int64_t pts = 1000LL * (dts + d);
  dts = 1000LL * dts;

  if(d < 0 || dts < r->seekpos) {
    skip = 1;
    r->in_seek_skip = 1;
  } else if(r->in_seek_skip) {
    skip = 2;
    r->in_seek_skip = 0;
  }

  r->lastdts = dts;

  e = sendpkt(r, &r->mp->mp_video, r->vcodec, dts, pts, AV_NOPTS_VALUE,
	      data, size, skip, MB_VIDEO, r->vframeduration);
  return e;
}


/**
 *
 */
static event_t *
get_packet_a(rtmp_t *r, uint8_t *data, size_t size, int64_t dts, 
	     media_pipe_t *mp)
{
  uint8_t flags;
  uint8_t type = 0;
  enum CodecID id;

  if(r->r->m_read.flags & RTMP_READ_SEEKING)
    return NULL; 

  if(size < 2)
    return NULL;

  flags = *data++;
  size--;

  switch(flags & 0xf0) {
  case 0xa0:   id = CODEC_ID_AAC;
    type = *data++;
    size--;
    break;

  case 0x20:
    id = CODEC_ID_MP3;
    break;

  default: 
    return NULL;
  }
    
  if(r->acodec == NULL) {
    AVCodecContext *ctx;
    int parse = 0;
    const char *fmt;

    switch(id) {
      
    case CODEC_ID_AAC:
      if(type != 0 || size < 0)
	return NULL;
	
      ctx = avcodec_alloc_context();
      ctx->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
      memcpy(ctx->extradata, data, size);
      ctx->extradata_size =  size;
      fmt = "AAC";
      break;

    case CODEC_ID_MP3:
      ctx = avcodec_alloc_context();
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
		 0);

    prop_set_string(mp->mp_prop_audio_track_current, "rtmp:1");

    r->acodec = media_codec_create(id, parse, NULL, ctx, NULL, mp);
    return NULL;
  }

  media_codec_t *mc = r->acodec;

  dts *= 1000;

  if(dts < r->seekpos)
    return NULL;


  if(mc->parser_ctx == NULL)
    return sendpkt(r, &mp->mp_audio, mc, 
		   dts, dts, dts, data, size, 0, MB_AUDIO, 0);

  while(size > 0) {
    int outlen;
    uint8_t *outbuf;
    int rlen = av_parser_parse2(mc->parser_ctx,
				mc->codec_ctx, &outbuf, &outlen, 
				data, size, dts, dts, AV_NOPTS_VALUE);
    if(outlen) {
      event_t *e = sendpkt(r, &mp->mp_audio, mc,
			   mc->parser_ctx->dts,
			   mc->parser_ctx->pts, mc->parser_ctx->dts,
			   outbuf, outlen, 0,
			   MB_AUDIO, 0);
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

  mp_set_playstatus_by_hold(mp, 0, NULL);

  while(1) {


    if(pos == -1) {

      ret = RTMP_GetNextMediaPacket(r->r, &p);

      if(ret == 2) {
	/* Wait for queues to drain */
      again:
	e = mp_wait_for_empty_queues(mp);

	if(e != NULL) {
	  e = rtmp_process_event(r, e, NULL);
	  if(e == NULL)
	    goto again;
	}
	mp_set_playstatus_stop(mp);

	if(e == NULL)
	  e = event_create_type(EVENT_EOF);
	break;
      }

      if(ret == 0) {
	RTMP_Close(r->r);
	  
	RTMP_Init(r->r);

	memset(&p, 0, sizeof(p));

	TRACE(TRACE_DEBUG, "RTMP", "Reconnecting stream at pos %d", 
	      r->seekbase);

	if(!RTMP_SetupURL(r->r, url)) {
	  snprintf(errbuf, errlen, "Unable to setup RTMP session");
	  e = NULL;
	  break;
	}

	if(!RTMP_Connect(r->r, NULL)) {
	  snprintf(errbuf, errlen, "Unable to connect RTMP session");
	  e = NULL;
	  break;
	}

	if(!RTMP_ConnectStream(r->r, r->can_seek ? r->seekbase / 1000 : 0)) {
	  snprintf(errbuf, errlen, "Unable to stream RTMP session");
	  return NULL;
	}
	r->epoch++;


	r->lastdts = 0;
	r->seekbase = AV_NOPTS_VALUE;
	mp_flush(mp, 0);
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


/**
 *
 */
static event_t *
rtmp_playvideo(const char *url0, media_pipe_t *mp,
	       int flags, int priority,
	       char *errbuf, size_t errlen,
	       const char *mimetype,
	       const char *canonical_url)
{
  rtmp_t r = {0};
  event_t *e;
  char *url = mystrdupa(url0);

  prop_set_string(mp->mp_prop_type, "video");

  RTMP_LogSetLevel(RTMP_LOGINFO);

  r.r = RTMP_Alloc();
  RTMP_Init(r.r);

  int64_t start = video_get_restartpos(canonical_url);

  if(!RTMP_SetupURL(r.r, url)) {
    snprintf(errbuf, errlen, "Unable to setup RTMP-session");
    rtmp_free(&r);
    return NULL;
  }

  if(!RTMP_Connect(r.r, NULL)) {
    snprintf(errbuf, errlen, "Unable to connect RTMP-session");
    rtmp_free(&r);
    return NULL;
  }

  if(!RTMP_ConnectStream(r.r, start)) {
    snprintf(errbuf, errlen, "Unable to connect RTMP-stream");
    rtmp_free(&r);
    return NULL;
  }
  r.seek_origin = start;
  r.mp = mp;
  r.hold = 0;
  r.lost_focus = 0;
  r.epoch = 1;
  
  mp->mp_audio.mq_stream = 0;
  mp->mp_video.mq_stream = 0;

  if(start > 0) {
    r.seekpos = start * 1000;
    r.seekbase = r.seekpos;
    mp->mp_video.mq_seektarget = r.seekpos;
    mp->mp_audio.mq_seektarget = r.seekpos;
  } else {
    mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
    mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
    r.seekbase = AV_NOPTS_VALUE;
    r.seekpos = AV_NOPTS_VALUE;
  }

  mp_configure(mp, MP_PLAY_CAPS_PAUSE, MP_BUFFER_DEEP);
  mp->mp_max_realtime_delay = (r.r->Link.timeout - 1) * 1000000;

  mp_become_primary(mp);

  metadb_register_play(canonical_url, 0, CONTENT_VIDEO);

  r.canonical_url = canonical_url;
  r.restartpos_last = -1;

  e = rtmp_loop(&r, mp, url, errbuf, errlen);

  if(r.total_duration) {
    int p = r.seekbase / (r.total_duration * 10);
    if(p >= video_settings.played_threshold) {
      TRACE(TRACE_DEBUG, "RTMP", "Playback reached %d%%, counting as played",
	    p);
      metadb_register_play(canonical_url, 1, CONTENT_VIDEO);
      metadb_set_video_restartpos(canonical_url, -1);
    }
  }

  mp_flush(mp, 0);
  mp_shutdown(mp);

  TRACE(TRACE_DEBUG, "RTMP", "End of stream");

  rtmp_free(&r);
  return e;
}


static void
rtmp_log(int level, const char *format, va_list vl)
{
  return;

  int mylevel = 0;
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
rtmp_probe(const char *url0, char *errbuf, size_t errlen)
{
  RTMP *r;
  char *url = mystrdupa(url0);

  r = RTMP_Alloc();
  RTMP_Init(r);

  if(!RTMP_SetupURL(r, url)) {
    snprintf(errbuf, errlen, "Unable to setup RTMP-session");
    RTMP_Free(r);
    return BACKEND_PROBE_FAIL;
  }

  if(!RTMP_Connect(r, NULL)) {
    snprintf(errbuf, errlen, "Unable to connect RTMP-session");
    RTMP_Close(r);
    RTMP_Free(r);
    return BACKEND_PROBE_FAIL;
  }

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
static backend_t be_rtmp = {
  .be_init = rtmp_init,
  .be_canhandle = rtmp_canhandle,
  .be_open = backend_open_video,
  .be_play_video = rtmp_playvideo,
  .be_probe = rtmp_probe,
};

BE_REGISTER(rtmp);
