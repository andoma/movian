#pragma once

#include <jni.h>


typedef struct android_glw_root {
  glw_root_t gr;

  jobject agr_vrp;

  int agr_running;
  hts_cond_t agr_runcond;

} android_glw_root_t;


