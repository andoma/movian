#pragma once
/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "media.h"

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

  int vo_fadein;
  int vo_fadeout;


  int16_t vo_padding_left;// if -1, padding is determined by overlay compositing
  int16_t vo_padding_top;
  int16_t vo_padding_right;
  int16_t vo_padding_bottom;


  int16_t vo_canvas_width;   // if -1, ==  same as video frame width
  int16_t vo_canvas_height;  // if -1, ==  same as video frame height

} video_overlay_t;

void video_overlay_dequeue_destroy(media_pipe_t *mp, video_overlay_t *vo);

void video_overlay_destroy(video_overlay_t *vo);

void video_overlay_decode(media_pipe_t *mp, media_buf_t *mb);

void video_overlay_flush_locked(media_pipe_t *mp, int send);

void video_overlay_enqueue(media_pipe_t *mp, video_overlay_t *vo);

video_overlay_t *video_overlay_dup(video_overlay_t *vo);

video_overlay_t *video_overlay_render_cleartext(const char *txt, int64_t start,
						int64_t stop, int tags,
						int fontdomain);

int calculate_subtitle_duration(int txt_len);

