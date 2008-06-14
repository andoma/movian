/*
 *  Functions for MPEG pes
 *  Copyright (C) 2007 Andreas Öman
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

#include <pthread.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <stdlib.h>

#include <libavformat/avformat.h>

#include "showtime.h"
#include "dvd_mpeg.h"
#include "media.h"

#include <netinet/in.h>

static void
pes_free_stream(pes_stream_t *ps)
{
  if(ps->ps_cw != NULL)
    wrap_codec_deref(ps->ps_cw, 1);
}





void
pes_do_block(pes_player_t *pp, uint32_t sc, uint8_t *buf, int len, int w,
	     enum CodecID pkt_type)
{
  uint8_t flags, hlen, x;
  media_buf_t *mb;
  int64_t dts = AV_NOPTS_VALUE, pts = AV_NOPTS_VALUE;
  int rlen, outlen, data_type = 0, rate = 0;
  pes_stream_t *ps;
  uint8_t *outbuf;
  int type;
  codecwrap_t *cw;
  AVCodecContext *ctx;
  media_pipe_t *mp = pp->pp_mp;
  enum CodecID codec_id;
  AVRational mpeg_tc = {1, 90000};

  x = getu8(buf, len);
  flags = getu8(buf, len);
  hlen = getu8(buf, len);
  
  if(len < hlen)
    return;

  if((x & 0xc0) != 0x80)
    /* no MPEG 2 PES */
    return;

  if((flags & 0xc0) == 0xc0) {
    if(hlen < 10)
      return;

    pts = getpts(buf, len);
    dts = getpts(buf, len);

    hlen -= 10;
  } else if((flags & 0xc0) == 0x80) {
    if(hlen < 5)
      return;

    dts = pts = getpts(buf, len);
    hlen -= 5;
  } else {


  }

  buf += hlen;
  len -= hlen;

  if(sc == PRIVATE_STREAM_1) {

    if(pkt_type == CODEC_ID_AC3) {
      sc = 0x80;
    } else {
      if(len < 1)
	return;
      
      sc = getu8(buf, len);
      if(sc >= 0x80 && sc <= 0xbf) {
	/* audio, skip heeader */
	if(len < 3)
	  return;
	buf += 3;
	len -= 3;
      }
    }
  }


  if(sc > 0x1ff)
    return;

  if(sc >= 0x1e0 && sc <= 0x1ef) {
    ps = &pp->pp_video;

    codec_id = pkt_type != CODEC_ID_NONE ? pkt_type : CODEC_ID_MPEG2VIDEO;

    type = CODEC_TYPE_VIDEO;

    data_type = MB_VIDEO;
    rate = pp->pp_aspect_override;


  } else if((sc >= 0x80 && sc <= 0x9f) || (sc >= 0x1c0 && sc <= 0x1df)) {

    ps = &pp->pp_audio;

    if(ps->ps_filter_cb != NULL && !ps->ps_filter_cb(ps->ps_aux, sc, pkt_type))
      return; 

    type = CODEC_TYPE_AUDIO;
    data_type = MB_AUDIO;

    if(pkt_type != CODEC_ID_NONE) {
      codec_id = pkt_type;
      rate = 48000;
    } else {

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
	return;
      }
    }
  } else if (sc >= 0x20 && sc <= 0x3f) {
    
    ps = &pp->pp_spu;

    if(ps->ps_filter_cb != NULL && !ps->ps_filter_cb(ps->ps_aux, sc, pkt_type))
      return; 

    data_type = MB_DVD_SPU;

    codec_id = CODEC_ID_DVD_SUBTITLE;

    type = CODEC_TYPE_SUBTITLE;

  } else {
    printf("Unknown startcode %x\n", sc);
    return;
  }
  
  if(ps->ps_force_reset || ps->ps_codec_id != codec_id) {
    ps->ps_force_reset = 0;

    pes_free_stream(ps);
    
    ps->ps_codec_id = codec_id;
    ps->ps_cw = wrap_codec_create(codec_id, type, 1, pp->pp_fw, NULL);
    ps->ps_cw->codec_ctx->codec_type = type;
    ps->ps_output->mq_stream = 0;
  }


  

  if(dts != AV_NOPTS_VALUE)
    dts = av_rescale_q(dts, mpeg_tc, AV_TIME_BASE_Q);
  
  if(pts != AV_NOPTS_VALUE)
    pts = av_rescale_q(pts, mpeg_tc, AV_TIME_BASE_Q);

  cw = ps->ps_cw;
  ctx = cw->codec_ctx;
 
  wrap_lock_codec(cw);
 

  if(cw->parser_ctx == NULL) {

    /* No parseing available */

    mb = media_buf_alloc();

    mb->mb_cw = wrap_codec_ref(cw);
    mb->mb_data_type = data_type;
    mb->mb_data = malloc(len);
    mb->mb_size = len;
    mb->mb_duration = 1000000LL * av_q2d(ctx->time_base);
    mb->mb_aspect_override = rate;
    mb->mb_pts = ps->ps_pts;
    wrap_unlock_codec(cw);

    memcpy(mb->mb_data, buf, len);

    mb_enqueue(mp, ps->ps_output, mb);
    return;
  }

  while(len > 0) {

    rlen = av_parser_parse(cw->parser_ctx, ctx, &outbuf, &outlen, buf, len, 
			   pts, dts);

    if(outlen) {
	
      mb = media_buf_alloc();

      mb->mb_data_type = data_type;
      mb->mb_data = malloc(outlen);
      mb->mb_size = outlen;

      memcpy(mb->mb_data, outbuf, outlen);

      mb->mb_duration = 1000000LL * av_q2d(ctx->time_base);

      mb->mb_aspect_override = rate;
      mb->mb_pts = cw->parser_ctx->pts;

      mb->mb_cw = wrap_codec_ref(cw);

      wrap_unlock_codec(cw);
      mb_enqueue(mp, ps->ps_output, mb);
      wrap_lock_codec(cw);

    }
    
    buf += rlen;
    len -= rlen;
  }
  wrap_unlock_codec(cw);
}


void
pes_init(pes_player_t *pp, media_pipe_t *mp, formatwrap_t *fw)
{
  memset(pp, 0, sizeof(pes_player_t));

  pp->pp_mp = mp;
  pp->pp_fw = fw;

  avgstat_init(&pp->pp_cc_errors, 60);

}





void
pes_deinit(pes_player_t *pp)
{
  pes_free_stream(&pp->pp_video);
  pes_free_stream(&pp->pp_audio);
  pes_free_stream(&pp->pp_spu);
  avgstat_flush(&pp->pp_cc_errors);

  memset(pp, 0, sizeof(pes_player_t));
}

