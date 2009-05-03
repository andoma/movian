/*
 *  DVD player
 *  Copyright (C) 2009 Andreas Öman
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

#include "event.h"
#include "media.h"
#include "fileaccess/fileaccess.h"
#include "dvd.h"
#include "notifications.h"

#include <fileaccess/svfs.h>
#include <dvdnav/dvdnav.h>

static char *make_nice_title(const char *t);


#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define SEQUENCE_END_CODE           ((unsigned int)0x000001b7)
#define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
#define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
#define ISO_11172_END_CODE          ((unsigned int)0x000001b9)
  
/* mpeg2 */
#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf




#define getu32(b, l) ({						\
  uint32_t x = (b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3]);	\
  b+=4;								\
  l-=4; 							\
  x;								\
})

#define getu16(b, l) ({						\
  uint16_t x = (b[0] << 8 | b[1]);	                        \
  b+=2;								\
  l-=2; 							\
  x;								\
})

#define getu8(b, l) ({						\
  uint8_t x = b[0];	                                        \
  b+=1;								\
  l-=1; 							\
  x;								\
})


#define getpts(b, l) ({					\
  int64_t _pts;						\
  _pts = (int64_t)((getu8(b, l) >> 1) & 0x07) << 30;	\
  _pts |= (int64_t)(getu16(b, l) >> 1) << 15;		\
  _pts |= (int64_t)(getu16(b, l) >> 1);			\
  _pts;							\
})



static void *
dvd_fa_open(const char *url)
{
  return fa_open(url, NULL, 0);
}

static int
dvd_fa_stat(const char *url, struct stat *st)
{
  return fa_stat(url, st, NULL, 0);
}


/**
 *
 */
static struct svfs_ops faops = {
  .open = dvd_fa_open,
  .close = fa_close,
  .read = fa_read,
  .seek = fa_seek,
  .stat = dvd_fa_stat,
  .findfile = fa_findfile,
};

/**
 *
 */
typedef struct dvd_player {
  dvdnav_t *dp_dvdnav;
  media_pipe_t *dp_mp;

  int dp_audio_track;
#define DP_AUDIO_DISABLE    -1
#define DP_AUDIO_FOLLOW_VM  -2
  int dp_audio_track_vm;

  int dp_spu_track;
#define DP_SPU_DISABLE   -1
#define DP_SPU_FOLLOW_VM -2
  int dp_spu_track_vm;

  int dp_aspect_override;

  codecwrap_t *dp_video;
  codecwrap_t *dp_audio;
  codecwrap_t *dp_spu;

  uint8_t dp_buf[DVD_VIDEO_LB_LEN];

  pci_t dp_pci;

  int dp_epoch;

  uint32_t dp_end_ptm;  /* end ptm from last nav packet */

  int dp_hold;
  int dp_lost_focus;

} dvd_player_t;

#define dvd_in_menu(dp) ((dp)->dp_pci.hli.hl_gi.hli_ss)

static event_t *dvd_process_event(dvd_player_t *dp, event_t *e);

/**
 *
 */
static void
dvd_release_codecs(dvd_player_t *dp)
{
  if(dp->dp_video != NULL) {
    wrap_codec_deref(dp->dp_video);
    dp->dp_video = NULL;
  }
  if(dp->dp_audio != NULL) {
    wrap_codec_deref(dp->dp_audio);
    dp->dp_audio = NULL;
  }
  if(dp->dp_spu != NULL) {
    wrap_codec_deref(dp->dp_spu);
    dp->dp_spu = NULL;
  }
}


/**
 *
 */
static void
dvd_video_push(dvd_player_t *dp)
{
  media_buf_t *mb;
  codecwrap_t *cw = dp->dp_video;
  media_pipe_t *mp = dp->dp_mp;

  if(cw == NULL)
    return;

  mb = media_buf_alloc();
  mb->mb_cw = wrap_codec_ref(cw);
  mb->mb_size = 0;
  mb->mb_data = NULL;
  mb->mb_aspect_override = dp->dp_aspect_override;
  mb->mb_disable_deinterlacer = 1;
  mb->mb_data_type = MB_VIDEO;
  mb->mb_duration = cw->codec_ctx->ticks_per_frame * 
    1000000LL * av_q2d(cw->codec_ctx->time_base);
  mb->mb_pts = AV_NOPTS_VALUE;
  mb->mb_dts = AV_NOPTS_VALUE;
  mb->mb_epoch = dp->dp_epoch;

  mb_enqueue_always(mp, &mp->mp_video, mb);
}

