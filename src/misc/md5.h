#pragma once

#include <libavutil/md5.h>

#define md5_decl(ctx) struct AVMD5 *ctx = alloca(av_md5_size)

#define md5_init(ctx) av_md5_init(ctx);

#define md5_update(ctx, data, len) av_md5_update(ctx, data, len)

#define md5_final(ctx, output) av_md5_final(ctx, output)
