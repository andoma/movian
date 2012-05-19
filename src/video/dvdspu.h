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

#pragma once
#include "video_decoder.h"


/**
 * DVD SPU (SubPicture Units)
 *
 * This include both subtitling and menus on DVDs
 */

typedef struct dvdspu {

  TAILQ_ENTRY(dvdspu) d_link;

  size_t d_size;

  int d_cmdpos;
  int64_t d_pts;

  uint8_t d_palette[4];
  uint8_t d_alpha[4];
  
  int d_x1, d_y1;
  int d_x2, d_y2;

  uint8_t *d_bitmap;

  int d_destroyme;

  uint32_t d_clut[16];

  int d_canvas_width;
  int d_canvas_height;

  uint8_t d_data[0];

} dvdspu_t;

void dvdspu_decoder_init(video_decoder_t *vd);

void dvdspu_decoder_deinit(video_decoder_t *vd);

void dvdspu_destroy(video_decoder_t *vd, dvdspu_t *d);

void dvdspu_decoder_dispatch(video_decoder_t *vd, media_buf_t *mb,
			     media_pipe_t *mp);

int dvdspu_decode(dvdspu_t *d, int64_t pts);

void dvdspu_decode_clut(uint32_t *dst, const uint32_t *src);

void dvdspu_enqueue(video_decoder_t *vd, const void *data, int size,
		    const uint32_t *clut, int w, int h,
		    int64_t pts);

void dvdspu_flush(video_decoder_t *vd);

int dvdspu_codec_create(media_codec_t *mc, enum CodecID id,
			AVCodecContext *ctx, media_pipe_t *mp);

