#pragma once

void metadata_from_libav(char *dst, size_t dstlen, 
			 const AVCodec *codec, const AVCodecContext *avctx);

void mp_set_mq_meta(media_queue_t *mq,
		    const AVCodec *codec, const AVCodecContext *avctx);

