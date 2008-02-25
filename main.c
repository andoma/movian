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
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <libavformat/avformat.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "app.h"

#include "gl/video_decoder.h"
#include "gl/sysglue/sysglue.h"
#include "hid/hid.h"
#include "audio/audio.h"
#include "layout/layout.h"

pthread_mutex_t ffmutex = PTHREAD_MUTEX_INITIALIZER;

int frame_duration;

int64_t wallclock;
time_t walltime;
int has_analogue_pad;

static int main_input_event(inputevent_t *ie);


static void
ffmpeglockmgr(int lock)
{
  if(lock)
    fflock();
  else
    ffunlock();
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

  setenv("__GL_SYNC_TO_VBLANK", "1", 1); // make nvidia sync to vblank


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


  config_open_by_prgname("showtime", cfgfile);

  hid_init();

  //  av_log_set_level(AV_LOG_DEBUG);
  av_register_all();

  gl_sysglue_init(argc, argv);

  if(glw_init(&config_list, ffmpeglockmgr)) {
    fprintf(stderr, "libglw user interface failed to initialize, exiting\n");
    exit(0);
  }

  audio_init();

  vd_init();

  layout_create();

  inputhandler_register(300, main_input_event);

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
  settings_write();

  if(suspend == -1)
    _exit(0);

  _exit(suspend ? 10 : 11);
}


static int
main_input_event(inputevent_t *ie)
{
  switch(ie->type) {
  default:
    break;

  case INPUT_KEY:
    switch(ie->u.key) {
    default:
      break;

    case INPUT_KEY_QUIT:
      showtime_exit(-1);
      return 1;

    case INPUT_KEY_POWER:
      showtime_exit(1);
      return 1;
    }
    break;
  }
  return 0;
}