/**
 *
 */
static event_t *
dvd_media_enqueue(dvd_player_t *dp, media_queue_t *mq, codecwrap_t *cw,
		  int data_type, void *data, int datalen, int rate,
		  int64_t dts, int64_t pts)
{
  media_buf_t *mb = media_buf_alloc();
  event_t *e;

  AVCodecContext *ctx = cw->codec_ctx;

  mb->mb_cw = wrap_codec_ref(cw);
  mb->mb_data_type = data_type;
  mb->mb_duration = cw->codec_ctx->ticks_per_frame * 
    1000000LL * av_q2d(ctx->time_base);
  mb->mb_aspect_override = dp->dp_aspect_override;
  mb->mb_disable_deinterlacer = 1;
  mb->mb_dts = dts;
  mb->mb_pts = pts;
  mb->mb_epoch = dp->dp_epoch;
  
  mb->mb_data = malloc(datalen + FF_INPUT_BUFFER_PADDING_SIZE);
  mb->mb_size = datalen;
  memcpy(mb->mb_data, data, datalen);
  memset(mb->mb_data + datalen, 0, FF_INPUT_BUFFER_PADDING_SIZE);

  do {

    if((e = mb_enqueue_with_events(dp->dp_mp, mq, mb)) == NULL)
      break;
    
    e = dvd_process_event(dp, e);

  } while(e == NULL);

  return e;
}


/**
 *
 */
