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

#ifndef MPEG_SUPPORT_H
#define MPEG_SUPPORT_H

#include "media.h"
#include <libhts/avg.h>

typedef int (pes_filter_callback_t)(void *aux, uint32_t startcode);

typedef struct pes_stream {

  codecwrap_t            *ps_cw;
  
  int                     ps_codec_id;
  int64_t                 ps_pts;
  int64_t                 ps_dts;

  pes_filter_callback_t  *ps_filter_cb;
  void                   *ps_aux;

  media_queue_t          *ps_output;

  int                     ps_force_reset;

} pes_stream_t;





LIST_HEAD(ts_pid_head, ts_pid);

typedef struct ts_pid {

  LIST_ENTRY(ts_pid) tsp_link;


  int tsp_pid;
  int tsp_cc;
  int tsp_cc_errors;

  int tsp_pus;

  uint8_t *tsp_buf;
  size_t tsp_bufptr;
  size_t tsp_bufsize;

} ts_pid_t;


typedef struct pes_player {

  pes_stream_t pp_video;
  pes_stream_t pp_audio;
  pes_stream_t pp_spu;

  /* For TS packet decoding */

  struct ts_pid_head pp_pidlist;

  /* */

  int pp_aspect_override;

  media_pipe_t *pp_mp;
  formatwrap_t *pp_fw;

  avgstat_t pp_cc_errors;

} pes_player_t;





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





void pes_flush_pids(pes_player_t *pp);

#define PS1_SUB_ENC 0
#define PS1_IS_AC3  1

struct media_control;

void pes_do_block(pes_player_t *pp, uint32_t sc, uint8_t *buf, int len, int w,
		  enum CodecID pkt_type);


int pes_do_tsb(pes_player_t *pp, uint8_t *tsb, int w, enum CodecID pkttype);

void pes_init(pes_player_t *pp, media_pipe_t *mp, formatwrap_t *fw);

void pes_deinit(pes_player_t *pp);

void pes_wait_video_audio(pes_player_t *pp);

#endif /* MPEG_PES_H */
