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
#include "navigator.h"
#include "omx.h"

static uint32_t screen_width, screen_height;
static EGLDisplay display;
static EGLContext context;
static EGLSurface surface;


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
  DISPMANX_DISPLAY_HANDLE_T dd;
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

  dd = vc_dispmanx_display_open(0);
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

  de = vc_dispmanx_element_add(u, dd, 10, &dst_rect, 0,
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


/**
 *
 */
static void
run(void)
{
  glw_root_t *gr = calloc(1, sizeof(glw_root_t));
  gr->gr_prop_ui = prop_create_root("ui");
  gr->gr_prop_nav = nav_spawn();
  gr->gr_width = screen_width;
  gr->gr_height = screen_height;

  if(glw_init(gr)) {
    TRACE(TRACE_ERROR, "GLW", "Unable to init GLW");
    exit(1);
  }

  glw_load_universe(gr);
  glw_opengl_init_context(gr);

  glClearColor(0,0,0,0);

  while(!gr->gr_stop) {

    glw_lock(gr);

    glViewport(0, 0, gr->gr_width, gr->gr_height);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    glw_prepare_frame(gr, 0);

    glw_rctx_t rc;
    glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1);
    glw_layout0(gr->gr_universe, &rc);
    glw_render0(gr->gr_universe, &rc);

    glw_unlock(gr);
    glw_post_scene(gr);

    eglSwapBuffers(display, surface);
  }
}


/**
 * Linux main
 */
int
main(int argc, char **argv)
{
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
