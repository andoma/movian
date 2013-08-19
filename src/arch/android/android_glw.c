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

static glw_root_t *gr;

JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwInit(JNIEnv *env,
                                                     jobject obj,
                                                     jint width,
                                                     jint height)
{
  if(gr == NULL) {
    gr = calloc(1, sizeof(glw_root_t));
    gr->gr_prop_ui = prop_create_root("ui");
    gr->gr_prop_nav = nav_spawn();

    if(glw_init(gr)) {
      TRACE(TRACE_ERROR, "GLW", "Unable to init GLW");
      exit(1);
    }

    glw_load_universe(gr);
  } else {
    TRACE(TRACE_INFO, "GLW", "Flusing all state");
    glw_flush(gr);
  }

  glw_opengl_init_context(gr);
  glClearColor(0,0,0,0);
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwResize(JNIEnv *env,
                                                       jobject obj,
                                                       jint width,
                                                       jint height)
{
  TRACE(TRACE_INFO, "GLW", "Resized to %d x %d", width, height);
  gr->gr_width  = width;
  gr->gr_height = height;
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwStep(JNIEnv *env,
                                                     jobject obj)
{
  glw_lock(gr);

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
                                                       jint action,
                                                       jint x,
                                                       jint y)
{
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


JNIEXPORT jboolean JNICALL
Java_com_showtimemediacenter_showtime_STCore_glwKeyDown(JNIEnv *env,
                                                        jobject obj,
                                                        jint keycode)
{
  event_t *e = NULL;

  if(keycode == AKEYCODE_BACK) {
    e = event_create_action_multi(
                                  (const action_type_t[]){
                                    ACTION_BS, ACTION_NAV_BACK}, 2);
  }
  if(e != NULL)
    glw_inject_event(gr, e);

  return e != NULL;
}
