/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <signal.h>
#include <assert.h>

#include <bcm_host.h>
#include <OMX_Core.h>
#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vchiq_arm/vchiq_if.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "main.h"
#include "arch/arch.h"
#include "arch/posix/posix.h"

#include "arch/linux/linux.h"
#include "prop/prop.h"
#include "ui/glw/glw.h"
#include "navigator.h"
#include "omx.h"
#include "backend/backend.h"
#include "notifications.h"
#include "misc/callout.h"
#include "misc/strtab.h"

#if ENABLE_CONNMAN
#include <gio/gio.h>
#include "networking/connman.h"
#endif

#include "rpi.h"

DISPMANX_DISPLAY_HANDLE_T dispman_display;


int display_status = DISPLAY_STATUS_ON;
int cec_we_are_not_active;
extern int auto_ui_shutdown;
static int runmode;
static int ctrlc;

/**
 *
 */
int64_t
arch_get_avtime(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}


static struct strtab rpirevisions[] = {
  {"-1 Model B r1.0 256MB", 2},
  {"-1 Model B r1.0 256MB + ECN0001", 3},
  {"-1 Model B r2.0 256MB", 4},
  {"-1 Model B r2.0 256MB", 5},
  {"-1 Model B r2.0 256MB", 6},
  {"-1 Model A 256MB", 7},
  {"-1 Model A 256MB", 8},
  {"-1 Model A 256MB", 9},
  {"-1 Model B r2.0 512MB", 0xd},
  {"-1 Model B r2.0 512MB", 0xe},
  {"-1 Model B r2.0 512MB", 0xf},
  {"-1 Model B+ 512MB", 0x10},
  {"-1 Compute Module 512MB", 0x11},
  {"-1 Model A+ 512MB", 0x12},
  {"-2 Model B (Sony) 1GB", 0xa01041},
  {"-2 Model B (Embest) 1GB", 0xa21041},
};

static void
rpi_get_revision(void)
{
  char buf[256] = {0};
  FILE *fp = fopen("/proc/cpuinfo", "r");
  if(fp == NULL)
    return;
  while(fgets(buf, sizeof(buf) - 1, fp) != NULL) {
    if(strncmp(buf, "Revision", strlen("Revision")))
      continue;
    const char *x = strchr(buf, ':');
    if(x == NULL)
      continue;
    x += 2;
    int rev = strtol(x, NULL, 16);
    rev &= 0xffffff;
    fclose(fp);
    const char *model = val2str(rev, rpirevisions);
    if(model == NULL) {
      snprintf(gconf.device_type, sizeof(gconf.device_type),
               "Raspberry Pi unknown revision 0x%x", rev);
    } else {
      snprintf(gconf.device_type, sizeof(gconf.device_type),
               "Raspberry Pi%s", model);
    }
    return;
  }
  fclose(fp);
}




static  DISPMANX_ELEMENT_HANDLE_T bg_element;
static int bg_resource;
static float bg_current_alpha;
static VC_RECT_T bg_src_rect;
static VC_RECT_T bg_dst_rect;



/**
 * Must be called under gr_lock()
 */