static event_t *
dvd_pes(dvd_player_t *dp, uint32_t sc, uint8_t *buf, int len)
{
  media_pipe_t *mp = dp->dp_mp;
  media_queue_t *mq;
  uint8_t flags, hlen, x;
  int64_t dts = AV_NOPTS_VALUE, pts = AV_NOPTS_VALUE;
  int rlen, outlen, data_type = 0, rate = 0;
  uint8_t *outbuf;
  int type, track;
  codecwrap_t *cw, **cwp;
  AVCodecContext *ctx;
  enum CodecID codec_id;
  AVRational mpeg_tc = {1, 90000};
  event_t *e;

  x     = getu8(buf, len);
  flags = getu8(buf, len);
  hlen  = getu8(buf, len);
  
  if(len < hlen)
    return NULL;

  if((x & 0xc0) != 0x80)
    /* no MPEG 2 PES */
    return NULL;

  if((flags & 0xc0) == 0xc0) {
    if(hlen < 10)
      return NULL;

    pts = getpts(buf, len);
    dts = getpts(buf, len);

    hlen -= 10;
  } else if((flags & 0xc0) == 0x80) {
    if(hlen < 5)
      return NULL;

    dts = pts = getpts(buf, len);
    hlen -= 5;
  }

  buf += hlen;
  len -= hlen;

  if(sc == PRIVATE_STREAM_1) {
    if(len < 1)
      return NULL;
      
    sc = getu8(buf, len);
    if(sc >= 0x80 && sc <= 0xbf) {
      /* audio, skip heeader */
      if(len < 3)
	return NULL;
      buf += 3;
      len -= 3;
    }
  }

  if(sc > 0x1ff)
    return NULL;

  if(sc >= 0x1e0 && sc <= 0x1ef) {
    codec_id  = CODEC_ID_MPEG2VIDEO;
    type      = CODEC_TYPE_VIDEO;
    data_type = MB_VIDEO;
    rate = dp->dp_aspect_override;
    cwp = &dp->dp_video;
    mq = &mp->mp_video;

  } else if((sc >= 0x80 && sc <= 0x9f) || (sc >= 0x1c0 && sc <= 0x1df)) {

    if(dp->dp_audio_track == DP_AUDIO_DISABLE)
      return NULL;
    
    track = dp->dp_audio_track == DP_AUDIO_FOLLOW_VM ? 
      dp->dp_audio_track_vm : dp->dp_audio_track;

    if((sc & 7) != track)
      return NULL;

    type = CODEC_TYPE_AUDIO;
    data_type = MB_AUDIO;

    switch(sc) {
    case 0x80 ... 0x87:
      codec_id = CODEC_ID_AC3;
      rate = 48000;
      break;
	    
    case 0x88 ... 0x9f:
      codec_id = CODEC_ID_DTS;
      rate = 48000;
      break;

    case 0x1c0 ... 0x1df:
      codec_id = CODEC_ID_MP2;
      rate = 48000;
      break;
    default:
      return NULL;
    }
    cwp = &dp->dp_audio;
    mq = &mp->mp_audio;

  } else if (sc >= 0x20 && sc <= 0x3f) {

    if(dp->dp_spu_track == DP_SPU_DISABLE)
      return NULL;
    
    track = dp->dp_spu_track == DP_SPU_FOLLOW_VM ? 
      dp->dp_spu_track_vm : dp->dp_spu_track;

    if((sc & 31) != track)
      return NULL;

    codec_id  = CODEC_ID_DVD_SUBTITLE;
    type      = CODEC_TYPE_SUBTITLE;
    data_type = MB_DVD_SPU;

    cwp = &dp->dp_spu;
    mq = &mp->mp_video;

  } else {
    return NULL;
  }
  
  cw = *cwp;

  if(cw == NULL || cw->codec->id != codec_id) {
    if(cw != NULL)
      wrap_codec_deref(cw);

    *cwp = cw = wrap_codec_create(codec_id, type, 1, NULL, NULL, 0);
    if(cw == NULL)
      return NULL;
  }

  if(dts != AV_NOPTS_VALUE)
    dts = av_rescale_q(dts, mpeg_tc, AV_TIME_BASE_Q);
  
  if(pts != AV_NOPTS_VALUE)
    pts = av_rescale_q(pts, mpeg_tc, AV_TIME_BASE_Q);


  ctx = cw->codec_ctx;
 
  if(cw->parser_ctx == NULL) /* No parser available */
    return dvd_media_enqueue(dp, mq, cw, data_type, buf, len, rate, dts, pts);

  while(len > 0) {
    rlen = av_parser_parse(cw->parser_ctx, ctx, &outbuf, &outlen, buf, len, 
			   pts, dts);
    if(outlen) {
      e = dvd_media_enqueue(dp, mq, cw, data_type, outbuf, outlen, rate,
			    cw->parser_ctx->dts, cw->parser_ctx->pts);
      if(e != NULL)
	return e;
    }
    pts = AV_NOPTS_VALUE;
    dts = AV_NOPTS_VALUE;
    buf += rlen;
    len -= rlen;
  }
  return NULL;
}


/**
 *
 */
static event_t *
dvd_block(dvd_player_t *dp, uint8_t *buf, int len)
{ 
  uint32_t startcode, pes_len;
  event_t *e;

  if(buf[13] & 7)
    return NULL; /* Stuffing is not supported */

  buf += 14;
  len -= 14;

  while(len > 0) {

    if(len < 4)
      break;

    startcode = getu32(buf, len);
    pes_len   = getu16(buf, len); 

    if(pes_len < 3)
      break;

    switch(startcode) {
    case PADDING_STREAM:
    case PRIVATE_STREAM_1:
    case PRIVATE_STREAM_2:
    case 0x1c0 ... 0x1df:
    case 0x1e0 ... 0x1ef:
      if((e = dvd_pes(dp, startcode, buf, pes_len)) != NULL)
	return e;
      len -= pes_len;
      buf += pes_len;
      break;

    default:
      break;
    }
  }
  return NULL;
}


/**
 *
 */
