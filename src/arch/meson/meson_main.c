/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2012 Andreas Öman
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"
#include "arch/arch.h"
#include "arch/posix/posix.h"
#include "arch/linux/linux.h"

#include "EGL/egl.h"
#include "GLES2/gl2.h"
#include "ui/glw/glw.h"

static int ctrlc = 0;
static int running = 1;

static const EGLint ui_config_attribs[] = {
  EGL_SAMPLES,             EGL_DONT_CARE,
  EGL_RED_SIZE,            8,
  EGL_GREEN_SIZE,          8,
  EGL_BLUE_SIZE,           8,
  EGL_ALPHA_SIZE,          8,
  EGL_BUFFER_SIZE,         32,
  EGL_STENCIL_SIZE,        0,
  EGL_RENDERABLE_TYPE,     EGL_OPENGL_ES2_BIT,
  EGL_SURFACE_TYPE,        EGL_WINDOW_BIT | EGL_PIXMAP_BIT,
  EGL_DEPTH_SIZE,          16,
  EGL_NONE
};

static const EGLint ui_ctx_attribs[] = {
  EGL_CONTEXT_CLIENT_VERSION, 2,
  EGL_NONE
};

static const EGLint ui_win_attribs[] = {
  EGL_RENDER_BUFFER,     EGL_BACK_BUFFER,
  EGL_NONE
};

typedef struct meson_ui {
  glw_root_t mu_gr;

  EGLDisplay mu_dpy;
  EGLConfig mu_config;


  int mu_fullwindow;
  int mu_screensaver;
} meson_ui_t;

static meson_ui_t mu;


extern void init_lock_time_tracing(hts_thread_t pid, int maxtime);


/**
 *
 */
static void
set_blank(int value)
{
  FILE *fp = fopen("/sys/class/graphics/fb0/blank", "w");
  if(fp == NULL)
    return;

  fprintf(fp, "%d\n", value);
  fclose(fp);
}

/**
 *
 */
static int
screen_init(void)
{
  int fd = open("/sys/class/display/mode", O_WRONLY);

  if(fd == -1) {
    TRACE(TRACE_ERROR, "EGL", "Failed to open /sys/class/display/mode");
    return -1;
  }
  const char disp_mode[] = "1080p50hz";
  int wlen = write(fd, disp_mode, strlen(disp_mode));
  if (wlen < strlen(disp_mode)) {
    TRACE(TRACE_ERROR, "EGL", "Failed to write to /sys/class/display/mode");
    close(fd);
    return -1;
  }

  close(fd);
  fd = open("/dev/fb0", O_RDWR);
  if(fd == -1) {
    TRACE(TRACE_ERROR, "EGL", "Failed to open /dev/fb0");
    return -1;
  }

  struct fb_var_screeninfo si;
  int r = ioctl(fd, FBIOGET_VSCREENINFO, &si);
  if(r) {
    TRACE(TRACE_ERROR, "EGL", "Unable to query for screen dimensions");
    return -1;
  }
  /* Yay magic numbers! TODO(mla) */
  si.xres = 1920;
  si.yres = 1080;
  si.xres_virtual = 1920;
  si.yres_virtual = 2160;
  si.bits_per_pixel = 32;

  r = ioctl(fd, FBIOPUT_VSCREENINFO, &si);
  if (r) {
    TRACE(TRACE_ERROR, "EGL", "Unable to set screen dimensions");
    return -1;
  }
  return 0;
}
/**
 *
 */
static void
set_in_fullwindow(void *opaque, int v)
{
  meson_ui_t *su = opaque;
  su->mu_fullwindow = v;
}


/**
 *
 */
static void
set_in_screensaver(void *opaque, int v)
{
  meson_ui_t *mu = opaque;
  mu->mu_screensaver = v;
}


/**
 *
 */
