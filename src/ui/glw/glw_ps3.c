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

  float scale;

} glw_ps3_t;


extern s32 ioPadGetDataExtra(u32 port, u32* type, PadData* data);

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
  gcmSetFlip(gp->gr.gr_be.be_ctx, buffer);
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

  TRACE(TRACE_INFO, "RSX",
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
drawFrame(glw_ps3_t *gp, int buffer, int with_universe) 
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
  glw_rctx_init(&rc, gp->gr.gr_width * gp->scale, gp->gr.gr_height, 1);
  glw_layout0(gp->gr.gr_universe, &rc);
  glw_render0(gp->gr.gr_universe, &rc);
  glw_unlock(&gp->gr);
}

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
  BTN_SELECT,
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
  [BTN_CROSS]      = AVEC(ACTION_ACTIVATE, ACTION_ENTER),
  [BTN_CIRCLE]     = AVEC(ACTION_NAV_BACK),
  [BTN_TRIANGLE]   = AVEC(ACTION_MENU),
  [BTN_L1]         = AVEC(ACTION_PREV_TRACK, ACTION_NAV_BACK),
  [BTN_L3]         = AVEC(ACTION_SHOW_MEDIA_STATS),
  [BTN_R1]         = AVEC(ACTION_NEXT_TRACK, ACTION_NAV_FWD),
  [BTN_R3]         = AVEC(ACTION_LOGWINDOW),
  [BTN_START]      = AVEC(ACTION_PLAYPAUSE),
  [BTN_SELECT]     = AVEC(ACTION_HOME),
  [BTN_ENTER]      = AVEC(ACTION_ACTIVATE, ACTION_ENTER),
  [BTN_RETURN]     = AVEC(ACTION_NAV_BACK),
  [BTN_EJECT]      = AVEC(ACTION_EJECT),
  [BTN_TOPMENU]    = AVEC(ACTION_HOME),
  [BTN_PREV]       = AVEC(ACTION_PREV_TRACK),
  [BTN_NEXT]       = AVEC(ACTION_NEXT_TRACK),
  [BTN_PLAY]       = AVEC(ACTION_PLAY),
  [BTN_SCAN_REV]   = AVEC(ACTION_SEEK_BACKWARD),
  [BTN_SCAN_FWD]   = AVEC(ACTION_SEEK_FORWARD),
  [BTN_STOP]       = AVEC(ACTION_STOP),
  [BTN_PAUSE]      = AVEC(ACTION_PAUSE),
  [BTN_POPUP_MENU] = AVEC(ACTION_MENU),
  [BTN_SUBTITLE]   = AVEC(ACTION_CYCLE_SUBTITLE),
  [BTN_AUDIO]      = AVEC(ACTION_CYCLE_AUDIO),

  //  [BTN_L2] = ACTION_L2,
  //  [BTN_R2] = ACTION_R2,
  //  [BTN_CLEAR] = ACTION_CLEAR,
  //  [BTN_TIME] = ACTION_TIME,
  //  [BTN_SLOW_REV] = ACTION_SLOW_REV,
  //  [BTN_SLOW_FWD] = ACTION_SLOW_FWD,
  //  [BTN_ANGLE] = ACTION_ANGLE,
  //  [BTN_DISPLAY] = ACTION_DISPLAY,
  //  [BTN_BLUE] = ACTION_BLUE,
  //  [BTN_RED] = ACTION_RED,
  //  [BTN_GREEN] = ACTION_GREEN,
  //  [BTN_YELLOW] = ACTION_YELLOW,
};




static int16_t button_counter[MAX_PADS][BTN_max];
static PadData paddata[MAX_PADS];


#define KEY_REPEAT_DELAY 30 // in frames
#define KEY_REPEAT_RATE  3  // in frames

