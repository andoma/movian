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

#include <sched.h>
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
#include "hid/hid.h"
#include "audio/audio.h"
#include "layout/layout.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_imageloader.h"
#include "fileaccess/fa_rawloader.h"

#ifdef HTS_HAVE_PTHREAD
#include <pthread.h>
#endif


pthread_mutex_t ffmutex;

int frame_duration;

int64_t wallclock;
time_t walltime;
int has_analogue_pad;
int concurrency;

static int main_event_handler(glw_event_t *ge);


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
#ifdef HTS_HAVE_PTHREAD
  cpu_set_t mask;
  int i, r = 0;

  memset(&mask, 0, sizeof(mask));
  sched_getaffinity(0, sizeof(mask), &mask);
  for(i = 0; i < CPU_SETSIZE; i++)
    if(CPU_ISSET(i, &mask))
      r++;

  return r?:1;
#else
  return 1;
#endif
}

/*
 *
 */
int
main(int argc, char **argv)
{
  int c;
  struct timeval tv;
  char *cfgfile = NULL;
  struct rlimit rlim;

  setenv("__GL_SYNC_TO_VBLANK", "1", 1); // make nvidia sync to vblan

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

  while((c = getopt(argc, argv, "c:")) != -1) {
    switch(c) {
    case 'c':
      cfgfile = optarg;
      break;
    }
  }

  concurrency = get_concurrency();

  hts_settings_init("showtime");

  event_init();

  hid_init();

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

  event_handler_register(300, main_event_handler);

  apps_load();

  gl_sysglue_mainloop();
  return 0;
}

/*
 *
 */

void
showtime_exit(int suspend)
{
  if(suspend == -1)
    _exit(0);

  _exit(suspend ? 10 : 11);
}


static int
main_event_handler(glw_event_t *ge)
{
  switch(ge->ge_type) {
  default:
    break;

  case EVENT_KEY_QUIT:
    showtime_exit(-1);
    return 1;

  case EVENT_KEY_POWER:
    showtime_exit(1);
    return 1;
  }
  return 0;
}
