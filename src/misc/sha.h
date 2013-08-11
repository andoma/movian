#pragma once

#include "config.h"

#if ENABLE_POLARSSL

#include "polarssl/sha1.h"

#define sha1_decl(ctx) sha1_context *ctx = alloca(sizeof(sha1_context));

#define sha1_init(ctx) sha1_starts(ctx);

#define sha1_final(ctx, output) sha1_finish(ctx, output);

#elif ENABLE_LIBAV

#include <libavutil/sha.h>
#include <libavutil/mem.h>

#define sha1_decl(ctx) struct AVSHA *ctx = NULL;

#define sha1_init(ctx) do {                     \
  ctx = av_sha_alloc();                         \
  av_sha_init(ctx, 160);                        \
  } while(0)

#define sha1_update(ctx, data, len) av_sha_update(ctx, data, len)

#define sha1_final(ctx, output) do {            \
  av_sha_final(ctx, output);                    \
  av_freep(&ctx);                               \
  } while(0)

#else
#error no sha1
#endif