static void
bg_refresh_element(int force_flush)
{
  DISPMANX_UPDATE_HANDLE_T u = vc_dispmanx_update_start(0);

  VC_DISPMANX_ALPHA_T alpha;
  alpha.flags =  DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS;
  alpha.opacity = bg_current_alpha * 255;
  alpha.mask = 0;

  if(force_flush) {
    vc_dispmanx_element_remove(u, bg_element);
    bg_element = 0;
  }

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
set_bg_image(rstr_t *url, const char **vpaths, glw_root_t *gr)
{
  char errbuf[256];
  image_meta_t im = {0};
  unsigned int w, h;

  glw_unlock(gr);

  graphics_get_display_size(0, &w, &h);

  im.im_req_width  = w;
  im.im_req_height = h;

  image_t *img;
  img = backend_imageloader(url, &im, vpaths, errbuf, sizeof(errbuf),
			   NULL, NULL);
  glw_lock(gr);

  if(img == NULL) {
    TRACE(TRACE_ERROR, "BG", "Unable to load %s -- %s", rstr_get(url), errbuf);
    return;
  }

  image_component_t *ic = image_find_component(img, IMAGE_PIXMAP);
  if(ic == NULL) {
    image_release(img);
    return;
  }

  const pixmap_t *pm = ic->pm;

  uint32_t ip;
  VC_IMAGE_TYPE_T it;

  switch(pm->pm_type) {
  case PIXMAP_BGR32:
    it = VC_IMAGE_RGBX32;
    break;
  case PIXMAP_RGB24:
    it = VC_IMAGE_RGB888;
    break;
  default:
    TRACE(TRACE_ERROR, "BG", "Can't handle format %d", pm->pm_type);
    image_release(img);
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
				  pm->pm_data, &bg_dst_rect);

  image_release(img);

  bg_refresh_element(1);
}


/**
 *
 */
static void
set_bg_alpha(float alpha)
{
  if(bg_current_alpha == alpha)
    return;

  bg_current_alpha = alpha;
  bg_refresh_element(0);
}

static int backdrop_loader_run;
static hts_cond_t backdrop_loader_cond;
LIST_HEAD(backdrop_list, backdrop);

struct backdrop_list backdrops;

typedef struct backdrop {
  LIST_ENTRY(backdrop) link;
  rstr_t *url;
  float alpha;
  int mark;
  atomic_t refcount;
} backdrop_t;

static backdrop_t *backdrop_current;
static backdrop_t *backdrop_pending;

/**
 *
 */
static void
backdrop_release(backdrop_t *b)
{
  if(atomic_dec(&b->refcount))
    return;

  rstr_release(b->url);
  free(b);
}


/**
 *
 */
static void *
backdrop_loader(void *aux)
{
  glw_root_t *gr = aux;

  glw_lock(gr);

  while(backdrop_loader_run) {

    if(backdrop_pending == NULL) {
      hts_cond_wait(&backdrop_loader_cond, &gr->gr_mutex);
      continue;
    }


    if(backdrop_current)
      backdrop_release(backdrop_current);

    rstr_t *tgt = backdrop_pending->url;
    backdrop_current = backdrop_pending;

    backdrop_pending = NULL;

    TRACE(TRACE_DEBUG, "RPI", "Backdrop loading %s", rstr_get(tgt));
    set_bg_image(tgt, gr->gr_vpaths, gr);
  }
  if(backdrop_current)
    backdrop_release(backdrop_current);
  glw_unlock(gr);
  return NULL;
}



/**
 *
 */
static void
pick_backdrop(glw_root_t *gr)
{
  char path[512];
  float alpha;
  backdrop_t *b, *next;

  for(int i = 0; i < gr->gr_externalize_cnt; i++) {
    if(glw_image_get_details(gr->gr_externalized[i],
			     path, sizeof(path), &alpha))
      continue;

    LIST_FOREACH(b, &backdrops, link) {
      if(!strcmp(rstr_get(b->url), path))
	break;
    }

    if(b == NULL) {
      b = calloc(1, sizeof(backdrop_t));
      b->url = rstr_alloc(path);
      LIST_INSERT_HEAD(&backdrops, b, link);
      atomic_set(&b->refcount, 1);
    }

    b->mark = 1;
    b->alpha = alpha;
  }
  
  backdrop_t *best = NULL;

  for(b = LIST_FIRST(&backdrops); b != NULL; b = next) {
    next = LIST_NEXT(b, link);
    if(b->mark) {
      if(best == NULL || b->alpha > best->alpha)
	best = b;

      b->mark = 0;
    } else {
      LIST_REMOVE(b, link);
      backdrop_release(b);
    }
  }

  if(best == NULL) {
    set_bg_alpha(0);
    return;
  }


  if(backdrop_current != NULL)
    set_bg_alpha(backdrop_current->alpha);

  if(best == backdrop_pending || best == backdrop_current)
    return;  // have correct or at least on our way

  if(backdrop_pending)
    backdrop_release(backdrop_pending);

  backdrop_pending = best;
  atomic_inc(&best->refcount);
  hts_cond_signal(&backdrop_loader_cond);
}


/**
 *
 */
static void
the_alarm(int x)
{
  extern int alarm_fired;
  alarm_fired = 1;
}


/**
 *
 */
static void
doexit(int x)
{
  if(ctrlc)
    exit(0);
  ctrlc = 1;
}


/**
 *
 */
static glw_root_t *
ui_create(void)
{
  glw_root_t *gr = calloc(1, sizeof(glw_root_t));
  gr->gr_reduce_cpu = 1;
  gr->gr_prop_ui = prop_create_root("ui");
  gr->gr_prop_nav = nav_spawn();

  if(glw_init4(gr, &prop_courier_poll_with_alarm,
               prop_courier_create_passive(), GLW_INIT_KEYBOARD_MODE)) {
    TRACE(TRACE_ERROR, "GLW", "Unable to init GLW");
    exit(1);
  }

  glw_load_universe(gr);

  // Arrange for prop_courier_poll_with_alarm

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = the_alarm;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGALRM, &sa, NULL);


  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGINT);
  signal(SIGTERM, doexit);
  signal(SIGINT, doexit);
  pthread_sigmask(SIG_UNBLOCK, &set, NULL);

  gr->gr_prop_maxtime = 5000;

  return gr;
}


