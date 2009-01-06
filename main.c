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

#include "showtime.h"
#include "event.h"
#include "prop.h"
#include "arch/arch.h"

#include "audio/audio.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_imageloader.h"
#include "fileaccess/fa_rawloader.h"
#include "navigator.h"
#include "settings.h"
#include "ui/ui.h"
#include "ui/keymapper.h"
#include "playqueue.h"
#include "keyring.h"

hts_mutex_t ffmutex;

int64_t wallclock;
time_t walltime;
int concurrency;
extern char *htsversion;
static int stopcode;

const char *themepath = HTS_CONTENT_PATH "/showtime/themes/new";

static int main_event_handler(event_t *e, void *opaque);


prop_t *prop_sec;
prop_t *prop_hour;
prop_t *prop_minute;
prop_t *prop_weekday;
prop_t *prop_month;
prop_t *prop_date;
prop_t *prop_day;

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

    prop_set_int(prop_sec, tm.tm_sec);
    prop_set_int(prop_hour, tm.tm_hour);
    prop_set_int(prop_minute, tm.tm_min);
    prop_set_int(prop_day, tm.tm_mday);

    strftime(buf, sizeof(buf), "%A", &tm);
    prop_set_string(prop_weekday, buf);

    strftime(buf, sizeof(buf), "%B", &tm);
    prop_set_string(prop_month, buf);

    strftime(buf, sizeof(buf), "%x", &tm);
    prop_set_string(prop_date, buf);

    sleep(1);
  }
}



/**
 *
 */
static void
global_prop_init(void)
{
  prop_t *p;
  prop_t *cpu;
  hts_thread_t tid;


  p = prop_create(prop_get_global(), "version");
  prop_set_string(p, htsversion);

  cpu = prop_create(prop_get_global(), "cpu");
  p = prop_create(cpu, "cores");
  prop_set_float(p, concurrency);


  /* */
  p = prop_create(prop_get_global(), "clock");

  prop_sec = prop_create(p, "sec");
  prop_hour = prop_create(p, "hour");
  prop_minute = prop_create(p, "minute");
  prop_weekday = prop_create(p, "weekday");
  prop_month = prop_create(p, "month");
  prop_date = prop_create(p, "date");
  prop_day = prop_create(p, "day");

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


  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

  while((c = getopt(argc, argv, "s:t:")) != -1) {
    switch(c) {
    case 's':
      settingspath = optarg;
      break;
    case 't':
      themepath = optarg;
      break;
    }
  }

  prop_init();

  arch_init();

  global_prop_init();

  hts_settings_init("showtime", settingspath);

  keyring_init();

  settings_init();

  event_init();

  hts_mutex_init(&ffmutex);
  av_log_set_level(AV_LOG_ERROR);
  av_register_all();

  fileaccess_init();
  playqueue_init();

  nav_init();

  audio_init();

  event_handler_register("main", main_event_handler, EVENTPRI_MAIN, NULL);

  ui_init();

  if(optind < argc)
    nav_open(argv[optind]);
  else
    nav_open("page://mainmenu");

  ui_main_loop();

  return stopcode;
}

/**
 * Catch buttons used for exiting
 */
static int
main_event_handler(event_t *e, void *opaque)
{
  switch(e->e_type) {
  default:
    return 0;

  case EVENT_CLOSE:
    stopcode = 0;
    break;

  case EVENT_QUIT:
    stopcode = 0;
    break;

  case EVENT_POWER:
    stopcode = 10;
    break;
  }

  ui_exit_showtime();
  return 1;
}
