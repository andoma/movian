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
#include <stdio.h>
#include <ctype.h>

#include "event.h"
#include "media/media.h"
#include "fileaccess/fileaccess.h"
#include "dvd.h"
#include "notifications.h"
#include "main.h"
#include "metadata/metadata.h"
#include "misc/isolang.h"
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>

#include "fileaccess/svfs.h"
#include "dvdnav/dvdnav.h"
#include "usage.h"

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


const static AVRational mpeg_tc = {1, 90000};


static void *
dvd_fa_open(const char *url)
{
  return fa_open_ex(url, NULL, 0, FA_BUFFERED_BIG, NULL);
}

static int
dvd_fa_stat(const char *url, struct stat *st)
{
  struct fa_stat fs;

  if(fa_stat(url, &fs, NULL, 0))
    return -1;

  if(fs.fs_size == -1)
    return -1; // Not a seekable file

  st->st_size  = fs.fs_size;
  st->st_mode  = fs.fs_type == CONTENT_DIR ? S_IFDIR : S_IFREG;
  st->st_mtime = fs.fs_mtime;
  return 0;
}


static int64_t
dvd_fa_seek(void *fh, int64_t pos, int whence)
{
  return fa_seek(fh, pos, whence);
}



/**
 *
 */
static struct svfs_ops faops = {
  .open = dvd_fa_open,
  .close = fa_close,
  .read = fa_read,
  .seek = dvd_fa_seek,
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

  media_codec_t *dp_video;
  media_codec_t *dp_audio;
  media_codec_t *dp_spu;

  uint8_t dp_buf[DVD_VIDEO_LB_LEN];

  pci_t dp_pci;

  uint32_t dp_end_ptm;  /* end ptm from last nav packet */

  int dp_hold;

  prop_t *dp_audio_props[8];
  prop_t *dp_spu_props[32];

  int dp_vwidth;
  int dp_vheight;

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
    media_codec_deref(dp->dp_video);
    dp->dp_video = NULL;
  }
  if(dp->dp_audio != NULL) {
    media_codec_deref(dp->dp_audio);
    dp->dp_audio = NULL;
  }
  if(dp->dp_spu != NULL) {
    media_codec_deref(dp->dp_spu);
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
  media_codec_t *cw = dp->dp_video;
  media_pipe_t *mp = dp->dp_mp;

  if(cw == NULL)
    return;

  const AVCodecContext *ctx = cw->fmt_ctx;

  mb = media_buf_alloc_unlocked(mp, 0);
  mb->mb_cw = media_codec_ref(cw);
  mb->mb_aspect_override = dp->dp_aspect_override;
  mb->mb_disable_deinterlacer = 1;
  mb->mb_data_type = MB_VIDEO;
  mb->mb_duration = ctx->ticks_per_frame * 1000000LL * av_q2d(ctx->time_base);
  mb->mb_pts = AV_NOPTS_VALUE;
  mb->mb_dts = AV_NOPTS_VALUE;

  mb_enqueue_always(mp, &mp->mp_video, mb);
}



/**
 *
 */
static event_t *
dvd_media_enqueue0(dvd_player_t *dp, media_queue_t *mq, media_buf_t *mb,
		   int64_t dts, int64_t pts)
{
  event_t *e;

  mb->mb_disable_deinterlacer = 1;
  mb->mb_dts = dts;
  mb->mb_pts = pts;

  if(mb->mb_data_type == MB_VIDEO) {
    mb->mb_user_time = av_rescale_q(dvdnav_get_current_time(dp->dp_dvdnav),
                                    mpeg_tc, AV_TIME_BASE_Q);
    mb->mb_drive_clock = 1;
  }

  do {

    if((e = mb_enqueue_with_events(dp->dp_mp, mq, mb)) == NULL) {
      mb = NULL;
      break;
    }

    e = dvd_process_event(dp, e);

  } while(e == NULL);

  if(mb != NULL)
    media_buf_free_unlocked(dp->dp_mp, mb);

  return e;
}



/**
 *
 */
static event_t *
dvd_media_enqueue(dvd_player_t *dp, media_queue_t *mq, media_codec_t *cw,
		  int data_type, void *data, int datalen,
		  int64_t dts, int64_t pts)
{
  media_buf_t *mb = media_buf_alloc_unlocked(dp->dp_mp, datalen);

  const AVCodecContext *ctx = cw->fmt_ctx;

  mb->mb_cw = media_codec_ref(cw);
  mb->mb_data_type = data_type;
  mb->mb_duration = 2000000LL * av_q2d(ctx->time_base);
  mb->mb_aspect_override = dp->dp_aspect_override;
  memcpy(mb->mb_data, data, datalen);
  return dvd_media_enqueue0(dp, mq, mb, dts, pts);
}

