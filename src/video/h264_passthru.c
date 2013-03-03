/*
 *  h264 passthrough decoder
 *  Copyright (C) 2013 Andreas Öman
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

#include <stdio.h>
#include <assert.h>

#include "showtime.h"
#include "video_decoder.h"
#include "h264_annexb.h"

/**
 *
 */
static void
h264_pt_decode(struct media_codec *mc, struct video_decoder *vd,
	       struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  h264_annexb_ctx_t *annexb = mc->opaque;


  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));
  fi.fi_pts         = mb->mb_pts;
  fi.fi_epoch       = mb->mb_epoch;
  fi.fi_delta       = mb->mb_delta;
  fi.fi_drive_clock = mb->mb_drive_clock;
  fi.fi_type        = 'h264';

  if(annexb->extradata != NULL && !annexb->extradata_injected) {

    fi.fi_data[0]  = annexb->extradata;
    fi.fi_pitch[0] = annexb->extradata_size;
    video_deliver_frame(vd, &fi);
    annexb->extradata_injected = 1;
  }
  
  uint8_t *data = mb->mb_data;
  size_t size = mb->mb_size;
  h264_to_annexb(annexb, &data, &size);

  fi.fi_data[0]  = data;
  fi.fi_pitch[0] = size;
  fi.fi_pitch[1] = mb->mb_skip == 1;
  video_deliver_frame(vd, &fi);
}


/**
 *
 */
static void
h264_pt_close(struct media_codec *mc)
{
  h264_to_annexb_cleanup(mc->opaque);
  free(mc->opaque);
}


/**
 *
 */
static int
h264_pt_codec_create(media_codec_t *mc, int id,
		     const media_codec_params_t *mcp,
		     media_pipe_t *mp)
{
  if(id != CODEC_ID_H264)
    return 1;

  if(mc->codec_ctx == NULL) {
    // this is lame
    mc->codec_ctx = avcodec_alloc_context3(NULL);
    mc->codec_ctx->codec_id   = id;
    mc->codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  }

  mc->opaque = calloc(1, sizeof(h264_annexb_ctx_t));
 
  if(mcp->extradata_size)
    h264_to_annexb_init(mc->opaque, mcp->extradata, mcp->extradata_size);

  mc->decode = h264_pt_decode;
  mc->close = h264_pt_close;
  return 0;
}

REGISTER_CODEC(NULL, h264_pt_codec_create, 100);
