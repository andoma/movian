/*
 *  Copyright (C) 2007-2018 Lonelycoder AB
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
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include <limits.h>
#include <syslog.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <signal.h>
#include <android/log.h>
#include <jni.h>
#include <sys/system_properties.h>
#include <GLES2/gl2.h>
#include <sys/mman.h>

#include "arch/arch.h"
#include "main.h"
#include "service.h"
#include "networking/net.h"
#include "ui/glw/glw.h"
#include "android.h"
#include "navigator.h"
#include "arch/halloc.h"
#include "misc/md5.h"
#include "misc/str.h"

static char android_manufacturer[PROP_VALUE_MAX];
static char android_model[PROP_VALUE_MAX];
static char android_name[PROP_VALUE_MAX];
static char android_version[PROP_VALUE_MAX];
static char android_serialno[PROP_VALUE_MAX];

char android_intent[PATH_MAX];

JavaVM *JVM;
jclass STCore;
prop_t *android_nav;
int android_sdk;

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
  __android_log_print(prio, APPNAMEUSER, "%s %s", prefix, str);
}


/**
 *
 */
int64_t
arch_get_ts(void)
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
size_t
arch_malloc_size(void *ptr)
{
  return 0;
}


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
arch_get_system_type(void)
{
  return "Android";
}

void
arch_exit(void)
{
  exit(0);
}

int
arch_stop_req(void)
{
  return ARCH_STOP_CALLER_MUST_HANDLE;
}

void
arch_localtime(const time_t *now, struct tm *tm)
{
  localtime_r(now, tm);
}

static int devurandom;

void
arch_get_random_bytes(void *ptr, size_t size)
{
  ssize_t r = read(devurandom, ptr, size);
  if(r != size)
    abort();
}

INITIALIZER(opendevurandom) {
  devurandom = open("/dev/urandom", O_RDONLY);
};


/**
 *
 */
int64_t
arch_get_avtime(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000000000LL + ts.tv_nsec) / 1000LL;
}


jint
JNI_OnLoad(JavaVM *vm, void *reserved)
{
  JVM = vm;
  char prop[PROP_VALUE_MAX];

  __system_property_get("ro.product.manufacturer",  android_manufacturer);
  __system_property_get("ro.product.model",         android_model);
  __system_property_get("ro.product.name",          android_name);
  __system_property_get("ro.build.version.release", android_version);
  __system_property_get("ro.build.version.sdk",     prop);
  android_sdk = atoi(prop);
  __system_property_get("ro.serialno",              android_serialno);

  snprintf(gconf.os_info, sizeof(gconf.os_info), "%s", android_version);

  snprintf(gconf.device_type, sizeof(gconf.device_type),
           "%s %s", android_manufacturer, android_model);

  return JNI_VERSION_1_6;
}

#if 0

static inline int ishex(int x)
{
	return	(x >= '0' && x <= '9')	||
		(x >= 'a' && x <= 'f')	||
		(x >= 'A' && x <= 'F');
}

static int decode(const char *s, char *dec)
{
	char *o;
	const char *end = s + strlen(s);
	int c;

	for (o = dec; s <= end; o++) {
		c = *s++;
		if (c == '+') c = ' ';
		else if (c == '%' && (	!ishex(*s++)	||
					!ishex(*s++)	||
					!sscanf(s - 2, "%2x", &c)))
			return -1;

		if (dec) *o = c;
	}

	return o - dec;
}

JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_coreIntent(JNIEnv *env, jobject obj, jstring j_intent)
{
   // strcpy(android_intent, (*env)->GetStringUTFChars(env, j_intent, 0));
    decode((*env)->GetStringUTFChars(env, j_intent, 0), android_intent);
    if(strstr(android_intent, "magnet:?"))
	strcat(android_intent, "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce&tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A80&tr=udp%3A%2F%2Ftracker.leechers-paradise.org%3A6969%2Fannounce&tr=udp%3A%2F%2Ftracker.coppersurfer.tk%3A6969%2Fannounce&tr=udp%3A%2F%2Fopen.demonii.com%3A1337&tr=udp%3A%2F%2F90.180.35.128%3A6969%2Fannonce&tr=udp%3A%2F%2F9.rarbg.to%3A2710%2Fannounce&tr=udp%3A%2F%2F9.rarbg.me%3A2710%2Fannounce&tr=udp%3A%2F%2F9.rarbg.com%3A2710%2Fannounce&tr=http%3A%2F%2Ftracker.tfile.me%2Fannounce&tr=http%3A%2F%2Fmgtracker.org%3A2710%2Fannounce&tr=http%3A%2F%2Fexplodie.org%3A6969%2Fannounce&tr=http%3A%2F%2F90.180.35.128%3A6969%2Fannonce");
}
#endif



