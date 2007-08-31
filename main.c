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
#include "gl/gl_input.h"
#include "hid/hid.h"
#include "audio/audio.h"

pthread_mutex_t ffmutex = PTHREAD_MUTEX_INITIALIZER;

static float root_aspect;

static void render_scene(void);

int64_t wallclock;
time_t walltime;

glw_t *wroot;

int showtime_fps;

int has_analogue_pad;

float framerate_measured;

void layout_std_create(void);
void layout_std_draw(void);

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

static int
check_gl_ext(const uint8_t *s, const char *func, int fail)
{
  int l = strlen(func);
  int found;
  char *v;

  v = strstr((const char *)s, func);
  found = v != NULL && v[l] < 33;
  
  fprintf(stderr, "Checking OpenGL extension \"%s\" : %svailable", func,
	  found ? "A" : "Not a");

  if(!found && fail) {
    fprintf(stderr, ", but is required, exiting\n");
    exit(1);
  }
  fprintf(stderr, "\n");
  return found ? 0 : -1;
}



/*
 *
 */

static void
setup_gl(void)
{
  const char *fullscreen = config_get_str("fullscreen", NULL);
  char *x;
  const	GLubyte	*s;

  glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);


  if(fullscreen == NULL) {
    glutInitWindowPosition(100,100);
    glutInitWindowSize(1280, 720);
    root_aspect = (float)1280 / (float)720;
    showtime_fps = 60;

    glutCreateWindow("Showtime Mediacenter");
  } else {

    glutGameModeString(fullscreen);

    x = strchr(fullscreen, '@');
    if(x != NULL)
      showtime_fps = atoi(x + 1);
    else
      showtime_fps = 60;
      
    root_aspect = (float)1280 / (float)720;

    if (glutGameModeGet(GLUT_GAME_MODE_POSSIBLE)) {
      glutEnterGameMode();
    } else {
      printf("The select mode is not available\n");
      exit(1);	
    }
  }
 
  //  printf("System FPS: %d\n", showtime_fps);

  fprintf(stderr, "OpenGL library: %s on %s, version %s\n",
	  glGetString(GL_VENDOR),
	  glGetString(GL_RENDERER),
	  glGetString(GL_VERSION));

  s = glGetString(GL_EXTENSIONS);

  check_gl_ext(s, "GL_ARB_pixel_buffer_object",      1);
  check_gl_ext(s, "GL_ARB_vertex_buffer_object",     1);
  check_gl_ext(s, "GL_ARB_fragment_program",         1);
  check_gl_ext(s, "GL_ARB_texture_non_power_of_two", 1);

  gl_input_setup();

  glutSetCursor(GLUT_CURSOR_NONE);
  glDisable(GL_CULL_FACE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glShadeModel(GL_SMOOTH);
  glEnable(GL_LINE_SMOOTH);

  glEnable(GL_POLYGON_OFFSET_FILL);
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



  glutInit(&argc, argv);

  if(glw_init(&config_list, ffmpeglockmgr)) {
    fprintf(stderr, "libglw user interface failed to initialize, exiting\n");
    exit(0);
  }

  audio_init();
  
  setup_gl();

  gvp_init();

  glutDisplayFunc(render_scene);
  glutIdleFunc(render_scene);

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

  glutMainLoop();
  return 0;
}

/*
 *
 */

int do_shut_down;

static int64_t last_frame_time;

static void 
render_scene(void)
{
  int64_t frame_time;
  static float frame_rate;
  struct timeval tv;

  gettimeofday(&tv, NULL);

  wallclock = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
  walltime = tv.tv_sec;

  if(last_frame_time != 0) {
    frame_time = wallclock - last_frame_time;

    if(frame_rate == 0) {
      frame_rate = 1e6 / (float)frame_time;
    } else {
      frame_rate = (frame_rate * 7.0f + (1e6 / (float)frame_time)) / 8.0f;
    }
  }
  last_frame_time = wallclock;

  framerate_measured = frame_rate;

  layout_std_draw();

  glutSwapBuffers();
  glw_reaper();
}


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
