#pragma once

#include "video_decoder.h"

void sub_ass_render(video_decoder_t *vd, const char *src,
		    const uint8_t *header, int header_len,
		    int context);

void sub_srt_render(video_decoder_t *vd, const char *src,
		    int64_t start, int64_t stop);
