#pragma once

#include <libavcodec/avcodec.h>
#include "media.h"

int video_vda_codec_create(media_codec_t *mc, enum CodecID id,
			   AVCodecContext *ctx, media_codec_params_t *mcp,
			   media_pipe_t *mp);
