#pragma once

#include "media.h"

void sub_ass_render(media_pipe_t *mp, const char *src,
		    const uint8_t *header, int header_len,
		    int context);
