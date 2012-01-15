#pragma once

#include <libavutil/sha.h>

#define sha1_decl(ctx) struct AVSHA *ctx = alloca(av_sha_size)

#define sha1_init(ctx) av_sha_init(ctx, 160);

#define sha1_update(ctx, data, len) av_sha_update(ctx, data, len)

#define sha1_final(ctx, output) av_sha_final(ctx, output)
