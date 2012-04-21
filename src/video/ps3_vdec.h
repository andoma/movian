#pragma once

#include <libavcodec/avcodec.h>

#include "media.h"

typedef struct rsx_video_frame {
  int rvf_offset[3];   // Offset in RSX mem
  int rvf_linesize[3]; // Linesize
  int rvf_size[3];     // Buffer size

} rsx_video_frame_t;

void video_ps3_vdec_init(void);

int video_ps3_vdec_codec_create(media_codec_t *mc, enum CodecID id,
				AVCodecContext *ctx, media_codec_params_t *mcp,
				media_pipe_t *mp);
