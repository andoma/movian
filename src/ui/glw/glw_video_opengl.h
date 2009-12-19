/*
 *  Video decoder
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

#ifndef GLW_VIDEO_H
#define GLW_VIDEO_H

#include "glw.h"
#include "video/video_decoder.h"
#include "video/video_playback.h"

TAILQ_HEAD(gl_video_sub_queue, gl_video_sub);

/**
 *
 */
typedef struct gl_video_sub {
  TAILQ_ENTRY(gl_video_sub) gvs_link;
  
  int gvs_x1, gvs_y1, gvs_x2, gvs_y2;
  int gvs_width, gvs_height;

  char *gvs_bitmap;

  int64_t gvs_start;
  int64_t gvs_end;

  GLuint gvs_texture;

  int gvs_tex_width;
  int gvs_tex_height;
} gl_video_sub_t;


/**
 *
 */
typedef struct gl_video_frame {

  video_decoder_frame_t gvf_vdf;

  GLuint gvf_pbo[3];
  void *gvf_pbo_ptr[3];

  int gvf_uploaded;

  GLuint gvf_textures[3];

  unsigned int gvf_frame_buffer;

} gl_video_frame_t;


/**
 *
 */
typedef struct glw_video {

  glw_t w;

  LIST_ENTRY(glw_video) gv_global_link;

  video_decoder_t *gv_vd;
  video_playback_t *gv_vp;

  float gv_cmatrix[9];
  float gv_zoom;

  video_decoder_frame_t *gv_fra, *gv_frb;
  float gv_blend;

  media_pipe_t *gv_mp;


  GLuint gv_sputex;
  int gv_sputex_height;
  int gv_sputex_width;


  int gv_in_menu;

  int gv_width;
  int gv_height;

  float gv_tex_x1, gv_tex_x2;
  float gv_tex_y1, gv_tex_y2;

  struct gl_video_sub_queue gv_subs;

} glw_video_t;


void glw_video_global_init(glw_root_t *gr, int rectmode);

void glw_video_global_flush(glw_root_t *gr);

void glw_video_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_VIDEO_H */