/**
 *
 */
static int
ui_should_run(void)
{
  if(runmode != RUNMODE_RUNNING)
    return 0;

  if(ctrlc)
    return 0;

  if(!auto_ui_shutdown)
    return 1;

  if(cec_we_are_not_active)
    return 0;

  if(display_status != DISPLAY_STATUS_ON)
    return 0;

  return 1;
}

/**
 *
 */
static void
ui_run(glw_root_t *gr, EGLDisplay dpy)
{
  int32_t success = 0;
  EGLBoolean result;
  EGLint num_config;
  unsigned int screen_width, screen_height;
  EGL_DISPMANX_WINDOW_T nw;

  DISPMANX_ELEMENT_HANDLE_T de;
  DISPMANX_UPDATE_HANDLE_T u;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;
  EGLContext context;
  EGLSurface surface;

  hts_thread_t backdrop_loader_tid;

  static const EGLint attribute_list[] = {
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };
  
  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  glGetError();

  EGLConfig config;

  while(1) {

    // get an appropriate EGL frame buffer configuration
    result = eglChooseConfig(dpy, attribute_list,
			     &config, 1, &num_config);

    if(glGetError()) {
      sleep(1);
      continue;
    }
    break;
  }

  // get an appropriate EGL frame buffer configuration
  result = eglBindAPI(EGL_OPENGL_ES_API);
  if(result == EGL_FALSE) {
    exit(2);
  }

  // create an EGL rendering context
  context = eglCreateContext(dpy, config, EGL_NO_CONTEXT,
			     context_attributes);

  if(context == EGL_NO_CONTEXT) {
    exit(2);
  }

  // create an EGL window surface
  success = graphics_get_display_size(0, &screen_width, &screen_height);
  if(success < 0) {
    exit(2);
  }

  VC_DISPMANX_ALPHA_T alpha;
  alpha.flags =
    DISPMANX_FLAGS_ALPHA_FROM_SOURCE | DISPMANX_FLAGS_ALPHA_PREMULT;
  alpha.opacity = 255;
  alpha.mask = 0;

  u = vc_dispmanx_update_start(0);

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
			       &alpha, NULL, 0);

  memset(&nw, 0, sizeof(nw));
  nw.element = de;
  nw.width = screen_width;
  nw.height = screen_height;
  vc_dispmanx_update_submit_sync(u);
      

  surface = eglCreateWindowSurface(dpy, config, &nw, NULL);
  if(surface == EGL_NO_SURFACE) {
    exit(2);
  }

  // connect the context to the surface
  result = eglMakeCurrent(dpy, surface, surface, context);
  if(result == EGL_FALSE) {
    exit(2);
  }


  gr->gr_width = screen_width;
  gr->gr_height = screen_height;

  glw_opengl_init_context(gr);

  glClearColor(0,0,0,0);

  TRACE(TRACE_DEBUG, "RPI", "UI starting");

  hts_cond_init(&backdrop_loader_cond, &gr->gr_mutex);

  backdrop_loader_run = 1;
  hts_thread_create_joinable("bgloader", &backdrop_loader_tid,
			     backdrop_loader, gr, 
			     THREAD_PRIO_UI_WORKER_LOW);

  while(ui_should_run()) {

    glw_lock(gr);

    glw_prepare_frame(gr, 0);

    int refresh = gr->gr_need_refresh;
    gr->gr_need_refresh = 0;
    if(refresh) {
      int zmax = 0;

      glw_rctx_t rc;

      gr->gr_can_externalize = 1;
      gr->gr_externalize_cnt = 0;
      glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1, &zmax);
      glw_layout0(gr->gr_universe, &rc);

      if(refresh & GLW_REFRESH_FLAG_RENDER) {
	glViewport(0, 0, gr->gr_width, gr->gr_height);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glw_render0(gr->gr_universe, &rc);
      }

      pick_backdrop(gr);
    }

    glw_unlock(gr);
    if(refresh & GLW_REFRESH_FLAG_RENDER) {
      glw_post_scene(gr);
      eglSwapBuffers(dpy, surface);
    } else {
      usleep(16666);
    }
  }
  glw_reap(gr);
  glw_reap(gr);
  glw_flush(gr);

  glw_lock(gr);
  backdrop_loader_run = 0;
  hts_cond_signal(&backdrop_loader_cond);
  glw_unlock(gr);

  hts_thread_join(&backdrop_loader_tid);

  hts_cond_destroy(&backdrop_loader_cond);

  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(dpy, surface);
  eglDestroyContext(dpy, context);
  TRACE(TRACE_DEBUG, "RPI", "UI terminated");
}


