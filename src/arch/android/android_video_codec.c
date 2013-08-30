/*
 *  h264 passthrough decoder
 *  Copyright (C) 2013 Andreas Öman
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
 */

#include <stdio.h>
#include <assert.h>

#include <jni.h>

#include "showtime.h"
#include "video/video_decoder.h"

extern JavaVM *JVM;

/**
 *
 */
typedef struct android_video_codec {

  jobject avc_decoder;

  int avc_width;
  int avc_height;

  int avc_configured;

} android_video_codec_t;


/**
 *
 */
static void
android_codec_decode(struct media_codec *mc, struct video_decoder *vd,
		 struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  





}


/**
 *
 */
static void
android_codec_flush(struct media_codec *mc, struct video_decoder *vd)
{
}


/**
 *
 */
static void
android_codec_close(struct media_codec *mc)
{
}

/**
 *
 */
static int
android_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
                     media_pipe_t *mp)
{
  jclass class;
  jmethodID mid;
  const char *type = NULL;

  if(mcp->width == 0 || mcp->height == 0)
    return 1;

  return 1;

  switch(mc->codec_id) {

  case CODEC_ID_H264:
    type = "video/avc";
    break;

  default:
    return 1;
  }

  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  class = (*env)->FindClass(env, "android/media/MediaCodec");
  if(!class) {
    TRACE(TRACE_DEBUG, "Video", "Unable to find Android MediaCodec class");
    return 1;
  }
  mid = (*env)->GetStaticMethodID(env, class, "createDecoderByType",
                                  "(Ljava/lang/String;)Landroid/media/MediaCodec;");

  jstring jtype = (*env)->NewStringUTF(env, type);
  jobject decoder = (*env)->CallStaticObjectMethod(env, class, mid, jtype);
  if(decoder == 0) {
    TRACE(TRACE_DEBUG, "Video", "Unable to create Android decoder for %s",
          type);
    return 1;
  }

  android_video_codec_t *avc = calloc(1, sizeof(android_video_codec_t));
  avc->avc_decoder = (*env)->NewGlobalRef(env, decoder);

  avc->avc_width  = mcp->width;
  avc->avc_height = mcp->height;

  mc->opaque = avc;
  mc->close  = android_codec_close;
  mc->decode = android_codec_decode;
  mc->flush  = android_codec_flush;
  return 0;
}


/**
 *
 */
static void
android_codec_init(void)
{
}


REGISTER_CODEC(android_codec_init, android_codec_create, 100);
