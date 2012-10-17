#pragma once

#include "video_decoder.h"

struct ext_subtitles;

/**
 * Video overlay
 */
typedef struct video_overlay {

  TAILQ_ENTRY(video_overlay) vo_link;

  enum { 
    VO_BITMAP,
    VO_TEXT,
    VO_FLUSH,
    VO_TIMED_FLUSH,
  } vo_type;

  int64_t vo_start;
  int64_t vo_stop;

  struct pixmap *vo_pixmap;
  uint32_t *vo_text;
  int vo_text_length;

  char vo_stop_estimated;
  char vo_alignment;  // LAYOUT_ALIGN_ from layout.h
  char vo_layer;

  int16_t vo_x;
  int16_t vo_y;

  int16_t vo_fadein;
  int16_t vo_fadeout;


  int16_t vo_padding_left;// if -1, padding is determined by overlay compositing
  int16_t vo_padding_top;
  int16_t vo_padding_right;
  int16_t vo_padding_bottom;


  int16_t vo_canvas_width;   // if -1, ==  same as video frame width
  int16_t vo_canvas_height;  // if -1, ==  same as video frame height

} video_overlay_t;

void video_overlay_dequeue_destroy(video_decoder_t *vd, 
				   video_overlay_t *vo);

void video_overlay_destroy(video_overlay_t *vo);

void video_overlay_decode(video_decoder_t *vd, media_buf_t *mb);

void video_overlay_flush(video_decoder_t *vd, int send);

void video_overlay_enqueue(video_decoder_t *vd, video_overlay_t *vo);

video_overlay_t *video_overlay_dup(video_overlay_t *vo);

void video_overlay_decode_ext_subtitle(video_decoder_t *vd, 
				       struct ext_subtitles *es,
				       video_overlay_t *vo);

video_overlay_t *video_overlay_render_cleartext(const char *txt, int64_t start,
						int64_t stop, int tags,
						int fontdomain);

int calculate_subtitle_duration(int txt_len);