static const int lpcm_freq_tab[4] = { 48000, 96000, 44100, 32000 };

/**
 *
 */
static event_t *
dvd_lpcm(dvd_player_t *dp, const uint8_t *buf, int len,
	 int64_t dts, int64_t pts)
{
  if(len < 3)
    return NULL;

  int channels = 1 + (buf[1] & 0x7);
  int freq     = lpcm_freq_tab[(buf[1] >> 4) & 3];
  int bps      = 16 + ((buf[1] >> 6) & 3) * 3;

  if(bps != 16)
    return NULL;

  buf += 3;
  len -= 3;

  int frames = len / channels / (bps >> 3);
  media_buf_t *mb = media_buf_alloc_unlocked(dp->dp_mp, len);

  mb->mb_data_type = MB_AUDIO;
  mb->mb_duration = 1000000 * frames / freq;
  mb->mb_channels = channels;
  mb->mb_rate = freq;

#if defined(__BIG_ENDIAN__)
  memcpy(mb->mb_data, buf, len);
#else
  const uint16_t *src = (const uint16_t *)buf;
  uint16_t *dst = (void *)mb->mb_data;
  int i;
  for(i = 0; i < len / 2; i++) {
    *dst++ = (*src >> 8) | (*src << 8) ;
    src++;
  }
#endif
  return dvd_media_enqueue0(dp, &dp->dp_mp->mp_audio, mb, dts, pts);
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
  int rlen, outlen, data_type = 0;
  uint8_t *outbuf;
  int track;
  media_codec_t *cw, **cwp;
  AVCodecContext *ctx;
  enum AVCodecID codec_id;
  event_t *e;
  media_codec_params_t mcp = {0};

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

  if(dts != AV_NOPTS_VALUE)
    dts = av_rescale_q(dts, mpeg_tc, AV_TIME_BASE_Q);
  
  if(pts != AV_NOPTS_VALUE)
    pts = av_rescale_q(pts, mpeg_tc, AV_TIME_BASE_Q);


  if(sc >= 0x1e0 && sc <= 0x1ef) {
    codec_id  = AV_CODEC_ID_MPEG2VIDEO;
    data_type = MB_VIDEO;
    cwp = &dp->dp_video;
    mq = &mp->mp_video;

    mcp.width = dp->dp_vwidth;
    mcp.height = dp->dp_vheight;

  } else if((sc >= 0x80 && sc <= 0xaf) || (sc >= 0x1c0 && sc <= 0x1df)) {

    if(dp->dp_audio_track == DP_AUDIO_DISABLE)
      return NULL;
    
    track = dp->dp_audio_track == DP_AUDIO_FOLLOW_VM ? 
      dp->dp_audio_track_vm : dp->dp_audio_track;

    if((sc & 7) != track)
      return NULL;

    data_type = MB_AUDIO;

    switch(sc) {
    case 0x80 ... 0x87:
      codec_id = AV_CODEC_ID_AC3;
      break;
	    
    case 0x88 ... 0x9f:
      codec_id = AV_CODEC_ID_DTS;
      break;

    case 0xa0 ... 0xaf:
      return dvd_lpcm(dp, buf, len, dts, pts);


    case 0x1c0 ... 0x1df:
      codec_id = AV_CODEC_ID_MP2;
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

    codec_id  = AV_CODEC_ID_DVD_SUBTITLE;
    data_type = MB_DVD_SPU;

    cwp = &dp->dp_spu;
    mq = &mp->mp_video;

  } else {
    return NULL;
  }
  
  cw = *cwp;

  if(cw == NULL || cw->codec_id != codec_id) {
    if(cw != NULL)
      media_codec_deref(cw);

    *cwp = cw = media_codec_create(codec_id, 1, NULL, NULL, &mcp, mp);
    if(cw == NULL)
      return NULL;
  }

  ctx = cw->fmt_ctx;
 
  if(cw->parser_ctx == NULL) /* No parser available */
    return dvd_media_enqueue(dp, mq, cw, data_type, buf, len, dts, pts);

  while(len > 0) {
    rlen = av_parser_parse2(cw->parser_ctx, ctx, &outbuf, &outlen, buf, len, 
			    pts, dts, 0);
    if(outlen) {
      e = dvd_media_enqueue(dp, mq, cw, data_type, outbuf, outlen,
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
static void
dvd_init_streams(dvd_player_t *dp, media_pipe_t *mp)
{
  prop_destroy_childs(mp->mp_prop_audio_tracks);

  mp_add_track(mp->mp_prop_audio_tracks, "Auto", "audio:auto",
	       NULL, NULL, NULL, NULL, _p("DVD"), 50, 1);

  prop_destroy_childs(mp->mp_prop_subtitle_tracks);

  mp_add_track_off(mp->mp_prop_subtitle_tracks, "sub:off");
  mp_add_track(mp->mp_prop_subtitle_tracks, "Auto", "sub:auto",
	       NULL, NULL, NULL, NULL, _p("DVD"), 50, 1);
}



/**
 *
 */
static void
dvd_set_audio_stream(dvd_player_t *dp, const char *id, int user)
{
  if(!strcmp(id, "off")) {
    dp->dp_audio_track = DP_AUDIO_DISABLE;
  } else if(!strcmp(id, "auto")) {
    dp->dp_audio_track = DP_AUDIO_FOLLOW_VM;
  } else {
    int idx = atoi(id);
    dp->dp_audio_track = idx;
  }

  prop_set_stringf(dp->dp_mp->mp_prop_audio_track_current,  "audio:%s", id);
  prop_set_int(dp->dp_mp->mp_prop_audio_track_current_manual, user);
}


/**
 *
 */
static void 
dvd_set_spu_stream(dvd_player_t *dp, const char *id, int manual)
{
  if(!strcmp(id, "off")) {
    dp->dp_spu_track = DP_SPU_DISABLE;
  } else if(!strcmp(id, "auto")) {
    dp->dp_spu_track = DP_SPU_FOLLOW_VM;
  } else {
    int idx = atoi(id);
    dp->dp_spu_track = idx;
  }
  prop_set_stringf(dp->dp_mp->mp_prop_subtitle_track_current,  "sub:%s", id);
  prop_set_int(dp->dp_mp->mp_prop_subtitle_track_current_manual, manual);
}


/**
 *
 */
static const isolang_t *
dvdlang(uint16_t code)
{
  char buf[3];
  buf[0] = code >> 8;
  buf[1] = code;
  buf[2] = 0;
  return isolang_find(buf);
}

/**
 *
 */
static void
dvd_update_streams(dvd_player_t *dp)
{
  int i;
  uint16_t lang;
  prop_t *p;
  media_pipe_t *mp = dp->dp_mp;

  for(i = 0; i < 8; i++) {

    if(dvdnav_get_audio_logical_stream(dp->dp_dvdnav, i) == -1) {

      /* Not present */

      if(dp->dp_audio_props[i] != NULL) {
	prop_destroy(dp->dp_audio_props[i]);
	dp->dp_audio_props[i] = NULL;
      }

    } else {
      
      if((p = dp->dp_audio_props[i]) == NULL) {
	p = dp->dp_audio_props[i] = prop_create_root(NULL);
	if(prop_set_parent(p, mp->mp_prop_audio_tracks))
	  abort();
      }

      prop_set_stringf(prop_create(p, "url"), "audio:%d", i);

      int channels = dvdnav_audio_stream_channels(dp->dp_dvdnav, i);

      const char *chtxt;
      switch(channels) {
      case 1:  chtxt = "Mono";   break;
      case 2:  chtxt = "Stereo"; break;
      case 6:  chtxt = "5.1";    break;
      default: chtxt = NULL;     break;
      }
      
      prop_set(p, "title", PROP_SET_STRING, chtxt);

      const char *format;
      switch(dvdnav_audio_stream_format(dp->dp_dvdnav, i)) {
      case DVDNAV_FORMAT_AC3:       format = "AC3";  break;
      case DVDNAV_FORMAT_MPEGAUDIO: format = "MPEG"; break;
      case DVDNAV_FORMAT_LPCM:      format = "PCM";  break;
      case DVDNAV_FORMAT_DTS:       format = "DTS";  break;
      case DVDNAV_FORMAT_SDDS:      format = "SDDS"; break;
      default:                      format = "???";  break;
      }

      prop_set_string(prop_create(p, "format"), format);

      if((lang = dvdnav_audio_stream_to_lang(dp->dp_dvdnav, i)) != 0xffff) {

        const isolang_t *il = dvdlang(lang);
        if(il != NULL) {
          prop_set(p, "language", PROP_SET_STRING, il->fullname);
          prop_set(p, "isolang", PROP_SET_STRING, il->iso639_2);
        }
      }
    }
  }

  for(i = 0; i < 32; i++) {

    if(dvdnav_get_spu_logical_stream(dp->dp_dvdnav, i) == -1) {

      /* Not present */

      if(dp->dp_spu_props[i] != NULL) {
	prop_destroy(dp->dp_spu_props[i]);
	dp->dp_spu_props[i] = NULL;
      }

    } else {
      
      if((p = dp->dp_spu_props[i]) == NULL) {
	p = dp->dp_spu_props[i] = prop_create_root(NULL);
	if(prop_set_parent(p, mp->mp_prop_subtitle_tracks))
	  abort();
      }

      prop_link(_p("DVD"), prop_create(p, "source"));

      if((lang = dvdnav_spu_stream_to_lang(dp->dp_dvdnav, i)) != 0xffff)
	prop_set(p, "language", PROP_SET_STRING, dvdlang(lang));

      prop_set_stringf(prop_create(p, "url"), "sub:%d", i);
      prop_set(p, "basescore", PROP_SET_INT, 32-i);
    }
  }
}


/**
 *
 */
static void
update_chapter(dvd_player_t *dp, media_pipe_t *mp)
{
  int title = 0, titles = 0, part = 0, parts = 0;

  dvdnav_get_number_of_titles(dp->dp_dvdnav, &titles);
  dvdnav_current_title_info(dp->dp_dvdnav, &title, &part);
  dvdnav_get_number_of_parts(dp->dp_dvdnav, title, &parts);

  prop_set(mp->mp_prop_root, "currenttitle", PROP_SET_INT, title);
  prop_set(mp->mp_prop_metadata, "titles", PROP_SET_INT, titles);
  prop_set(mp->mp_prop_root, "currentchapter", PROP_SET_INT, part);
  prop_set(mp->mp_prop_metadata, "chapters", PROP_SET_INT, parts);
}

/**
 *
 */
static void
update_duration(dvd_player_t *dp, media_pipe_t *mp)
{
  uint64_t totdur;
  uint64_t *times;
  int title = 0, part = 0;
  dvdnav_current_title_info(dp->dp_dvdnav, &title, &part);

  if(dvdnav_describe_title_chapters(dp->dp_dvdnav, title, &times, &totdur)) {
    totdur = av_rescale_q(totdur, mpeg_tc, AV_TIME_BASE_Q);
    prop_set(mp->mp_prop_metadata, "duration", PROP_SET_INT,
	     totdur / 1000000LL);
    free(times);
  } else {
    prop_set_void(prop_create(mp->mp_prop_metadata, "duration"));
  }
}


/**
 *
 */
event_t *
dvd_play(const char *url, media_pipe_t *mp, char *errstr, size_t errlen,
	 int vfs)
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
  const char *title;

  usage_event("Play video", 1, USAGE_SEG("format", "DVD"));

  TRACE(TRACE_DEBUG, "DVD", "Starting playback of %s", url);

  prop_set_stringf(prop_create(mp->mp_prop_metadata, "format"), "DVD");

 restart:
  dp = calloc(1, sizeof(dvd_player_t));

  dp->dp_mp = mp;

  mp->mp_video.mq_stream = 0;
  mp->mp_audio.mq_stream = 0;

  if(dvdnav_open(&dp->dp_dvdnav, url, 
		 vfs ? &faops : NULL) != DVDNAV_STATUS_OK) {
    snprintf(errstr, errlen, "dvdnav: Unable to open DVD");
    free(dp);
    return NULL;
  }
  dvdnav_set_readahead_flag(dp->dp_dvdnav, 1);
  dvdnav_set_PGC_positioning_flag(dp->dp_dvdnav, 1);

  mp_become_primary(mp);

  /* Might wanna use deep buffering but it requires some modification
     to buffer draining code */

  mp_configure(mp, MP_CAN_PAUSE | MP_CAN_EJECT,
	       MP_BUFFER_SHALLOW, 0, "dvd");

  prop_set_int(mp->mp_prop_canSkipForward,  1);
  prop_set_int(mp->mp_prop_canSkipBackward, 1);

  dvd_init_streams(dp, mp);

  if(dvdnav_get_title_string(dp->dp_dvdnav, &title) == DVDNAV_STATUS_OK) {
    char *s = NULL;

    if(title && *title) {
      s = make_nice_title(title);
    } else {
      char *x, *s0 = mystrdupa(url);
      x = strrchr(s0, '/');
      if(x != NULL && x[1] == 0)
	*x = 0;

      x = strrchr(s0, '/');
      if(x != NULL) {
	s = make_nice_title(x + 1);
      }
    }
    prop_set_string(prop_create(mp->mp_prop_metadata, "title"), s);
    free(s);
  }

  /**
   * DVD main loop
   */
  while(e == NULL) {
    block = dp->dp_buf;
    result = dvdnav_get_next_cache_block(dp->dp_dvdnav, &block, &event, &len);
    if(result == DVDNAV_STATUS_ERR) {
      notify_add(NULL, NOTIFY_ERROR, NULL, 5, 
		 _("DVD read error, restarting disc"));
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
      dvd_set_audio_stream(dp, "auto", 0);
      dvd_set_spu_stream(dp, "auto", 0);
      dp->dp_aspect_override = dvdnav_get_video_aspect(dp->dp_dvdnav) ? 2 : 1;
      dvdnav_get_video_res(dp->dp_dvdnav, &dp->dp_vwidth, &dp->dp_vheight);
      mp_bump_epoch(mp);
      dvd_update_streams(dp);
      update_duration(dp, mp);
      break;

    case DVDNAV_NAV_PACKET:
      pci = malloc(sizeof(pci_t));
      memcpy(pci, dvdnav_get_current_nav_pci(dp->dp_dvdnav), sizeof(pci_t));
      if(dp->dp_end_ptm != pci->pci_gi.vobu_s_ptm) {
	mp_bump_epoch(mp);
      }
      dp->dp_end_ptm = pci->pci_gi.vobu_e_ptm;

      update_chapter(dp, mp);

      mp_send_cmd_data(mp, &mp->mp_video, MB_DVD_PCI, pci);
      break;

    case DVDNAV_HIGHLIGHT:
      hevent = (dvdnav_highlight_event_t *)block;
      mp_send_cmd_u32(mp, &mp->mp_video, MB_CTRL_DVD_HILITE, hevent->buttonN);
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
	if((e = mp_dequeue_event_deadline(mp, t * 1000)) == NULL) {
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
  const event_payload_t *ep = (event_payload_t *)e;

  if(event_is_type(e, EVENT_EXIT) ||
     event_is_type(e, EVENT_PLAY_URL))
    return e;

  if(event_is_type(e, EVENT_SELECT_AUDIO_TRACK)) {
    event_select_track_t *est = (event_select_track_t *)e;
    
    if(!strncmp(est->id, "audio:", strlen("audio:")))
      dvd_set_audio_stream(dp, est->id + strlen("audio:"), est->manual);

  } else if(event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)) {
    event_select_track_t *est = (event_select_track_t *)e;

    if(!strncmp(est->id, "sub:", strlen("sub:")))
      dvd_set_spu_stream(dp, est->id + strlen("sub:"), est->manual);

  } else if(event_is_action(e, ACTION_ACTIVATE)) {

    dvdnav_button_activate(dp->dp_dvdnav, pci);

  } else if(event_is_action(e, ACTION_UP)) {

    dvdnav_upper_button_select(dp->dp_dvdnav, pci);

  } else if(event_is_action(e, ACTION_DOWN)) {

    dvdnav_lower_button_select(dp->dp_dvdnav, pci);

  } else if(event_is_action(e, ACTION_LEFT)) {

    dvdnav_left_button_select(dp->dp_dvdnav, pci);

  } else if(event_is_action(e, ACTION_RIGHT)) {

    dvdnav_right_button_select(dp->dp_dvdnav, pci);

  } else if(event_is_type(e, EVENT_DVD_PCI)) {

    memcpy(&dp->dp_pci, ep->payload, sizeof(pci_t));

  } else if(event_is_type(e, EVENT_DVD_SELECT_BUTTON)) {

    dvdnav_button_select(dp->dp_dvdnav, pci, ep->payload[0]);

  } else if(event_is_type(e, EVENT_DVD_ACTIVATE_BUTTON)) {

    dvdnav_button_select_and_activate(dp->dp_dvdnav, pci, ep->payload[0]);

  } else if(event_is_action(e, ACTION_SKIP_BACKWARD)) {
    
    mp_flush(mp);
    dvdnav_prev_pg_search(dp->dp_dvdnav);

  } else if(event_is_action(e, ACTION_SKIP_FORWARD)) {
    mp_flush(mp);
    dvdnav_next_pg_search(dp->dp_dvdnav);

  } else if(event_is_action(e, ACTION_EJECT)) {
    return e;
  }

  event_release(e);
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
      *r++ = toupper((int)*t);
      uc = 0;
    } else {
      *r++ = tolower((int)*t);
    }
    t++;
  }
  return ret;
}


