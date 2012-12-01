#pragma once

void metadata_from_ffmpeg(char *dst, size_t dstlen, 
			  struct AVCodec *codec, struct AVCodecContext *avctx);

void mp_set_mq_meta(media_queue_t *mq, struct AVCodec *codec,
		    struct AVCodecContext *avctx);

