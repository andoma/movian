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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <assert.h>

#include <bcm_host.h>
#include <OMX_Core.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "showtime.h"
#include "arch/arch.h"
#include "arch/posix/posix.h"

#include "arch/linux/linux.h"
#include "prop/prop.h"
#include "ui/glw/glw.h"
#include "ui/background.h"
#include "navigator.h"
#include "omx.h"
#include "backend/backend.h"
#include "notifications.h"


static uint32_t screen_width, screen_height;
static EGLDisplay display;
static EGLContext context;
static EGLSurface surface;

static DISPMANX_DISPLAY_HANDLE_T dispman_display;

/**
 *
 */
int64_t
showtime_get_avtime(void)
{
  return showtime_get_ts();
}


#define check() assert(glGetError() == 0)

/**
 *
 */
static void
egl_init(void)
{
  int32_t success = 0;
  EGLBoolean result;
  EGLint num_config;

  EGL_DISPMANX_WINDOW_T nw;

  DISPMANX_ELEMENT_HANDLE_T de;
  DISPMANX_UPDATE_HANDLE_T u;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;

  static const EGLint attribute_list[] =
    {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
    };
   
  static const EGLint context_attributes[] = 
    {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
    };


  EGLConfig config;

  // get an EGL display connection
  display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(display != EGL_NO_DISPLAY);
  check();

  // initialize the EGL display connection
  result = eglInitialize(display, NULL, NULL);
  assert(result != EGL_FALSE);
  check();

  // get an appropriate EGL frame buffer configuration
  result = eglChooseConfig(display, attribute_list, &config, 1, &num_config);
  assert(result != EGL_FALSE);
  check();

  // get an appropriate EGL frame buffer configuration
  result = eglBindAPI(EGL_OPENGL_ES_API);
  assert(result != EGL_FALSE);
  check();

  // create an EGL rendering context
  context = eglCreateContext(display, config, EGL_NO_CONTEXT,
			     context_attributes);
  assert(context != EGL_NO_CONTEXT);
  check();

  // create an EGL window surface
  success = graphics_get_display_size(0, &screen_width, &screen_height);
  assert(success >= 0);

  dispman_display = vc_dispmanx_display_open(0);


  u = vc_dispmanx_update_start(0);

  DISPMANX_RESOURCE_HANDLE_T r;
  uint32_t ip;
 
  int rw = 32;
  int rh = 32;

  r = vc_dispmanx_resource_create(VC_IMAGE_RGB565, rw, rh, &ip);
  
  void *zero = calloc(1, rw * rh * 2);
  memset(zero, 0xcc, rw * rh * 2);
  VC_RECT_T rect;
  vc_dispmanx_rect_set(&rect, 0, 0, rw, rh);

  vc_dispmanx_resource_write_data(r, VC_IMAGE_RGB565, rw * 2, zero, &rect);

  free(zero);

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = screen_width;
  dst_rect.height = screen_height;

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = rw << 16;
  src_rect.height = rh << 16;     
#if 0
  vc_dispmanx_element_add(u, dd, 1, &dst_rect, r,
			  &src_rect, DISPMANX_PROTECTION_NONE,
			  NULL, NULL, 0);
#endif

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = screen_width;
  dst_rect.height = screen_height;

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = screen_width << 16;
  src_rect.height = screen_height << 16;        

  de = vc_dispmanx_element_add(u, dispman_display, 10, &dst_rect, 0,
			       &src_rect, DISPMANX_PROTECTION_NONE,
			       NULL, NULL, 0);

  memset(&nw, 0, sizeof(nw));
  nw.element = de;
  nw.width = screen_width;
  nw.height = screen_height;
  vc_dispmanx_update_submit_sync(u);
      
  check();

  surface = eglCreateWindowSurface(display, config, &nw, NULL);
  assert(surface != EGL_NO_SURFACE);
  check();

  // connect the context to the surface
  result = eglMakeCurrent(display, surface, surface, context);
  assert(EGL_FALSE != result);
  check();
}


static  DISPMANX_ELEMENT_HANDLE_T bg_element;
static int bg_resource;
static float bg_current_alpha;
static VC_RECT_T bg_src_rect;
static VC_RECT_T bg_dst_rect;

/**
 *
 */
static void
bg_refresh_element(void)
{
  DISPMANX_UPDATE_HANDLE_T u = vc_dispmanx_update_start(0);

  VC_DISPMANX_ALPHA_T alpha;
  alpha.flags =  DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS;
  alpha.opacity = bg_current_alpha * 255;
  alpha.mask = 0;

  if(alpha.opacity == 0) {
    if(bg_element) {
      vc_dispmanx_element_remove(u, bg_element);
      bg_element = 0;
    }
  } else if(!bg_element) {
    if(bg_resource) {
      bg_element = vc_dispmanx_element_add(u, dispman_display, -10,
					   &bg_dst_rect, bg_resource,
					   &bg_src_rect,
					   DISPMANX_PROTECTION_NONE,
					   &alpha, NULL, 0);
    }
  } else {
    vc_dispmanx_element_change_attributes(u, bg_element,
					  1 << 1,
					  0,
					  alpha.opacity,
					  NULL, NULL, 0, 0);
  }
  vc_dispmanx_update_submit_sync(u);
}




/**
 *
 */
