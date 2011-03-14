/*
 *  PS3 UI mainloop
 *  Copyright (C) 2011 Andreas Öman
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

#include <assert.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>

#include "glw.h"
#include "glw_video_common.h"

#include "showtime.h"
#include "settings.h"
#include "misc/extents.h"


#include <rsx/commands.h>
#include <rsx/nv40.h>
#include <rsx/reality.h>

#include <sysutil/video.h>
#include <sysutil/events.h>

#include <io/pad.h>
#include <io/kb.h>

#include <sysmodule/sysmodule.h>


#include "glw_rec.h"

typedef struct glw_ps3 {

  glw_root_t gr;

  int stop;
  
  VideoResolution res;

  u32 framebuffer[2];
  int framebuffer_pitch;

  u32 depthbuffer;
  int depthbuffer_pitch;

  char kb_present[MAX_KEYBOARDS];

  KbConfig kb_config[MAX_KEYBOARDS];

} glw_ps3_t;



static void
waitFlip()
{
  int i = 0;
  while(gcmGetFlipStatus() != 0) {
    i++;
    usleep(200);
    if(i == 10000) {
      TRACE(TRACE_ERROR, "GLW", "Flip never happend");
      exit(0);
    }
  }
  gcmResetFlipStatus();
}

static void
flip(glw_ps3_t *gp, s32 buffer) 
{
  assert(gcmSetFlip(gp->gr.gr_be.be_ctx, buffer) == 0);
  realityFlushBuffer(gp->gr.gr_be.be_ctx);
  gcmSetWaitFlip(gp->gr.gr_be.be_ctx);
}


/**
 *
 */
int
rsx_alloc(glw_root_t *gr, int size, int alignment)
{
  int pos;

  hts_mutex_lock(&gr->gr_be.be_mempool_lock);
  pos = extent_alloc_aligned(gr->gr_be.be_mempool,
			     (size + 15) >> 4, alignment >> 4);
  if(0)TRACE(TRACE_DEBUG, "RSXMEM", "Alloc %d bytes (%d align) -> 0x%x",
	size, alignment, pos << 4);
  hts_mutex_unlock(&gr->gr_be.be_mempool_lock);
  return pos << 4;
}


/**
 *
 */
void
rsx_free(glw_root_t *gr, int pos, int size)
{
  int r;

  hts_mutex_lock(&gr->gr_be.be_mempool_lock);
  r = extent_free(gr->gr_be.be_mempool, pos >> 4, (size + 15) >> 4);

  if(0)TRACE(TRACE_DEBUG, "RSXMEM", "Free %d + %d = %d", pos, size, r);

  if(r != 0) {
    TRACE(TRACE_ERROR, "GLX", "RSX memory corrupted, error %d", r);
  }

  hts_mutex_unlock(&gr->gr_be.be_mempool_lock);
}



/**
 *
 */
