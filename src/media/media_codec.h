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
struct AVCodecContext;
struct video_decoder;

/**
 *
 */
typedef struct media_codec {
  atomic_t refcount;
  struct media_format *fw;
  int codec_id;

  struct AVCodecContext *fmt_ctx;     // Context owned by AVFormatContext
  struct AVCodecContext *ctx;         // Context owned by decoder thread

  struct AVCodecParserContext *parser_ctx;

  void *opaque;

  struct media_pipe *mp;

  void (*decode)(struct media_codec *mc, struct video_decoder *vd,
		 struct media_queue *mq, struct media_buf *mb, int reqsize);

  int (*decode_locked)(struct media_codec *mc, struct video_decoder *vd,
                       struct media_queue *mq, struct media_buf *mb);

  void (*flush)(struct media_codec *mc, struct video_decoder *vd);

  void (*close)(struct media_codec *mc);
  void (*reinit)(struct media_codec *mc);
  void (*reconfigure)(struct media_codec *mc, const struct frame_info *fi);

  unsigned int sar_num;
  unsigned int sar_den;

  int (*get_buffer2)(struct AVCodecContext *s, AVFrame *frame, int flags);

} media_codec_t;

struct AVFormatContext;
struct AVCodecContext;
struct media_format;

/**
 *
 */
typedef struct media_codec_params {
  const void *extradata;
  size_t extradata_size;

  unsigned int width;
  unsigned int height;
  int profile;
  int level;
  int cheat_for_speed : 1;
  int broken_aud_placement : 1;
  unsigned int sar_num;
  unsigned int sar_den;

  unsigned int frame_rate_num;
  unsigned int frame_rate_den;

} media_codec_params_t;


/**
 *
 */
typedef struct codec_def {
  LIST_ENTRY(codec_def) link;
  void (*init)(void);
  int (*open)(media_codec_t *mc, const media_codec_params_t *mcp,
	      struct media_pipe *mp);
  int prio;
} codec_def_t;

void media_register_codec(codec_def_t *cd);

// Higher value of prio_ == better preference

#define REGISTER_CODEC(init_, open_, prio_)			   \
  static codec_def_t HTS_JOIN(codecdef, __LINE__) = {		   \
    .init = init_,						   \
    .open = open_,						   \
    .prio = prio_						   \
  };								   \
  INITIALIZER(HTS_JOIN(registercodecdef, __LINE__))                \
  { media_register_codec(&HTS_JOIN(codecdef, __LINE__)); }


/**
 *
 */
typedef struct media_format {
  atomic_t refcount;
  struct AVFormatContext *fctx;
} media_format_t;

#if ENABLE_LIBAV

media_format_t *media_format_create(struct AVFormatContext *fctx);

void media_format_deref(media_format_t *fw);

#endif

/**
 * Codecs
 */
void media_codec_deref(media_codec_t *cw);

media_codec_t *media_codec_ref(media_codec_t *cw);

media_codec_t *media_codec_create(int codec_id, int parser,
				  struct media_format *fw, 
				  struct AVCodecContext *ctx,
				  const media_codec_params_t *mcp,
                                  struct media_pipe *mp);

void media_codec_init(void);
