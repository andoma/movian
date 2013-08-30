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
#include <locale.h>
#include <limits.h>
#include <syslog.h>
#include <sys/vfs.h>
#include <signal.h>
#include <android/log.h>
#include <jni.h>
#include <GLES2/gl2.h>

#include "arch/arch.h"
#include "showtime.h"
#include "service.h"
#include "networking/net.h"
#include "ui/glw/glw.h"
#include "prop/prop_jni.h"
#include "android.h"
#include "navigator.h"

JavaVM *JVM;
jclass STCore;
prop_t *android_nav;

/**
 *
 */
void
trace_arch(int level, const char *prefix, const char *str)
{
  int prio;
  switch(level) {
  case TRACE_EMERG:   prio = ANDROID_LOG_FATAL; break;
  case TRACE_ERROR:   prio = ANDROID_LOG_ERROR; break;
  case TRACE_INFO:    prio = ANDROID_LOG_INFO;  break;
  case TRACE_DEBUG:   prio = ANDROID_LOG_DEBUG; break;
  default:            prio = ANDROID_LOG_ERROR; break;
  }
  __android_log_print(prio, "Showtime", "%s %s", prefix, str);
}


/**
 *
 */
int64_t
showtime_get_ts(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/**
 *
 */
int64_t
arch_cache_avail_bytes(void)
{
  struct statfs buf;

  if(gconf.cache_path == NULL || statfs(gconf.cache_path, &buf))
    return 0;

  return buf.f_bfree * buf.f_bsize;
}

/**
 *
 */
uint64_t
arch_get_seed(void)
{
  uint64_t v = getpid();
  v = (v << 16) ^ getppid();
  v = (v << 32) ^ time(NULL);
  return v;
}


#include <sys/mman.h>
#include "arch/halloc.h"

/**
 *
 */
void *
halloc(size_t size)
{
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(p == MAP_FAILED)
    return NULL;
  return p;
}

/**
 *
 */
void
hfree(void *ptr, size_t size)
{
  munmap(ptr, size);
}


void
my_localtime(const time_t *now, struct tm *tm)
{
  localtime_r(now, tm);
}


int
arch_pipe(int pipefd[2])
{
  return pipe(pipefd);
}

void *
mymalloc(size_t size)
{
  return malloc(size);
}

void *
myrealloc(void *ptr, size_t size)
{
  return realloc(ptr, size);
}

void *
mycalloc(size_t count, size_t size)
{
  return calloc(count, size);
}

void *
mymemalign(size_t align, size_t size)
{
  return memalign(align, size);
}

const char *
showtime_get_system_type(void)
{
  return "android";
}

void
arch_exit(void)
{
  exit(0);
}

int
arch_stop_req(void)
{
  return 0;
}

/**
 *
 */
int64_t
showtime_get_avtime(void)
{
  return showtime_get_ts();
}


jint
JNI_OnLoad(JavaVM *vm, void *reserved)
{
  JVM = vm;
  return JNI_VERSION_1_6;
}


/**
 *
 */
JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_coreInit(JNIEnv *env, jobject obj, jstring j_settings, jstring j_cachedir)
{
  gconf.trace_level = TRACE_DEBUG;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

  const char *settings = (*env)->GetStringUTFChars(env, j_settings, 0);
  const char *cachedir = (*env)->GetStringUTFChars(env, j_cachedir, 0);

  gconf.persistent_path = strdup(settings);
  gconf.cache_path      = strdup(cachedir);

  (*env)->ReleaseStringUTFChars(env, j_settings, settings);
  (*env)->ReleaseStringUTFChars(env, j_cachedir, cachedir);

  gconf.concurrency =   sysconf(_SC_NPROCESSORS_CONF);

  setlocale(LC_ALL, "");

  signal(SIGPIPE, SIG_IGN);

  net_initialize();

  showtime_init();

  jclass c = (*env)->FindClass(env, "com/showtimemediacenter/showtime/STCore");
  STCore = (*env)->NewGlobalRef(env, c);

  prop_jni_init(env);

  service_create("music", "Music", "file:///sdcard/Music",
                 "music", NULL, 0, 1, SVC_ORIGIN_SYSTEM);

  service_create("music", "Movies", "file:///sdcard/Movies",
                 "video", NULL, 0, 1, SVC_ORIGIN_SYSTEM);

  android_nav = nav_spawn();
}


/**
 *
 */
JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_pollCourier(JNIEnv *env, jobject obj)
{
  prop_jni_poll();
}