static void
init_screen(glw_ps3_t *gp)
{

  // Allocate a 1Mb buffer, alligned to a 1Mb boundary to be our shared IO memory with the RSX.
  void *host_addr = memalign(1024*1024, 1024*1024);
  assert(host_addr != NULL);

  // Initilise Reality, which sets up the command buffer and shared IO memory
  gp->gr.gr_be.be_ctx = realityInit(0x10000, 1024*1024, host_addr); 
  assert(gp->gr.gr_be.be_ctx != NULL);
  
  gcmConfiguration config;
  gcmGetConfiguration(&config);

  TRACE(TRACE_INFO, "RSX", "memory @ 0x%x size = %d\n",
	config.localAddress, config.localSize);

  hts_mutex_init(&gp->gr.gr_be.be_mempool_lock);
  gp->gr.gr_be.be_mempool = extent_create(0, config.localSize >> 4);
  gp->gr.gr_be.be_rsx_address = (void *)(uint64_t)config.localAddress;


  VideoState state;
  assert(videoGetState(0, 0, &state) == 0); // Get the state of the display
  assert(state.state == 0); // Make sure display is enabled
  
  // Get the current resolution
  assert(videoGetResolution(state.displayMode.resolution, &gp->res) == 0);
  
  TRACE(TRACE_INFO, "RSX", "Video resolution %d x %d",
	gp->res.width, gp->res.height);

  gp->framebuffer_pitch = 4 * gp->res.width; // each pixel is 4 bytes
  gp->depthbuffer_pitch = 4 * gp->res.width; // And each value in the depth buffer is a 16 bit float
  
  // Configure the buffer format to xRGB
  VideoConfiguration vconfig;
  memset(&vconfig, 0, sizeof(VideoConfiguration));
  vconfig.resolution = state.displayMode.resolution;
  vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
  vconfig.pitch = gp->framebuffer_pitch;

  assert(videoConfigure(0, &vconfig, NULL, 0) == 0);
  assert(videoGetState(0, 0, &state) == 0); 
  
  const s32 buffer_size = gp->framebuffer_pitch * gp->res.height; 
  const s32 depth_buffer_size = gp->depthbuffer_pitch * gp->res.height;
  TRACE(TRACE_INFO, "RSX", "Buffer will be %d bytes", buffer_size);
  
  gcmSetFlipMode(GCM_FLIP_VSYNC); // Wait for VSYNC to flip
  
  // Allocate two buffers for the RSX to draw to the screen (double buffering)
  gp->framebuffer[0] = rsx_alloc(&gp->gr, buffer_size, 16);
  gp->framebuffer[1] = rsx_alloc(&gp->gr, buffer_size, 16);

  TRACE(TRACE_INFO, "RSX", "Buffers at 0x%x 0x%x\n",
	gp->framebuffer[0], gp->framebuffer[1]);

  gp->depthbuffer = rsx_alloc(&gp->gr, depth_buffer_size * 4, 16);
  
  // Setup the display buffers
  gcmSetDisplayBuffer(0, gp->framebuffer[0],
		      gp->framebuffer_pitch, gp->res.width, gp->res.height);
  gcmSetDisplayBuffer(1, gp->framebuffer[1],
		      gp->framebuffer_pitch, gp->res.width, gp->res.height);

  gcmResetFlipStatus();
  flip(gp, 1);
}





/**
 *
 */
static int
glw_ps3_init(glw_ps3_t *gp)
{
  init_screen(gp);
  glw_rsx_init_context(&gp->gr);

  ioPadInit(7);
  ioKbInit(MAX_KB_PORT_NUM);
  return 0;
}


static void 
eventHandle(u64 status, u64 param, void *userdata) 
{
  glw_ps3_t *gp = userdata;
  switch(status) {
  case EVENT_REQUEST_EXITAPP:
    gp->stop = 1;
    break;
  case EVENT_MENU_OPEN:
    TRACE(TRACE_INFO, "XMB", "Opened");
    break;
  case EVENT_MENU_CLOSE:
    TRACE(TRACE_INFO, "XMB", "Closed");
    break;
  case EVENT_DRAWING_BEGIN:
    break;
  case EVENT_DRAWING_END:
    break;
  default:
    TRACE(TRACE_DEBUG, "LV2", "Unhandled event 0x%lx", status);
    break;
  }
}


static void
setupRenderTarget(glw_ps3_t *gp, u32 currentBuffer) 
{
  realitySetRenderSurface(gp->gr.gr_be.be_ctx,
			  REALITY_SURFACE_COLOR0, REALITY_RSX_MEMORY, 
			  gp->framebuffer[currentBuffer],
			  gp->framebuffer_pitch);
  
  realitySetRenderSurface(gp->gr.gr_be.be_ctx,
			  REALITY_SURFACE_ZETA, REALITY_RSX_MEMORY, 
			  gp->depthbuffer, gp->depthbuffer_pitch);

  realitySelectRenderTarget(gp->gr.gr_be.be_ctx, REALITY_TARGET_0, 
			    REALITY_TARGET_FORMAT_COLOR_X8R8G8B8 | 
			    REALITY_TARGET_FORMAT_ZETA_Z16 | 
			    REALITY_TARGET_FORMAT_TYPE_LINEAR,
			    gp->res.width, gp->res.height, 0, 0);

  realityDepthTestEnable(gp->gr.gr_be.be_ctx, 0);
  realityDepthWriteEnable(gp->gr.gr_be.be_ctx, 0);
}

