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
    VO_FLUSH,
  } vo_type;

  int64_t vo_start;
  int64_t vo_stop;

  int vo_x;
  int vo_y;
  int vo_w;
  int vo_h;

  uint8_t *vo_bitmap;
  int vo_fadein;
  int vo_fadeout;

} video_overlay_t;

void video_overlay_destroy(video_decoder_t *vd, video_overlay_t *vo);

void video_overlay_decode(video_decoder_t *vd, media_buf_t *mb);

void video_overlay_flush(video_decoder_t *vd, int send);

void video_overlay_enqueue(video_decoder_t *vd, video_overlay_t *vo);

video_overlay_t *video_overlay_from_pixmap(const struct pixmap *pm);

void video_overlay_decode_ext_subtitle(video_decoder_t *vd, 
				       struct ext_subtitles *es,
				       struct ext_subtitle_entry *ese);

void video_overlay_render_cleartext(video_decoder_t *vd, const char *txt,
				    int64_t start, int64_t stop, int tags);
