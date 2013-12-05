#pragma once
/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "config.h"

#if ENABLE_LIBAV

#include <libavutil/md5.h>
#include <libavutil/mem.h>

#define md5_decl(ctx) struct AVMD5 *ctx = NULL;

#define md5_init(ctx) do {                      \
  ctx = av_md5_alloc();                         \
  av_md5_init(ctx);                             \
  } while(0)

#define md5_update(ctx, data, len) av_md5_update(ctx, data, len)

#define md5_final(ctx, output) do {             \
  av_md5_final(ctx, output);                    \
  av_freep(&ctx);                               \
  } while(0)
#elif ENABLE_POLARSSL

#include "polarssl/md5.h"

#define md5_decl(ctx) md5_context *ctx = alloca(sizeof(md5_context));

#define md5_init(ctx) md5_starts(ctx);

#define md5_final(ctx, output) md5_finish(ctx, output);

#endif
