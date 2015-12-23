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
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include <jni.h>

#include "main.h"
#include "video/video_decoder.h"
#include "video/video_settings.h"
#include "video/h264_annexb.h"

extern JavaVM *JVM;

#define AVC_TRACE(x, ...) do {                                          \
    if(gconf.enable_MediaCodec_debug)                                   \
      TRACE(TRACE_DEBUG, "MediaCodec", x, ##__VA_ARGS__);		\
  } while(0)

#define PTS_IS_REORDER_INDEX

static int
check_exception(JNIEnv *env, const char *what)
{
  if((*env)->ExceptionOccurred(env)) {
    (*env)->ExceptionClear(env);
    TRACE(TRACE_ERROR, "VIDEO", "Exception occured at %s", what);
    return 1;
  }
  return 0;
}

/**
 *
 */
typedef struct android_video_codec {

  int avc_direct;

  jobject avc_decoder;
  jclass avc_MediaCodec;

  const char *avc_mime;
  int avc_width;
  int avc_height;

  int avc_configured;


  jobjectArray avc_input_buffers;
  jobjectArray avc_output_buffers;

  jobject avc_buffer_info;


  jmethodID avc_dequeueInputBuffer;
  jmethodID avc_queueInputBuffer;
  jmethodID avc_dequeueOutputBuffer;
  jmethodID avc_releaseOutputBuffer;


  void *avc_extradata;
  int avc_extradata_size;

  int avc_out_width;
  int avc_out_height;
  int avc_out_stride;
  int avc_out_fmt;

  h264_annexb_ctx_t avc_annexb;

  const char *avc_nicename;

  int avc_slot_issues;

  int64_t avc_ts1;
  int64_t avc_ts2;

  int avc_decode_time;
} android_video_codec_t;


/**
 *
 */
static int
avc_enq(JNIEnv *env, android_video_codec_t *avc, void *data, int size,
        jlong pts, int flags, jlong timeout)
{
  int idx = (*env)->CallIntMethod(env, avc->avc_decoder,
                                  avc->avc_dequeueInputBuffer, timeout);
  if(check_exception(env, "dequeueInputBuffer"))
    return -2;

  if(idx < 0)
    return -1;

  jobject bb =
    (*env)->GetObjectArrayElement(env, avc->avc_input_buffers, idx);

  void *bb_buf  = (*env)->GetDirectBufferAddress(env, bb);
  int   bb_size = (*env)->GetDirectBufferCapacity(env, bb);
  if(bb_size < size) {
    TRACE(TRACE_ERROR, "android", "Video packet buffer too small %d < %d",
          bb_size, size);
    return idx;
  }
  memcpy(bb_buf, data, size);

  (*env)->CallVoidMethod(env, avc->avc_decoder, avc->avc_queueInputBuffer,
                         idx, 0, size, pts, flags);
  check_exception(env, "queueInputBuffer");
  return idx;
}


/**
 *
 */
static int
getInteger(JNIEnv *env, jobject obj, const char *name)
{
  jclass class = (*env)->GetObjectClass(env, obj);
  jmethodID mid = (*env)->GetMethodID(env, class, "getInteger",
                                      "(Ljava/lang/String;)I");
  return (*env)->CallIntMethod(env, obj, mid, (*env)->NewStringUTF(env, name));
}


/**
 *
 */
static void
update_output_format(JNIEnv *env, android_video_codec_t *avc,
                     media_pipe_t *mp)
{
  jmethodID mid;

  mid = (*env)->GetMethodID(env, avc->avc_MediaCodec, "getOutputFormat",
                            "()Landroid/media/MediaFormat;");
  jobject format = (*env)->CallObjectMethod(env, avc->avc_decoder, mid);

  avc->avc_out_width  = getInteger(env, format, "width");
  avc->avc_out_height = getInteger(env, format, "height");
  avc->avc_out_stride = getInteger(env, format, "stride");
  avc->avc_out_fmt    = getInteger(env, format, "color-format");

  TRACE(TRACE_DEBUG, "VIDEO", "Output format changed to %d x %d [%d] colfmt:%d",
        avc->avc_out_width, avc->avc_out_height,
        avc->avc_out_stride, avc->avc_out_fmt);

  char codec_info[64];
  snprintf(codec_info, sizeof(codec_info), "%s %dx%d (Accelerated)",
           avc->avc_nicename,
           avc->avc_out_width, avc->avc_out_height);
  prop_set_string(mp->mp_video.mq_prop_codec, codec_info);
}