static int
ui_init(void)
{
  EGLConfig configs[32];
  int noc;
  mu.mu_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

  if(eglInitialize(mu.mu_dpy, NULL, NULL) != EGL_TRUE) {
    TRACE(TRACE_ERROR, "EGL", "eglInitialize failed");
    return 1;
  }

  if(eglChooseConfig(mu.mu_dpy, ui_config_attribs, configs,
		     ARRAYSIZE(configs), &noc) != EGL_TRUE) {
    TRACE(TRACE_ERROR, "EGL", "Unable to choose config");
    return 1;
  }

  mu.mu_config = configs[0];

  glw_root_t *gr = &mu.mu_gr;

  gr->gr_prop_ui = prop_create_root("ui");
  gr->gr_prop_nav = nav_spawn();
  prop_set(gr->gr_prop_ui, "nobackground", PROP_SET_INT, 1);
  if(glw_init(gr)) {
    TRACE(TRACE_ERROR, "GLW", "Unable to init GLW");
    return 1;
  }

  glw_load_universe(gr);

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","fullwindow"),
		 PROP_TAG_CALLBACK_INT, set_in_fullwindow, &mu,
		 PROP_TAG_ROOT, gr->gr_prop_ui,
		 PROP_TAG_COURIER, gr->gr_courier,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","screensaverActive"),
		 PROP_TAG_CALLBACK_INT, set_in_screensaver, &mu,
		 PROP_TAG_ROOT, gr->gr_prop_ui,
		 PROP_TAG_COURIER, gr->gr_courier,
		 NULL);
  return 0;
}




static int
read_value_from_file(const char *path)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;

  char buf[64];
  int r = read(fd, buf, 63);
  close(fd);
  if(r < 0)
    return -1;

  buf[r] = 0;
  return strtol(buf, NULL, 0);
}

static void
tsync_debug(void)
{
  int pts_pcrscr = read_value_from_file("/sys/class/tsync/pts_pcrscr");
  int pts_video  = read_value_from_file("/sys/class/tsync/pts_video");

  static int last_pcrscr;
  static int last_video;

  TRACE(TRACE_DEBUG, "TSYNC", "P:%12d:%-8d V:%12d:%-8d Diff:%-10d\n",
        pts_pcrscr,
        pts_pcrscr - last_pcrscr,
        pts_video,
        pts_video - last_video,
        pts_video - pts_pcrscr);

  last_video = pts_video;
  last_pcrscr = pts_pcrscr;
}


/**
 *
 */
static void
writeval(const char *str, int val)
{
  FILE *fp = fopen(str, "w");
  if(fp == NULL)
    return;

  fprintf(fp, "%d\n", val);
  fclose(fp);
}



/**
 *
 */
static unsigned int
get_val(const char *str)
{
  int val, cnt;
  FILE *fp = fopen(str, "r");
  if(fp == NULL)
    return 0;

  if(fscanf(fp, "%d:%u\n", &cnt, &val) != 2)
    val = 0;
  fclose(fp);
  return val;
}


/**
 *
 */
static void
mali_setup_perf(void)
{
  // reset
  writeval("/debug/mali/pp/pp0/counter_src0", -1);
  writeval("/debug/mali/pp/pp0/counter_src1", -1);
  writeval("/debug/mali/pp/pp1/counter_src0", -1);
  writeval("/debug/mali/pp/pp1/counter_src1", -1);

  // enable
  writeval("/debug/mali/pp/pp0/counter_src0", 37);
  writeval("/debug/mali/pp/pp0/counter_src1", 31);
  writeval("/debug/mali/pp/pp1/counter_src0", 37);
  writeval("/debug/mali/pp/pp1/counter_src1", 31);
}

/**
 *
 */
static void
mali_read_val(const glw_root_t *gr)
{

  int cur[4];
  static int last[4];

  cur[0] = get_val("/debug/mali/pp/pp0/counter_src0");
  cur[1] = get_val("/debug/mali/pp/pp0/counter_src1");
  cur[2] = get_val("/debug/mali/pp/pp1/counter_src0");
  cur[3] = get_val("/debug/mali/pp/pp1/counter_src1");

  printf("%10u %10u %10u %10u @ %2.2f fps\n",
         cur[0] - last[0],
         cur[1] - last[1],
         cur[2] - last[2],
         cur[3] - last[3],
         gr->gr_framerate);
  last[0] = cur[0];
  last[1] = cur[1];
  last[2] = cur[2];
  last[3] = cur[3];
}



/**
 *
 */
