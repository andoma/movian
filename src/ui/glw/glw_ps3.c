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
#include <assert.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>

#include "settings.h"

#include "glw.h"
#include "glw_settings.h"
#include "glw_video_common.h"

#include "main.h"
#include "settings.h"
#include "misc/extents.h"
#include "misc/str.h"
#include "navigator.h"
#include "arch/arch.h"

#include <psl1ght/lv2.h>
#include <rsx/commands.h>
#include <rsx/nv40.h>
#include <rsx/reality.h>

#include <psl1ght/lv2/memory.h>

#include <sysutil/video.h>
#include <sysutil/events.h>

#include <io/pad.h>
#include <io/kb.h>
#include <io/osk.h>

#include <sysmodule/sysmodule.h>


#include "glw_rec.h"

typedef struct glw_ps3 {

  glw_root_t gr;

  float gp_browser_alpha;

  int gp_stop;
  int gp_seekmode;

  VideoResolution res;

  u32 framebuffer[2];
  int framebuffer_pitch;

  u32 depthbuffer;
  int depthbuffer_pitch;

  char kb_present[MAX_KEYBOARDS];

  KbConfig kb_config[MAX_KEYBOARDS];

  float scale;
  int button_assign;

  struct glw *osk_widget;
  mem_container_t osk_container;

} glw_ps3_t;

glw_ps3_t *glwps3;
char *rsx_address;
static struct extent_pool *rsx_mempool;
static hts_mutex_t rsx_mempool_lock;
static void clear_btns(void);



extern s32 ioPadGetDataExtra(u32 port, u32* type, PadData* data);

static void
waitFlip()
{
  int i = 0;
  while(gcmGetFlipStatus() != 0) {
    i++;
    usleep(200);
    if(i == 10000) {
      TRACE(TRACE_ERROR, "GLW", "Flip never happend, system reboot");
      Lv2Syscall3(379, 0x1200, 0, 0 );
      gcmResetFlipStatus();
    }
  }
  gcmResetFlipStatus();
}

static void
flip(glw_ps3_t *gp, s32 buffer) 
{
  gcmSetFlip(gp->gr.gr_be.be_ctx, buffer);
  realityFlushBuffer(gp->gr.gr_be.be_ctx);
  gcmSetWaitFlip(gp->gr.gr_be.be_ctx);
}


/**
 *
 */
int
rsx_alloc(int size, int alignment)
{
  int pos;

  hts_mutex_lock(&rsx_mempool_lock);
  pos = extent_alloc_aligned(rsx_mempool, (size + 15) >> 4, alignment >> 4);
  if(0)TRACE(TRACE_DEBUG, "RSXMEM", "Alloc %d bytes (%d align) -> 0x%x",
	size, alignment, pos << 4);

  if(pos == -1) {
    int total, avail, fragments;
    extent_stats(rsx_mempool, &total, &avail, &fragments);
    TRACE(TRACE_ERROR, "RSX",
          "Low memory condition. Available %d of %d in %d fragments",
          avail * 16, total * 16, fragments);
  }

  hts_mutex_unlock(&rsx_mempool_lock);
  return pos == -1 ? -1 : pos << 4;
}


/**
 *
 */
void
rsx_free(int pos, int size)
{
  int r;

  hts_mutex_lock(&rsx_mempool_lock);
  r = extent_free(rsx_mempool, pos >> 4, (size + 15) >> 4);

  if(0)TRACE(TRACE_DEBUG, "RSXMEM", "Free %d + %d = %d", pos, size, r);

  if(r != 0)
    panic("RSX memory corrupted, error %d", r);

  hts_mutex_unlock(&rsx_mempool_lock);
}


/**
 *
 */
static pixmap_t *
rsx_read_pixels(glw_root_t *gr)
{
  glw_ps3_t *gp = (glw_ps3_t *)gr;

  pixmap_t *pm = pixmap_create(gr->gr_width, gr->gr_height, PIXMAP_RGBA, 0);

  memcpy(pm->pm_data, rsx_to_ppu(gp->framebuffer[0]),
         pm->pm_linesize * pm->pm_height);
  return pm;
}


/**
 *
 */
