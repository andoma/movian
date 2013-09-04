/*
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

#include <unistd.h>

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <libswscale/swscale.h>

#include "showtime.h"
#include "glw_video_common.h"

#include "arch/android/android_glw.h"
#include "arch/android/android.h"

#include <libyuv.h>

extern JavaVM *JVM;

typedef struct android_video {

  jobject av_VideoRenderer;
  jclass av_VideoRendererClass;

  int64_t av_pts;

  int av_display_width, av_display_height;
  int av_display_left, av_display_top;

} android_video_t;


/**
 *
 */
static int
android_init(glw_video_t *gv)
{
  android_glw_root_t *agr = (android_glw_root_t *)gv->w.glw_root;

  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  jclass class = (*env)->GetObjectClass(env, agr->agr_vrp);
  jmethodID mid = (*env)->GetMethodID(env, class, "createVideoRenderer",
                                      "()Lcom/showtimemediacenter/showtime/VideoRenderer;");
  jobject vr = (*env)->CallObjectMethod(env, agr->agr_vrp, mid);


  android_video_t *av = calloc(1, sizeof(android_video_t));
  gv->gv_aux = av;

  av->av_VideoRenderer = (*env)->NewGlobalRef(env, vr);

  class = (*env)->GetObjectClass(env, av->av_VideoRenderer);
  av->av_VideoRendererClass = (*env)->NewGlobalRef(env, class);

  return 0;
}



/**
 *
 */
static int64_t
android_newframe(glw_video_t *gv, video_decoder_t *vd0, int flags)
{
  android_video_t *av = gv->gv_aux;
  return av->av_pts;
}



const static float projection[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};


/**
 *
 */
static void
android_render(glw_video_t *gv, glw_rctx_t *rc)
{
  android_video_t *av = gv->gv_aux;
  glw_root_t *gr = gv->w.glw_root;
  Mtx foo;

  PMtx tm, tp;
  Vec4 T0, T1;
  Vec4 V0, V1;

  glw_pmtx_mul_prepare(tm,  rc->rc_mtx);
  glw_pmtx_mul_vec4(T0, tm, glw_vec4_make(-1,  1, 0, 1));
  glw_pmtx_mul_vec4(T1, tm, glw_vec4_make( 1, -1, 0, 1));

  memcpy(foo, projection, sizeof(float) * 16);
  glw_pmtx_mul_prepare(tp, foo);

  glw_pmtx_mul_vec4(V0, tp, T0);
  glw_pmtx_mul_vec4(V1, tp, T1);

  int x1, y1, x2, y2;

  float w;

  w = glw_vec4_extract(V0, 3);

  x1 = roundf((1.0 + (glw_vec4_extract(V0, 0) / w)) * gr->gr_width  / 2.0);
  y1 = roundf((1.0 - (glw_vec4_extract(V0, 1) / w)) * gr->gr_height / 2.0);

  w = glw_vec4_extract(V1, 3);

  x2 = roundf((1.0 + (glw_vec4_extract(V1, 0) / w)) * gr->gr_width  / 2.0);
  y2 = roundf((1.0 - (glw_vec4_extract(V1, 1) / w)) * gr->gr_height / 2.0);

  if(x2 <= x1 || y2 <= y1)
    return;

  int width  = x2 - x1;
  int height = y2 - y1;

  if(x1     == av->av_display_left &&
     y1     == av->av_display_top &&
     width  == av->av_display_width &&
     height == av->av_display_height)
    return;

  av->av_display_left   = x1;
  av->av_display_top    = y1;
  av->av_display_width  = width;
  av->av_display_height = height;

  jmethodID mid;
  jclass class = av->av_VideoRendererClass;

  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  mid = (*env)->GetMethodID(env, class, "setPosition",
                            "(IIII)V");
  (*env)->CallVoidMethod(env, av->av_VideoRenderer, mid,
                         x1, y1, width, height);
}



/**
 *
 */
static void
android_reset(glw_video_t *gv)
{
  android_glw_root_t *agr = (android_glw_root_t *)gv->w.glw_root;
  android_video_t *av = gv->gv_aux;

  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  jclass class = (*env)->GetObjectClass(env, agr->agr_vrp);
  jmethodID mid = (*env)->GetMethodID(env, class, "destroyVideoRenderer",
                                      "(Lcom/showtimemediacenter/showtime/VideoRenderer;)V");
  (*env)->CallVoidMethod(env, agr->agr_vrp, mid, av->av_VideoRenderer);

  (*env)->DeleteGlobalRef(env, av->av_VideoRenderer);
  (*env)->DeleteGlobalRef(env, av->av_VideoRendererClass);

  free(av);
}


static void android_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_android_video_yuvp = {
  .gve_type              = 'YUVP',
  .gve_newframe          = android_newframe,
  .gve_render            = android_render,
  .gve_reset             = android_reset,
  .gve_init              = android_init,
  .gve_deliver           = android_deliver,
};

GLW_REGISTER_GVE(glw_android_video_yuvp);

static void
android_deliver(const frame_info_t *fi, glw_video_t *gv)
{
  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  if(glw_video_configure(gv, &glw_android_video_yuvp))
    return;

  android_video_t *av = gv->gv_aux;
  media_pipe_t *mp = gv->gv_mp;

  int64_t aclock, d;
  int a_epoch;

 recheck:
  if(gv->w.glw_flags & GLW_DESTROYING)
    return;

  int64_t now = showtime_get_ts();
  hts_mutex_lock(&mp->mp_clock_mutex);
  aclock = mp->mp_audio_clock + now -
    mp->mp_audio_clock_avtime + mp->mp_avdelta;
  a_epoch = mp->mp_audio_clock_epoch;
  hts_mutex_unlock(&mp->mp_clock_mutex);

  int64_t pts = fi->fi_pts;

  d = pts - aclock;

  int delta = 0;

  if(pts == AV_NOPTS_VALUE || d < -5000000LL || d > 5000000LL)
    pts = gv->gv_nextpts;

  if(pts != AV_NOPTS_VALUE && (pts - delta) >= aclock &&
     a_epoch == fi->fi_epoch) {

    int64_t waittime = (pts - delta) - aclock;

    if(waittime > 100000)
      waittime = 100000;
    usleep(waittime);
    goto recheck;
  }

  av->av_pts = pts;

  jmethodID mid;
  jclass class = av->av_VideoRendererClass;

  (*env)->PushLocalFrame(env, 64);

  mid = (*env)->GetMethodID(env, class, "getSurface",
                            "()Landroid/view/Surface;");

  jobject surface = (*env)->CallObjectMethod(env, av->av_VideoRenderer, mid);

  if(surface) {

    ANativeWindow *anw = ANativeWindow_fromSurface(env, surface);

    ANativeWindow_setBuffersGeometry(anw, fi->fi_width, fi->fi_height,
                                     WINDOW_FORMAT_RGBA_8888);

    ANativeWindow_Buffer buffer;

    ANativeWindow_lock(anw, &buffer, NULL);

    I420ToARGB(fi->fi_data[0], fi->fi_pitch[0],
               fi->fi_data[1], fi->fi_pitch[1],
               fi->fi_data[2], fi->fi_pitch[2],
               buffer.bits, buffer.stride * 4,
               fi->fi_width, fi->fi_height);

    ANativeWindow_unlockAndPost(anw);

    ANativeWindow_release(anw);
    (*env)->DeleteLocalRef(env, surface);
  }

  mid = (*env)->GetMethodID(env, class, "releaseSurface", "()V");
  (*env)->CallVoidMethod(env, av->av_VideoRenderer, mid);

  (*env)->PopLocalFrame(env, NULL);
}
