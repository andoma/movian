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

typedef struct {

  int vframeduration;
  
  media_codec_t *vcodec;
  media_codec_t *acodec;

  RTMP *r;

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


/**
 *
 */
static int
rtmp_open(struct navigator *nav,
	  const char *url0, const char *type0, prop_t *psource,
	  nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *np;
  prop_t *src;

  np = nav_page_create(nav, url0, sizeof(nav_page_t), 0);

  src = prop_create(np->np_prop_root, "source");
  prop_set_string(prop_create(src, "type"), "video");
  *npp = np;
  return 0;
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
SAVC(videoframerate);

static int
handle_metadata(rtmp_t *r, char *body, unsigned int len,
		char *errstr, size_t errlen)
{
  AMFObject obj;
  AVal metastring;
  AMFObjectProperty prop;
  char str[256];

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

  return 0;
}


static media_buf_t *
get_packet(rtmp_t *r, int v, uint8_t *data, size_t size, int32_t dts)
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
      r->vcodec = media_codec_create(id, CODEC_TYPE_VIDEO, 0, NULL, ctx, 0, 0);
      printf("video codec created\n");
      return NULL;
    }

    mb = media_buf_alloc();
    mb->mb_data_type = MB_VIDEO;
    mb->mb_duration = r->vframeduration;
    mb->mb_cw = media_codec_ref(r->vcodec);

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
      r->acodec = media_codec_create(id, CODEC_TYPE_AUDIO, 0, NULL, ctx, 0, 0);
      printf("audio codec created\n");
      return NULL;
    }

    mb = media_buf_alloc();
    mb->mb_data_type = MB_AUDIO;
    mb->mb_duration = 0;
    mb->mb_cw = media_codec_ref(r->acodec);
  }

  mb->mb_dts = 1000LL * dts;
  mb->mb_pts = 1000LL * dts + d;

  mb->mb_data = malloc(size +   FF_INPUT_BUFFER_PADDING_SIZE);
  memset(mb->mb_data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
  memcpy(mb->mb_data, data, size);
  mb->mb_size = size;
  return mb;
}

/**
 *
 */
static event_t *
rtmp_loop(rtmp_t *r, media_pipe_t *mp, char *errbuf, size_t errlen)
{
  RTMPPacket p = {0};
  media_buf_t *mb = NULL;
  media_queue_t *mq = NULL;
  int pos = -1;
  uint32_t dts;
  event_t *e;

  mp->mp_video.mq_seektarget = AV_NOPTS_VALUE;
  mp->mp_audio.mq_seektarget = AV_NOPTS_VALUE;
  mp_set_playstatus_by_hold(mp, 0);

  while(1) {

    if(mb == NULL) {

      if(pos == -1) {
	if(!RTMP_GetNextMediaPacket(r->r, &p)) {
	  return event_create_type(EVENT_EOF);
	}

	dts = p.m_nTimeStamp;

	switch(p.m_packetType) {
	case RTMP_PACKET_TYPE_INFO:
	  if(handle_metadata(r, p.m_body, p.m_nBodySize, errbuf, errlen))
	    return NULL;
	  break;

	case RTMP_PACKET_TYPE_VIDEO:
	  mb = get_packet(r, 1, (void *)p.m_body, p.m_nBodySize, dts);
	  mq = &mp->mp_video;
	  break;

	case RTMP_PACKET_TYPE_AUDIO:
	  mb = get_packet(r, 0, (void *)p.m_body, p.m_nBodySize, dts);
	  mq = &mp->mp_audio;
	  break;
	
	case 0x16:
	  pos = 0;
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
	    if(handle_metadata(r, p.m_body, p.m_nBodySize, errbuf, errlen))
	      return NULL;
	  } else if(p.m_body[pos] == RTMP_PACKET_TYPE_VIDEO) {
	    mb = get_packet(r, 1, (void *)p.m_body + pos + 11, ds, dts);
	    mq = &mp->mp_video;
	  } else if(p.m_body[pos] == RTMP_PACKET_TYPE_AUDIO) {
	    mb = get_packet(r, 0, (void *)p.m_body + pos + 11, ds, dts);
	    mq = &mp->mp_audio;
	  }
	  pos += 11 + ds + 4;
	} else {
	  pos = -1;
	}
      }
    }

    if(mb == NULL)
      continue;

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }
  }
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
rtmp_playvideo(const char *url, media_pipe_t *mp, char *errbuf, size_t errlen)
{
  rtmp_t r = {0};
  event_t *e;

  r.r = RTMP_Alloc();
  RTMP_Init(r.r);

  RTMP_LogSetLevel(RTMP_LOGINFO);

  if(!RTMP_SetupURL(r.r, (char *)url)) {
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

  e = rtmp_loop(&r, mp, errbuf, errlen);
  rtmp_free(&r);
  return e;
}



/**
 *
 */
static backend_t be_rtmp = {
  .be_canhandle = rtmp_canhandle,
  .be_open = rtmp_open,
  .be_play_video = rtmp_playvideo,
};

BE_REGISTER(rtmp);
