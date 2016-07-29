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
#pragma once
#include "misc/queue.h"
#include "misc/minmax.h"

struct AVPacket;
struct media_pipe;
struct media_queue;

/**
 *
 */
typedef union media_buf_flags {

  struct {
    uint32_t aspect_override      : 2;
    uint32_t skip                 : 1;
    uint32_t keyframe             : 1;
    uint32_t flush                : 1;
    uint32_t nopts                : 1;
    uint32_t nodts                : 1;
    uint32_t drive_clock          : 2;
    uint32_t disable_deinterlacer : 1;
  };

  uint32_t u32;

} media_buf_flags_t;



/**
 *
 */
typedef struct media_buf_meta {
  int64_t mbm_user_time;
  int64_t mbm_pts;
  int64_t mbm_dts;
  int mbm_epoch;
  uint32_t mbm_duration;
  int mbm_sequence;

  media_buf_flags_t mbm_flags;

#define mbm_aspect_override          mbm_flags.aspect_override
#define mbm_skip                     mbm_flags.skip
#define mbm_keyframe                 mbm_flags.keyframe
#define mbm_flush                    mbm_flags.flush
#define mbm_nopts                    mbm_flags.nopts
#define mbm_nodts                    mbm_flags.nodts
#define mbm_drive_clock              mbm_flags.drive_clock
#define mbm_disable_deinterlacer     mbm_flags.disable_deinterlacer
} media_buf_meta_t;


/**
 * A buffer
 */
typedef struct media_buf {
  TAILQ_ENTRY(media_buf) mb_link;

  AVPacket mb_pkt;
#define mb_pts        mb_pkt.pts
#define mb_dts        mb_pkt.dts
#define mb_duration   mb_pkt.duration
#define mb_data       mb_pkt.data
#define mb_size       mb_pkt.size
#define mb_stream     mb_pkt.stream_index

  int64_t mb_user_time;

  media_buf_flags_t mb_flags;

  int mb_epoch;
  int mb_sequence;

#define mb_aspect_override      mb_flags.aspect_override
#define mb_skip                 mb_flags.skip
#define mb_disable_deinterlacer mb_flags.disable_deinterlacer
#define mb_keyframe             mb_flags.keyframe
#define mb_drive_clock          mb_flags.drive_clock
#define mb_flush                mb_flags.flush

  enum {
    MB_VIDEO,
    MB_AUDIO,
    MB_SET_PROP_STRING,

    MB_DVD_CLUT,
    MB_DVD_RESET_SPU,
    MB_DVD_SPU,
    MB_DVD_PCI,

    MB_SUBTITLE,

    MB_CTRL,

    MB_CTRL_FLUSH,
    MB_CTRL_PAUSE,
    MB_CTRL_PLAY,
    MB_CTRL_EXIT,
    MB_CTRL_FLUSH_SUBTITLES,

    MB_CTRL_DVD_HILITE,
    MB_CTRL_EXT_SUBTITLE,

    MB_CTRL_REINITIALIZE, // Full reinit (such as VDPAU context loss)
    MB_CTRL_RECONFIGURE,  // Reconfigure (such as OMX output port changed)

    MB_CTRL_REQ_OUTPUT_SIZE,
    MB_CTRL_DVD_SPU2,

    MB_CTRL_UNBLOCK,

    MB_CTRL_SET_VOLUME_MULTIPLIER,

  } mb_data_type;

  struct media_codec *mb_cw;
  void (*mb_dtor)(struct media_buf *mb);


  union {
    int32_t mb_data32;
    int mb_rate;
    float mb_float;
    struct prop *mb_prop;
    uint16_t mb_font_context;
    struct frame_info *mb_frame_info;
  };

  uint8_t mb_channels;

  int mb_codecid;

} media_buf_t;

#define mb_buffered_size(mb) MAX((mb)->mb_size, 4096)

void copy_mbm_from_mb(media_buf_meta_t *mbm, const media_buf_t *mb);

void media_buf_free_locked(struct media_pipe *mp, media_buf_t *mb);

void media_buf_free_unlocked(struct media_pipe *mp, media_buf_t *mb);

media_buf_t *media_buf_alloc_locked(struct media_pipe *mp, size_t payloadsize);

media_buf_t *media_buf_alloc_unlocked(struct media_pipe *mp,
                                      size_t payloadsize);

media_buf_t *media_buf_from_avpkt_unlocked(struct media_pipe *mp,
                                           struct AVPacket *pkt);

void media_buf_dtor_frame_info(media_buf_t *mb);
