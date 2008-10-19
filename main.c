/*
 *  Showtime mediacenter
 *  Copyright (C) 2007 Andreas Öman
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

#include "config.h"
#ifdef HTS_USE_PTHREADS
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#endif

#include <sys/time.h>
#include <sys/resource.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <libavformat/avformat.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "event.h"

#include "video/video_decoder.h"
#include "display/display.h"
#include "audio/audio.h"
#include "layout/layout.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_imageloader.h"
#include "fileaccess/fa_rawloader.h"

pthread_mutex_t ffmutex;

int frame_duration;

int64_t wallclock;
time_t walltime;
int concurrency;
extern char *htsversion;
glw_prop_t *prop_global;
glw_prop_t *prop_ui_scale;
int showtime_running;
static int stopcode;

static int main_event_handler(glw_event_t *ge, void *opaque);


static void
ffmpeglockmgr(int lock)
{
  if(lock)
    fflock();
  else
    ffunlock();
}


/**
 * Return the number of CPUs
 */
static int
get_concurrency(void)
{
#ifdef HTS_USE_PTHREADS
  cpu_set_t mask;
  int i, r = 0;

  memset(&mask, 0, sizeof(mask));
  sched_getaffinity(0, sizeof(mask), &mask);
  for(i = 0; i < CPU_SETSIZE; i++)
    if(CPU_ISSET(i, &mask))
      r++;

  fprintf(stderr, "%d CPUs detected\n", r);
  return r?:1;
#else
  return 1;
#endif
}

glw_prop_t *prop_hour;
glw_prop_t *prop_minute;
glw_prop_t *prop_weekday;
glw_prop_t *prop_month;
glw_prop_t *prop_date;
glw_prop_t *prop_day;

/**
 *
 */
static void *
propupdater(void *aux)
{
  char buf[30];

  time_t now;
  struct tm tm;

  while(1) {

    time(&now);

    localtime_r(&now, &tm);

    glw_prop_set_float(prop_hour, tm.tm_hour);
    glw_prop_set_float(prop_minute, tm.tm_min);
    glw_prop_set_float(prop_day, tm.tm_mday);

    strftime(buf, sizeof(buf), "%A", &tm);
    glw_prop_set_string(prop_weekday, buf);

    strftime(buf, sizeof(buf), "%B", &tm);
    glw_prop_set_string(prop_month, buf);

    strftime(buf, sizeof(buf), "%x", &tm);
    glw_prop_set_string(prop_date, buf);

    sleep(60);
  }
}

/**
 *
 */
static void
global_prop_init(void)
{
  glw_prop_t *p;
  glw_prop_t *cpu;
  hts_thread_t tid;

  prop_global = glw_prop_create(NULL, "global");

  prop_ui_scale = glw_prop_create(prop_global, "uiscale");

  p = glw_prop_create(prop_global, "version");
  glw_prop_set_string(p, htsversion);

  cpu = glw_prop_create(prop_global, "cpu");
  p = glw_prop_create(cpu, "cores");
  glw_prop_set_float(p, concurrency);


  /* */
  p = glw_prop_create(prop_global, "clock");

  prop_hour = glw_prop_create(p, "hour");
  prop_minute = glw_prop_create(p, "minute");
  prop_weekday = glw_prop_create(p, "weekday");
  prop_month = glw_prop_create(p, "month");
  prop_date = glw_prop_create(p, "date");
  prop_day = glw_prop_create(p, "day");

  hts_thread_create_detached(&tid, propupdater, NULL);

}


/*
 *
 */
int
main(int argc, char **argv)
{
  int c;
  struct timeval tv;
  const char *settingspath = NULL;
  struct rlimit rlim;

#ifdef RLIMIT_AS
  getrlimit(RLIMIT_AS, &rlim);
  rlim.rlim_cur = 512 * 1024 * 1024;
  setrlimit(RLIMIT_AS, &rlim);
#endif

#ifdef RLIMIT_DATA
  getrlimit(RLIMIT_DATA, &rlim);
  rlim.rlim_cur = 512 * 1024 * 1024;
  setrlimit(RLIMIT_DATA, &rlim);
#endif

  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

  while((c = getopt(argc, argv, "s:")) != -1) {
    switch(c) {
    case 's':
      settingspath = optarg;
      break;
    }
  }


  concurrency = get_concurrency();

  global_prop_init();

  hts_settings_init("showtime", settingspath);

  event_init();

  hts_mutex_init(&ffmutex);
  av_log_set_level(AV_LOG_ERROR);
  av_register_all();

  fileaccess_init();

  gl_sysglue_init(argc, argv);

  if(glw_init(ffmpeglockmgr, fa_imageloader, fa_rawloader, fa_rawunload,
	      concurrency)) {
    fprintf(stderr, "libglw user interface failed to initialize, exiting\n");
    exit(0);
  }

  audio_init();

  vd_init();

  layout_create();

  event_handler_register("main", main_event_handler, EVENTPRI_MAIN, NULL);

  apps_load();

  showtime_running = 1;

  gl_sysglue_mainloop();

  return stopcode;
}

/**
 * Catch buttons used for exiting
 */
static int
main_event_handler(glw_event_t *ge, void *opaque)
{
  switch(ge->ge_type) {
  default:
    return 0;

  case EVENT_KEY_QUIT:
    stopcode = 0;
    break;

  case EVENT_KEY_POWER:
    stopcode = 10;
    break;
  }
  showtime_running = 0;
  return 1;
}
