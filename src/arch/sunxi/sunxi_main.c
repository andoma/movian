/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
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

#include "arch/sunxi/sunxi.h"

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

typedef struct sunxi_ui {
  glw_root_t su_gr;

  EGLDisplay su_dpy;
  EGLConfig su_config;


  int su_fullwindow;
  int su_screensaver;
} sunxi_ui_t;

static sunxi_ui_t su;


/**
 *
 */
static void
set_in_fullwindow(void *opaque, int v)
{
  sunxi_ui_t *su = opaque;
  su->su_fullwindow = v;
}


/**
 *
 */
static void
set_in_screensaver(void *opaque, int v)
{
  sunxi_ui_t *su = opaque;
  su->su_screensaver = v;
}


/**
 *
 */
static int
ui_init(void)
{
  EGLConfig configs[32];
  int noc;
  su.su_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

  if(eglInitialize(su.su_dpy, NULL, NULL) != EGL_TRUE) {
    TRACE(TRACE_ERROR, "EGL", "eglInitialize failed");
    return 1;
  }

  if(eglChooseConfig(su.su_dpy, ui_config_attribs, configs,
		     ARRAYSIZE(configs), &noc) != EGL_TRUE) {
    TRACE(TRACE_ERROR, "EGL", "Unable to choose config");
    return 1;
  }
  su.su_config = configs[0];

  glw_root_t *gr = &su.su_gr;

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
		 PROP_TAG_CALLBACK_INT, set_in_fullwindow, &su,
		 PROP_TAG_ROOT, gr->gr_prop_ui,
		 PROP_TAG_COURIER, gr->gr_courier,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","screensaverActive"),
		 PROP_TAG_CALLBACK_INT, set_in_screensaver, &su,
		 PROP_TAG_ROOT, gr->gr_prop_ui,
		 PROP_TAG_COURIER, gr->gr_courier,
		 NULL);
  return 0;
}

/**
 *
 */
static void
display_onoff(int on)
{
  ioctl(sunxi.dispfd, on ? DISP_CMD_HDMI_ON : DISP_CMD_HDMI_OFF);
}


/**
 *
 */
static void
display_mode(int mode)
{
  glw_root_t *gr = &su.su_gr;
  unsigned long args[4] = {0};
  


  switch(mode) {
  case DISP_TV_MOD_720P_50HZ:
  case DISP_TV_MOD_720P_60HZ:
    gr->gr_width  = 1280;
    gr->gr_height = 720;
    break;

  case DISP_TV_MOD_1080I_50HZ:
  case DISP_TV_MOD_1080I_60HZ:
  case DISP_TV_MOD_1080P_24HZ:
  case DISP_TV_MOD_1080P_50HZ:
  case DISP_TV_MOD_1080P_60HZ:
    gr->gr_width  = 1920;
    gr->gr_height = 1080;
    break;

  default:
    abort();
  }

  TRACE(TRACE_INFO, "UI", "Display size %d x %d", gr->gr_width, gr->gr_height);

  if(ioctl(sunxi.dispfd, DISP_CMD_HDMI_GET_MODE, args) == mode)
    return;

  display_onoff(0);

  args[1] = mode;
  ioctl(sunxi.dispfd, DISP_CMD_HDMI_SET_MODE, args);
  args[1] = 0;
}


/**
 *
 */
static int
ui_run(void)
{
  glw_root_t *gr = &su.su_gr;
  fbdev_window fbwin = {0};

  display_mode(DISP_TV_MOD_1080P_60HZ);
  display_onoff(1);

  fbwin.width  = gr->gr_width;
  fbwin.height = gr->gr_height;

  EGLSurface surface = eglCreateWindowSurface(su.su_dpy, su.su_config,
					      (EGLNativeWindowType)&fbwin,
					      ui_win_attribs);
  if(surface == EGL_NO_SURFACE) {
    TRACE(TRACE_ERROR, "EGL", "Failed to create EGL surface");
    return -1;
  }

  eglBindAPI(EGL_OPENGL_ES_API);
        
  EGLContext ctx = eglCreateContext(su.su_dpy, su.su_config, EGL_NO_CONTEXT,
				    ui_ctx_attribs);

  if(ctx == EGL_NO_CONTEXT) {
    TRACE(TRACE_ERROR, "EGL", "Failed to create context");
    return -1;
  }

  eglMakeCurrent(su.su_dpy, surface, surface, ctx);
  glw_opengl_init_context(gr);

  //  eglSwapInterval(su.su_dpy, 1);

  glClearColor(0,0,0,0);

  int64_t ts0 = 0;

  int framenum = ioctl(sunxi.fb0fd, 0x4740);
  printf("Starting at frame %d\n", framenum);
  while(running) {


    glw_lock(gr);
    
    glViewport(0, 0, gr->gr_width, gr->gr_height);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    
    gr->gr_can_externalize = 1;
    gr->gr_externalize_cnt = 0;

    glw_prepare_frame(gr, 0);

    glw_rctx_t rc;
    glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1);
    glw_layout0(gr->gr_universe, &rc);
    glw_render0(gr->gr_universe, &rc);

    glw_unlock(gr);


    glw_post_scene(gr);
    
    int64_t ts2 = showtime_get_ts();
    eglSwapBuffers(su.su_dpy, surface);
#if 0
    int err = ioctl(sunxi.fb0fd, 0x4741, framenum);
    if(err < 0)
      printf("Swap error: %d\n", err);
    else {
      framenum = err + 1;
      static int foo;
      
      if(framenum - foo != 1)
	printf("delta:%d\n", framenum - foo);
      foo = framenum;
    }
#endif
    int64_t ts3 = showtime_get_ts();
    if(0)printf("ts3-ts2 = %lld\n", ts3 - ts2);

    int64_t ts = showtime_get_ts();

    if(0)printf("%lld\n", ts - ts0);
    ts0 = ts;

    sunxi_bg_every_frame(su.su_fullwindow || su.su_screensaver);

    if(ctrlc == 1) {
      ctrlc = 2;
      showtime_shutdown(0);
    }

  }


  // Reap twice makes all resources flushed out of memory
  glw_lock(gr);
  glw_unload_universe(gr);
  glw_reap(gr);
  glw_reap(gr);
  glw_unlock(gr);

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  eglSwapBuffers(su.su_dpy, surface);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  eglSwapBuffers(su.su_dpy, surface);

  eglDestroyContext(su.su_dpy, ctx);
  eglDestroySurface(su.su_dpy, surface);

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

/**
 * Program main()
 */
int
main(int argc, char **argv)
{
  gconf.binary = argv[0];
  
  linux_check_capabilities();

  posix_init();

  parse_opts(argc, argv);

  gconf.concurrency =   sysconf(_SC_NPROCESSORS_CONF);

  showtime_init();

  trap_init();

  sunxi_init();

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

  if(ui_init())
    exit(1);

  ui_run();

  sunxi_fini();

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

