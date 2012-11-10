#pragma once

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

