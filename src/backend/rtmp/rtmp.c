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

#include "navigator.h"
#include "backend/backend.h"
#include "media.h"
#include "showtime.h"

typedef struct {

  int vframeduration;
  
  media_codec_t *vcodec;
  media_codec_t *acodec;

  RTMP *r;

  double duration;
  int lastdts;

  int in_seek_skip;

  int width;
  int height;

} rtmp_t;


/**
 *
 */
static int
rtmp_canhandle(backend_t *be, const char *url)
{
  return 
    !strncmp(url, "rtmp://", strlen("rtmp://")) ||
    !strncmp(url, "rtmpe://", strlen("rtmpe://"));
}


/**
 *
 */
static int 
set_vcodec(rtmp_t *r, const char *str)
{
  enum CodecID id;
  if(!strcmp(str, "avc1")) {
    id = CODEC_ID_H264;
  } else {
    return -1;
  }

  //  r->vcodec_id = id;
  return 0;
}


/**
 *
 */
static int 
set_acodec(rtmp_t *r, const char *str)
{
  enum CodecID id;
  if(!strcmp(str, "mp4a")) {
    id = CODEC_ID_AAC;
  } else {
    return -1;
  }

  //  r->acodec_id = id;
  return 0;
}






#define SAVC(x)	static AVal av_##x = { (char *)#x, sizeof(#x)-1};

SAVC(onMetaData);
SAVC(duration);
SAVC(videocodecid);
SAVC(audiocodecid);
SAVC(videoframerate)
SAVC(width);
SAVC(height);

static int
handle_metadata(rtmp_t *r, char *body, unsigned int len,
		media_pipe_t *mp, char *errstr, size_t errlen)
{
  AMFObject obj;
  AVal metastring;
  AMFObjectProperty prop;
  char str[256];
  prop_t *m = mp->mp_prop_metadata;

  if(AMF_Decode(&obj, body, len, 0) < 0) {
    snprintf(errstr, errlen, "Unable to decode metadata AMF");
    return -1;
  }

  AMFProp_GetString(AMF_GetProp(&obj, NULL, 0), &metastring);

  if(!AVMATCH(&metastring, &av_onMetaData)) {
    snprintf(errstr, errlen, "No metadata in metadata packet");
    return -1;
  }
  if(!RTMP_FindFirstMatchingProperty(&obj, &av_duration, &prop) ||
     prop.p_type != AMF_NUMBER) {
    snprintf(errstr, errlen, "Unable to parse total duration");
    return -1;
  }
  prop_set_float(prop_create(m, "duration"), prop.p_vu.p_number);
  r->duration = prop.p_vu.p_number;

  if(!RTMP_FindFirstMatchingProperty(&obj, &av_videocodecid, &prop) ||
     prop.p_type != AMF_STRING) {
    snprintf(errstr, errlen, "Unable to parse video codec");
    return -1;
  }
  snprintf(str, sizeof(str), "%.*s", prop.p_vu.p_aval.av_len,
	   prop.p_vu.p_aval.av_val);
  if(set_vcodec(r, str)) {
    snprintf(errstr, errlen, "Unsupported video codec: %s", str);
    return -1;
  }

  if(!RTMP_FindFirstMatchingProperty(&obj, &av_audiocodecid, &prop) ||
     prop.p_type != AMF_STRING)
    return -1;
  snprintf(str, sizeof(str), "%.*s", prop.p_vu.p_aval.av_len,
	   prop.p_vu.p_aval.av_val);
  if(set_acodec(r, str)) {
    snprintf(errstr, errlen, "Unsupported audio codec: %s", str);
    return -1;
  }

  if(!RTMP_FindFirstMatchingProperty(&obj, &av_videoframerate, &prop) ||
     prop.p_type != AMF_NUMBER) {
    snprintf(errstr, errlen, "Unable to parse video framerate");
    return -1;
  }
  r->vframeduration = 1000000.0 / prop.p_vu.p_number;


  if(!RTMP_FindFirstMatchingProperty(&obj, &av_videoframerate, &prop) ||
     prop.p_type != AMF_NUMBER) {
    snprintf(errstr, errlen, "Unable to parse video framerate");
    return -1;
  }
  r->vframeduration = 1000000.0 / prop.p_vu.p_number;

  r->width = r->height = 0;
  if(RTMP_FindFirstMatchingProperty(&obj, &av_width, &prop) &&
     prop.p_type == AMF_NUMBER)
    r->width = prop.p_vu.p_number;

  if(RTMP_FindFirstMatchingProperty(&obj, &av_height, &prop) &&
     prop.p_type == AMF_NUMBER)
    r->height = prop.p_vu.p_number;

  if(r->width && r->height)
    TRACE(TRACE_DEBUG, "RTMP", "Video size %d x %d", r->width, r->height);
  return 0;
}


