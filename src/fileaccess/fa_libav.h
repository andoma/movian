/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include "fileaccess.h"
#include <libavformat/avio.h>

struct AVFormatContext;

AVIOContext *fa_libav_reopen(fa_handle_t *fh, int no_seek);

void fa_libav_close(AVIOContext *io);

struct AVFormatContext *fa_libav_open_format(AVIOContext *avio,
					     const char *url,
					     char *errbuf, size_t errlen,
					     const char *mimetype,
                                             int probe_size,
                                             int max_analyze_duration,
					     int fps_probe_frames);

void fa_libav_close_format(struct AVFormatContext *fctx, int park);