static void 
drawFrame(glw_ps3_t *gp, int buffer) 
{
  gcmContextData *ctx = gp->gr.gr_be.be_ctx;

  realityViewportTranslate(ctx,
			   gp->res.width/2, gp->res.height/2, 0.0, 0.0);
  realityViewportScale(ctx,
		       gp->res.width/2, -gp->res.height/2, 1.0, 0.0); 

  realityZControl(ctx, 0, 1, 1); // disable viewport culling

  // Enable alpha blending.
  realityBlendFunc(ctx,
		   NV30_3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA |
		   NV30_3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
		   NV30_3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA |
		   NV30_3D_BLEND_FUNC_DST_ALPHA_ZERO);
  realityBlendEquation(ctx, NV40_3D_BLEND_EQUATION_RGB_FUNC_ADD |
		       NV40_3D_BLEND_EQUATION_ALPHA_FUNC_ADD);
  realityBlendEnable(ctx, 1);

  realityViewport(ctx, gp->res.width, gp->res.height);
  
  setupRenderTarget(gp, buffer);

  // set the clear color
  realitySetClearColor(ctx, 0x00000000);

  realitySetClearDepthValue(ctx, 0xffff);

  // Clear the buffers
  realityClearBuffers(ctx,
		      REALITY_CLEAR_BUFFERS_COLOR_R |
		      REALITY_CLEAR_BUFFERS_COLOR_G |
		      REALITY_CLEAR_BUFFERS_COLOR_B |
		      NV30_3D_CLEAR_BUFFERS_COLOR_A | 
		      REALITY_CLEAR_BUFFERS_DEPTH);


  // XMB may overwrite currently loaded shaders, so clear them out
  gp->gr.gr_be.be_vp_current = NULL;
  gp->gr.gr_be.be_fp_current = NULL;

  realityCullEnable(ctx, REALITY_CULL_FACE_BACK);
  realityFrontFace(ctx, REALITY_FRONT_FACE_CW);
  realityCullFace(ctx, REALITY_FRONT_FACE_CCW);

  glw_lock(&gp->gr);
  glw_prepare_frame(&gp->gr, 0);

  gp->gr.gr_width = gp->res.width;
  gp->gr.gr_height = gp->res.height;

  glw_rctx_t rc;
  glw_rctx_init(&rc, gp->gr.gr_width, gp->gr.gr_height);
  glw_layout0(gp->gr.gr_universe, &rc);
  glw_render0(gp->gr.gr_universe, &rc);
  glw_unlock(&gp->gr);
}



/**
 *
 */
typedef struct krepeat {
  int held_frames;
} krepeat_t;


static krepeat_t k_left, k_right, k_up, k_down;
static krepeat_t k_enter, k_back, k_menu;

static void
btn(glw_ps3_t *gp, krepeat_t *kr, int pressed, action_type_t ac)
{
  event_t *e;

  if(ac == ACTION_NONE)
    return;

  if(pressed) {

    if(kr->held_frames == 0 ||
       (kr->held_frames > 30 && (kr->held_frames % 3 == 0))) {
      e = event_create_action(ac);
      glw_dispatch_event(&gp->gr.gr_uii, e);
      event_release(e);
    }
    kr->held_frames++;
  } else {
    kr->held_frames = 0;
  }
}


/**
 *
 */
static void
handle_pads(glw_ps3_t *gp)
{
  PadInfo padinfo;
  PadData paddata;
  int i;

  // Check the pads.
  ioPadGetInfo(&padinfo);
  for(i=0; i<MAX_PADS; i++){
    if(!padinfo.status[i])
      continue;
    ioPadGetData(i, &paddata);

    if(paddata.BTN_SQUARE || paddata.BTN_START)
      gp->stop = 1;

    btn(gp, &k_left,  paddata.BTN_LEFT,     ACTION_LEFT);
    btn(gp, &k_up,    paddata.BTN_UP,       ACTION_UP);
    btn(gp, &k_right, paddata.BTN_RIGHT,    ACTION_RIGHT);
    btn(gp, &k_down,  paddata.BTN_DOWN,     ACTION_DOWN);
    btn(gp, &k_enter, paddata.BTN_CROSS,    ACTION_ACTIVATE);
    btn(gp, &k_back,  paddata.BTN_CIRCLE,   ACTION_NAV_BACK);
    btn(gp, &k_menu,  paddata.BTN_TRIANGLE, ACTION_MENU);
  }
}


#define KB_SHIFTMASK 0x1
#define KB_ALTMASK   0x2
#define KB_CTRLMASK  0x4

/**
 *
 */
