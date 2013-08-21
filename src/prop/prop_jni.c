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
#include <jni.h>

#include "showtime.h"
#include "arch/threads.h"
#include "prop/prop.h"
#include "misc/redblack.h"
#include "misc/dbl.h"

#include "prop_jni.h"

extern JavaVM *JVM;

RB_HEAD(jni_subscription_tree, jni_subscription);
RB_HEAD(jni_prop_tree, jni_prop);
LIST_HEAD(jni_prop_list, jni_prop);

static struct jni_subscription_tree jni_subscriptions;
static struct jni_prop_tree jni_props;
//static int jni_prop_tally;
static int jni_sub_tally;
static hts_mutex_t jni_prop_mutex;
static prop_courier_t *jni_courier;

typedef struct jni_subscription {

  RB_ENTRY(jni_subscription) js_link;
  unsigned int js_id;
  prop_sub_t *js_sub;
  struct jni_prop_list js_props; // Exported props
  jobject js_cbif;
} jni_subscription_t;


/**
 *
 */
static int
js_cmp(const jni_subscription_t *a, const jni_subscription_t *b)
{
  return a->js_id - b->js_id;
}

/**
 * An exported property
 */
typedef struct jni_prop {
  RB_ENTRY(jni_prop) jp_link;
  unsigned int jp_id;
  prop_t *jp_prop;
  jni_subscription_t *jp_sub;
  LIST_ENTRY(jni_prop) jp_sub_link;
} jni_prop_t;


#if 0
/**
 *
 */
static int
jp_cmp(const jni_prop_t *a, const jni_prop_t *b)
{
  return a->jp_id - b->jp_id;
}
#endif


/**
 *
 */
