#pragma once

#include "video_decoder.h"

struct ext_subtitles;
struct ext_subtitle_entry;

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
  int vo_stop_estimated;

  int vo_x;
  int vo_y;

  struct pixmap *vo_pixmap;

  int vo_fadein;
  int vo_fadeout;

  int vo_alignment;  // LAYOUT_ALIGN_ from layout.h

  int vo_padding_left; // if -1, padding is determined by overlay compositing
  int vo_padding_top;
  int vo_padding_right;
  int vo_padding_bottom;

  uint32_t *vo_text;
  int vo_text_length;

  int vo_canvas_width;   // if -1, ==  same as video frame width
  int vo_canvas_height;  // if -1, ==  same as video frame height

  int vo_layer;

} video_overlay_t;

void video_overlay_destroy(video_decoder_t *vd, video_overlay_t *vo);

void video_overlay_decode(video_decoder_t *vd, media_buf_t *mb);

void video_overlay_flush(video_decoder_t *vd, int send);

void video_overlay_enqueue(video_decoder_t *vd, video_overlay_t *vo);

void video_overlay_decode_ext_subtitle(video_decoder_t *vd, 
				       struct ext_subtitles *es,
				       struct ext_subtitle_entry *ese);

void video_overlay_render_cleartext(video_decoder_t *vd, const char *txt,
				    int64_t start, int64_t stop, int tags);

int calculate_subtitle_duration(int txt_len);