static const struct {
  int code;
  int modifier;
  const char *sym;
  int action1;
  int action2;
  int action3;
} kb2action[] = {

  { KB_RAWDAT|KB_RAWKEY_LEFT_ARROW,         0,      NULL, ACTION_LEFT },
  { KB_RAWDAT|KB_RAWKEY_RIGHT_ARROW,        0,      NULL, ACTION_RIGHT },
  { KB_RAWDAT|KB_RAWKEY_UP_ARROW,           0,      NULL, ACTION_UP },
  { KB_RAWDAT|KB_RAWKEY_DOWN_ARROW,         0,      NULL, ACTION_DOWN },

  { 9,     0,             NULL, ACTION_FOCUS_NEXT },
  { 9,     KB_SHIFTMASK,  NULL, ACTION_FOCUS_PREV },
  { 8,     0,             NULL, ACTION_BS, ACTION_NAV_BACK },
  { 10,    0,             NULL, ACTION_ACTIVATE, ACTION_ENTER },
  { 27,    0,             NULL, ACTION_CANCEL },

  { KB_RAWDAT|KB_RAWKEY_F1, -1,   "F1" },
  { KB_RAWDAT|KB_RAWKEY_F2, -1,   "F2" },
  { KB_RAWDAT|KB_RAWKEY_F3, -1,   "F3" },
  { KB_RAWDAT|KB_RAWKEY_F4, -1,   "F4" },
  { KB_RAWDAT|KB_RAWKEY_F5, -1,   "F5" },
  { KB_RAWDAT|KB_RAWKEY_F6, -1,   "F6" },
  { KB_RAWDAT|KB_RAWKEY_F7, -1,   "F7" },
  { KB_RAWDAT|KB_RAWKEY_F8, -1,   "F8" },
  { KB_RAWDAT|KB_RAWKEY_F9, -1,   "F9" },
  { KB_RAWDAT|KB_RAWKEY_F10, -1,   "F10" },
  { KB_RAWDAT|KB_RAWKEY_F11, -1,   "F11" },
  { KB_RAWDAT|KB_RAWKEY_F12, -1,   "F12" },

  { KB_RAWDAT|KB_RAWKEY_PAGE_UP,   -1,   "Prior" },
  { KB_RAWDAT|KB_RAWKEY_PAGE_DOWN, -1,   "Next" },
  { KB_RAWDAT|KB_RAWKEY_HOME,      -1,   "Home" },
  { KB_RAWDAT|KB_RAWKEY_END,       -1,   "End" },

  { KB_RAWDAT|KB_RAWKEY_LEFT_ARROW,  -1,   "Left" },
  { KB_RAWDAT|KB_RAWKEY_RIGHT_ARROW, -1,   "Right" },
  { KB_RAWDAT|KB_RAWKEY_UP_ARROW,    -1,   "Up" },
  { KB_RAWDAT|KB_RAWKEY_DOWN_ARROW,  -1,   "Down" },
};


/**
 *
 */
