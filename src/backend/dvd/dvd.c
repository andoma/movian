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
#include "showtime.h"
#include "metadata/metadata.h"

#include <libavcodec/avcodec.h>

#include <fileaccess/svfs.h>
#include <dvdnav/dvdnav.h>

static char *make_nice_title(const char *t);

static const char *dvd_langcode_to_string(uint16_t langcode);


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

  media_codec_t *dp_video;
  media_codec_t *dp_audio;
  media_codec_t *dp_spu;

  uint8_t dp_buf[DVD_VIDEO_LB_LEN];

  pci_t dp_pci;

  int dp_epoch;

  uint32_t dp_end_ptm;  /* end ptm from last nav packet */

  int dp_hold;
  int dp_lost_focus;

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

  mb = media_buf_alloc_unlocked(mp, 0);
  mb->mb_cw = media_codec_ref(cw);
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
dvd_media_enqueue(dvd_player_t *dp, media_queue_t *mq, media_codec_t *cw,
		  int data_type, void *data, int datalen, int rate,
		  int64_t dts, int64_t pts)
{
  media_buf_t *mb = media_buf_alloc_unlocked(dp->dp_mp, datalen);
  event_t *e;

  AVCodecContext *ctx = cw->codec_ctx;

  mb->mb_cw = media_codec_ref(cw);
  mb->mb_data_type = data_type;
  mb->mb_duration = cw->codec_ctx->ticks_per_frame * 
    1000000LL * av_q2d(ctx->time_base);
  mb->mb_aspect_override = dp->dp_aspect_override;
  mb->mb_disable_deinterlacer = 1;
  mb->mb_dts = dts;
  mb->mb_pts = pts;
  //  mb->mb_time = (dvdnav_get_current_time(dp->dp_dvdnav) * 1000000) / 90000;
  mb->mb_epoch = dp->dp_epoch;
  
  memcpy(mb->mb_data, data, datalen);

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
  int track;
  media_codec_t *cw, **cwp;
  AVCodecContext *ctx;
  enum CodecID codec_id;
  AVRational mpeg_tc = {1, 90000};
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

  if(sc >= 0x1e0 && sc <= 0x1ef) {
    codec_id  = CODEC_ID_MPEG2VIDEO;
    data_type = MB_VIDEO;
    rate = dp->dp_aspect_override;
    cwp = &dp->dp_video;
    mq = &mp->mp_video;

    mcp.width = dp->dp_vwidth;
    mcp.height = dp->dp_vheight;

  } else if((sc >= 0x80 && sc <= 0x9f) || (sc >= 0x1c0 && sc <= 0x1df)) {

    if(dp->dp_audio_track == DP_AUDIO_DISABLE)
      return NULL;
    
    track = dp->dp_audio_track == DP_AUDIO_FOLLOW_VM ? 
      dp->dp_audio_track_vm : dp->dp_audio_track;

    if((sc & 7) != track)
      return NULL;

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
    data_type = MB_DVD_SPU;

    cwp = &dp->dp_spu;
    mq = &mp->mp_video;

  } else {
    return NULL;
  }
  
  cw = *cwp;

  if(cw == NULL || cw->codec->id != codec_id) {
    if(cw != NULL)
      media_codec_deref(cw);

    *cwp = cw = media_codec_create(codec_id, 1, NULL, NULL, &mcp, mp);
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
    rlen = av_parser_parse2(cw->parser_ctx, ctx, &outbuf, &outlen, buf, len, 
			    pts, dts, AV_NOPTS_VALUE);
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
static void
dvd_init_streams(dvd_player_t *dp, media_pipe_t *mp)
{
  prop_destroy_childs(mp->mp_prop_audio_tracks);
  mp_add_track(mp->mp_prop_audio_tracks, "Off", "audio:off",
	       NULL, NULL, NULL, NULL, _p("DVD"), 0);
  mp_add_track(mp->mp_prop_audio_tracks, "Auto", "audio:auto",
	       NULL, NULL, NULL, NULL, _p("DVD"), 0);

  prop_destroy_childs(mp->mp_prop_subtitle_tracks);
  mp_add_track(mp->mp_prop_subtitle_tracks, "Off", "sub:off",
	       NULL, NULL, NULL, NULL, _p("DVD"), 0);
  mp_add_track(mp->mp_prop_subtitle_tracks, "Auto", "sub:auto",
	       NULL, NULL, NULL, NULL, _p("DVD"), 0);
}



/**
 *
 */
static void 
dvd_set_audio_stream(dvd_player_t *dp, const char *id)
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
}


/**
 *
 */
static void 
dvd_set_spu_stream(dvd_player_t *dp, const char *id)
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
}