static void
init_screen(glw_ps3_t *gp)
{

  // Allocate a 1Mb buffer, alligned to a 1Mb boundary to be our shared IO memory with the RSX.
  u32 taddr;
  Lv2Syscall3(348, 1024*1024, 0x400, (u64)&taddr);
  void *host_addr = (void *)(uint64_t)taddr;
  assert(host_addr != NULL);

  // Initilise Reality, which sets up the command buffer and shared IO memory
  gp->gr.gr_be.be_ctx = realityInit(0x10000, 1024*1024, host_addr); 
  assert(gp->gr.gr_be.be_ctx != NULL);
  
  gcmConfiguration config;
  gcmGetConfiguration(&config);

  TRACE(TRACE_DEBUG, "RSX", "memory @ 0x%x size = %d\n",
	config.localAddress, config.localSize);

  hts_mutex_init(&rsx_mempool_lock);
  rsx_mempool = extent_create(0, config.localSize >> 4);
  rsx_address = (void *)(uint64_t)config.localAddress;


  VideoState state;
  videoGetState(0, 0, &state);
  
  // Get the current resolution
  videoGetResolution(state.displayMode.resolution, &gp->res);
  
  int num = gp->res.width;
  int den = gp->res.height;
  
  switch(state.displayMode.aspect) {
  case VIDEO_ASPECT_4_3:
    num = 4; den = 3;
    break;
  case VIDEO_ASPECT_16_9:
    num = 16; den = 9;
    break;
  }

  gp->scale = (float)(num * gp->res.height) / (float)(den * gp->res.width);

  TRACE(TRACE_DEBUG, "RSX",
	"Video resolution %d x %d  aspect=%d, pixel wscale=%f",
	gp->res.width, gp->res.height, state.displayMode.aspect, gp->scale);


  gp->framebuffer_pitch = 4 * gp->res.width; // each pixel is 4 bytes
  gp->depthbuffer_pitch = 4 * gp->res.width; // And each value in the depth buffer is a 16 bit float
  
  // Configure the buffer format to xRGB
  VideoConfiguration vconfig;
  memset(&vconfig, 0, sizeof(VideoConfiguration));
  vconfig.resolution = state.displayMode.resolution;
  vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
  vconfig.pitch = gp->framebuffer_pitch;

  videoConfigure(0, &vconfig, NULL, 0);
  videoGetState(0, 0, &state);
  
  const s32 buffer_size = gp->framebuffer_pitch * gp->res.height; 
  const s32 depth_buffer_size = gp->depthbuffer_pitch * gp->res.height;
  TRACE(TRACE_DEBUG, "RSX", "Buffer will be %d bytes", buffer_size);
  
  gcmSetFlipMode(GCM_FLIP_VSYNC); // Wait for VSYNC to flip
  
  // Allocate two buffers for the RSX to draw to the screen (double buffering)
  gp->framebuffer[0] = rsx_alloc(buffer_size, 16);
  gp->framebuffer[1] = rsx_alloc(buffer_size, 16);

  TRACE(TRACE_DEBUG, "RSX", "Buffers at 0x%x 0x%x\n",
	gp->framebuffer[0], gp->framebuffer[1]);

  gp->depthbuffer = rsx_alloc(depth_buffer_size * 4, 16);
  
  // Setup the display buffers
  gcmSetDisplayBuffer(0, gp->framebuffer[0],
		      gp->framebuffer_pitch, gp->res.width, gp->res.height);
  gcmSetDisplayBuffer(1, gp->framebuffer[1],
		      gp->framebuffer_pitch, gp->res.width, gp->res.height);

  gp->gr.gr_br_read_pixels = rsx_read_pixels;

  gcmResetFlipStatus();
  flip(gp, 1);
}




extern int sysUtilGetSystemParamInt(int, int *);

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

  int i;
  for(i = 0; i < 7; i++)
    ioPadSetPortSetting(i, 0x2);

  if(sysUtilGetSystemParamInt(0x112, &gp->button_assign))
    gp->button_assign = 1;

  return 0;
}


/**
 *
 */