/**
 *
 */
static void
rpi_mainloop(void)
{
  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(dpy == EGL_NO_DISPLAY)
    exit(2);
  
  // initialize the EGL display connection
  int result = eglInitialize(dpy, NULL, NULL);
  if(result == EGL_FALSE)
    exit(2);

  dispman_display = vc_dispmanx_display_open(0);

  glw_root_t *gr = ui_create();

  runmode = RUNMODE_RUNNING;

  while(runmode != RUNMODE_EXIT && !ctrlc) {
    if(ui_should_run()) {
      swrefresh();
      ui_run(gr, dpy);
    } else {
      glw_lock(gr);
      glw_idle(gr);
      glw_unlock(gr);
      usleep(100000);
    }
  }
}


/**
 * Stop STOS splash screen
 */
static void
stos_stop_splash(void)
{
  const char *runfile = "/var/run/stos-splash.pid";
  int val;
  FILE *fp = fopen(runfile, "r");
  if(fp == NULL)
    return;
  int r = fscanf(fp, "%d", &val);
  fclose(fp);
  if(r != 1)
    return;
  TRACE(TRACE_DEBUG, "STOS", "Asking stos-splash (pid: %d) to stop", val);
  kill(val, SIGINT);

  for(int i = 0; i < 100; i++) {
    struct stat st;
    if(stat(runfile, &st)) {
      TRACE(TRACE_DEBUG, "STOS", "stos-splash is gone");
      return;
    }
    usleep(10000);
  }
  TRACE(TRACE_ERROR, "STOS", "stos-splash fails to terminate");
}


