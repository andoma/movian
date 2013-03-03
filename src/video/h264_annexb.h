#pragma once

#include <stddef.h>
#include <stdlib.h>

typedef struct h264_annexb_ctx {
  int lsize;
  uint8_t *tmpbuf;
  int tmpbufsize;
  size_t extradata_size;
  uint8_t *extradata;
  int extradata_injected;
} h264_annexb_ctx_t;

void h264_to_annexb_init(h264_annexb_ctx_t *ctx, 
			 const uint8_t *data, int len);

int h264_to_annexb(h264_annexb_ctx_t *ctx, uint8_t **datap, size_t *sizep);

static inline void h264_to_annexb_cleanup(h264_annexb_ctx_t *ctx)
{
  free(ctx->tmpbuf);
  free(ctx->extradata);

}
