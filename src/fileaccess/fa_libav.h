#pragma once

#include <libavformat/avio.h>
#include <libavformat/avformat.h>

#include "fileaccess.h"

AVIOContext *fa_libav_open(const char *url, int buf_size,
			   char *errbuf, size_t errlen);

void fa_libav_close(AVIOContext *io);

AVFormatContext *fa_libav_open_format(AVIOContext *avio, const char *url,
				      char *errbuf, size_t errlen);

void fa_libav_close_format(AVFormatContext *fctx);

uint8_t *fa_libav_load_and_close(AVIOContext *avio, size_t *sizep);
