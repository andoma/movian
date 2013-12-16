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

#include "showtime.h"
#include "android.h"


void
android_bitmap_destroy(JNIEnv *env, jobject bitmap)
{
  jclass class = (*env)->GetObjectClass(env, bitmap);
  jmethodID mid = (*env)->GetMethodID(env, class, "recycle", "()V");
  (*env)->CallVoidMethod(env, bitmap, mid);
  (*env)->DeleteGlobalRef(env, bitmap);
}


jobject
android_bitmap_create(JNIEnv *env, int width, int height)
{
  jmethodID mid = (*env)->GetStaticMethodID(env, STCore, "createBitmap", "(II)Landroid/graphics/Bitmap;");
  jobject obj = (*env)->CallStaticObjectMethod(env, STCore, mid, width, height);
  return (*env)->NewGlobalRef(env, obj);
}
