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

#include <stddef.h>
#include <stdlib.h>
#include "media.h"

typedef struct h264_annexb_ctx {
  int lsize;
  uint8_t *tmpbuf;
  int tmpbufsize;
  size_t extradata_size;
  uint8_t *extradata;
  int extradata_injected;
} h264_annexb_ctx_t;

void h264_to_annexb_init(h264_annexb_ctx_t *ctx, 
			 const uint8_t *data, int len);

int h264_to_annexb(h264_annexb_ctx_t *ctx, uint8_t **datap, size_t *sizep);

static __inline void h264_to_annexb_cleanup(h264_annexb_ctx_t *ctx)
{
  free(ctx->tmpbuf);
  free(ctx->extradata);

}

int h264_annexb_to_avc(media_codec_t *mc, media_pipe_t *mp,
                       int (*create)(media_codec_t *mc,
                                     const media_codec_params_t *mcp,
                                     media_pipe_t *mp));
