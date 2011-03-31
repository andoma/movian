#pragma once

#include <libavcodec/avcodec.h>

#include "media.h"

void video_ps3_vdec_init(void);

int video_ps3_vdec_codec_create(media_codec_t *mc, enum CodecID id,
				AVCodecContext *ctx, media_codec_params_t *mcp,
				media_pipe_t *mp);
