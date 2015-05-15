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
event_t *hls_play_extm3u(char *s, const char *url, media_pipe_t *mp,
			 char *errbuf, size_t errlen,
			 video_queue_t *vq, struct vsource_list *vsl,
			 const video_args_t *va0);




#define HLS_QUEUE_MERGE         0x1
#define HLS_QUEUE_KEYFRAME_SEEN 0x2

#define HLS_EOF  ((void *)-1)
#define HLS_NYA  ((void *)-2)


TAILQ_HEAD(hls_variant_queue, hls_variant);
TAILQ_HEAD(hls_segment_queue, hls_segment);
LIST_HEAD(hls_audio_track_list, hls_audio_track);

#define HLS_CRYPTO_NONE   0
#define HLS_CRYPTO_AES128 1

/**
 *
 */
typedef struct hls_segment {
  TAILQ_ENTRY(hls_segment) hs_link;
  char *hs_url;
  int hs_size;
  int hs_byte_offset;
  int hs_byte_size;
  int64_t hs_duration; // in usec

  int64_t hs_time_offset;
  int64_t hs_ts_offset;

  int hs_seq;

  uint8_t hs_crypto;
  uint8_t hs_open_error;

  rstr_t *hs_key_url;
  uint8_t hs_iv[16];

  struct hls_variant *hs_variant;

  int64_t hs_opened_at;
  int hs_block_cnt;

  char hs_unavailable;
  char hs_discontinuity;
  char hs_mark;

  fa_handle_t *hs_fh;

} hls_segment_t;


typedef enum {
  HLS_ERROR_OK = 0,
  HLS_ERROR_SEGMENT_NOT_FOUND,
  HLS_ERROR_SEGMENT_BROKEN,
  HLS_ERROR_SEGMENT_BAD_KEY,
  HLS_ERROR_VARIANT_PROBE_ERROR,
  HLS_ERROR_VARIANT_NO_VIDEO,
  HLS_ERROR_VARIANT_UNKNOWN_AUDIO,
} hls_error_t;


/**
 *
 */
typedef struct hls_variant {
  TAILQ_ENTRY(hls_variant) hv_link;
  char *hv_url;
  struct hls_demuxer *hv_demuxer;
  int64_t hv_byte_counter;

  void *hv_demuxer_private;
  void (*hv_demuxer_close)(struct hls_variant *);
  void (*hv_demuxer_flush)(struct hls_variant *);

  int hv_vstream;
  int hv_astream;

  int hv_last_seq;
  int hv_first_seq;

  struct hls_segment_queue hv_segments;

  char hv_frozen;
  char hv_audio_only;
  int hv_h264_profile;
  int hv_h264_level;
  int hv_target_duration;

  time_t hv_loaded; /* last time it was loaded successfully
                     *  0 means not loaded
                     */

  int hv_program;
  int hv_bitrate;

  int hv_width;
  int hv_height;

  int hv_corrupt_counter;

#define HV_CORRUPT_LIMIT 3 /* If corrupt_counter >= this, we never consider
			    * this variant again
			    */

  char *hv_subs_group;
  char *hv_audio_group;

  int64_t hv_duration;

  hls_segment_t *hv_current_seg;

  int hv_opening_file;

  rstr_t *hv_key_url;
  buf_t *hv_key;


  uint8_t *hv_video_headers;
  int hv_video_headers_size;

  hls_segment_t *hv_last_pos_search;

  int hv_continuity_counter;

  char hv_name[32];

  int hv_audio_stream;

} hls_variant_t;


/**
 *
 */
typedef struct hls_demuxer {
  struct hls_variant_queue hd_variants;
  const char *hd_type;

  hls_variant_t *hd_current;
  hls_variant_t *hd_req;

  int hd_bw;

  int hd_current_stream;

  media_codec_t *hd_audio_codec;

  time_t hd_last_switch;

  cancellable_t *hd_cancellable;

  struct hls *hd_hls;

  /** Used to find segment to seek to, will be reset to PTS_UNSET
   * once the segment has been found. After the correct segment has
   * been found we rely on mq_seek_target mark frames as skipped until
   * we reach the final seek point
   */
  int64_t hd_seek_to_segment;

  media_buf_t *hd_mb;

} hls_demuxer_t;



/**
 *
 */
typedef struct hls {
  const char *h_baseurl;

  int h_debug;

  media_pipe_t *h_mp;

  media_codec_t *h_codec_h264;

  int h_blocked;

  hls_demuxer_t h_primary;
  hls_demuxer_t h_audio;

  int h_playback_priority;

  int h_restartpos_last;
  int64_t h_last_timestamp_presented;
  char h_sub_scanning_done;
  char h_enqueued_something;

  event_t *h_exit_event;
  int64_t h_pending_seek;

  hts_mutex_t h_mutex;
  hts_cond_t h_cond;

  int64_t h_duration;  // Total duration (if known)

  struct hls_audio_track_list h_audio_tracks;

} hls_t;



typedef struct hls_audio_track {
  LIST_ENTRY(hls_audio_track) hat_link;
  int hat_stream_id;
  int hat_pid;
  char *hat_id;
} hls_audio_track_t;


#define HLS_TRACE(h, x, ...) do {                               \
    if((h)->h_debug)                                            \
      TRACE(TRACE_DEBUG, "HLS", x, ##__VA_ARGS__);		\
  } while(0)


hls_error_t hls_segment_open(hls_segment_t *hs);

void hls_segment_close(hls_segment_t *hs);

void hls_variant_open(hls_variant_t *hv);

void hls_variant_close(hls_variant_t *hv);

int hls_variant_update(hls_variant_t *hv, time_t now);

hls_variant_t *hls_demuxer_select_variant(hls_demuxer_t *hd);

int hls_check_bw_switch(hls_demuxer_t *hd, time_t now);

hls_variant_t *hls_select_default_variant(hls_demuxer_t *hd);


hls_segment_t *hls_variant_select_next_segment(hls_variant_t *hv, time_t now);

int hls_get_audio_track(hls_t *h, int pid, const char *name,
                        const char *language,
                        const char *fmt, int autosel);

hls_segment_t *hv_find_segment_by_seq(const hls_variant_t *hv, int seq);

// TS demuxer

media_buf_t *hls_ts_demuxer_read(hls_demuxer_t *hd);