static void
handle_btn(glw_ps3_t *gp, int pad, int code, int pressed)
{
  int16_t *store = &button_counter[pad][code];
  if(code == 0)
    return;

  if(pressed) {

    if(*store == 0 ||
       (*store > KEY_REPEAT_DELAY && (*store % KEY_REPEAT_RATE == 0))) {
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
	const action_type_t *avec = btn_to_action[code];
	if(avec) {
	  int i = 0;
	  while(avec[i] != 0)
	    i++;
	  e = event_create_action_multi(avec, i);
	}
      }

      if(e != NULL) {
	glw_dispatch_event(&gp->gr.gr_uii, e);
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
  [BTN_BD_SELECT] = BTN_SELECT,
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

  // Check the pads.
  ioPadGetInfo2(&padinfo2);
  for(i=0; i<MAX_PADS; i++){
    if(!padinfo2.port_status[i])
      continue;

    if(padinfo2.device_type[i] == 4) {
      uint32_t type = 4;
      int r = ioPadGetDataExtra(i, &type, &paddata[i]);

      if(r == 0) {
	int btn = paddata[i].button[25];
	if(btn != remote_last_btn[i]) {
	  if(remote_last_btn[i] < 0xff)
	    handle_btn(gp, i, bd_to_local_map[remote_last_btn[i]], 0);
	  remote_last_btn[i] = btn;
	}

	if(btn != 0xff)
	  handle_btn(gp, i, bd_to_local_map[btn], 1);
      }
      continue;
    }


    ioPadGetData(i, &paddata[i]);
    PadData *pd = &paddata[i];
    handle_btn(gp, i, BTN_LEFT,     pd->BTN_LEFT);
    handle_btn(gp, i, BTN_UP,       pd->BTN_UP);
    handle_btn(gp, i, BTN_RIGHT,    pd->BTN_RIGHT);
    handle_btn(gp, i, BTN_DOWN,     pd->BTN_DOWN);
    handle_btn(gp, i, BTN_CROSS,    pd->BTN_CROSS);
    handle_btn(gp, i, BTN_CIRCLE,   pd->BTN_CIRCLE);
    handle_btn(gp, i, BTN_TRIANGLE, pd->BTN_TRIANGLE);
    handle_btn(gp, i, BTN_SQUARE,   pd->BTN_SQUARE);
    handle_btn(gp, i, BTN_START,    pd->BTN_START);
    handle_btn(gp, i, BTN_SELECT,   pd->BTN_SELECT);
    handle_btn(gp, i, BTN_R1,       pd->BTN_R1);
    handle_btn(gp, i, BTN_L1,       pd->BTN_L1);
    handle_btn(gp, i, BTN_R2,       pd->BTN_R2);
    handle_btn(gp, i, BTN_L2,       pd->BTN_L2);
    handle_btn(gp, i, BTN_R3,       pd->BTN_R3);
    handle_btn(gp, i, BTN_L3,       pd->BTN_L3);
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
#if 0
  int r = ioPadSetPortSetting(6, 0xffffffff);
  TRACE(TRACE_ERROR, "PS3PAD", "portsetting=0x%x", r);
#endif

  sysRegisterCallback(EVENT_SLOT0, eventHandle, gp);
  while(!gp->stop) {

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


/**
 *
 */
static int
glw_ps3_start(ui_t *ui, prop_t *root, int argc, char *argv[], int primary)
{
  glw_ps3_t *gp = calloc(1, sizeof(glw_ps3_t));
  char confname[PATH_MAX];
  const char *theme_path = SHOWTIME_GLW_DEFAULT_THEME_URL;
  const char *displayname_title  = NULL;
  const char *skin = NULL;

  gp->gr.gr_uii.uii_prop = root;

  prop_set_int(prop_create(root, "fullscreen"), 1);

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


  if(gp->res.height >= 1080) {
    gr->gr_base_size = 26;
    gr->gr_base_underscan_h = 66;
    gr->gr_base_underscan_v = 34;
  } else if(gp->res.height >= 720) {
    gr->gr_base_size = 21;
    gr->gr_base_underscan_h = 43;
    gr->gr_base_underscan_v = 22;
  } else {
    gr->gr_base_size = 18;
    gr->gr_base_underscan_h = 36;
    gr->gr_base_underscan_v = 20;
  }


  
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

