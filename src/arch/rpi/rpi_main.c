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
#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vchiq_arm/vchiq_if.h>

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
#include "backend/backend.h"
#include "notifications.h"

#if ENABLE_CONNMAN
#include <gio/gio.h>
#include "networking/connman.h"
#endif

DISPMANX_DISPLAY_HANDLE_T dispman_display;

#define DISPLAY_STATUS_OFF             0
#define DISPLAY_STATUS_ON              1
#define DISPLAY_STATUS_ON_NOT_VISIBLE  2

#define RUNMODE_EXIT                   0
#define RUNMODE_RUNNING                1
#define RUNMODE_STANDBY                2

hts_mutex_t display_mutex;
hts_cond_t display_cond;

static int display_status;
static int runmode;


/**
 *
 */
int64_t
showtime_get_avtime(void)
{
  return showtime_get_ts();
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

  pixmap_t *pm;
  pm = backend_imageloader(url, &im, vpaths, errbuf, sizeof(errbuf),
			   NULL, NULL, NULL);
  glw_lock(gr);

  if(pm == NULL) {
    TRACE(TRACE_ERROR, "BG", "Unable to load %s -- %s", rstr_get(url), errbuf);
    return;
  }

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
  int refcount;
} backdrop_t;

static backdrop_t *backdrop_current;
static backdrop_t *backdrop_pending;

/**
 *
 */
