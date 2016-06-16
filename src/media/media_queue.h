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
TAILQ_HEAD(media_buf_queue, media_buf);

struct media_pipe;


/**
 * Helper struct to track discontinuities
 */
typedef struct media_discontinuity_aux {
  int64_t dts;
  int64_t pts;
  int epoch;
  int skip;
} media_discontinuity_aux_t;

/**
 * Media queue
 */
typedef struct media_queue {
  struct media_buf_queue mq_q_data;
  struct media_buf_queue mq_q_ctrl;
  struct media_buf_queue mq_q_aux;

  unsigned int mq_packets_current;    /* Packets currently in queue */

  int mq_stream;             /* Stream id, or -1 if queue is inactive */
  int mq_stream2;            /* Complementary stream */
  int mq_no_data_interest;   // Don't wakeup if adding new DATA packet
  int mq_demuxer_flags;      // For demuxer use
  hts_cond_t mq_avail;

  int64_t mq_last_deq_dts;

  int64_t mq_seektarget;

  int64_t mq_buffer_delay;

  prop_t *mq_prop_qlen_cur;
  prop_t *mq_prop_qlen_max;

  prop_t *mq_prop_bitrate;   // In kbps

  prop_t *mq_prop_decode_avg;
  prop_t *mq_prop_decode_peak;

  prop_t *mq_prop_upload_avg;
  prop_t *mq_prop_upload_peak;

  prop_t *mq_prop_codec;

  prop_t *mq_prop_too_slow;

  struct media_pipe *mq_mp;

  // Copies to avoid updating codec user facing info too often

  int mq_meta_codec_id;
  int mq_meta_profile;
  int mq_meta_channels;
  uint64_t mq_meta_channel_layout;
  int mq_meta_width;
  int mq_meta_height;

  media_discontinuity_aux_t mq_demux_debug;

} media_queue_t;



void mp_send_cmd_locked(struct media_pipe *mp, media_queue_t *mq, int cmd);

void mp_send_cmd(struct media_pipe *mp, media_queue_t *mq, int cmd);

void mp_send_cmd_data(struct media_pipe *mp, media_queue_t *mq,
                      int cmd, void *d);

void mp_send_cmd_u32(struct media_pipe *mp, media_queue_t *mq, int cmd,
		     uint32_t u);

void mp_send_prop_set_string(struct media_pipe *mp, media_queue_t *mq,
                             prop_t *prop, const char *str);

void mp_send_cmd_u32(struct media_pipe *mp, media_queue_t *mq,
                     int cmd, uint32_t u);

void mp_send_volume_update_locked(struct media_pipe *mp);

void mb_enq(struct media_pipe *mp, media_queue_t *mq, media_buf_t *mb);

int mb_enqueue_no_block(struct media_pipe *mp, media_queue_t *mq,
                        media_buf_t *mb, int auxtype);

struct event *mb_enqueue_with_events(struct media_pipe *mp,
                                     media_queue_t *mq,
                                     media_buf_t *mb);

void mb_enqueue_always(struct media_pipe *mp, media_queue_t *mq,
                       media_buf_t *mb);

void mp_flush_locked(struct media_pipe *mp, int final);

void mp_flush(struct media_pipe *mp);

void mq_flush(struct media_pipe *mp, media_queue_t *mq, int all);

void mq_init(media_queue_t *mq, prop_t *p, hts_mutex_t *mutex,
             struct media_pipe *mp);

void mq_destroy(media_queue_t *mq);

void mq_update_stats(struct media_pipe *mp, media_queue_t *mq, int force);

void mp_update_buffer_delay(struct media_pipe *mp);
