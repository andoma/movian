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

#include <assert.h>

#include <jni.h>

#include "showtime.h"
#include "arch/threads.h"
#include "prop/prop.h"
#include "misc/redblack.h"
#include "misc/dbl.h"
#include "arch/android/android.h"
#include "prop_jni.h"

extern JavaVM *JVM;

RB_HEAD(jni_subscription_tree, jni_subscription);
RB_HEAD(jni_prop_tree, jni_prop);
LIST_HEAD(jni_prop_list, jni_prop);

static struct jni_subscription_tree jni_subscriptions;
//static struct jni_prop_tree jni_props;
//static int jni_prop_tally;
static int jni_sub_tally;
static prop_courier_t *jni_courier;


typedef struct jni_subscription {

  RB_ENTRY(jni_subscription) js_link;
  unsigned int js_id;
  prop_sub_t *js_sub;
  struct jni_prop_list js_props; // Exported props
  jobject js_cbif;


#define SUB_METHODS 4
  jmethodID js_methods[SUB_METHODS];

  // Value subscription
#define js_setstr     js_methods[0]
#define js_setint     js_methods[1]
#define js_setfloat   js_methods[2]
#define js_setvoid    js_methods[3]

  // Node subscription
#define js_mknode     js_methods[0]
#define js_addnodes   js_methods[1]
#define js_delnodes   js_methods[2]
#define js_movenode   js_methods[3]

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
 *
 */
static void
jni_sub_node_cb(void *opaque, prop_event_t event, ...)
{
}


/**
 *
 */
static void
jni_sub_value_cb(void *opaque, prop_event_t event, ...)
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
  double dbl;
  int i32;

  va_start(ap, event);

  switch(event) {
  case PROP_SET_CSTRING:
    str = va_arg(ap, const char *);
    if(0)
  case PROP_SET_RSTRING:
      str = rstr_get(va_arg(ap, rstr_t *));

    if(js->js_setstr) {
      jstring jstr = (*env)->NewStringUTF(env, str);
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setstr, jstr);
    } else if(js->js_setfloat) {
      dbl = my_str2double(str, NULL);
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setfloat, dbl);
    } else if(js->js_setint) {
      i32 = atoi(str);
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setint, i32);
    } else {
      TRACE(TRACE_DEBUG, "JNI_PROP", "Appropriate set() not found for string");
    }
    break;

  case PROP_SET_FLOAT:
    dbl = va_arg(ap, double);
    if(js->js_setfloat) {
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setfloat, dbl);
    } else if(js->js_setint) {
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setint, (int)dbl);
    } else if(js->js_setstr) {
      char buf[32];
      my_double2str(buf, sizeof(buf), dbl);
      jstring jstr = (*env)->NewStringUTF(env, buf);
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setstr, jstr);
    } else {
      TRACE(TRACE_DEBUG, "JNI_PROP", "Appropriate set() not found for float");
    }
    break;

  case PROP_SET_INT:
    i32 = va_arg(ap, int);

    if(js->js_setint) {
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setint, i32);
    } else if(js->js_setfloat) {
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setfloat, (float)i32);
    } else if(js->js_setstr) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d", i32);
      jstring jstr = (*env)->NewStringUTF(env, buf);
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setstr, jstr);
    } else {
      TRACE(TRACE_DEBUG, "JNI_PROP", "Appropriate set() not found for int");
    }
    break;

  case PROP_SET_DIR:
  case PROP_SET_VOID:
    if(js->js_setvoid) {
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setvoid);
    } if(js->js_setint) {
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setint, 0);
    } else if(js->js_setfloat) {
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setfloat, 0);
    } else if(js->js_setstr) {
      jstring jstr = (*env)->NewStringUTF(env, "");
      (*env)->CallVoidMethod(env, js->js_cbif, js->js_setstr, jstr);
    } else {
      TRACE(TRACE_DEBUG, "JNI_PROP", "Appropriate set() not found for void");
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

#if 0
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

#endif



static jni_subscription_t *
makesub(JNIEnv *env,
        prop_callback_t *cb,
        jint j_propid,
        jstring j_path,
        jobject j_cbif)
{
  jni_subscription_t *js = malloc(sizeof(jni_subscription_t));
  const char *path = (*env)->GetStringUTFChars(env, j_path, 0);

  js->js_cbif = (*env)->NewGlobalRef(env, j_cbif);

  prop_t *p = j_propid ? (prop_t *)j_propid : prop_get_global();

  js->js_id = ++jni_sub_tally;

  if(RB_INSERT_SORTED(&jni_subscriptions, js, js_link, js_cmp))
    abort();

  js->js_sub = prop_subscribe(PROP_SUB_ALT_PATH,
			      PROP_TAG_NAMESTR, path,
			      PROP_TAG_CALLBACK, cb, js,
			      PROP_TAG_ROOT, p,
                              PROP_TAG_COURIER, jni_courier,
			      NULL);

  (*env)->ReleaseStringUTFChars(env, j_path, path);
  return js;
}







JNIEXPORT jint JNICALL
Java_com_showtimemediacenter_showtime_STCore_subValue(JNIEnv *, jobject , jint, jstring, jobject);

JNIEXPORT jint JNICALL
Java_com_showtimemediacenter_showtime_STCore_subValue(JNIEnv *env,
                                                       jobject obj,
                                                       jint j_propid,
                                                       jstring j_path,
                                                       jobject j_cbif)
{
  jni_subscription_t *js;
  js = makesub(env, jni_sub_value_cb, j_propid, j_path, j_cbif);

  jclass c = (*env)->GetObjectClass(env, js->js_cbif);

  js->js_setstr   = (*env)->GetMethodID(env, c, "set", "(Ljava/lang/String;)V");
  (*env)->ExceptionClear(env);
  js->js_setint   = (*env)->GetMethodID(env, c, "set", "(I)V");
  (*env)->ExceptionClear(env);
  js->js_setfloat = (*env)->GetMethodID(env, c, "set", "(F)V");
  (*env)->ExceptionClear(env);
  js->js_setvoid  = (*env)->GetMethodID(env, c, "set", "()V");
  (*env)->ExceptionClear(env);

  return js->js_id;
}


JNIEXPORT jint JNICALL
Java_com_showtimemediacenter_showtime_STCore_subNodes(JNIEnv *, jobject , jint, jstring, jobject);


JNIEXPORT jint JNICALL
Java_com_showtimemediacenter_showtime_STCore_subNodes(JNIEnv *env,
                                                      jobject obj,
                                                      jint j_propid,
                                                      jstring j_path,
                                                      jobject j_cbif)
{
  jni_subscription_t *js;
  js = makesub(env, jni_sub_node_cb, j_propid, j_path, j_cbif);

  jclass c = (*env)->GetObjectClass(env, js->js_cbif);

  js->js_addnodes = (*env)->GetMethodID(env, c, "addNodes", "([II)V");
  (*env)->ExceptionClear(env);
  js->js_setfloat = (*env)->GetMethodID(env, c, "delNodes", "([I)V");
  (*env)->ExceptionClear(env);
  js->js_setvoid  = (*env)->GetMethodID(env, c, "moveNode", "(II)V");
  (*env)->ExceptionClear(env);
  return js->js_id;
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_unsub(JNIEnv *env, jobject obj, jint j_sid);

JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_unsub(JNIEnv *env, jobject obj, jint j_sid)
{
  jni_subscription_t *js = jni_sub_find(j_sid);

  if(js == NULL)
    return;

  //    js_clear_props(js);
  prop_unsubscribe(js->js_sub);
  RB_REMOVE(&jni_subscriptions, js, js_link);
  if(js->js_cbif)
    (*env)->DeleteGlobalRef(env, js->js_cbif);

  for(int i = 0; i < SUB_METHODS; i++)
    if(js->js_methods[i])
      (*env)->DeleteGlobalRef(env, js->js_methods[i]);

  free(js);
}

JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_propRelease(JNIEnv *env, jobject obj, jint j_prop);

JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_propRelease(JNIEnv *env, jobject obj, jint j_prop)
{
  prop_ref_dec((prop_t *)j_prop);
}


JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_propRetain(JNIEnv *env, jobject obj, jint j_prop);

JNIEXPORT void JNICALL
Java_com_showtimemediacenter_showtime_STCore_propRetain(JNIEnv *env, jobject obj, jint j_prop)
{
  prop_t *p = prop_ref_inc((prop_t *)j_prop);
  (void)p;
}



static jclass notify_class;
static jmethodID notify_mid;

/**
 *
 */
static void
jni_prop_wakeup(void *opaque)
{
  JNIEnv *env;
  int r = (*JVM)->GetEnv(JVM, (void **)&env, JNI_VERSION_1_6);

  if(r)
    return;
  (*env)->CallStaticVoidMethod(env, notify_class, notify_mid);

}


/**
 *
 */
void
prop_jni_init(JNIEnv *env)
{
  jni_courier = prop_courier_create_notify(jni_prop_wakeup, NULL);
  notify_mid= (*env)->GetStaticMethodID(env, STCore,
                                        "wakeupMainDispatcher", "()V");
  assert(notify_mid != 0);
}


/**
 *
 */
void
prop_jni_poll(void)
{
  prop_courier_poll(jni_courier);
}