/**
 *
 */
static void
avc_get_output_buffers(JNIEnv *env, android_video_codec_t *avc)
{
  jmethodID mid =
    (*env)->GetMethodID(env, avc->avc_MediaCodec, "getOutputBuffers",
                        "()[Ljava/nio/ByteBuffer;");
  jobject obj = (*env)->CallObjectMethod(env, avc->avc_decoder, mid);
  check_exception(env, "getOutputBuffers");
  if(avc->avc_output_buffers)
    (*env)->DeleteGlobalRef(env, avc->avc_output_buffers);

  avc->avc_output_buffers = obj ? (*env)->NewGlobalRef(env, obj) : 0;
}


/**
 *
 */
static void
get_output(JNIEnv *env, android_video_codec_t *avc, int loop,
           video_decoder_t *vd)
{
  do {
    int idx = (*env)->CallIntMethod(env, avc->avc_decoder,
                                    avc->avc_dequeueOutputBuffer,
                                    avc->avc_buffer_info,
                                    (jlong) 0);
    check_exception(env, "dequeueOutputBuffer");

    if(idx >= 0) {


      jclass class = (*env)->GetObjectClass(env, avc->avc_buffer_info);
      jfieldID f_pts = (*env)->GetFieldID(env, class,
                                          "presentationTimeUs", "J");

      jlong pts = (*env)->GetLongField(env, avc->avc_buffer_info, f_pts);
      frame_info_t fi = {};
      // We only support square pixels here
      fi.fi_dar_num = avc->avc_out_width;
      fi.fi_dar_den = avc->avc_out_height;

#ifdef PTS_IS_REORDER_INDEX
      int slot = pts & VIDEO_DECODER_REORDER_MASK;
      const media_buf_meta_t *mbm = &vd->vd_reorder[slot];
      fi.fi_pts = mbm->mbm_pts;
      fi.fi_epoch = mbm->mbm_epoch;
      fi.fi_user_time = mbm->mbm_user_time;
      fi.fi_drive_clock = mbm->mbm_drive_clock;
      fi.fi_duration = mbm->mbm_duration;
      AVC_TRACE("Dequeue buffer reorderslot:0x%lx -> %d "
                "PTS=%"PRId64" duration=%d",
                (long)pts, slot, fi.fi_pts, fi.fi_duration);
#else
      fi.fi_pts = slot;
      fi.fi_epoch = 1;
      fi.fi_drive_clock = 1;
#endif
      if(avc->avc_direct) {
        fi.fi_type = 'SURF';

        if(gconf.enable_MediaCodec_debug) {
          avc->avc_ts1 = arch_get_ts();
          if(avc->avc_ts2)
            avc->avc_decode_time = avc->avc_ts1 - avc->avc_ts2;
        }
        video_deliver_frame(vd, &fi);
        if(gconf.enable_MediaCodec_debug)
          avc->avc_ts2 = arch_get_ts();

      } else {
        jobject buf =
          (*env)->GetObjectArrayElement(env, avc->avc_output_buffers, idx);

        jsize buf_size = (*env)->GetDirectBufferCapacity(env, buf);
        uint8_t *ptr   = (*env)->GetDirectBufferAddress(env,  buf);
        TRACE(TRACE_DEBUG, "GLW", "The size is %d", (int)buf_size);

        fi.fi_data[0]  = ptr;
        fi.fi_pitch[0] = avc->avc_out_width * 2;
        fi.fi_width  = avc->avc_out_width;
        fi.fi_height = avc->avc_out_height;
        fi.fi_type = 'YUVP';
        video_deliver_frame(vd, &fi);
      }

      AVC_TRACE("Timings Decode:%-5d Display:%-5d Total:%-5d",
                avc->avc_decode_time,
                (int)(avc->avc_ts2 - avc->avc_ts1),
                (int)(avc->avc_ts2 - avc->avc_ts1) + avc->avc_decode_time);

      (*env)->CallVoidMethod(env, avc->avc_decoder,
                             avc->avc_releaseOutputBuffer,
                             idx, 1);
      check_exception(env, "releaseOutputBuffer");
    } else if(idx == -2) {
      update_output_format(env, avc, vd->vd_mp);
      continue;
    } else if(idx == -3) {
      avc_get_output_buffers(env, avc);
      continue;
    } else {
      break;
    }
  } while(loop);
}