static void
jni_sub_cb(void *opaque, prop_event_t event, ...)
{
  JNIEnv *env;
  int r = (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  if(r) {
    TRACE(TRACE_DEBUG, "JNI_PROP", "No environment");
    return;
  }

  const char *str;
  va_list ap;

  jni_subscription_t *js = opaque;
  jmethodID mid;
  double dbl;
  int i32;

  jclass c = (*env)->GetObjectClass(env, js->js_cbif);

  va_start(ap, event);

  switch(event) {
  case PROP_SET_CSTRING:
    str = va_arg(ap, const char *);
    if(0)
  case PROP_SET_RSTRING:
      str = rstr_get(va_arg(ap, rstr_t *));

    if((mid = (*env)->GetMethodID(env, c, "set", "(Ljava/lang/String;)V"))) {
      jstring jstr = (*env)->NewStringUTF(env, str);
      (*env)->CallVoidMethod(env, js->js_cbif, mid, jstr);
    } else {
      (*env)->ExceptionClear(env);
      if((mid = (*env)->GetMethodID(env, c, "set", "(F)V"))) {
        dbl = my_str2double(str, NULL);
        (*env)->CallVoidMethod(env, js->js_cbif, mid, dbl);
      } else {
        (*env)->ExceptionClear(env);
        if((mid = (*env)->GetMethodID(env, c, "set", "(I)V"))) {
          i32 = atoi(str);
          (*env)->CallVoidMethod(env, js->js_cbif, mid, i32);
        } else {
          TRACE(TRACE_DEBUG, "JNI_PROP", "Appropriate set() not found for string");
        }
      }
    }
    break;

  case PROP_SET_FLOAT:
    dbl = va_arg(ap, double);
    if((mid = (*env)->GetMethodID(env, c, "set", "(F)V"))) {
      (*env)->CallVoidMethod(env, js->js_cbif, mid, dbl);
    } else {
      (*env)->ExceptionClear(env);
      if((mid = (*env)->GetMethodID(env, c, "set", "(I)V"))) {
        (*env)->CallVoidMethod(env, js->js_cbif, mid, (int)dbl);
      } else {
        (*env)->ExceptionClear(env);
        if((mid = (*env)->GetMethodID(env, c, "set", "(Ljava/lang/String;)V"))) {
          char buf[32];
          my_double2str(buf, sizeof(buf), dbl);
          jstring jstr = (*env)->NewStringUTF(env, buf);
          (*env)->CallVoidMethod(env, js->js_cbif, mid, jstr);
        } else {
          TRACE(TRACE_DEBUG, "JNI_PROP", "Appropriate set() not found for float");
        }
      }
    }
    break;

  case PROP_SET_INT:
    i32 = va_arg(ap, int);

    if((mid = (*env)->GetMethodID(env, c, "set", "(I)V"))) {
      (*env)->CallVoidMethod(env, js->js_cbif, mid, i32);
    } else {
      (*env)->ExceptionClear(env);
      if((mid = (*env)->GetMethodID(env, c, "set", "(F)V"))) {
        (*env)->CallVoidMethod(env, js->js_cbif, mid, (float)i32);
      } else {
        (*env)->ExceptionClear(env);
        if((mid = (*env)->GetMethodID(env, c, "set", "(Ljava/lang/String;)V"))) {
          char buf[32];
          snprintf(buf, sizeof(buf), "%d", i32);
          jstring jstr = (*env)->NewStringUTF(env, buf);
          (*env)->CallVoidMethod(env, js->js_cbif, mid, jstr);
        } else {
          TRACE(TRACE_DEBUG, "JNI_PROP", "Appropriate set() not found for int");
        }
      }
    }
    break;


  default:
    TRACE(TRACE_DEBUG, "JNI_PROP", "Not hanlding event %d", event);
    break;
  }

  if((*env)->ExceptionCheck(env)) {
    TRACE(TRACE_DEBUG, "JNI_PROP",
          "Exception raised during prop notify, ignoring");
    (*env)->ExceptionDescribe(env);
  }
  va_end(ap);
}


/**
 *
 */
static jni_subscription_t *
jni_sub_find(int id)
{
  jni_subscription_t skel;
  skel.js_id = id;
  return RB_FIND(&jni_subscriptions, &skel, js_link, js_cmp);
}


/**
 *
 */
static void
jni_property_unexport_from_sub(jni_subscription_t *js, jni_prop_t *jp)
{
  prop_ref_dec(jp->jp_prop);
  LIST_REMOVE(jp, jp_sub_link);
  RB_REMOVE(&jni_props, jp, jp_link);
  free(jp);
}


/**
 *
 */
static void
js_clear_props(jni_subscription_t *js)
{
  jni_prop_t *jp;
  while((jp = LIST_FIRST(&js->js_props)) != NULL) {
    prop_tag_clear(jp->jp_prop, js);
    jni_property_unexport_from_sub(js, jp);
  }
}



JNIEXPORT jint JNICALL
Java_com_showtimemediacenter_showtime_STCore_sub(JNIEnv *env,
                                                 jobject obj,
                                                 jint j_propid,
                                                 jstring j_path,
                                                 jobject j_cbif);

JNIEXPORT jint JNICALL
Java_com_showtimemediacenter_showtime_STCore_sub(JNIEnv *env,
                                                 jobject obj,
                                                 jint j_propid,
                                                 jstring j_path,
                                                 jobject j_cbif)
{
  jni_subscription_t *js = malloc(sizeof(jni_subscription_t));
  const char *path = (*env)->GetStringUTFChars(env, j_path, 0);

  TRACE(TRACE_DEBUG, "PROP_JNI", "Subscribe to %s", path);

  js->js_cbif = (*env)->NewGlobalRef(env, j_cbif);

  hts_mutex_lock(&jni_prop_mutex);

  prop_t *p = prop_get_global();

  js->js_id = ++jni_sub_tally;

  if(RB_INSERT_SORTED(&jni_subscriptions, js, js_link, js_cmp))
    abort();

  js->js_sub = prop_subscribe(PROP_SUB_ALT_PATH,
			      PROP_TAG_NAMESTR, path,
			      PROP_TAG_CALLBACK, jni_sub_cb, js,
			      PROP_TAG_ROOT, p,
                              PROP_TAG_COURIER, jni_courier,
			      NULL);

  hts_mutex_unlock(&jni_prop_mutex);

  (*env)->ReleaseStringUTFChars(env, j_path, path);
  return js->js_id;
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_unsub(JNIEnv *env,
                                                   jobject obj,
                                                   jint j_sid);

JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_unsub(JNIEnv *env,
                                                   jobject obj,
                                                   jint j_sid)
{
  hts_mutex_lock(&jni_prop_mutex);

  jni_subscription_t *js = jni_sub_find(j_sid);

  if(js != NULL) {
    js_clear_props(js);
    prop_unsubscribe(js->js_sub);
    RB_REMOVE(&jni_subscriptions, js, js_link);
    free(js);
  }

  hts_mutex_unlock(&jni_prop_mutex);
}


/**
 *
 */
void
prop_jni_init(void)
{
  hts_mutex_init(&jni_prop_mutex);
  jni_courier = prop_courier_create_thread(&jni_prop_mutex, "jniprop");
}