event_t *
dvd_play(const char *url, media_pipe_t *mp, char *errstr, size_t errlen)
{
  dvd_player_t *dp;
  dvdnav_highlight_event_t *hevent;
  dvdnav_spu_stream_change_event_t *spu_event;
  dvdnav_audio_stream_change_event_t *audio_event;
  int  result, event, len, t;
  uint8_t *block;
  void *data;
  pci_t *pci;
  event_t *e = NULL;
  time_t deadline;
  char *title;
  const char *rawtitle;

 restart:
  dp = calloc(1, sizeof(dvd_player_t));
  dp->dp_epoch = 1;

  dp->dp_mp = mp;
  
  mp->mp_video.mq_stream = 0;
  mp->mp_audio.mq_stream = 0;

  if(dvdnav_open(&dp->dp_dvdnav, url, &faops) != DVDNAV_STATUS_OK) {
    snprintf(errstr, errlen, "Unable to open DVD");
    free(dp);
    return NULL;
  }
  dvdnav_set_readahead_flag(dp->dp_dvdnav, 1);
  dvdnav_set_PGC_positioning_flag(dp->dp_dvdnav, 1);

  /**
   * By default, follow DVD VM machine
   */
  dp->dp_audio_track = DP_AUDIO_FOLLOW_VM;
  dp->dp_spu_track   = DP_SPU_FOLLOW_VM;

  mp_become_primary(mp);

  /**
   * Popup loading
   */
  dvdnav_get_title_string(dp->dp_dvdnav, &rawtitle);
  title = make_nice_title(rawtitle);
  if(*title == 0) {
    title = strrchr(url, '/');
    if(title != NULL)
      title = make_nice_title(title + 1);
  }
  notify_add(NOTIFY_INFO, NULL, 5, "Loading DVD: %s", title);
  free(title);

  /**
   * DVD main loop
   */
  while(e == NULL) {
    block = dp->dp_buf;
    result = dvdnav_get_next_cache_block(dp->dp_dvdnav, &block, &event, &len);
    if(result == DVDNAV_STATUS_ERR) {
      notify_add(NOTIFY_INFO, NULL, 5, "DVD read error, restarting disc");
      dvd_release_codecs(dp);
      dvdnav_close(dp->dp_dvdnav);
      free(dp);
      goto restart;
    }

    switch(event) {
    case DVDNAV_BLOCK_OK:
      e = dvd_block(dp, block, len);
      break;

    case DVDNAV_NOP:
      break;

    case DVDNAV_SPU_STREAM_CHANGE:
      spu_event = (void *)block;
      dp->dp_spu_track_vm = spu_event->physical_wide & 0x1f;
      break;

    case DVDNAV_AUDIO_STREAM_CHANGE:
      audio_event = (void *)block;
      dp->dp_audio_track_vm = audio_event->physical & 0x7;
      break;

    case DVDNAV_VTS_CHANGE:
      mp_send_cmd(mp, &mp->mp_video, MB_DVD_RESET_SPU);
      dvd_video_push(dp);
      dvd_release_codecs(dp);
      mp_send_cmd(mp, &mp->mp_video, MB_FLUSH);
      dp->dp_spu_track   = DP_SPU_FOLLOW_VM;
      dp->dp_audio_track = DP_AUDIO_FOLLOW_VM;
      dp->dp_aspect_override = dvdnav_get_video_aspect(dp->dp_dvdnav) ? 2 : 1;
      dp->dp_epoch++;
      break;

    case DVDNAV_NAV_PACKET:
      pci = malloc(sizeof(pci_t));
      memcpy(pci, dvdnav_get_current_nav_pci(dp->dp_dvdnav), sizeof(pci_t));
      if(dp->dp_end_ptm != pci->pci_gi.vobu_s_ptm)
	dp->dp_epoch++; // Discontinuity
      dp->dp_end_ptm = pci->pci_gi.vobu_e_ptm;

      mp_send_cmd_data(mp, &mp->mp_video, MB_DVD_PCI, pci);
      break;

    case DVDNAV_HIGHLIGHT:
      hevent = (dvdnav_highlight_event_t *)block;
      mp_send_cmd_u32_head(mp, &mp->mp_video, MB_DVD_HILITE, hevent->buttonN);
      break;
      
    case DVDNAV_CELL_CHANGE:
      mp_send_cmd(mp, &mp->mp_video, MB_DVD_RESET_SPU);
      break;

    case DVDNAV_SPU_CLUT_CHANGE:
      data = malloc(sizeof(uint32_t) * 16);
      memcpy(data, block, sizeof(uint32_t) * 16);
      mp_send_cmd_data(mp, &mp->mp_video, MB_DVD_CLUT, data);
      break;

    case DVDNAV_WAIT:
      if((e = mp_wait_for_empty_queues(mp)) == NULL)
	dvdnav_wait_skip(dp->dp_dvdnav);
      else
	e = dvd_process_event(dp, e);
      break;

    case DVDNAV_HOP_CHANNEL:
      break;

    case DVDNAV_STILL_FRAME:
      dvd_video_push(dp);
      t = *(int *)block;

      if(t == 255) {
	e = mp_dequeue_event(mp);
      } else {
	deadline = time(NULL) + t;
	if((e = mp_dequeue_event_deadline(mp, deadline)) == NULL) {
	  dvdnav_still_skip(dp->dp_dvdnav);
	  break;
	}
      }

      e = dvd_process_event(dp, e);
      break;

    default:
      printf("Unknown event %d\n", event);
      abort();
    }
    dvdnav_free_cache_block(dp->dp_dvdnav, block);
  }

  mp_shutdown(mp);

  dvd_release_codecs(dp);
  dvdnav_close(dp->dp_dvdnav);
  free(dp);
  return e;
}


