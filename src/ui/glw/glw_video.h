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

/**
 *
 */
typedef struct gl_video_frame {

  video_decoder_frame_t gvf_vdf;

  unsigned int gvf_pbo;
  void *gvf_pbo_ptr;

  int gvf_pbo_offset[3];
  
  int gvf_uploaded;

  unsigned int gvf_textures[3];

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


  unsigned int gv_sputex;

  int gv_in_menu;

  int gv_width;
  int gv_height;

} glw_video_t;


void glw_video_global_init(glw_root_t *gr);

void glw_video_global_flush(glw_root_t *gr);

void glw_video_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_VIDEO_H */

