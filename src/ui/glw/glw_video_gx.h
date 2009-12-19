/*
 *  Video output on GL surfaces
 *  Copyright (C) 2009 Andreas Öman
 *
 *  Based on gx_supp.c from Mplayer CE/TT et al.
 *
 *      softdev 2007
 *	dhewg 2008
 *	sepp256 2008 - Coded YUV->RGB conversion in TEV.
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

#ifndef GLW_VIDEO_GX_H
#define GLW_VIDEO_GX_H

#include "glw.h"
#include "video/video_decoder.h"
#include "video/video_playback.h"

/**
 *
 */
typedef struct gx_video_frame {
  video_decoder_frame_t gvf_vdf;

  GXTexObj gvf_obj[3];
  void *gvf_mem[3];
  int gvf_size[3];

} gx_video_frame_t;


/**
 *
 */
typedef struct glw_video {
  glw_t w;

  video_decoder_t *gv_vd;
  video_playback_t *gv_vp;

  float gv_zoom;

  video_decoder_frame_t *gv_fra, *gv_frb;
  float gv_blend;

  media_pipe_t *gv_mp;

#if 0 // DVD stuff
  GLuint gv_sputex;
  int gv_sputex_height;
  int gv_sputex_width;
  int gv_in_menu;
#endif

  int gv_width;
  int gv_height;

  float gv_tex_x1, gv_tex_x2;
  float gv_tex_y1, gv_tex_y2;

} glw_video_t;


void glw_video_global_init(glw_root_t *gr, int rectmode);

void glw_video_global_flush(glw_root_t *gr);

void glw_video_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_VIDEO_GX_H */