/**
 *
 */
static event_t *
dvd_process_event(dvd_player_t *dp, event_t *e)
{
  pci_t *pci = &dp->dp_pci;
  media_pipe_t *mp = dp->dp_mp;

  switch(e->e_type) {
  case EVENT_EXIT:
  case EVENT_PLAY_URL:
    return e;

  case EVENT_PLAYPAUSE:
  case EVENT_PLAY:
  case EVENT_PAUSE:
    if(dvd_in_menu(dp))
      break;

    mp_become_primary(mp);
    dp->dp_hold = mp_update_hold_by_event(dp->dp_hold, e->e_type);
    mp_send_cmd_head(mp, &mp->mp_video, 
		     dp->dp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_send_cmd_head(mp, &mp->mp_audio, 
		     dp->dp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    dp->dp_lost_focus = 0;
    break;

  case EVENT_MP_NO_LONGER_PRIMARY:
    if(dvd_in_menu(dp))
      break;

    dp->dp_hold = 1;
    dp->dp_lost_focus = 1;
    mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PAUSE);
    mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PAUSE);
    break;

  case EVENT_MP_IS_PRIMARY:
    if(dvd_in_menu(dp))
      break;

    if(dp->dp_lost_focus) {
      dp->dp_hold = 0;
      dp->dp_lost_focus = 0;
      mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PLAY);
      mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PLAY);
    }
    break;

  case EVENT_ENTER:
    dvdnav_button_activate(dp->dp_dvdnav, pci);
    break;
  case EVENT_UP:
    dvdnav_upper_button_select(dp->dp_dvdnav, pci);
    break;
  case EVENT_DOWN:
    dvdnav_lower_button_select(dp->dp_dvdnav, pci);
    break;
  case EVENT_LEFT:
    dvdnav_left_button_select(dp->dp_dvdnav, pci);
    break;
  case EVENT_RIGHT:
    dvdnav_right_button_select(dp->dp_dvdnav, pci);
    break;

  case EVENT_DVD_PCI:
    memcpy(&dp->dp_pci, e->e_payload, sizeof(pci_t));
    break;

  case EVENT_DVD_SELECT_BUTTON:
    dvdnav_button_select(dp->dp_dvdnav, pci, e->e_payload[0]);
    break;

  case EVENT_DVD_ACTIVATE_BUTTON:
    dvdnav_button_select_and_activate(dp->dp_dvdnav, pci, e->e_payload[0]);
    break;

  default:
    break;
  }

  event_unref(e);
  return NULL;
}


/**
 *
 */
static char *
make_nice_title(const char *t)
{
  int l = strlen(t);
  char *ret = malloc(l + 1);
  char *r;
  int uc = 1;

  ret[l] = 0;
  r = ret;

  while(*t) {
    if(*t == '_' || *t == ' ') {
      *r++ = ' ';
      uc = 1;
    } else if(uc) {
      *r++ = toupper(*t);
      uc = 0;
    } else {
      *r++ = tolower(*t);
    }
    t++;
  }
  return ret;
}