/**
 *
 */
static void
kill_framebuffer(void)
{
  struct stat st;
  if(stat("/dev/fb0", &st))
    return; // No frame buffer

  // Turning off TV output seems to kill framebuffer
  vc_tv_power_off();
  vc_tv_hdmi_power_on_preferred();
}



/**
 *
 */
static void
tv_update_state(void)
{
  TV_DISPLAY_STATE_T state = {};
  if(vc_tv_get_display_state(&state)) {
    printf("failed to get state\n");
    return;
  }
}


/**
 *
 */
static void
tv_service_callback(void *callback_data, uint32_t reason,
		    uint32_t param1, uint32_t param2)
{
  TRACE(TRACE_DEBUG, "TV", "State change 0x%08x 0x%08x 0x%08x",
	reason, param1, param2);
#if 0
  if(reason & 1) {
    display_status = DISPLAY_STATUS_OFF;
    TRACE(TRACE_INFO, "TV", "Display status = off");
  } else {
    display_status = DISPLAY_STATUS_ON;
    TRACE(TRACE_INFO, "TV", "Display status = on");
  }
#endif
  tv_update_state();
}


/**
 *
 */
static void
tv_init(void)
{
  vc_tv_register_callback(tv_service_callback, NULL);
  tv_update_state();
}


/**
 *
 */
static void
my_vcos_log(const VCOS_LOG_CAT_T *cat, VCOS_LOG_LEVEL_T _level,
	    const char *fmt, va_list args)
{
  int stlevel;
  switch(_level) {
  case  VCOS_LOG_ERROR:   stlevel = TRACE_ERROR; break;
  case VCOS_LOG_WARN:     stlevel = TRACE_ERROR; break;
  default:
    stlevel = TRACE_DEBUG; break;
  }
  tracev(0, stlevel, cat->name, fmt, args);
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

  rpi_get_revision();

  linux_check_capabilities();

  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  sigprocmask(SIG_BLOCK, &set, NULL);

  bcm_host_init();



  vcos_set_vlog_impl(my_vcos_log);

  kill_framebuffer();

  omx_init();

  gconf.binary = argv[0];

  posix_init();

  parse_opts(argc, argv);

  linux_init();

  main_init();

  tv_init();

  rpi_cec_init();

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


#if ENABLE_CONNMAN
  // connman uses dbus (via glib)
  g_thread_init(NULL);
  g_type_init();
  connman_init();
#endif

  stos_stop_splash();

  rpi_mainloop();
  shutdown_hook_run(1);
  main_fini();
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
  runmode = RUNMODE_EXIT;
  return 0;
}


/**
 *
 */
int
rpi_is_codec_enabled(const char *id)
{
  char query[64];
  char buf[64];
  snprintf(query, sizeof(query), "codec_enabled %s", id);
  vc_gencmd(buf, sizeof(buf), query);
  TRACE(TRACE_INFO, "VideoCore", "%s", buf);
  return !!strstr(buf, "=enabled");
}


static callout_t timer;

/**
 *
 */
static void
rpi_monitor_timercb(callout_t *c, void *aux)
{
  callout_arm(&timer, rpi_monitor_timercb, aux, 10);

  prop_t *tempprop = prop_create(aux, "temp");

  char buf[64];

  buf[0] = 0;
  vc_gencmd(buf, sizeof(buf), "measure_temp");
  const char *x = mystrbegins(buf, "temp=");
  if(x != NULL)
    prop_set(tempprop, "cpu", PROP_SET_INT, atoi(x));
}



/**
 *
 */
static void
rpi_monitor_init(void)
{
  prop_t *p = prop_create(prop_get_global(), "system");
  rpi_monitor_timercb(NULL, p);
}

INITME(INIT_GROUP_API, rpi_monitor_init, NULL);