/**
 *
 */
static void
android_codec_decode(struct media_codec *mc, struct video_decoder *vd,
                     struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  android_video_codec_t *avc = mc->opaque;
  media_pipe_t *mp = mc->mp;
  jmethodID mid;
  jobject obj;

  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  (*env)->PushLocalFrame(env, 64);

  if(!avc->avc_configured) {

    jclass MediaFormat = (*env)->FindClass(env, "android/media/MediaFormat");
    mid = (*env)->GetStaticMethodID(env, MediaFormat, "createVideoFormat",
                                    "(Ljava/lang/String;II)Landroid/media/MediaFormat;");

    jobject format =
      (*env)->CallStaticObjectMethod(env, MediaFormat, mid,
                                     (*env)->NewStringUTF(env, avc->avc_mime),
                                     avc->avc_width, avc->avc_height);
    check_exception(env, "createVideoFormat");

    jclass ByteBuffer = (*env)->FindClass(env, "java/nio/ByteBuffer");

    mid = (*env)->GetStaticMethodID(env, ByteBuffer, "allocateDirect",
                                    "(I)Ljava/nio/ByteBuffer;");

    const void *extradata;
    int extradata_size;

    if(avc->avc_annexb.extradata) {
      extradata      = avc->avc_annexb.extradata;
      extradata_size = avc->avc_annexb.extradata_size;
      avc->avc_annexb.extradata_injected = 1;
      hexdump("csd-0", extradata, extradata_size);
    } else {
      extradata      = avc->avc_extradata;
      extradata_size = avc->avc_extradata_size;
    }


    if(extradata_size) {
      jobject bb = (*env)->CallStaticObjectMethod(env, ByteBuffer,
                                                  mid,
                                                  extradata_size);
      check_exception(env, "allocateDirect");

      uint8_t *ptr = (*env)->GetDirectBufferAddress(env, bb);

      memcpy(ptr, extradata, extradata_size);

      mid = (*env)->GetMethodID(env, ByteBuffer, "limit",
                                "(I)Ljava/nio/Buffer;");

      (*env)->CallObjectMethod(env, bb, mid, extradata_size);
      check_exception(env, "limit");

      mid = (*env)->GetMethodID(env, MediaFormat, "setByteBuffer",
                                "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");

      (*env)->CallVoidMethod(env, format, mid,
                             (*env)->NewStringUTF(env, "csd-0"), bb);
      check_exception(env, "setByteBuffer");
    }
    // ------------------------------------------------

    int surface;
    if(avc->avc_direct) {
      surface = mp->mp_set_video_codec('SURF', mc, mp->mp_video_frame_opaque,
                                       NULL);
    } else {
      surface = 0;
    }
    // ------------------------------------------------

    mid = (*env)->GetMethodID(env, avc->avc_MediaCodec, "configure", "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");

    (*env)->CallVoidMethod(env, avc->avc_decoder, mid, format,
                           surface, NULL, 0);
    check_exception(env, "MediaCodec.configure");

    mid = (*env)->GetMethodID(env, avc->avc_MediaCodec, "start", "()V");
    (*env)->CallVoidMethod(env, avc->avc_decoder, mid);
    check_exception(env, "MediaCodec.start");

    // -----------------------------------------------------------

    mid = (*env)->GetMethodID(env, avc->avc_MediaCodec, "getInputBuffers",
                              "()[Ljava/nio/ByteBuffer;");

    obj = (*env)->CallObjectMethod(env, avc->avc_decoder, mid);
    check_exception(env, "MediaCodec.getInputBuffers");
    avc->avc_input_buffers = (*env)->NewGlobalRef(env, obj);

    // -----------------------------------------------------------

    avc_get_output_buffers(env, avc);

    // -----------------------------------------------------------

    jclass BufferInfo;

    BufferInfo = (*env)->FindClass(env, "android/media/MediaCodec$BufferInfo");

    mid = (*env)->GetMethodID(env, BufferInfo, "<init>", "()V");
    avc->avc_buffer_info = (*env)->NewObject(env, BufferInfo, mid);
    avc->avc_buffer_info = (*env)->NewGlobalRef(env, avc->avc_buffer_info);

    // -----------------------------------------------------------


    avc->avc_dequeueInputBuffer =
      (*env)->GetMethodID(env, avc->avc_MediaCodec, "dequeueInputBuffer", "(J)I");

    avc->avc_queueInputBuffer =
      (*env)->GetMethodID(env, avc->avc_MediaCodec, "queueInputBuffer", "(IIIJI)V");

    avc->avc_dequeueOutputBuffer =
      (*env)->GetMethodID(env, avc->avc_MediaCodec, "dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I");

    avc->avc_releaseOutputBuffer =
      (*env)->GetMethodID(env, avc->avc_MediaCodec, "releaseOutputBuffer", "(IZ)V");

    // -----------------------------------
    avc->avc_configured = 1;
  }

  uint8_t *data = mb->mb_data;
  size_t size = mb->mb_size;

  if(avc->avc_annexb.extradata_injected)
    h264_to_annexb(&avc->avc_annexb, &data, &size);


  jlong pts;
#ifdef PTS_IS_REORDER_INDEX
  media_buf_meta_t *mbm = &vd->vd_reorder[vd->vd_reorder_ptr];
  copy_mbm_from_mb(mbm, mb);
  pts = vd->vd_reorder_ptr | 0x010c0000;
  AVC_TRACE("Enqueue buffer reorderslot:%d PTS=%"PRId64,
            vd->vd_reorder_ptr, mbm->mbm_pts);
  vd->vd_reorder_ptr = (vd->vd_reorder_ptr + 1) & VIDEO_DECODER_REORDER_MASK;
#else
  pts = mb->mb_pts;
#endif

  int timeout = 0;
  const int flags = mb->mb_keyframe ? 1 : 0; // BUFFER_FLAG_KEY_FRAME
  while(1) {
    int idx = avc_enq(env, avc, data, size, pts, flags, timeout);

    if(idx == -2)
      break;

    if(idx == -1) {
      get_output(env, avc, timeout > 0, vd);
      timeout = 1000;
      continue;
    }
    break;
  }
  (*env)->PopLocalFrame(env, NULL);
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
  android_video_codec_t *avc = mc->opaque;
  jmethodID mid;
  JNIEnv *env;

  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  (*env)->PushLocalFrame(env, 64);

  mid = (*env)->GetMethodID(env, avc->avc_MediaCodec, "release", "()V");
  (*env)->CallVoidMethod(env, avc->avc_decoder, mid);
  check_exception(env, "MediaCodec.release");

  (*env)->PopLocalFrame(env, NULL);
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
  const char *nicename = NULL;
  if(!video_settings.video_accel)
    return 1;

  switch(mc->codec_id) {

  case AV_CODEC_ID_H264:
    type = "video/avc";
    nicename = "h264";
    break;
  case AV_CODEC_ID_VP8:
    type = "video/x-vnd.on2.vp8";
    nicename = "vp8";
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
  if(check_exception(env, "MediaCodec.createDecoderByType")) {
    TRACE(TRACE_DEBUG, "Video", "Unable to create Android decoder for %s",
          type);
    return 1;
  }
  if(decoder == 0) {
    TRACE(TRACE_DEBUG, "Video", "Unable to create Android decoder for %s",
          type);
    return 1;
  }

  android_video_codec_t *avc = calloc(1, sizeof(android_video_codec_t));
  avc->avc_decoder = (*env)->NewGlobalRef(env, decoder);

  avc->avc_MediaCodec =
    (*env)->NewGlobalRef(env, (*env)->GetObjectClass(env, avc->avc_decoder));

  avc->avc_mime   = type;

  if(mcp != NULL) {

    avc->avc_width  = mcp->width;
    avc->avc_height = mcp->height;

    if(mcp->extradata != NULL && mcp->extradata_size) {

      if(mc->codec_id == AV_CODEC_ID_H264) {

        h264_to_annexb_init(&avc->avc_annexb, mcp->extradata,
                            mcp->extradata_size);
      } else {
        avc->avc_extradata = malloc(mcp->extradata_size);
        memcpy(avc->avc_extradata, mcp->extradata, mcp->extradata_size);
        avc->avc_extradata_size = mcp->extradata_size;
      }
    }
  } else {
    avc->avc_width  = 1280;
    avc->avc_height = 720;
  }

  avc->avc_nicename = nicename;
  avc->avc_direct = 1;
  mc->opaque = avc;
  mc->close  = android_codec_close;
  mc->decode = android_codec_decode;
  mc->flush  = android_codec_flush;
  TRACE(TRACE_DEBUG, "Video", "Accelerated codec for %s", type);
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
