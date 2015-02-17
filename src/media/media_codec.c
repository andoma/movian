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
#include "media.h"

static LIST_HEAD(, codec_def) registeredcodecs;

/**
 *
 */
media_codec_t *
media_codec_ref(media_codec_t *cw)
{
  atomic_inc(&cw->refcount);
  return cw;
}

/**
 *
 */
void
media_codec_deref(media_codec_t *cw)
{
  if(atomic_dec(&cw->refcount))
    return;
#if ENABLE_LIBAV
  if(cw->ctx != NULL && cw->ctx->codec != NULL)
    avcodec_close(cw->ctx);

  if(cw->fmt_ctx != NULL && cw->fmt_ctx->codec != NULL)
    avcodec_close(cw->fmt_ctx);
#endif

  if(cw->close != NULL)
    cw->close(cw);

  free(cw->ctx);

  if(cw->fmt_ctx && cw->fw == NULL)
    free(cw->fmt_ctx);

#if ENABLE_LIBAV
  if(cw->parser_ctx != NULL)
    av_parser_close(cw->parser_ctx);

  if(cw->fw != NULL)
    media_format_deref(cw->fw);
#endif

  free(cw);
}


/**
 *
 */
media_codec_t *
media_codec_create(int codec_id, int parser,
		   struct media_format *fw, struct AVCodecContext *ctx,
		   const media_codec_params_t *mcp, media_pipe_t *mp)
{
  media_codec_t *mc = calloc(1, sizeof(media_codec_t));
  codec_def_t *cd;

  mc->mp = mp;
  mc->fmt_ctx = ctx;
  mc->codec_id = codec_id;

#if ENABLE_LIBAV
  if(ctx != NULL && mcp != NULL) {
    assert(ctx->extradata      == mcp->extradata);
    assert(ctx->extradata_size == mcp->extradata_size);
  }
#endif

  if(mcp != NULL) {
    mc->sar_num = mcp->sar_num;
    mc->sar_den = mcp->sar_den;
  }

  LIST_FOREACH(cd, &registeredcodecs, link)
    if(!cd->open(mc, mcp, mp))
      break;

  if(cd == NULL) {
    free(mc);
    return NULL;
  }

#if ENABLE_LIBAV
  if(parser) {
    assert(fw == NULL);

    const AVCodec *codec = avcodec_find_decoder(codec_id);
    assert(codec != NULL);
    mc->fmt_ctx = avcodec_alloc_context3(codec);
    mc->parser_ctx = av_parser_init(codec_id);
  }
#endif

  atomic_set(&mc->refcount, 1);
  mc->fw = fw;

  if(fw != NULL) {
    assert(!parser);
    atomic_inc(&fw->refcount);
  }

  return mc;
}


/**
 *
 */
void
media_codec_init(void)
{
  codec_def_t *cd;
  LIST_FOREACH(cd, &registeredcodecs, link)
    if(cd->init)
      cd->init();
}





/**
 *
 */
static int
codec_def_cmp(const codec_def_t *a, const codec_def_t *b)
{
  return a->prio - b->prio;
}

/**
 *
 */
void
media_register_codec(codec_def_t *cd)
{
  LIST_INSERT_SORTED(&registeredcodecs, cd, link, codec_def_cmp, codec_def_t);
}

