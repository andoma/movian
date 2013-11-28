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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <jni.h>
#include <android/keycodes.h>
#include <GLES2/gl2.h>

#include "arch/arch.h"
#include "text/text.h"
#include "showtime.h"
#include "navigator.h"

#include "ui/glw/glw.h"

#include "android.h"
#include "android_glw.h"

JNIEXPORT jint JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwCreate(JNIEnv *env,
                                                       jobject obj,
                                                       jobject vrp)
{
  android_glw_root_t *agr = calloc(1, sizeof(android_glw_root_t));
  agr->gr.gr_prop_ui = prop_create_root("ui");
  agr->gr.gr_prop_nav = android_nav;

  if(glw_init(&agr->gr))
    return 0;

  hts_cond_init(&agr->agr_runcond, &gr->gr_mutex);

  agr->agr_vrp = (*env)->NewGlobalRef(env, vrp);

  TRACE(TRACE_DEBUG, "GLW", "GLW %p created", agr);

  glw_load_universe(&agr->gr);
  return (intptr_t)agr;
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwInit(JNIEnv *env,
                                                     jobject obj,
                                                     jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  TRACE(TRACE_DEBUG, "GLW", "GLW %p initialied", agr);
  agr->agr_running = 1;
  glw_opengl_init_context(&agr->gr);
  glClearColor(0,0,0,0);
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwFini(JNIEnv *env,
                                                     jobject obj,
                                                     jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;

  TRACE(TRACE_DEBUG, "GLW", "GLW %p finishing", agr);

  glw_lock(gr);
  // Calling twice will unload all textures, etc
  glw_reap(gr);
  glw_reap(gr);
  glw_flush(gr);
  agr->agr_running = 0;
  hts_cond_signal(&agr->agr_runcond);
  glw_unlock(gr);
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwDestroy(JNIEnv *env,
                                                     jobject obj,
                                                     jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;

  TRACE(TRACE_DEBUG, "GLW", "GLW %p destroying", agr);

  glw_lock(gr);
  while(agr->agr_running == 1)
    hts_cond_wait(&agr->agr_runcond, &gr->gr_mutex);
  glw_unlock(gr);

  TRACE(TRACE_DEBUG, "GLW", "GLW %p destroyed", agr);

  (*env)->DeleteGlobalRef(env, agr->agr_vrp);

  glw_unload_universe(gr);
  glw_fini(gr);
  free(gr);
}



JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwResize(JNIEnv *env,
                                                       jobject obj,
                                                       jint id,
                                                       jint width,
                                                       jint height)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;

  TRACE(TRACE_INFO, "GLW", "Resized to %d x %d", width, height);
  agr->gr.gr_width  = width;
  agr->gr.gr_height = height;
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwStep(JNIEnv *env,
                                                     jobject obj,
                                                     jint id)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;

  glw_lock(gr);
  gr->gr_can_externalize = 1;
  gr->gr_externalize_cnt = 0;
  glViewport(0, 0, gr->gr_width, gr->gr_height);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glw_prepare_frame(gr, 0);

  glw_rctx_t rc;
  glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1);
  glw_layout0(gr->gr_universe, &rc);
  glw_render0(gr->gr_universe, &rc);

  glw_unlock(gr);
  glw_post_scene(gr);

}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwMotion(JNIEnv *env,
                                                       jobject obj,
                                                       jint id,
                                                       jint action,
                                                       jint x,
                                                       jint y)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;
  glw_pointer_event_t gpe = {0};

  switch(action) {
  case 0: gpe.type = GLW_POINTER_LEFT_PRESS;    break;
  case 1: gpe.type = GLW_POINTER_LEFT_RELEASE;  break;
  case 2: gpe.type = GLW_POINTER_MOTION_UPDATE; break;
  default: return;
  }

  glw_lock(gr);
  gpe.x =  (2.0f * x / gr->gr_width ) - 1.0f;
  gpe.y = -(2.0f * y / gr->gr_height) + 1.0f;
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
  [AKEYCODE_DPAD_CENTER]     = AVEC(ACTION_ACTIVATE),
  [AKEYCODE_MENU]            = AVEC(ACTION_MENU),
  [AKEYCODE_STAR]            = AVEC(ACTION_ITEMMENU),
};



JNIEXPORT jboolean JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwKeyDown(JNIEnv *env,
                                                        jobject obj,
                                                        jint id,
                                                        jint keycode)
{
  android_glw_root_t *agr = (android_glw_root_t *)id;
  glw_root_t *gr = &agr->gr;
  event_t *e = NULL;

  if(keycode < end_of_AKEYCODE) {
    const action_type_t *avec = btn_to_action[keycode];
    if(avec) {
      int i = 0;
      while(avec[i] != 0)
        i++;
      e = event_create_action_multi(avec, i);
    }
  }

  TRACE(TRACE_DEBUG, "ANDROID", "Input key %d -> %p", keycode, e);

  if(e != NULL)
    glw_inject_event(gr, e);

  return e != NULL;
}
