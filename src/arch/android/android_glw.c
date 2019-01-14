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
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <jni.h>
#include <android/keycodes.h>
#include <GLES2/gl2.h>

#include "arch/arch.h"
#include "text/text.h"
#include "main.h"
#include "navigator.h"

#include "ui/glw/glw.h"

#include "android.h"
#include "android_glw.h"

static android_glw_root_t *permission_glw_root;
static pthread_mutex_t permission_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t permission_cond = PTHREAD_COND_INITIALIZER;

static enum {
  PERMISSION_STATE_NONE,
  PERMISSION_STATE_WAITING,
  PERMISSION_STATE_APPROVED,
  PERMISSION_STATE_DENIED,
} permission_state;



int
android_get_permission(const char *permission, int interactive)
{
  extern int android_sdk;
  if(android_sdk < 23)
    return 1; // Before Android 6.0, permissions are always granted
  jmethodID mid;
  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);
  jstring jstr = (*env)->NewStringUTF(env, permission);

  mid = (*env)->GetStaticMethodID(env, STCore, "checkPermission",
                                  "(Ljava/lang/String;)Z");

  int ok = (*env)->CallStaticBooleanMethod(env, STCore, mid, jstr);

  if(ok)
    return 1;

  if(!interactive)
    return 0;

  pthread_mutex_lock(&permission_mutex);

  while(permission_state != PERMISSION_STATE_NONE)
    pthread_cond_wait(&permission_cond, &permission_mutex);

  if(permission_glw_root == NULL) {
    permission_state = PERMISSION_STATE_NONE;
    pthread_mutex_unlock(&permission_mutex);
    return 0;
  }

  permission_state = PERMISSION_STATE_WAITING;

  jclass class = (*env)->GetObjectClass(env, permission_glw_root->agr_vrp);
  mid = (*env)->GetMethodID(env, class, "askPermission",
                            "(Ljava/lang/String;)V");
  (*env)->CallVoidMethod(env, permission_glw_root->agr_vrp, mid, jstr);

  while(permission_state == PERMISSION_STATE_WAITING)
    pthread_cond_wait(&permission_cond, &permission_mutex);

  const int r = permission_state;
  permission_state = PERMISSION_STATE_NONE;
  pthread_cond_broadcast(&permission_cond);
  pthread_mutex_unlock(&permission_mutex);
  return r == PERMISSION_STATE_APPROVED;
}



/**
 *
 */
JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_permissionResult(JNIEnv *env, jboolean ok)
{
  pthread_mutex_lock(&permission_mutex);
  permission_state = ok ? PERMISSION_STATE_APPROVED : PERMISSION_STATE_DENIED;
  pthread_cond_broadcast(&permission_cond);
  pthread_mutex_unlock(&permission_mutex);

}

extern char android_intent[PATH_MAX];
static void
dis_screensaver_callback(void *opaque, int value)
{
  android_glw_root_t *agr = opaque;
  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  jclass class = (*env)->GetObjectClass(env, agr->agr_vrp);
  jmethodID mid = (*env)->GetMethodID(env, class,
                                      value ? "disableScreenSaver" :
                                      "enableScreenSaver",
                                      "()V");

  (*env)->CallVoidMethod(env, agr->agr_vrp, mid);
}


static void
nav_eventsink(void *opaque, event_t *e)
{
  android_glw_root_t *agr = opaque;
  JNIEnv *env;
  (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  jclass class = (*env)->GetObjectClass(env, agr->agr_vrp);
  if(event_is_action(e, ACTION_SYSTEM_HOME)) {
    jmethodID mid = (*env)->GetMethodID(env, class, "sysHome", "()V");
    (*env)->CallVoidMethod(env, agr->agr_vrp, mid);
  }
}

JNIEXPORT jint JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwCreate(JNIEnv *env,
                                                       jobject obj,
                                                       jobject vrp)
{
  android_glw_root_t *agr = calloc(1, sizeof(android_glw_root_t));
  agr->gr.gr_prop_ui = prop_create_root("ui");
  agr->gr.gr_prop_nav = android_nav;

  if(glw_init(&agr->gr))
    return 0;

  agr->gr.gr_be.gbr_use_stencil_buffer = 1;

  hts_cond_init(&agr->agr_runcond, &agr->gr.gr_mutex);

  agr->agr_vrp = (*env)->NewGlobalRef(env, vrp);

  agr->agr_disable_screensaver_sub =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK_INT, dis_screensaver_callback, agr,
                   PROP_TAG_NAME("ui", "disableScreensaver"),
                   PROP_TAG_ROOT, agr->gr.gr_prop_ui,
                   PROP_TAG_COURIER, agr->gr.gr_courier,
                   NULL);

  agr->agr_nav_eventsink_sub =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK_EVENT, nav_eventsink, agr,
                   PROP_TAG_NAME("nav", "eventSink"),
                   PROP_TAG_NAMED_ROOT, android_nav, "nav",
                   PROP_TAG_COURIER, agr->gr.gr_courier,
                   NULL);

  pthread_mutex_lock(&permission_mutex);
  permission_glw_root = agr;
  pthread_mutex_unlock(&permission_mutex);


  glw_load_universe(&agr->gr);
  return (intptr_t)agr;
}


JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwInit(JNIEnv *env,
                                                     jobject obj,
                                                     jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  agr->agr_running = 1;
  glw_opengl_init_context(&agr->gr);
  glClearColor(0,0,0,0);
}


JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwFini(JNIEnv *env,
                                                     jobject obj,
                                                     jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;

  pthread_mutex_lock(&permission_mutex);
  permission_glw_root = NULL;
  permission_state = PERMISSION_STATE_DENIED;
  pthread_cond_broadcast(&permission_cond);
  pthread_mutex_unlock(&permission_mutex);

  glw_lock(gr);
  // Calling twice will unload all textures, etc
  glw_reap(gr);
  glw_reap(gr);
  glw_flush(gr);
  glw_opengl_fini_context(gr);
  agr->agr_running = 0;
  hts_cond_signal(&agr->agr_runcond);
  glw_unlock(gr);
}


JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwDestroy(JNIEnv *env,
                                                 jobject obj,
                                                 jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;

  prop_unsubscribe(agr->agr_disable_screensaver_sub);
  prop_unsubscribe(agr->agr_nav_eventsink_sub);

  glw_lock(gr);
  while(agr->agr_running == 1)
    hts_cond_wait(&agr->agr_runcond, &gr->gr_mutex);
  glw_unlock(gr);

  (*env)->DeleteGlobalRef(env, agr->agr_vrp);

  glw_unload_universe(gr);
  glw_fini(gr);
  glw_release_root(gr);
}



JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwResize(JNIEnv *env,
                                                       jobject obj,
                                                       jint id,
                                                       jint width,
                                                       jint height)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;

  TRACE(TRACE_INFO, "GLW", "Resized to %d x %d", width, height);
  agr->gr.gr_width  = width;
  agr->gr.gr_height = height;

  if(android_intent[0])
  {
    TRACE(TRACE_INFO, "INTENT", "Loading: %s", android_intent);
    nav_open(android_intent, NULL);
    android_intent[0]=0;
  }

}


JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwFlush(JNIEnv *env,
                                               jobject obj,
                                               jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;
  TRACE(TRACE_INFO, "GLW", "Flushed");
  glw_lock(gr);
  // Calling twice will unload all textures, etc
  glw_reap(gr);
  glw_reap(gr);
  glw_flush(gr);
  glw_unlock(gr);
}


JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwStep(JNIEnv *env,
                                                     jobject obj,
                                                     jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;
  int zmax;

  glw_lock(gr);
  glViewport(0, 0, gr->gr_width, gr->gr_height);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glEnable(GL_STENCIL_TEST);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glDepthMask(GL_FALSE);

  glw_prepare_frame(gr, 0);

  glw_rctx_t rc;
  glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1, &zmax);
  glw_layout0(gr->gr_universe, &rc);
  glw_render0(gr->gr_universe, &rc);


  if(gconf.enable_touch_debug) {
    glw_line(gr, &rc,
             gr->gr_touch_start_x, 1,
             gr->gr_touch_start_x, -1,
             1, 0, 0, 1);

    glw_line(gr, &rc,
             -1, gr->gr_touch_start_y,
             1, gr->gr_touch_start_y,
             1, 0, 0, 1);


    glw_line(gr, &rc,
             gr->gr_touch_move_x, 1,
             gr->gr_touch_move_x, -1,
             0, 1, 0, 1);

    glw_line(gr, &rc,
             -1, gr->gr_touch_move_y,
             1, gr->gr_touch_move_y,
             0, 1, 0, 1);


    glw_line(gr, &rc,
             gr->gr_touch_end_x, 1,
             gr->gr_touch_end_x, -1,
             0, 0, 1, 1);

    glw_line(gr, &rc,
             -1, gr->gr_touch_end_y,
             1, gr->gr_touch_end_y,
             0, 0, 1, 1);
  }

  glw_unlock(gr);

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);

  glw_post_scene(gr);

  if(longpress_periodic(&agr->agr_dpad_center, gr->gr_frame_start)) {
    event_t *e = event_create_action(ACTION_ITEMMENU);
    e->e_flags |= EVENT_KEYPRESS;
    glw_inject_event(gr, e);
  }
}


JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwMotion(JNIEnv *env,
                                                jobject obj,
                                                jint id,
                                                jint source,
                                                jint action,
                                                jint x,
                                                jint y,
                                                jlong ts)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;
  glw_pointer_event_t gpe = {0};

  // source == 0x1002  == touch
  gpe.ts = ts * 1000LL;
  switch(action) {
  case 0: gpe.type = GLW_POINTER_TOUCH_START;  break;
  case 1: gpe.type = GLW_POINTER_TOUCH_END;    break;
  case 2: gpe.type = GLW_POINTER_TOUCH_MOVE;   break;
  case 3: gpe.type = GLW_POINTER_TOUCH_CANCEL; break;
  default: return;
  }

  if(gconf.enable_input_event_debug)
    TRACE(TRACE_DEBUG, "Touch", "%6s [%d] @ %4d,%-4d  src=0x%x",
          gpe.type == GLW_POINTER_TOUCH_START  ? "Start" :
          gpe.type == GLW_POINTER_TOUCH_END    ? "End" :
          gpe.type == GLW_POINTER_TOUCH_MOVE   ? "Move" :
          gpe.type == GLW_POINTER_TOUCH_CANCEL ? "Cancel" : "?",
          action,
          x, y, source);

  glw_lock(gr);
  gpe.screen_x =  (2.0f * x / gr->gr_width ) - 1.0f;
  gpe.screen_y = -(2.0f * y / gr->gr_height) + 1.0f;
  glw_pointer_event(gr, &gpe);
  glw_unlock(gr);
}



#define end_of_AKEYCODE (AKEYCODE_BUTTON_MODE+1)

#define AVEC(x...) (const action_type_t []){x, ACTION_NONE}

const static action_type_t *btn_to_action[end_of_AKEYCODE] = {
  [AKEYCODE_BACK]            = AVEC(ACTION_NAV_BACK),
  [AKEYCODE_DPAD_LEFT]       = AVEC(ACTION_LEFT),
  [AKEYCODE_DPAD_UP]         = AVEC(ACTION_UP),
  [AKEYCODE_DPAD_RIGHT]      = AVEC(ACTION_RIGHT),
  [AKEYCODE_DPAD_DOWN]       = AVEC(ACTION_DOWN),
  [AKEYCODE_MENU]            = AVEC(ACTION_MENU),
  [AKEYCODE_STAR]            = AVEC(ACTION_ITEMMENU),
  [AKEYCODE_MEDIA_REWIND]       = AVEC(ACTION_SEEK_BACKWARD),
  [AKEYCODE_MEDIA_FAST_FORWARD] = AVEC(ACTION_SEEK_FORWARD),
  [AKEYCODE_MEDIA_PLAY_PAUSE]   = AVEC(ACTION_PLAYPAUSE),
  [AKEYCODE_ENTER]           = AVEC(ACTION_ACTIVATE),
  [AKEYCODE_DEL]             = AVEC(ACTION_NAV_BACK, ACTION_BS),
};

const static action_type_t *shift_btn_to_action[end_of_AKEYCODE] = {
  [AKEYCODE_DPAD_LEFT]       = AVEC(ACTION_MOVE_LEFT),
  [AKEYCODE_DPAD_UP]         = AVEC(ACTION_MOVE_UP),
  [AKEYCODE_DPAD_RIGHT]      = AVEC(ACTION_MOVE_RIGHT),
  [AKEYCODE_DPAD_DOWN]       = AVEC(ACTION_MOVE_DOWN),
};


JNIEXPORT jboolean JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwKeyDown(JNIEnv *env,
                                                 jobject obj,
                                                 jint id,
                                                 jint keycode,
                                                 jint unicode,
                                                 jboolean shift)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;
  event_t *e = NULL;

  // along with AKEYCODE_ENTER usually passed Line Feed char - 10
  if(keycode == AKEYCODE_ENTER)
    unicode = 0;

  if(gconf.enable_input_event_debug)
    TRACE(TRACE_DEBUG, "KEYBOARD", "KeyDown: AndroidKey:%d Unicode:%d",
          keycode, unicode);

  if(unicode != 0) {
    e = event_create_int(EVENT_UNICODE, unicode);
  } else {


    if(keycode == AKEYCODE_DPAD_CENTER) {
      longpress_down(&agr->agr_dpad_center);
      return 1;
    }

    if(keycode < end_of_AKEYCODE) {
      const action_type_t *avec = shift ? shift_btn_to_action[keycode] :
        btn_to_action[keycode];
      if(avec) {
        int i = 0;
        while(avec[i] != 0)
          i++;
        e = event_create_action_multi(avec, i);
      }
    }
  }

  if(e != NULL) {
    e->e_flags |= EVENT_KEYPRESS;
    glw_inject_event(gr, e);
  }
  
  return e != NULL;
}

JNIEXPORT jboolean JNICALL
Java_com_lonelycoder_mediaplayer_Core_glwKeyUp(JNIEnv *env,
                                                      jobject obj,
                                                      jint id,
                                                      jint keycode)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  event_t *e = NULL;

  if(gconf.enable_input_event_debug)
    TRACE(TRACE_DEBUG, "KEYBOARD", "KeyUp: AndroidKey:%d", keycode);

  switch(keycode) {
  case AKEYCODE_DPAD_CENTER:
    if(longpress_up(&agr->agr_dpad_center))
      e = event_create_action(ACTION_ACTIVATE);
    break;
  }

  if(e != NULL) {
    e->e_flags |= EVENT_KEYPRESS;
    glw_inject_event(&agr->gr, e);
    return 1;
  }
  return 0;
}