/**
 *
 */
static void
dvd_update_streams(dvd_player_t *dp)
{
  int i;
  uint16_t lang;
  prop_t *before = NULL, *p;
  media_pipe_t *mp = dp->dp_mp;

  for(i = 7; i >= 0; i--) {

    if(dvdnav_get_audio_logical_stream(dp->dp_dvdnav, i) == -1 ||
       (lang = dvdnav_audio_stream_to_lang(dp->dp_dvdnav, i)) == 0xffff) {

      /* Not present */

      if(dp->dp_audio_props[i] != NULL) {
	prop_destroy(dp->dp_audio_props[i]);
	dp->dp_audio_props[i] = NULL;
      }

    } else {
      
      if((p = dp->dp_audio_props[i]) == NULL) {
	p = dp->dp_audio_props[i] = prop_create_root(NULL);
	if(prop_set_parent_ex(p, mp->mp_prop_audio_tracks, before, NULL))
	  abort();
      }

      prop_set_string(prop_create(p, "title"), dvd_langcode_to_string(lang));
      prop_set_stringf(prop_create(p, "id"), "audio:%d", i);


      int channels = dvdnav_audio_stream_channels(dp->dp_dvdnav, i);
      prop_set_int(prop_create(p, "channels"), channels);

      const char *chtxt;
      switch(channels) {
      case 1:  chtxt = "mono";   break;
      case 2:  chtxt = "stereo"; break;
      case 6:  chtxt = "5.1";    break;
      default: chtxt = "???";    break;
      }
      
      prop_set_string(prop_create(p, "channelstext"), chtxt);

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

      before = p;
    }
  }

  before = NULL;
  for(i = 31; i >= 0; i--) {

    if(dvdnav_get_spu_logical_stream(dp->dp_dvdnav, i) == -1 ||
       (lang = dvdnav_spu_stream_to_lang(dp->dp_dvdnav, i)) == 0xffff) {

      /* Not present */

      if(dp->dp_spu_props[i] != NULL) {
	prop_destroy(dp->dp_spu_props[i]);
	dp->dp_spu_props[i] = NULL;
      }

    } else {
      
      if((p = dp->dp_spu_props[i]) == NULL) {
	p = dp->dp_spu_props[i] = prop_create_root(NULL);
	if(prop_set_parent_ex(p, mp->mp_prop_subtitle_tracks, before, NULL))
	  abort();
      }

      prop_set_string(prop_create(p, "title"), dvd_langcode_to_string(lang));
      prop_set_stringf(prop_create(p, "id"), "sub:%d", i);
      before = p;
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

  prop_set_int(prop_create(mp->mp_prop_root, "currenttitle"), title);
  prop_set_int(prop_create(mp->mp_prop_metadata, "titles"), titles);

  prop_set_int(prop_create(mp->mp_prop_root, "currentchapter"), part);
  prop_set_int(prop_create(mp->mp_prop_metadata, "chapters"), parts);
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

  prop_set_string(mp->mp_prop_type, "dvd");

  TRACE(TRACE_DEBUG, "DVD", "Starting playback of %s", url);

  prop_set_stringf(prop_create(mp->mp_prop_metadata, "format"), "DVD");

 restart:
  dp = calloc(1, sizeof(dvd_player_t));

  dp->dp_epoch = 1;

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

  /**
   * By default, follow DVD VM machine
   */
  dvd_set_audio_stream(dp, "auto");
  dvd_set_spu_stream(dp, "auto");

  mp_become_primary(mp);

  mp_configure(mp, MP_PLAY_CAPS_PAUSE | MP_PLAY_CAPS_EJECT,
	       MP_BUFFER_SHALLOW); /* Might wanna use deep buffering
				      but it requires some modification
				      to buffer draining code */

  mp_set_playstatus_by_hold(mp, dp->dp_hold, NULL);

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
      dvd_set_audio_stream(dp, "auto");
      dvd_set_spu_stream(dp, "auto");
      dp->dp_aspect_override = dvdnav_get_video_aspect(dp->dp_dvdnav) ? 2 : 1;
      dvdnav_get_video_res(dp->dp_dvdnav, &dp->dp_vwidth, &dp->dp_vheight);
      dp->dp_epoch++;
      dvd_update_streams(dp);
      break;

    case DVDNAV_NAV_PACKET:
      pci = malloc(sizeof(pci_t));
      memcpy(pci, dvdnav_get_current_nav_pci(dp->dp_dvdnav), sizeof(pci_t));
      if(dp->dp_end_ptm != pci->pci_gi.vobu_s_ptm)
	dp->dp_epoch++; // Discontinuity
      dp->dp_end_ptm = pci->pci_gi.vobu_e_ptm;

      update_chapter(dp, mp);

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

  if(event_is_type(e, EVENT_EXIT) ||
     event_is_type(e, EVENT_PLAY_URL))
    return e;

  if(event_is_type(e, EVENT_SELECT_AUDIO_TRACK)) {
    event_select_track_t *est = (event_select_track_t *)e;
    
    if(!strncmp(est->id, "audio:", strlen("audio:")))
      dvd_set_audio_stream(dp, est->id + strlen("audio:"));

  } else if(event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)) {
    event_select_track_t *est = (event_select_track_t *)e;

    if(!strncmp(est->id, "sub:", strlen("sub:")))
      dvd_set_spu_stream(dp, est->id + strlen("sub:"));

  } else if(!dvd_in_menu(dp) && 
     (event_is_action(e, ACTION_PLAYPAUSE) ||
      event_is_action(e, ACTION_PLAY) ||
      event_is_action(e, ACTION_PAUSE))) {

    mp_become_primary(mp);
    dp->dp_hold = action_update_hold_by_event(dp->dp_hold, e);
    mp_send_cmd_head(mp, &mp->mp_video, 
		     dp->dp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_send_cmd_head(mp, &mp->mp_audio, 
		     dp->dp_hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, dp->dp_hold, NULL);
    dp->dp_lost_focus = 0;

  } else if(!dvd_in_menu(dp) && event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

    dp->dp_hold = 1;
    dp->dp_lost_focus = 1;
    mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PAUSE);
    mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PAUSE);
    mp_set_playstatus_by_hold(mp, dp->dp_hold, e->e_payload);
    
  } else if(!dvd_in_menu(dp) && event_is_type(e, EVENT_MP_IS_PRIMARY)) {

    if(dp->dp_lost_focus) {
      dp->dp_hold = 0;
      dp->dp_lost_focus = 0;
      mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_PLAY);
      mp_send_cmd_head(mp, &mp->mp_audio, MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(mp, dp->dp_hold, NULL);
    }

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

    memcpy(&dp->dp_pci, e->e_payload, sizeof(pci_t));

  } else if(event_is_type(e, EVENT_DVD_SELECT_BUTTON)) {

    dvdnav_button_select(dp->dp_dvdnav, pci, e->e_payload[0]);

  } else if(event_is_type(e, EVENT_DVD_ACTIVATE_BUTTON)) {

    dvdnav_button_select_and_activate(dp->dp_dvdnav, pci, e->e_payload[0]);

  } else if(event_is_action(e, ACTION_PREV_TRACK)) {

    dvdnav_prev_pg_search(dp->dp_dvdnav);
    mp_flush(mp, 1);

  } else if(event_is_action(e, ACTION_NEXT_TRACK)) {

    dvdnav_next_pg_search(dp->dp_dvdnav);
    mp_flush(mp, 1);

  } else if(event_is_action(e, ACTION_STOP)) {
    mp_set_playstatus_stop(mp);

  } else if(event_is_action(e, ACTION_EJECT)) {
    mp_set_playstatus_stop(mp);
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


/**
 * DVD language codes
 */
const struct {
  const char *langcode;
  const char *displayname;
} langtbl[] = {
  {"AB", "Abkhazian"},
  {"LT", "Lithuanian"},
  {"AA", "Afar"},
  {"MK", "Macedonian"},
  {"AF", "Afrikaans"},
  {"MG", "Malagasy"},
  {"SQ", "Albanian"},
  {"MS", "Malay"},
  {"AM", "Amharic"},
  {"ML", "Malayalam"},
  {"AR", "Arabic"},
  {"MT", "Maltese"},
  {"HY", "Armenian"},
  {"MI", "Maori"},
  {"AS", "Assamese"},
  {"MR", "Marathi"},
  {"AY", "Aymara"},
  {"MO", "Moldavian"},
  {"AZ", "Azerbaijani"},
  {"MN", "Mongolian"},
  {"BA", "Bashkir"},
  {"NA", "Nauru"},
  {"EU", "Basque"},
  {"NE", "Nepali"},
  {"BN", "Bengali"},
  {"NO", "Norwegian"},
  {"DZ", "Bhutani"},
  {"OC", "Occitan"},
  {"BH", "Bihari"},
  {"OR", "Oriya"},
  {"BI", "Bislama"},
  {"OM", "Afan"},
  {"BR", "Breton"},
  {"PA", "Panjabi"},
  {"BG", "Bulgarian"},
  {"PS", "Pashto"},
  {"MY", "Burmese"},
  {"FA", "Persian"},
  {"BE", "Byelorussian"},
  {"PL", "Polish"},
  {"KM", "Cambodian"},
  {"PT", "Portuguese"},
  {"CA", "Catalan"},
  {"QU", "Quechua"},
  {"ZH", "Chinese"},
  {"RM", "Rhaeto-Romance"},
  {"CO", "Corsican"},
  {"RO", "Romanian"},
  {"HR", "Croatian"},
  {"RU", "Russian"},
  {"CS", "Czech"},
  {"SM", "Samoan"},
  {"DA", "Danish"},
  {"SG", "Sangho"},
  {"NL", "Dutch"},
  {"SA", "Sanskrit"},
  {"EN", "English"},
  {"GD", "Gaelic"},
  {"EO", "Esperanto"},
  {"SH", "Serbo-Crotain"},
  {"ET", "Estonian"},
  {"ST", "Sesotho"},
  {"FO", "Faroese"},
  {"SR", "Serbian"},
  {"FJ", "Fiji"},
  {"TN", "Setswana"},
  {"FI", "Finnish"},
  {"SN", "Shona"},
  {"FR", "French"},
  {"SD", "Sindhi"},
  {"FY", "Frisian"},
  {"SI", "Singhalese"},
  {"GL", "Galician"},
  {"SS", "Siswati"},
  {"KA", "Georgian"},
  {"SK", "Slovak"},
  {"DE", "German"},
  {"SL", "Slovenian"},
  {"EL", "Greek"},
  {"SO", "Somali"},
  {"KL", "Greenlandic"},
  {"ES", "Spanish"},
  {"GN", "Guarani"},
  {"SU", "Sundanese"},
  {"GU", "Gujarati"},
  {"SW", "Swahili"},
  {"HA", "Hausa"},
  {"SV", "Swedish"},
  {"IW", "Hebrew"},
  {"TL", "Tagalog"},
  {"HI", "Hindi"},
  {"TG", "Tajik"},
  {"HU", "Hungarian"},
  {"TT", "Tatar"},
  {"IS", "Icelandic"},
  {"TA", "Tamil"},
  {"IN", "Indonesian"},
  {"TE", "Telugu"},
  {"IA", "Interlingua"},
  {"TH", "Thai"},
  {"IE", "Interlingue"},
  {"BO", "Tibetian"},
  {"IK", "Inupiak"},
  {"TI", "Tigrinya"},
  {"GA", "Irish"},
  {"TO", "Tonga"},
  {"IT", "Italian"},
  {"TS", "Tsonga"},
  {"JA", "Japanese"},
  {"TR", "Turkish"},
  {"JW", "Javanese"},
  {"TK", "Turkmen"},
  {"KN", "Kannada"},
  {"TW", "Twi"},
  {"KS", "Kashmiri"},
  {"UK", "Ukranian"},
  {"KK", "Kazakh"},
  {"UR", "Urdu"},
  {"RW", "Kinyarwanda"},
  {"UZ", "Uzbek"},
  {"KY", "Kirghiz"},
  {"VI", "Vietnamese"},
  {"RN", "Kirundi"},
  {"VO", "Volapuk"},
  {"KO", "Korean"},
  {"CY", "Welsh"},
  {"KU", "Kurdish"},
  {"WO", "Wolof"},
  {"LO", "Laothian"},
  {"JI", "Yiddish"},
  {"LA", "Latin"},
  {"YO", "Yoruba"},
  {"LV", "Lettish"},
  {"XH", "Xhosa"},
  {"LN", "Lingala"},
  {"ZU", "Zulu"},
  {NULL, NULL}
};

/**
 *
 */
static const char *
dvd_langcode_to_string(uint16_t langcode)
{
  char str[3];
  int i;

  str[0] = langcode >> 8;
  str[1] = langcode & 0xff;
  str[2] = 0;

  i = 0;
  
  while(langtbl[i].langcode != NULL) {
    if(!strcasecmp(langtbl[i].langcode, str))
      return langtbl[i].displayname;
    i++;
  }
  return "Other";
}
