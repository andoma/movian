#pragma once

#include <libavformat/avio.h>
#include <libavformat/avformat.h>

#include "fileaccess.h"

AVIOContext *fa_libav_reopen(fa_handle_t *fh);

void fa_libav_close(AVIOContext *io);

AVFormatContext *fa_libav_open_format(AVIOContext *avio, const char *url,
				      char *errbuf, size_t errlen,
				      const char *mimetype);

void fa_libav_close_format(AVFormatContext *fctx);