static void
set_bg_image(rstr_t *url, const char **vpaths, void *opaque)
{
  char errbuf[256];
  image_meta_t im = {0};
  im.im_req_width  = screen_width;
  im.im_req_height = screen_height;

  pixmap_t *pm;
  pm = backend_imageloader(url, &im, vpaths, errbuf, sizeof(errbuf),
			   NULL, NULL, NULL);

  if(pm == NULL) {
    TRACE(TRACE_ERROR, "BG", "Unable to load %s -- %s", rstr_get(url), errbuf);
    return;
  }

  uint32_t ip;
  VC_IMAGE_TYPE_T it;

  switch(pm->pm_type) {
  case PIXMAP_BGR32:
    it = VC_IMAGE_ARGB8888;
    break;
  case PIXMAP_RGB24:
    it = VC_IMAGE_RGB888;
    break;
  default:
    pixmap_release(pm);
    return;
  }

  if(bg_resource)
    vc_dispmanx_resource_delete(bg_resource);


  bg_resource =
    vc_dispmanx_resource_create(it, pm->pm_width, pm->pm_height, &ip);
  
  vc_dispmanx_rect_set(&bg_src_rect, 0, 0,
		       pm->pm_width << 16, pm->pm_height << 16);
  vc_dispmanx_rect_set(&bg_dst_rect, 0, 0,
		       pm->pm_width, pm->pm_height);

  vc_dispmanx_resource_write_data(bg_resource, it, pm->pm_linesize,
				  pm->pm_pixels, &bg_dst_rect);

  pixmap_release(pm);

  bg_refresh_element();
}

/**
 *
 */
static void
set_bg_alpha(float alpha, void *opaque)
{
  bg_current_alpha = alpha;
  bg_refresh_element();
}



static void
the_alarm(int x)
{
  extern int alarm_fired;
  alarm_fired = 1;
}

static int avgit(const int *p, int num)
{
  int i, v = 0;
  for(i = 0; i < num; i++)
    v += p[i];
  return v / num;
}


/**
 *
 */
static void
run(void)
{
  glw_root_t *gr = calloc(1, sizeof(glw_root_t));
  gr->gr_reduce_cpu = 1;
  gr->gr_prop_ui = prop_create_root("ui");
  gr->gr_prop_nav = nav_spawn();
  prop_set(gr->gr_prop_ui, "nobackground", PROP_SET_INT, 1);

  background_init(gr->gr_prop_ui, gr->gr_prop_nav,
		  set_bg_image, set_bg_alpha, NULL);

  gr->gr_width = screen_width;
  gr->gr_height = screen_height;

  if(glw_init(gr)) {
    TRACE(TRACE_ERROR, "GLW", "Unable to init GLW");
    exit(1);
  }

  glw_load_universe(gr);
  glw_opengl_init_context(gr);

  glClearColor(0,0,0,0);

  // Arrange for prop_courier_poll_with_alarm

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = the_alarm;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGALRM, &sa, NULL);


  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  pthread_sigmask(SIG_UNBLOCK, &set, NULL);

  gr->gr_prop_dispatcher = &prop_courier_poll_with_alarm;
  gr->gr_prop_maxtime = 5000;

  int numframes = 0;
  int tprep[10] = {};
  int tlayout[10] = {};
  int trender[10] = {};
  int tpost[10] = {};

  int64_t t1, t2, t3, t4, t5;

  while(!gr->gr_stop) {

    glw_lock(gr);

    glViewport(0, 0, gr->gr_width, gr->gr_height);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    t1 = showtime_get_ts();
    glw_prepare_frame(gr, 0);

    glw_rctx_t rc;
    glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1);
    t2 = showtime_get_ts();
    glw_layout0(gr->gr_universe, &rc);
    t3 = showtime_get_ts();
    glw_render0(gr->gr_universe, &rc);
    t4 = showtime_get_ts();

    glw_unlock(gr);
    glw_post_scene(gr);
    t5 = showtime_get_ts();
    eglSwapBuffers(display, surface);


    tprep[numframes]   = t2 - t1;
    tlayout[numframes] = t3 - t2;
    trender[numframes] = t4 - t3;
    tpost[numframes]   = t5 - t4;

    numframes++;

    if(numframes == 10) {
      numframes = 0;
      if(0) {
	printf("%10d %10d %10d %10d\n",
	       avgit(tprep, 10),
	       avgit(tlayout, 10),
	       avgit(trender, 10),
	       avgit(tpost, 10));
      }
    }
  }
}


/**
 * Linux main
 */
int
main(int argc, char **argv)
{
  asm volatile("vmrs r0, fpscr\n"
	       "orr r0, $(1 << 24)\n"
	       "vmsr fpscr, r0" : : : "r0");

  linux_check_capabilities();

  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  sigprocmask(SIG_BLOCK, &set, NULL);

  bcm_host_init();

  omx_init();

  gconf.binary = argv[0];

  posix_init();

  parse_opts(argc, argv);

  gconf.concurrency = 1;

  trap_init();

  showtime_init();

  linux_init_monitors();

  egl_init();

  extern int posix_set_thread_priorities;

  if(!posix_set_thread_priorities) {
    char buf[512];
    
    snprintf(buf, sizeof(buf),
	     "Showtime runs without realtime scheduling on your Raspberry Pi\n"
	     "This may impact performance during video playback.\n"
	     "You have been warned! Please set SYS_CAP_NICE:\n"
	     "  sudo setcap 'cap_sys_nice+ep %s'", gconf.binary);
    notify_add(NULL, NOTIFY_WARNING, NULL, 10,  rstr_alloc(buf));
  }

  run();

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