static media_buf_t *
get_packet(rtmp_t *r, int v, uint8_t *data, size_t size, int32_t dts,
	   int epoch, media_pipe_t *mp)
{
  media_buf_t *mb;
  uint8_t flags;
  uint8_t type;
  enum CodecID id;
  int d = 0;

  if(size < 2)
    return NULL;

  flags = data[0];
  type  = data[1];

  data += 2;
  size -= 2;

  if(v) {
    // Video
    switch(flags & 0xf) {
    case 7:   id = CODEC_ID_H264; break;
    default:  return NULL;
    }

    if(id == CODEC_ID_H264) {
      if(size < 3)
	return NULL;
    
      d = (AMF_DecodeInt24((char *)data) + 0xff800000) ^ 0xff800000;
      data += 3;
      size -= 3;
    }

    if(r->vcodec == NULL) {
      AVCodecContext *ctx;
      media_codec_params_t mcp = {0};
      if(id == CODEC_ID_H264) {
	if(type != 0 || size < 0)
	  return NULL;

	ctx = avcodec_alloc_context();
	ctx->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
	memcpy(ctx->extradata, data, size);
	ctx->extradata_size =  size;
      } else {
	ctx = NULL;
      }
      mcp.width = r->width;
      mcp.height = r->height;
      r->vcodec = media_codec_create(id, CODEC_TYPE_VIDEO, 0, NULL, ctx, 
				     &mcp, mp);
      return NULL;
    }

    mb = media_buf_alloc();
    mb->mb_data_type = MB_VIDEO;
    mb->mb_duration = r->vframeduration;
    mb->mb_cw = media_codec_ref(r->vcodec);
    mb->mb_time = AV_NOPTS_VALUE;

    if(d < 0) {
      mb->mb_skip = 1;
      r->in_seek_skip = 1;
      //      printf("VIDEO: %20d %d SKIP\n", dts, d);
    } else if(r->in_seek_skip) {
      mb->mb_skip = 2;
      r->in_seek_skip = 0;
      //      printf("VIDEO: %20d %d SKIP LAST\n", dts, d);
    } else {
      //      printf("VIDEO: %20d %d\n", dts, d);
    }
  } else {
    // Audio


    switch(flags & 0xf0) {
    case 0xa0:   id = CODEC_ID_AAC; break;
    default:  return NULL;
    }
    
    if(r->acodec == NULL) {
      AVCodecContext *ctx;

      if(id == CODEC_ID_AAC) {
	if(type != 0 || size < 0)
	  return NULL;

	ctx = avcodec_alloc_context();
	ctx->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
	memcpy(ctx->extradata, data, size);
	ctx->extradata_size =  size;
      } else {
	ctx = NULL;
      }
      r->acodec = media_codec_create(id, CODEC_TYPE_AUDIO, 0, NULL, ctx,
				     NULL, mp);
      return NULL;
    }

    mb = media_buf_alloc();
    mb->mb_data_type = MB_AUDIO;
    mb->mb_duration = 0;
    mb->mb_cw = media_codec_ref(r->acodec);
    mb->mb_time = 1000LL * dts;
    //    printf("AUDIO: %20d\n", dts);
  }

  r->lastdts = dts;

  mb->mb_dts = 1000LL * dts;
  mb->mb_pts = 1000LL * (dts + d);

  mb->mb_data = malloc(size +   FF_INPUT_BUFFER_PADDING_SIZE);
  memset(mb->mb_data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
  memcpy(mb->mb_data, data, size);
  mb->mb_size = size;
  mb->mb_epoch = epoch;
  return mb;
}


/**
 *
 */
static int64_t
video_seek(rtmp_t *r, media_pipe_t *mp, media_buf_t **mbp,
	   int64_t pos, int backward, const char *txt)
{
  TRACE(TRACE_DEBUG, "Video", "seek %s to %.2f", txt, pos / 1000000.0);
 
  RTMP_SendSeek(r->r, pos / 1000);

  mp->mp_video.mq_seektarget = pos;
  mp->mp_audio.mq_seektarget = pos;

  mp_flush(mp, 0);
  
  if(*mbp != NULL) {
    media_buf_free(*mbp);
    *mbp = NULL;
  }

  prop_set_float(prop_create(mp->mp_prop_root, "seektime"), pos / 1000000.0);

  return pos;
}



/**
 *
 */
static event_t *
rtmp_loop(rtmp_t *r, media_pipe_t *mp, char *url, char *errbuf, size_t errlen)
{
  RTMPPacket p = {0};
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  int pos = -1, ret;
  uint32_t dts;
  event_t *e;
  int hold = 0, lost_focus = 0, epoch = 1;
  int64_t seekbase;
  int forcerestart = 0;

  seekbase = AV_NOPTS_VALUE;

  mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
  mp_set_playstatus_by_hold(mp, 0, NULL);

  while(1) {

    if(mb == NULL) {

      if(pos == -1) {
	ret = RTMP_GetNextMediaPacket(r->r, &p);
	if(ret == 2) {
	  /* Wait for queues to drain */
	  e = mp_wait_for_empty_queues(mp, 0);
	  mp_set_playstatus_stop(mp);

	  if(e == NULL)
	    e = event_create_type(EVENT_EOF);
	  break;
	}

	if(forcerestart || ret == 0) {
	  forcerestart = 0,

	  RTMP_Close(r->r);
	  
	  RTMP_Init(r->r);

	  memset(&p, 0, sizeof(p));

	  TRACE(TRACE_DEBUG, "RTMP", "Reconnecting stream at pos %d", 
		r->lastdts);

	  if(!RTMP_SetupURL(r->r, url)) {
	    snprintf(errbuf, errlen, "Unable to setup RTMP session");
	    return NULL;
	  }

	  if(!RTMP_Connect(r->r, NULL)) {
	    snprintf(errbuf, errlen, "Unable to connect RTMP session");
	    return NULL;
	  }

	  if(!RTMP_ConnectStream(r->r, seekbase / 1000)) {
	    snprintf(errbuf, errlen, "Unable to stream RTMP session");
	    return NULL;
	  }
	  epoch++;


	  r->lastdts = 0;
	  seekbase = AV_NOPTS_VALUE;
	  mp_flush(mp, 0);
	  continue;
	}

	dts = p.m_nTimeStamp;

	switch(p.m_packetType) {
	case RTMP_PACKET_TYPE_INFO:
	  if(handle_metadata(r, p.m_body, p.m_nBodySize, mp, errbuf, errlen))
	    return NULL;
	  break;

	case RTMP_PACKET_TYPE_VIDEO:
	  mb = get_packet(r, 1, (void *)p.m_body, p.m_nBodySize, dts, epoch, 
			  mp);
	  mq = &mp->mp_video;
	  break;

	case RTMP_PACKET_TYPE_AUDIO:
	  mb = get_packet(r, 0, (void *)p.m_body, p.m_nBodySize, dts, epoch,
			  mp);
	  mq = &mp->mp_audio;
	  break;
	
	case 0x16:
	  pos = 0;
	  break;
	default:
	  TRACE(TRACE_DEBUG, "RTMP", 
		"Got unknown packet type %d\n", p.m_packetType);
	  break;
	}
      }

      if(pos != -1) {
	if(pos + 11 < p.m_nBodySize) {
	  uint32_t ds = AMF_DecodeInt24(p.m_body + pos + 1);
	  
	  if(pos + 11 + ds + 4 > p.m_nBodySize) {
	    snprintf(errbuf, errlen, "Corrupt stream");
	    return NULL;
	  }

	  dts = AMF_DecodeInt24(p.m_body + pos + 4);
	  dts |= (p.m_body[pos + 7] << 24);

	  if(p.m_body[pos] == RTMP_PACKET_TYPE_INFO) {
	    if(handle_metadata(r, p.m_body, p.m_nBodySize, mp, errbuf, errlen))
	      return NULL;
	  } else if(p.m_body[pos] == RTMP_PACKET_TYPE_VIDEO) {
	    mb = get_packet(r, 1, (void *)p.m_body + pos + 11, ds, dts, epoch,
			    mp);
	    mq = &mp->mp_video;
	  } else if(p.m_body[pos] == RTMP_PACKET_TYPE_AUDIO) {
	    mb = get_packet(r, 0, (void *)p.m_body + pos + 11, ds, dts, epoch,
			    mp);
	    mq = &mp->mp_audio;
	  } else {
	    TRACE(TRACE_DEBUG, "RTMP", 
		  "Got unknown packet type %d\n", p.m_body[pos]);
	  }
	  pos += 11 + ds + 4;
	} else {
	  pos = -1;
	}
      }
    }

    if(mb == NULL)
      continue;

    if(mq->mq_seektarget != AV_NOPTS_VALUE) {
      int64_t ts = mb->mb_pts != AV_NOPTS_VALUE ? mb->mb_pts : mb->mb_dts;
      if(ts < mq->mq_seektarget) {
	mb->mb_skip = 1;
      } else {
	mb->mb_skip = 2;
	mq->mq_seektarget = AV_NOPTS_VALUE;
      }
    }

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
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
      if(!hold)
	forcerestart = 1;

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
      event_ts_t *ets = (event_ts_t *)e;

      seekbase = ets->pts;

    } else if(event_is_type(e, EVENT_SEEK)) {
      event_ts_t *ets = (event_ts_t *)e;

      epoch++;
      
      seekbase = video_seek(r, mp, &mb, ets->pts, 1, "direct");

    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      seekbase = video_seek(r, mp, &mb, seekbase - 60000000, 1, "-60s");

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      seekbase = video_seek(r, mp, &mb, seekbase - 15000000, 1, "-15s");

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      seekbase = video_seek(r, mp, &mb, seekbase + 15000000, 1, "+15s");

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      seekbase = video_seek(r, mp, &mb, seekbase + 60000000, 1, "+60s");

    } else if(event_is_action(e, ACTION_STOP)) {
      mp_set_playstatus_stop(mp);

    } else if(event_is_type(e, EVENT_EXIT) ||
       event_is_type(e, EVENT_PLAY_URL)) {
      break;
    }
    event_unref(e);
  }

  if(mb != NULL)
    media_buf_free(mb);
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
rtmp_playvideo(backend_t *be, const char *url0,
	       media_pipe_t *mp, int primary, int priority,
	       char *errbuf, size_t errlen)
{
  rtmp_t r = {0};
  event_t *e;
  char *url = mystrdupa(url0);

  RTMP_LogSetLevel(RTMP_LOGINFO);

  r.r = RTMP_Alloc();
  RTMP_Init(r.r);

  if(!RTMP_SetupURL(r.r, url)) {
    snprintf(errbuf, errlen, "Unable to setup RTMP session");
    rtmp_free(&r);
    return NULL;
  }

  if(!RTMP_Connect(r.r, NULL)) {
    snprintf(errbuf, errlen, "Unable to connect RTMP session");
    rtmp_free(&r);
    return NULL;
  }

  if(!RTMP_ConnectStream(r.r, 0)) {
    snprintf(errbuf, errlen, "Unable to stream RTMP session");
    rtmp_free(&r);
    return NULL;
  }


  mp->mp_audio.mq_stream = 0;
  mp->mp_video.mq_stream = 0;

  mp_set_play_caps(mp, MP_PLAY_CAPS_SEEK | MP_PLAY_CAPS_PAUSE);
  mp_become_primary(mp);

  e = rtmp_loop(&r, mp, (char *)url, errbuf, errlen);

  mp_flush(mp, 0);
  mp_shutdown(mp);

  TRACE(TRACE_DEBUG, "RTMP", "End of stream");

  rtmp_free(&r);
  return e;
}



/**
 *
 */
static backend_t be_rtmp = {
  .be_canhandle = rtmp_canhandle,
  .be_open = backend_open_video,
  .be_play_video = rtmp_playvideo,
};

BE_REGISTER(rtmp);