static void
backdrop_release(backdrop_t *b)
{
  if(atomic_add(&b->refcount, -1) > 1)
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
      b->refcount = 1;
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
  atomic_add(&best->refcount, 1);
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
static glw_root_t *
ui_create(void)
{
  glw_root_t *gr = calloc(1, sizeof(glw_root_t));
  gr->gr_reduce_cpu = 1;
  gr->gr_prop_ui = prop_create_root("ui");
  gr->gr_prop_nav = nav_spawn();

  if(glw_init(gr)) {
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
  pthread_sigmask(SIG_UNBLOCK, &set, NULL);

  gr->gr_prop_dispatcher = &prop_courier_poll_with_alarm;
  gr->gr_prop_maxtime = 5000;

  return gr;
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
			       NULL, NULL, 0);

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

  while(runmode == RUNMODE_RUNNING &&
	display_status == DISPLAY_STATUS_ON) {

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

    pick_backdrop(gr);

    glw_unlock(gr);
    glw_post_scene(gr);
    eglSwapBuffers(dpy, surface);
  }
  glw_reap(gr);
  glw_reap(gr);
  glw_flush(gr);

  glw_lock(gr);
  backdrop_loader_run = 1;
  hts_cond_signal(&backdrop_loader_cond);
  glw_unlock(gr);

  hts_thread_join(&backdrop_loader_tid);

  hts_cond_destroy(&backdrop_loader_cond);

  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(dpy, surface);
  eglDestroyContext(dpy, context);
  TRACE(TRACE_DEBUG, "RPI", "UI terminated");
}


#define AVEC(x...) (const action_type_t []){x, ACTION_NONE}
const static action_type_t *btn_to_action[256] = {
  [CEC_User_Control_Select]      = AVEC(ACTION_ACTIVATE),
  [CEC_User_Control_Left]        = AVEC(ACTION_LEFT),
  [CEC_User_Control_Left]        = AVEC(ACTION_LEFT),
  [CEC_User_Control_Up]          = AVEC(ACTION_UP),
  [CEC_User_Control_Right]       = AVEC(ACTION_RIGHT),
  [CEC_User_Control_Down]        = AVEC(ACTION_DOWN),
  [CEC_User_Control_Exit]        = AVEC(ACTION_NAV_BACK),

  [CEC_User_Control_Pause]       = AVEC(ACTION_PLAYPAUSE),
  [CEC_User_Control_Play]        = AVEC(ACTION_PLAY),
  [CEC_User_Control_Stop]        = AVEC(ACTION_STOP),

  [CEC_User_Control_Rewind]      = AVEC(ACTION_SEEK_BACKWARD),
  [CEC_User_Control_FastForward] = AVEC(ACTION_SEEK_FORWARD),

  [CEC_User_Control_Rewind]      = AVEC(ACTION_SKIP_BACKWARD),
  [CEC_User_Control_Forward]     = AVEC(ACTION_SKIP_FORWARD),

  [CEC_User_Control_Record]      = AVEC(ACTION_RECORD),

  [CEC_User_Control_RootMenu]    = AVEC(ACTION_HOME),

  [CEC_User_Control_F1Blue]      = AVEC(ACTION_ENABLE_SCREENSAVER),
  [CEC_User_Control_F2Red]       = AVEC(ACTION_MENU),
  [CEC_User_Control_F3Green]     = AVEC(ACTION_SHOW_MEDIA_STATS),
  [CEC_User_Control_F4Yellow]    = AVEC(ACTION_ITEMMENU),
};



/**
 *
 */
static void
cec_emit_key_down(int code)
{
  const action_type_t *avec = btn_to_action[code];
  if(avec != NULL) {
    int i = 0;
    while(avec[i] != 0)
      i++;
    event_t *e = event_create_action_multi(avec, i);
    event_to_ui(e);
  } else {
    TRACE(TRACE_DEBUG, "CEC", "Unmapped code 0x%02x", code);
  }
}






const uint32_t myVendorId = CEC_VENDOR_ID_BROADCOM;
uint16_t physical_address;
CEC_AllDevices_T logical_address;



static void
SetStreamPath(const VC_CEC_MESSAGE_T *msg)
{
    uint16_t requestedAddress;

    requestedAddress = (msg->payload[1] << 8) + msg->payload[2];
    if (requestedAddress != physical_address)
        return;
    vc_cec_send_ActiveSource(physical_address, VC_FALSE);
}


static void
give_device_power_status(const VC_CEC_MESSAGE_T *msg)
{
    // Send CEC_Opcode_ReportPowerStatus
    uint8_t response[2];
    response[0] = CEC_Opcode_ReportPowerStatus;
    response[1] = CEC_POWER_STATUS_ON;
    vc_cec_send_message(msg->initiator, response, 2, VC_TRUE);
}


static void
give_device_vendor_id(const VC_CEC_MESSAGE_T *msg)
 {
  uint8_t response[4];
  response[0] = CEC_Opcode_DeviceVendorID;
  response[1] = (uint8_t) ((myVendorId >> 16) & 0xff);
  response[2] = (uint8_t) ((myVendorId >> 8) & 0xff);
  response[3] = (uint8_t) ((myVendorId >> 0) & 0xff);
  vc_cec_send_message(msg->initiator, response, 4, VC_TRUE);
}


static void
send_cec_version(const VC_CEC_MESSAGE_T *msg)
 {
  uint8_t response[2];
  response[0] = CEC_Opcode_CECVersion;
  response[1] = 0x5;
  vc_cec_send_message(msg->initiator, response, 2, VC_TRUE);
}


static void
vc_cec_report_physicalAddress(uint8_t dest)
{
    uint8_t msg[4];
    msg[0] = CEC_Opcode_ReportPhysicalAddress;
    msg[1] = (uint8_t) ((physical_address) >> 8 & 0xff);
    msg[2] = (uint8_t) ((physical_address) >> 0 & 0xff);
    msg[3] = CEC_DeviceType_Tuner;
    vc_cec_send_message(CEC_BROADCAST_ADDR, msg, 4, VC_TRUE);
}

static void
send_deck_status(const VC_CEC_MESSAGE_T *msg)
{
  uint8_t response[2];
  response[0] = CEC_Opcode_DeckStatus;
  response[1] = CEC_DECK_INFO_NO_MEDIA;
  vc_cec_send_message(msg->initiator, response, 2, VC_TRUE);
}


static void
send_osd_name(const VC_CEC_MESSAGE_T *msg, const char *name)
{
  uint8_t response[15];
  int l = MIN(14, strlen(name));
  response[0] = CEC_Opcode_SetOSDName;
  memcpy(response + 1, name, l);
  vc_cec_send_message(msg->initiator, response, l+1, VC_TRUE);
}


static void
cec_callback(void *callback_data, uint32_t param0, uint32_t param1,
	     uint32_t param2, uint32_t param3, uint32_t param4)
{
  VC_CEC_NOTIFY_T reason  = (VC_CEC_NOTIFY_T) CEC_CB_REASON(param0);
  VC_CEC_MESSAGE_T msg;
  CEC_OPCODE_T opcode;

  uint32_t len     = CEC_CB_MSG_LENGTH(param0);
#if 1
  uint32_t retval  = CEC_CB_RC(param0);
  printf("cec_callback: debug: "
	 "reason=0x%04x, len=0x%02x, retval=0x%02x, "
	 "param1=0x%08x, param2=0x%08x, param3=0x%08x, param4=0x%08x\n",
	 reason, len, retval, param1, param2, param3, param4);
#endif


  msg.length = len - 1;
  msg.initiator = CEC_CB_INITIATOR(param1);
  msg.follower  = CEC_CB_FOLLOWER(param1);

  if(msg.length) {
    uint32_t tmp = param1 >> 8;
    memcpy(msg.payload,                      &tmp,    sizeof(uint32_t)-1);
    memcpy(msg.payload+sizeof(uint32_t)-1,   &param2, sizeof(uint32_t));
    memcpy(msg.payload+sizeof(uint32_t)*2-1, &param3, sizeof(uint32_t));
    memcpy(msg.payload+sizeof(uint32_t)*3-1, &param4, sizeof(uint32_t));
  } else {
    memset(msg.payload, 0, sizeof(msg.payload));
  }


  switch(reason) {
  default:
    break;
  case VC_CEC_BUTTON_PRESSED:
    cec_emit_key_down(msg.payload[1]);
    break;


  case VC_CEC_RX:

    opcode = CEC_CB_OPCODE(param1);
#if 1
    printf("opcode = %x (from:0x%x to:0x%x)\n", opcode,
	   CEC_CB_INITIATOR(param1), CEC_CB_FOLLOWER(param1));
#endif
    switch(opcode) {
    case CEC_Opcode_GiveDevicePowerStatus:
      give_device_power_status(&msg);
      break;

    case CEC_Opcode_GiveDeviceVendorID:
      give_device_vendor_id(&msg);
      break;

    case CEC_Opcode_SetStreamPath:
      SetStreamPath(&msg);
      break;

    case CEC_Opcode_GivePhysicalAddress:
      vc_cec_report_physicalAddress(msg.initiator);
      break;

    case CEC_Opcode_GiveOSDName:
      send_osd_name(&msg, "Showtime");
      break;

    case CEC_Opcode_GetCECVersion:
      send_cec_version(&msg);
      break;

    case CEC_Opcode_GiveDeckStatus:
      send_deck_status(&msg);
      break;

    default:
      //      printf("\nDon't know how to handle status code 0x%x\n\n", opcode);
      vc_cec_send_FeatureAbort(msg.initiator, opcode,
			       CEC_Abort_Reason_Unrecognised_Opcode);
      break;
    }
    break;
  }
}


/**
 *
 */
static void
tv_service_callback(void *callback_data, uint32_t reason,
		    uint32_t param1, uint32_t param2)
{
  TRACE(TRACE_INFO, "TV", "State change 0x%08x 0x%08x 0x%08x",
	reason, param1, param2);

  hts_mutex_lock(&display_mutex);

  if(reason & 1) {
    display_status = DISPLAY_STATUS_OFF;
  } else {
    display_status = DISPLAY_STATUS_ON;
  }

  hts_mutex_unlock(&display_mutex);

}


/**
 * We deal with CEC and HDMI events, etc here
 */
static void *
cec_thread(void *aux)
{
  TV_DISPLAY_STATE_T state;

  vc_tv_register_callback(tv_service_callback, NULL);
  vc_tv_get_display_state(&state);

  vc_cec_set_passive(1);

  vc_cec_register_callback(((CECSERVICE_CALLBACK_T) cec_callback), NULL);
  vc_cec_register_all();

 restart:
  while(1) {
    if(!vc_cec_get_physical_address(&physical_address) &&
       physical_address == 0xffff) {
    } else {
      TRACE(TRACE_DEBUG, "CEC",
	    "Got physical address 0x%04x\n", physical_address);
      break;
    }
    
    sleep(1);
  }


  const int addresses = 
    (1 << CEC_AllDevices_eRec1) |
    (1 << CEC_AllDevices_eRec2) |
    (1 << CEC_AllDevices_eRec3) |
    (1 << CEC_AllDevices_eFreeUse);

  for(logical_address = 0; logical_address < 15; logical_address++) {
    if(((1 << logical_address) & addresses) == 0)
      continue;
    if(vc_cec_poll_address(CEC_AllDevices_eRec1) > 0)
      break;
  }

  if(logical_address == 15) {
    printf("Unable to find a free logical address, retrying\n");
    sleep(1);
    goto restart;
  }

  vc_cec_set_logical_address(logical_address, CEC_DeviceType_Rec, myVendorId);

  while(1) {
    sleep(1);
  }

  vc_cec_set_logical_address(0xd, CEC_DeviceType_Rec, myVendorId);
  return NULL;
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

  while(runmode != RUNMODE_EXIT) {

    if(display_status == DISPLAY_STATUS_ON && runmode == RUNMODE_RUNNING) {
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


  hts_mutex_init(&display_mutex);
  hts_cond_init(&display_cond, &display_mutex);

  bcm_host_init();

  vc_tv_power_off();
  vc_tv_hdmi_power_on_preferred();

  hts_thread_create_detached("cec", cec_thread, NULL, THREAD_PRIO_BGTASK);

  omx_init();

  gconf.binary = argv[0];

  posix_init();

  parse_opts(argc, argv);

  gconf.concurrency = 1;

  trap_init();

  showtime_init();

  linux_init_monitors();

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

  rpi_mainloop();
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
  runmode = RUNMODE_EXIT;
  return 0;
}

