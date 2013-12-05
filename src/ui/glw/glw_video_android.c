/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

  glw_rect_t av_display_rect;

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


/**
 *
 */
static void
android_render(glw_video_t *gv, glw_rctx_t *rc)
{
  android_video_t *av = gv->gv_aux;

  if(!memcmp(&av->av_display_rect, &gv->gv_rect, sizeof(glw_rect_t)))
    return;

  av->av_display_rect = gv->gv_rect;

  jclass class = av->av_VideoRendererClass;

  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  jmethodID mid = (*env)->GetMethodID(env, class, "setPosition", "(IIII)V");

  TRACE(TRACE_DEBUG, "GLW", "Video repositioned to %d %d %d %d",
        gv->gv_rect.x1,
        gv->gv_rect.y1,
        gv->gv_rect.x2,
        gv->gv_rect.y2);

  (*env)->CallVoidMethod(env, av->av_VideoRenderer, mid,
                         gv->gv_rect.x1,
                         gv->gv_rect.y1,
                         gv->gv_rect.x2 - gv->gv_rect.x1,
                         gv->gv_rect.y2 - gv->gv_rect.y1);
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


static void android_yuvp_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_android_video_yuvp = {
  .gve_type              = 'YUVP',
  .gve_newframe          = android_newframe,
  .gve_render            = android_render,
  .gve_reset             = android_reset,
  .gve_init              = android_init,
  .gve_deliver           = android_yuvp_deliver,
};

GLW_REGISTER_GVE(glw_android_video_yuvp);


static void
android_yuvp_deliver(const frame_info_t *fi, glw_video_t *gv)
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
               fi->fi_data[2], fi->fi_pitch[2],
               fi->fi_data[1], fi->fi_pitch[1],
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



static void android_xxxx_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_android_video_xxxx = {
  .gve_type              = 'XXXX',
  .gve_newframe          = android_newframe,
  .gve_render            = android_render,
  .gve_reset             = android_reset,
  .gve_init              = android_init,
  .gve_deliver           = android_xxxx_deliver,
};

GLW_REGISTER_GVE(glw_android_video_xxxx);


static void
android_xxxx_deliver(const frame_info_t *fi, glw_video_t *gv)
{
  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  if(glw_video_configure(gv, &glw_android_video_xxxx))
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

    UYVYToARGB(fi->fi_data[0], fi->fi_pitch[0],
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




static int surface_set_codec(media_codec_t *mc, glw_video_t *gv);
static void surface_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_android_video_surface = {
  .gve_type              = 'SURF',
  .gve_newframe          = android_newframe,
  .gve_render            = android_render,
  .gve_reset             = android_reset,
  .gve_init              = android_init,
  .gve_set_codec         = surface_set_codec,
  .gve_deliver           = surface_deliver,
};

GLW_REGISTER_GVE(glw_android_video_surface);


/**
 * This is crap and broken in many ways
 *
 * (related to how the surface locking is (not) done)
 */
static int
surface_set_codec(media_codec_t *mc, glw_video_t *gv)
{

  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  TRACE(TRACE_DEBUG, "VIDEO", "Configuring video surface");

  if(glw_video_configure(gv, &glw_android_video_surface))
    return 0;


  android_video_t *av = gv->gv_aux;
  jmethodID mid;
  jclass class = av->av_VideoRendererClass;

  mid = (*env)->GetMethodID(env, class, "getSurfaceUnlocked",
                            "()Landroid/view/Surface;");

  jobject surface = 0;

  while(1) {
    surface = (*env)->CallObjectMethod(env, av->av_VideoRenderer, mid);

    if(surface)
      break;
    usleep(10000);
  }
  return (int)surface;
}


static void
surface_deliver(const frame_info_t *fi, glw_video_t *gv)
{
  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  if(glw_video_configure(gv, &glw_android_video_surface))
    return;

  android_video_t *av = gv->gv_aux;
  media_pipe_t *mp = gv->gv_mp;

  int64_t aclock, d;
  int a_epoch;

  static int64_t lastpts;
  static int64_t lastts;
  int64_t ts = showtime_get_ts();

  TRACE(TRACE_DEBUG, "TIMESTAMP", "%20lld %10lld %20lld %10lld",
        fi->fi_pts, fi->fi_pts - lastpts,
        ts,         ts - lastts);

  lastpts = fi->fi_pts;
  lastts  = ts;

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
}
