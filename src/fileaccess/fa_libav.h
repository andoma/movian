#pragma once

#include <libavformat/avio.h>
#include <libavformat/avformat.h>

#include "fileaccess.h"

AVIOContext *fa_libav_open(const char *url, int buf_size,
			   char *errbuf, size_t errlen, int flags,
			   struct prop *stats);

AVIOContext *fa_libav_open_vpaths(const char *url, int buf_size,
				  const char **vpaths);

AVIOContext *fa_libav_reopen(fa_handle_t *fh, int buf_size);

void fa_libav_close(AVIOContext *io);

AVFormatContext *fa_libav_open_format(AVIOContext *avio, const char *url,
				      char *errbuf, size_t errlen,
				      const char *mimetype);

void fa_libav_close_format(AVFormatContext *fctx);

uint8_t *fa_libav_load_and_close(AVIOContext *avio, size_t *sizep);