/**
 *
 */
JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_coreInit(JNIEnv *env, jobject obj, jstring j_settings, jstring j_cachedir, jstring j_sdcard, jstring j_android_id, jint time_24hrs, jstring j_music, jstring j_pictures, jstring j_movies, jint audio_sample_rate, jint audio_frames_per_buffer)
{
  static int initialized;
  if(initialized)
    return;
  initialized = 1;

  jclass c = (*env)->FindClass(env, "com/lonelycoder/mediaplayer/Core");
  STCore = (*env)->NewGlobalRef(env, c);


  char initmsg[128];
  snprintf(initmsg, sizeof(initmsg), "Native core init pid %d SDK:%d",
           getpid(), android_sdk);
  trace_arch(TRACE_INFO, "Core", initmsg);

  gconf.trace_level = TRACE_DEBUG;
  gconf.time_format_system = time_24hrs ? TIME_FORMAT_24 : TIME_FORMAT_12;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

  const char *settings   = (*env)->GetStringUTFChars(env, j_settings, 0);
  const char *cachedir   = (*env)->GetStringUTFChars(env, j_cachedir, 0);
  const char *sdcard     = (*env)->GetStringUTFChars(env, j_sdcard, 0);
  const char *android_id = (*env)->GetStringUTFChars(env, j_android_id, 0);

  extern char *android_fs_settings_path;
  extern char *android_fs_cache_path;
  extern char *android_fs_sdcard_path;

  android_fs_settings_path = strdup(settings);
  android_fs_cache_path    = strdup(cachedir);
  android_fs_sdcard_path   = strdup(sdcard);
  gconf.persistent_path    = strdup("persistent://");
  gconf.cache_path         = strdup("cache://");

  uint8_t digest[16];

  md5_decl(ctx);
  md5_init(ctx);

  md5_update(ctx, (const void *)android_id, strlen(android_id));
  md5_update(ctx, (const void *)android_serialno, strlen(android_serialno));

  md5_final(ctx, digest);
  bin2hex(gconf.device_id, sizeof(gconf.device_id), digest, sizeof(digest));


  (*env)->ReleaseStringUTFChars(env, j_settings, settings);
  (*env)->ReleaseStringUTFChars(env, j_cachedir, cachedir);
  (*env)->ReleaseStringUTFChars(env, j_android_id, android_id);
  (*env)->ReleaseStringUTFChars(env, j_sdcard, sdcard);

  gconf.concurrency =   sysconf(_SC_NPROCESSORS_CONF);

  setlocale(LC_ALL, "");

  signal(SIGPIPE, SIG_IGN);

  main_init();

  service_createp("androidstorage", _p("Android Storage"),
                  "es://", "storage", NULL, 0, 1, SVC_ORIGIN_SYSTEM);


  android_nav = nav_spawn();

  extern int android_system_audio_sample_rate;
  extern int android_system_audio_frames_per_buffer;

  android_system_audio_sample_rate = audio_sample_rate;
  android_system_audio_frames_per_buffer = audio_frames_per_buffer;
}


JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_openUri(JNIEnv *env, jobject obj, jstring j_uri)
{
  const char *uri = (*env)->GetStringUTFChars(env, j_uri, 0);

  event_t *e = event_create_openurl(.url  = uri);
  prop_send_ext_event(prop_create(android_nav, "eventSink"), e);
  event_release(e);

  (*env)->ReleaseStringUTFChars(env, j_uri, uri);
}


/**
 *
 */
JNIEXPORT void JNICALL
Java_com_lonelycoder_mediaplayer_Core_networkStatusChanged(JNIEnv *env, jobject obj)
{
  extern void asyncio_trig_network_change(void);
  asyncio_trig_network_change();
}