static int
ui_run(void)
{
  glw_root_t *gr = &mu.mu_gr;
  fbdev_window fbwin = {0};


  int fd = open("/dev/fb0", O_RDWR);
  if(fd == -1) {
    TRACE(TRACE_ERROR, "EGL", "Failed to open /dev/fb0");
    return -1;
  }

  struct fb_var_screeninfo si;
  int r = ioctl(fd, FBIOGET_VSCREENINFO, &si);
  close(fd);
  if(r) {
    TRACE(TRACE_ERROR, "EGL", "Unable to query for screen dimensions");
    return -1;
  }

  TRACE(TRACE_DEBUG, "EGL", "Screen dimensions %d x %d", si.xres, si.yres);

  gr->gr_width  = si.xres;
  gr->gr_height = si.yres;

  fbwin.width  = gr->gr_width;
  fbwin.height = gr->gr_height;

  EGLSurface surface = eglCreateWindowSurface(mu.mu_dpy, mu.mu_config,
					      (EGLNativeWindowType)&fbwin,
					      ui_win_attribs);
  if(surface == EGL_NO_SURFACE) {
    TRACE(TRACE_ERROR, "EGL", "Failed to create %d x %d EGL surface",
	  gr->gr_width, gr->gr_height);
    return -1;
  }

  eglBindAPI(EGL_OPENGL_ES_API);

  EGLContext ctx = eglCreateContext(mu.mu_dpy, mu.mu_config, EGL_NO_CONTEXT,
				    ui_ctx_attribs);

  if(ctx == EGL_NO_CONTEXT) {
    TRACE(TRACE_ERROR, "EGL", "Failed to create context");
    return -1;
  }

  eglMakeCurrent(mu.mu_dpy, surface, surface, ctx);
  glw_opengl_init_context(gr);

  glClearColor(0,0,0,0);

  set_blank(0);

  mali_setup_perf();

  while(running) {

    int64_t ts = showtime_get_ts();

    glw_lock(gr);
    
    glViewport(0, 0, gr->gr_width, gr->gr_height);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    
    //    gr->gr_can_externalize = 1;
    gr->gr_externalize_cnt = 0;

    glw_prepare_frame(gr, 0);

    glw_rctx_t rc;
    glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1);
    glw_layout0(gr->gr_universe, &rc);
    glw_render0(gr->gr_universe, &rc);

    glw_unlock(gr);


    glw_post_scene(gr);
    
    eglSwapBuffers(mu.mu_dpy, surface);

    if(ctrlc == 1) {
      ctrlc = 2;
      showtime_shutdown(0);
    }
    if(0)mali_read_val(gr);
    if(0)tsync_debug();

    ts = showtime_get_ts() - ts;
    if(ts > 30000) {
      TRACE(TRACE_INFO, "MAINLOOP", "VERY HIGH FRAME DELAY: %d", (int)ts);
    }

  }


  // Reap twice makes all resources flushed out of memory
  glw_lock(gr);
  glw_unload_universe(gr);
  glw_reap(gr);
  glw_reap(gr);
  glw_unlock(gr);

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  eglSwapBuffers(mu.mu_dpy, surface);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  eglSwapBuffers(mu.mu_dpy, surface);

  eglDestroyContext(mu.mu_dpy, ctx);
  eglDestroySurface(mu.mu_dpy, surface);

  return 0;
}

static void
doexit(int x)
{
  printf("ctrl:%d\n", ctrlc);
  if(ctrlc)
    exit(0);
  ctrlc = 1;
}


static void
set_main_thread_prio(void)
{
  struct sched_param params = {};
  params.sched_priority = sched_get_priority_max(SCHED_RR);
  printf("The prio is %d\n", params.sched_priority);
  int ret = pthread_setschedparam(pthread_self(), SCHED_RR, &params);

  printf("set_main_thread_prio: %d\n", ret);
}



/**
 * Program main()
 */
int
main(int argc, char **argv)
{
  gconf.binary = argv[0];
  
  linux_check_capabilities();

  screen_init();

  posix_init();

  parse_opts(argc, argv);

  gconf.concurrency =   sysconf(_SC_NPROCESSORS_CONF);

  showtime_init();

  trap_init();

  /**
   * Wait for SIGTERM / SIGINT, but only in this thread
   */
  sigset_t set;

  sigemptyset(&set);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGINT);

  signal(SIGTERM, doexit);
  signal(SIGINT, doexit);

  pthread_sigmask(SIG_UNBLOCK, &set, NULL);

  extern int posix_set_thread_priorities;
  if(!posix_set_thread_priorities)
    printf("tut prio error WAT?!\n");

  if(1)
    set_main_thread_prio();

  if(ui_init())
    exit(1);

  ui_run();

  showtime_fini();

  arch_exit();
}


/**
 *
 */
void
arch_exit(void)
{
  exit(gconf.exit_code);
}


/**
 *
 */
int
arch_stop_req(void)
{
  running = 0;
  return 0;
}


#include "audio2/alsa.h"


int64_t
showtime_get_avtime(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

const char *alsa_get_devicename(void)
{
  return "hw:0,0";
}
