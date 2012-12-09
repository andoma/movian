#pragma once

#include "config.h"

#if ENABLE_LIBAV

#include <libavutil/md5.h>

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