static void
osk_returned(glw_ps3_t *gp)
{
  oskCallbackReturnParam param = {0};
  uint8_t ret[512];
  uint8_t buf[512];
  
  if(param.result != 0)
    return;

  assert(gp->osk_widget != NULL);

  param.length = sizeof(ret)/2;
  param.str_addr = (intptr_t)ret;
  oskUnloadAsync(&param);

  ucs2_to_utf8(buf, sizeof(buf), ret, sizeof(ret), 0);

  glw_lock(&gp->gr);

  glw_t *w = gp->osk_widget;
  if(!(w->glw_flags & GLW_DESTROYING) && w->glw_class->gc_update_text) {
    w->glw_class->gc_update_text(w, (const char *)buf);
  }
  glw_osk_close(&gp->gr);
  glw_unlock(&gp->gr);
}


/**
 *
 */
static void
osk_destroyed(glw_ps3_t *gp)
{
  glw_t *w = gp->osk_widget;
  assert(w != NULL);

  if(!(w->glw_flags & GLW_DESTROYING)) {
    event_t *e = event_create_action(ACTION_SUBMIT);
    glw_event_to_widget(w, e);
    event_release(e);
  }
  glw_unref(w);
  gp->osk_widget = NULL;

  if(gp->osk_container != 0xFFFFFFFFU)
    lv2MemContinerDestroy(gp->osk_container);
}

/**
 *
 */
static void
osk_open(glw_root_t *gr, const char *title, const char *input, glw_t *w,
	 int password)
{
  oskParam param = {0};
  oskInputFieldInfo ifi = {0};
  glw_ps3_t *gp = (glw_ps3_t *)gr;
  
  if(gp->osk_widget)
    return;

  if(title == NULL)
    title = "";

  if(input == NULL)
    input = "";

  void *title16;
  void *input16;

  size_t s;
  s = utf8_to_ucs2(NULL, title, 0);
  title16 = malloc(s);
  utf8_to_ucs2(title16, title, 0);

  s = utf8_to_ucs2(NULL, input, 0);
  input16 = malloc(s);
  utf8_to_ucs2(input16, input, 0);

  param.firstViewPanel = password ? OSK_PANEL_TYPE_PASSWORD :
    OSK_PANEL_TYPE_DEFAULT;
  param.allowedPanels = param.firstViewPanel;
  param.prohibitFlags = OSK_PROHIBIT_RETURN;

  ifi.message_addr = (intptr_t)title16;
  ifi.startText_addr = (intptr_t)input16;
  ifi.maxLength = 256;

  if(lv2MemContinerCreate(&gp->osk_container, 2 * 1024 * 1024))
    gp->osk_container = 0xFFFFFFFFU;

  oskSetKeyLayoutOption(3);

  int ret = oskLoadAsync(gp->osk_container, &param, &ifi);

  if(!ret) {
    gp->osk_widget = w;
    glw_ref(w);
  }

  free(title16);
  free(input16);
}


/**
 *
 */