static void
handle_kb(glw_ps3_t *gp)
{
  KbInfo kbinfo;
  KbData kbdata;
  int i, j;
  int uc;
  event_t *e;
  action_type_t av[3];
  int mods;

  if(ioKbGetInfo(&kbinfo))
    return;

  for(i=0; i<MAX_KEYBOARDS; i++) {
    if(kbinfo.status[i] == 0) {
      if(gp->kb_present[i])
	TRACE(TRACE_INFO, "PS3", "Keyboard %d disconnected", i);

    } else {
      if(!gp->kb_present[i]) {

	ioKbGetConfiguration(i, &gp->kb_config[i]);

	TRACE(TRACE_INFO, "PS3",
	      "Keyboard %d connected, mapping=%d, rmode=%d, codetype=%d",
	      i, gp->kb_config[i].mapping, gp->kb_config[i].rmode,
	      gp->kb_config[i].codetype);

	ioKbSetCodeType(i, KB_CODETYPE_RAW);
      }

      if(!ioKbRead(i, &kbdata)) {
	for(j = 0; j < kbdata.nb_keycode; j++) {

	  if(0) TRACE(TRACE_DEBUG, "PS3", "Keystrike %x %x %x %x",
		      gp->kb_config[i].mapping,
		      kbdata.mkey.mkeys,
		      kbdata.led.leds,
		      kbdata.keycode[j]);

	  uc = ioKbCnvRawCode(gp->kb_config[i].mapping, kbdata.mkey,
			      kbdata.led, kbdata.keycode[j]);

	  mods = 0;
	  if(kbdata.mkey.l_shift || kbdata.mkey.r_shift)
	    mods |= KB_SHIFTMASK;
	  if(kbdata.mkey.l_alt || kbdata.mkey.r_alt)
	    mods |= KB_ALTMASK;
	  if(kbdata.mkey.l_ctrl || kbdata.mkey.r_ctrl)
	    mods |= KB_CTRLMASK;

	  for(i = 0; i < sizeof(kb2action) / sizeof(*kb2action); i++) {
	    if(kb2action[i].code == uc &&
	       (kb2action[i].modifier == -1 || kb2action[i].modifier == mods)) {

	      av[0] = kb2action[i].action1;
	      av[1] = kb2action[i].action2;
	      av[2] = kb2action[i].action3;

	      if(kb2action[i].action3 != ACTION_NONE)
		e = event_create_action_multi(av, 3);
	      else if(kb2action[i].action2 != ACTION_NONE)
		e = event_create_action_multi(av, 2);
	      else if(kb2action[i].action1 != ACTION_NONE)
		e = event_create_action_multi(av, 1);
	      else if(kb2action[i].sym != NULL) {
		char buf[128];

		snprintf(buf, sizeof(buf),
			 "%s%s%s%s",
			 mods & KB_SHIFTMASK   ? "Shift+" : "",
			 mods & KB_ALTMASK     ? "Alt+"   : "",
			 mods & KB_CTRLMASK    ? "Ctrl+"  : "",
			 kb2action[i].sym);
		e = event_create_str(EVENT_KEYDESC, buf);
	      } else {
		e = NULL;
	      }

	      if(e != NULL) {
		glw_dispatch_event(&gp->gr.gr_uii, e);
		event_release(e);
		break;
	      }
	    }
	  }

	  if(i == sizeof(kb2action) / sizeof(*kb2action) && uc < 0x8000 && uc) {
	    e = event_create_int(EVENT_UNICODE, uc);
	    glw_dispatch_event(&gp->gr.gr_uii, e);
	    event_release(e);
	  }
	}
      }
    }
    gp->kb_present[i] = kbinfo.status[i];
  }

}


/**
 *
 */
static void
glw_ps3_mainloop(glw_ps3_t *gp)
{
  int currentBuffer = 0;

  TRACE(TRACE_INFO, "GLW", "Entering mainloop");

  sysRegisterCallback(EVENT_SLOT0, eventHandle, gp);
  while(!gp->stop) {

    handle_pads(gp);
    handle_kb(gp);


    waitFlip();
    drawFrame(gp, currentBuffer);
    flip(gp, currentBuffer);
    currentBuffer = !currentBuffer;
    sysCheckCallback();
  }
  sysUnregisterCallback(EVENT_SLOT0);
}


/**
 *
 */
static int
glw_ps3_start(ui_t *ui, int argc, char *argv[], int primary)
{
  glw_ps3_t *gp = calloc(1, sizeof(glw_ps3_t));
  char confname[PATH_MAX];
  const char *theme_path = SHOWTIME_GLW_DEFAULT_THEME_URL;
  const char *displayname_title  = NULL;
  const char *skin = NULL;

  snprintf(confname, sizeof(confname), "glw/ps3");

  /* Parse options */

  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "--theme") && argc > 1) {
      theme_path = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--skin") && argc > 1) {
      skin = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else {
      break;
    }
  }


  if(glw_ps3_init(gp))
     return 1;

  glw_root_t *gr = &gp->gr;
  
  if(glw_init(gr, theme_path, skin, ui, primary, confname, displayname_title))
    return 1;

  TRACE(TRACE_INFO, "GLW", "loading universe");

  glw_load_universe(gr);
  glw_ps3_mainloop(gp);
  glw_unload_universe(gr);
  glw_reap(gr);
  glw_reap(gr);
  return 0;
}


/**
 *
 */
static void
glw_ps3_dispatch_event(uii_t *uii, event_t *e)
{
  /* Pass it on to GLW */
  glw_dispatch_event(uii, e);
  event_release(e);
}


/**
 *
 */
static void
glw_ps3_stop(uii_t *uii)
{
  glw_ps3_t *gp = (glw_ps3_t *)uii;
  gp->stop = 1;
}


/**
 *
 */
ui_t glw_ui = {
  .ui_title = "glw",
  .ui_start = glw_ps3_start,
  .ui_dispatch_event = glw_ps3_dispatch_event,
  .ui_stop = glw_ps3_stop,
};

