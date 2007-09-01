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
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "app.h"

#include "gl/gl_video.h"
#include "gl/sysglue/sysglue.h"
#include "hid/hid.h"
#include "audio/audio.h"

pthread_mutex_t ffmutex = PTHREAD_MUTEX_INITIALIZER;

int frame_duration;

int64_t wallclock;
time_t walltime;
int has_analogue_pad;

void layout_std_create(void);

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

  setenv("__GL_SYNC_TO_VBLANK", "1", 1); // make nvidia sync to vblank

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

  gvp_init();

  layout_std_create();

  app_init();

  APP_REGISTER(app_iptv);
  APP_REGISTER(app_cd);
  APP_REGISTER(app_fb);
  APP_REGISTER(app_radio);
  APP_REGISTER(app_pl);
  APP_REGISTER(app_pvr);
  APP_REGISTER(app_rss);

#if 0
  APP_REGISTER(app_trailers);
#endif
  inputhandler_register(200, main_input_event);

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

    case INPUT_KEY_POWER:
      showtime_exit(1);
      return 1;
    }
    break;
  }
  return 0;
}