static void 
eventHandle(u64 status, u64 param, void *userdata) 
{
  glw_ps3_t *gp = userdata;
  switch(status) {
  case 0x11:
    TRACE(TRACE_INFO, "XMB", "Got close request from XMB");
    break;
  case EVENT_REQUEST_EXITAPP:
    gp->gp_stop = 1;
    break;
  case EVENT_MENU_OPEN:
    TRACE(TRACE_INFO, "XMB", "Opened");
    media_global_hold(1, MP_HOLD_OS);
    break;
  case EVENT_MENU_CLOSE:
    TRACE(TRACE_INFO, "XMB", "Closed");
    media_global_hold(0, MP_HOLD_OS);
    break;
  case EVENT_DRAWING_BEGIN:
    break;
  case EVENT_DRAWING_END:
    break;
  case EVENT_OSK_LOADED: 
    TRACE(TRACE_DEBUG, "OSK", "Loaded");
    break;
  case EVENT_OSK_FINISHED: 
    TRACE(TRACE_DEBUG, "OSK", "Finished");
    osk_returned(gp);
    break;
  case EVENT_OSK_UNLOADED:
    TRACE(TRACE_DEBUG, "OSK", "Unloaded");
    osk_destroyed(gp);
    break;
  case EVENT_OSK_INPUT_CANCELED:
    TRACE(TRACE_DEBUG, "OSK", "Input canceled");
    oskAbort();
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
drawFrame(glw_ps3_t *gp, int buffer, int with_universe)
{
  extern int browser_visible;

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

  realityCullEnable(ctx, 1);
  realityFrontFace(ctx, REALITY_FRONT_FACE_CCW);
  realityCullFace(ctx, REALITY_CULL_FACE_BACK);

  if(!with_universe)
    return;

  glw_lock(&gp->gr);
  glw_prepare_frame(&gp->gr, 0);

  gp->gr.gr_width = gp->res.width;
  gp->gr.gr_height = gp->res.height;

  glw_rctx_t rc;
  int zmax = 0;
  glw_rctx_init(&rc, gp->gr.gr_width * gp->scale, gp->gr.gr_height, 1, &zmax);

  glw_lp(&gp->gp_browser_alpha, &gp->gr, browser_visible, 0.1);

  rc.rc_alpha = 1 - gp->gp_stop * 0.1 - gp->gp_browser_alpha * 0.8;

  glw_layout0(gp->gr.gr_universe, &rc);
  glw_render0(gp->gr.gr_universe, &rc);
  glw_unlock(&gp->gr);
  glw_post_scene(&gp->gr);
}


/**
 *
 */
typedef enum {
  BTN_LEFT = 1,
  BTN_UP,
  BTN_RIGHT,
  BTN_DOWN,
  BTN_CROSS,
  BTN_CIRCLE,
  BTN_TRIANGLE,
  BTN_SQUARE,
  BTN_L1,
  BTN_L2,
  BTN_L3,
  BTN_R1,
  BTN_R2,
  BTN_R3,
  BTN_START,
  BTN_KEY_1,
  BTN_KEY_2,
  BTN_KEY_3,
  BTN_KEY_4,
  BTN_KEY_5,
  BTN_KEY_6,
  BTN_KEY_7,
  BTN_KEY_8,
  BTN_KEY_9,
  BTN_KEY_0,
  BTN_ENTER,
  BTN_RETURN,
  BTN_CLEAR,
  BTN_EJECT,
  BTN_TOPMENU,
  BTN_TIME,
  BTN_PREV,
  BTN_NEXT,
  BTN_PLAY,
  BTN_SCAN_REV,
  BTN_SCAN_FWD,
  BTN_STOP,
  BTN_PAUSE,
  BTN_POPUP_MENU,
  BTN_SLOW_REV,
  BTN_SLOW_FWD,
  BTN_SUBTITLE,
  BTN_AUDIO,
  BTN_ANGLE,
  BTN_DISPLAY,
  BTN_BLUE,
  BTN_RED,
  BTN_GREEN,
  BTN_YELLOW,
  BTN_max
} buttoncode_t;

#define AVEC(x...) (const action_type_t []){x, ACTION_NONE}
const static action_type_t *btn_to_action[BTN_max] = {
  [BTN_LEFT]       = AVEC(ACTION_LEFT),
  [BTN_UP]         = AVEC(ACTION_UP),
  [BTN_RIGHT]      = AVEC(ACTION_RIGHT),
  [BTN_DOWN]       = AVEC(ACTION_DOWN),
  [BTN_CROSS]      = AVEC(ACTION_ACTIVATE),
  [BTN_CIRCLE]     = AVEC(ACTION_NAV_BACK),
  [BTN_TRIANGLE]   = AVEC(ACTION_MENU),
  [BTN_SQUARE]     = AVEC(ACTION_ITEMMENU, ACTION_SHOW_MEDIA_STATS),

  [BTN_L1]         = AVEC(ACTION_SKIP_BACKWARD),
  [BTN_R1]         = AVEC(ACTION_SKIP_FORWARD),

  [BTN_L3]         = AVEC(ACTION_SYSINFO),
  [BTN_R3]         = AVEC(ACTION_LOGWINDOW),
  [BTN_START]      = AVEC(ACTION_PLAYPAUSE),
  [BTN_ENTER]      = AVEC(ACTION_ACTIVATE, ACTION_ENTER),
  [BTN_RETURN]     = AVEC(ACTION_NAV_BACK),
  [BTN_EJECT]      = AVEC(ACTION_EJECT),
  [BTN_TOPMENU]    = AVEC(ACTION_HOME),
  [BTN_PREV]       = AVEC(ACTION_SKIP_BACKWARD),
  [BTN_NEXT]       = AVEC(ACTION_SKIP_FORWARD),
  [BTN_PLAY]       = AVEC(ACTION_PLAY),
  [BTN_SCAN_REV]   = AVEC(ACTION_SEEK_BACKWARD),
  [BTN_SCAN_FWD]   = AVEC(ACTION_SEEK_FORWARD),
  [BTN_STOP]       = AVEC(ACTION_STOP),
  [BTN_PAUSE]      = AVEC(ACTION_PAUSE),
  [BTN_POPUP_MENU] = AVEC(ACTION_MENU),
  [BTN_SUBTITLE]   = AVEC(ACTION_CYCLE_SUBTITLE),
  [BTN_AUDIO]      = AVEC(ACTION_CYCLE_AUDIO),
};


const static action_type_t *btn_to_action_sel[BTN_max] = {
  [BTN_LEFT]       = AVEC(ACTION_MOVE_LEFT),
  [BTN_UP]         = AVEC(ACTION_MOVE_UP),
  [BTN_RIGHT]      = AVEC(ACTION_MOVE_RIGHT),
  [BTN_DOWN]       = AVEC(ACTION_MOVE_DOWN),
  [BTN_TRIANGLE]   = AVEC(ACTION_SWITCH_VIEW),
  [BTN_CIRCLE]     = AVEC(ACTION_STOP),
  [BTN_START]      = AVEC(ACTION_PLAYQUEUE),
  [BTN_SQUARE]     = AVEC(ACTION_ENABLE_SCREENSAVER),
};




static int16_t button_counter[MAX_PADS][BTN_max];
static PadData paddata[MAX_PADS];


static void
clear_btns(void)
{
  memset(button_counter, 0, sizeof(button_counter));
}

#define KEY_REPEAT_DELAY 30 // in frames
#define KEY_REPEAT_RATE  3  // in frames


static void
handle_seek(glw_ps3_t *gp, int pad, int sign, int pressed, int pre)
{
  if(pressed && pre > 10) {
    event_t *e = event_create_int3(EVENT_DELTA_SEEK_REL, pre, sign, 
				   gp->gr.gr_framerate);
    event_dispatch(e);
  }
}



static void
handle_btn(glw_ps3_t *gp, int pad, int code, int pressed, int sel, int pre)
{
  if(gp->button_assign == 0) {
    // Swap X and O
    if(code == BTN_CROSS)
      code = BTN_CIRCLE;
    else if(code == BTN_CIRCLE)
      code = BTN_CROSS;
  }

  int16_t *store = &button_counter[pad][code];
  int rate = KEY_REPEAT_RATE;
  int xrep = 0;
  if(code == 0)
    return;
  
  if(pressed) {

    if(pre > 200 && *store > KEY_REPEAT_DELAY)
      xrep = 1;
    if(pre > 150)
      rate = 1;
    else if(pre > 100)
      rate = 2;

    if(*store == 0 ||
       (*store > KEY_REPEAT_DELAY && (*store % rate == 0))) {
      int uc = 0;
      event_t *e = NULL;

      if(code >= BTN_KEY_1 && code <= BTN_KEY_9) {
	uc = code - BTN_KEY_1 + '1';
      } else if(code == BTN_KEY_0) {
	uc = '0';
      }
      
      if(uc != 0)
	e = event_create_int(EVENT_UNICODE, uc);

      if(e == NULL) {
	const action_type_t *avec =
	  sel ? btn_to_action_sel[code] : btn_to_action[code];
	if(avec) {
	  int i = 0;
	  while(avec[i] != 0)
	    i++;
	  e = event_create_action_multi(avec, i);
	}
      }

      if(e != NULL) {
        e->e_flags |= EVENT_KEYPRESS;
	event_addref(e);
	event_to_ui(e);

	if(xrep) {
	  event_addref(e);
	  event_to_ui(e);
	}

	event_release(e);
      }
    }
    (*store)++;
  } else {
    *store = 0;
  }
}


typedef enum _io_pad_bd_code
{
       BTN_BD_KEY_1           = 0x00,
       BTN_BD_KEY_2           = 0x01,
       BTN_BD_KEY_3           = 0x02,
       BTN_BD_KEY_4           = 0x03,
       BTN_BD_KEY_5           = 0x04,
       BTN_BD_KEY_6           = 0x05,
       BTN_BD_KEY_7           = 0x06,
       BTN_BD_KEY_8           = 0x07,
       BTN_BD_KEY_9           = 0x08,
       BTN_BD_KEY_0           = 0x09,
       BTN_BD_ENTER           = 0x0b,
       BTN_BD_RETURN          = 0x0e,
       BTN_BD_CLEAR           = 0x0f,
       BTN_BD_EJECT           = 0x16,
       BTN_BD_TOPMENU         = 0x1a,
       BTN_BD_TIME            = 0x28,
       BTN_BD_PREV            = 0x30,
       BTN_BD_NEXT            = 0x31,
       BTN_BD_PLAY            = 0x32,
       BTN_BD_SCAN_REV        = 0x33,
       BTN_BD_SCAN_FWD        = 0x34,
       BTN_BD_STOP            = 0x38,
       BTN_BD_PAUSE           = 0x39,
       BTN_BD_POPUP_MENU      = 0x40,
       BTN_BD_SELECT          = 0x50,
       BTN_BD_L3              = 0x51,
       BTN_BD_R3              = 0x52,
       BTN_BD_START           = 0x53,
       BTN_BD_UP              = 0x54,
       BTN_BD_RIGHT           = 0x55,
       BTN_BD_DOWN            = 0x56,
       BTN_BD_LEFT            = 0x57,
       BTN_BD_L2              = 0x58,
       BTN_BD_R2              = 0x59,
       BTN_BD_L1              = 0x5a,
       BTN_BD_R1              = 0x5b,
       BTN_BD_TRIANGLE        = 0x5c,
       BTN_BD_CIRCLE          = 0x5d,
       BTN_BD_CROSS           = 0x5e,
       BTN_BD_SQUARE          = 0x5f,
       BTN_BD_SLOW_REV        = 0x60,
       BTN_BD_SLOW_FWD        = 0x61,
       BTN_BD_SUBTITLE        = 0x63,
       BTN_BD_AUDIO           = 0x64,
       BTN_BD_ANGLE           = 0x65,
       BTN_BD_DISPLAY         = 0x70,
       BTN_BD_BLUE            = 0x80,
       BTN_BD_RED             = 0x81,
       BTN_BD_GREEN           = 0x82,
       BTN_BD_YELLOW          = 0x83,
       BTN_BD_RELEASE         = 0xff,

       /* TV controller */
       BTN_BD_NUMBER_11       = 0x101e,
       BTN_BD_NUMBER_12       = 0x101f,
       BTN_BD_NUMBER_PERIOD   = 0x102a,
       BTN_BD_PROGRAM_UP      = 0x1030,
       BTN_BD_PROGRAM_DOWN    = 0x1031,
       BTN_BD_PREV_CHANNEL    = 0x1032,
       BTN_BD_PROGRAM_GUIDE   = 0x1053
} ioPadBdCode;


static const uint8_t bd_to_local_map[256] = {
  [BTN_BD_KEY_1] = BTN_KEY_1,
  [BTN_BD_KEY_2] = BTN_KEY_2,
  [BTN_BD_KEY_3] = BTN_KEY_3,
  [BTN_BD_KEY_4] = BTN_KEY_4,
  [BTN_BD_KEY_5] = BTN_KEY_5,
  [BTN_BD_KEY_6] = BTN_KEY_6,
  [BTN_BD_KEY_7] = BTN_KEY_7,
  [BTN_BD_KEY_8] = BTN_KEY_8,
  [BTN_BD_KEY_9] = BTN_KEY_9,
  [BTN_BD_KEY_0] = BTN_KEY_0,
  [BTN_BD_ENTER] = BTN_ENTER,
  [BTN_BD_RETURN] = BTN_RETURN,
  [BTN_BD_CLEAR] = BTN_CLEAR,
  [BTN_BD_EJECT] = BTN_EJECT,
  [BTN_BD_TOPMENU] = BTN_TOPMENU,
  [BTN_BD_TIME] = BTN_TIME,
  [BTN_BD_PREV] = BTN_PREV,
  [BTN_BD_NEXT] = BTN_NEXT,
  [BTN_BD_PLAY] = BTN_PLAY,
  [BTN_BD_SCAN_REV] = BTN_SCAN_REV,
  [BTN_BD_SCAN_FWD] = BTN_SCAN_FWD,
  [BTN_BD_STOP] = BTN_STOP,
  [BTN_BD_PAUSE] = BTN_PAUSE,
  [BTN_BD_POPUP_MENU] = BTN_POPUP_MENU,
  [BTN_BD_L3] = BTN_L3,
  [BTN_BD_R3] = BTN_R3,
  [BTN_BD_START] = BTN_START,
  [BTN_BD_UP] = BTN_UP,
  [BTN_BD_RIGHT] = BTN_RIGHT,
  [BTN_BD_DOWN] = BTN_DOWN,
  [BTN_BD_LEFT] = BTN_LEFT,
  [BTN_BD_L2] = BTN_L2,
  [BTN_BD_R2] = BTN_R2,
  [BTN_BD_L1] = BTN_L1,
  [BTN_BD_R1] = BTN_R1,
  [BTN_BD_TRIANGLE] = BTN_TRIANGLE,
  [BTN_BD_CIRCLE] = BTN_CIRCLE,
  [BTN_BD_CROSS] = BTN_CROSS,
  [BTN_BD_SQUARE] = BTN_SQUARE,
  [BTN_BD_SLOW_REV] = BTN_SLOW_REV,
  [BTN_BD_SLOW_FWD] = BTN_SLOW_FWD,
  [BTN_BD_SUBTITLE] = BTN_SUBTITLE,
  [BTN_BD_AUDIO] = BTN_AUDIO,
  [BTN_BD_ANGLE] = BTN_ANGLE,
  [BTN_BD_DISPLAY] = BTN_DISPLAY,
  [BTN_BD_BLUE] = BTN_BLUE,
  [BTN_BD_RED] = BTN_RED,
  [BTN_BD_GREEN] = BTN_GREEN,
  [BTN_BD_YELLOW] = BTN_YELLOW,
};


static uint16_t remote_last_btn[MAX_PADS] =
  {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/**
 *
 */
static void
handle_pads(glw_ps3_t *gp)
{
  PadInfo2 padinfo2;
  int i;

  if(gp->osk_widget) {
    clear_btns();
    return;
  }

  // Check the pads.
  ioPadGetInfo2(&padinfo2);
  for(i=0; i<7; i++){
    if(!padinfo2.port_status[i])
      continue;

    if(padinfo2.device_type[i] == 4) {
      uint32_t type = 4;
      int r = ioPadGetDataExtra(i, &type, &paddata[i]);

      if(r == 0) {
	int btn = paddata[i].button[25];
	if(btn != remote_last_btn[i]) {
	  if(remote_last_btn[i] < 0xff)
	    handle_btn(gp, i, bd_to_local_map[remote_last_btn[i]], 0, 0, 0);
	  remote_last_btn[i] = btn;
	}

	if(btn != 0xff)
	  handle_btn(gp, i, bd_to_local_map[btn], 1, 0, 0);
      }
      continue;
    }


    ioPadGetData(i, &paddata[i]);
    PadData *pd = &paddata[i];
    int sel = !!pd->BTN_SELECT;
    handle_btn(gp, i, BTN_LEFT,     pd->BTN_LEFT,     sel, pd->PRE_LEFT);
    handle_btn(gp, i, BTN_UP,       pd->BTN_UP,       sel, pd->PRE_UP);
    handle_btn(gp, i, BTN_RIGHT,    pd->BTN_RIGHT,    sel, pd->PRE_RIGHT);
    handle_btn(gp, i, BTN_DOWN,     pd->BTN_DOWN,     sel, pd->PRE_DOWN);
    handle_btn(gp, i, BTN_CROSS,    pd->BTN_CROSS,    sel, pd->PRE_CROSS);
    handle_btn(gp, i, BTN_CIRCLE,   pd->BTN_CIRCLE,   sel, pd->PRE_CIRCLE);
    handle_btn(gp, i, BTN_TRIANGLE, pd->BTN_TRIANGLE, sel, pd->PRE_TRIANGLE);
    handle_btn(gp, i, BTN_SQUARE,   pd->BTN_SQUARE,   sel, pd->PRE_SQUARE);
    handle_btn(gp, i, BTN_START,    pd->BTN_START,    sel, 0);
    handle_btn(gp, i, BTN_R1,       pd->BTN_R1,       sel, pd->PRE_R1);
    handle_btn(gp, i, BTN_L1,       pd->BTN_L1,       sel, pd->PRE_L1);
    handle_btn(gp, i, BTN_R3,       pd->BTN_R3,       sel, 0);
    handle_btn(gp, i, BTN_L3,       pd->BTN_L3,       sel, 0);


    if(gp->gp_seekmode == 0 || (gp->gp_seekmode == 1 && sel)) {
      handle_seek(gp, i, 1,        pd->BTN_R2,       pd->PRE_R2);
      handle_seek(gp, i, -1,       pd->BTN_L2,       pd->PRE_L2);
    }
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
                e->e_flags |= EVENT_KEYPRESS;
		event_to_ui(e);
		break;
	      }
	    }
	  }

	  if(i == sizeof(kb2action) / sizeof(*kb2action) && uc < 0x8000 && uc) {
	    e = event_create_int(EVENT_UNICODE, uc);
	    event_to_ui(e);
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
  TRACE(TRACE_DEBUG, "GLW", "Entering mainloop");



  sysRegisterCallback(EVENT_SLOT0, eventHandle, gp);
  while(gp->gp_stop != 10) {

    if(gp->gp_stop)
      gp->gp_stop++;

    handle_pads(gp);
    handle_kb(gp);

    waitFlip();
    drawFrame(gp, currentBuffer, 1);
    flip(gp, currentBuffer);
    currentBuffer = !currentBuffer;
    sysCheckCallback();
  }
  waitFlip();
  drawFrame(gp, currentBuffer, 0);
  flip(gp, currentBuffer);
  currentBuffer = !currentBuffer;

  sysUnregisterCallback(EVENT_SLOT0);
}


static void
set_seekmode(void *opaque, const char *str)
{
  glw_ps3_t *gp = opaque;
  gp->gp_seekmode = atoi(str);
}


int glw_ps3_start(void);

/**
 *
 */
int
glw_ps3_start(void)
{
  glw_ps3_t *gp = calloc(1, sizeof(glw_ps3_t));
  glwps3 = gp;
  prop_t *root = gp->gr.gr_prop_ui = prop_create(prop_get_global(), "ui");
  gp->gr.gr_prop_nav = nav_spawn();

  prop_set_int(prop_create(root, "fullscreen"), 1);

  if(glw_ps3_init(gp))
     return 1;

  gp->gr.gr_prop_maxtime = 10000;

  glw_root_t *gr = &gp->gr;

  if(glw_init2(gr,
               GLW_INIT_KEYBOARD_MODE |
               GLW_INIT_OVERSCAN |
               GLW_INIT_IN_FULLSCREEN))
    return 1;

  settings_create_separator(glw_settings.gs_settings, _p("Dual-Shock Remote"));

  setting_create(SETTING_MULTIOPT, glw_settings.gs_settings,
                 SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Seek using L2 and R2 button")),
                 SETTING_OPTION("0", _p("Yes")),
                 SETTING_OPTION("1", _p("Yes with Select button")),
                 SETTING_OPTION("2", _p("No")),
                 SETTING_COURIER(gr->gr_courier),
                 SETTING_CALLBACK(set_seekmode, gp),
                 SETTING_STORE("glw", "analogseekmode"),
                 NULL);

  gr->gr_open_osk = osk_open;

  TRACE(TRACE_DEBUG, "GLW", "loading universe");

  glw_load_universe(gr);
  glw_ps3_mainloop(gp);
  glw_unload_universe(gr);
  glw_reap(gr);
  glw_reap(gr);
  return 0;
}

int
arch_stop_req(void)
{
  glwps3->gp_stop = 1;
  return 0;
}
