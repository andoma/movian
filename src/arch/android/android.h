#include <jni.h>

struct prop;

extern JavaVM *JVM;
extern jclass STCore;
extern struct prop *android_nav; // We only have one navigator at all times on android

void android_bitmap_destroy(JNIEnv *env, jobject bitmap);

jobject android_bitmap_create(JNIEnv *env, int width, int height);
