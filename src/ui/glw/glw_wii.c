/*
 *  Code for using Wii hardware as system glue
 *  Copyright (C) 2008 Andreas Öman
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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>

#include <wiiuse/wpad.h>

#include "showtime.h"
#include "ui/ui.h"
#include "ui/keymapper.h"
#include "settings.h"
#include "ui/glw/glw.h"
#include "prop.h"
#include "glw_texture.h"

#define DEFAULT_FIFO_SIZE	(256*1024)

extern void *wii_xfb[2];
extern GXRModeObj *wii_rmode;

typedef struct glw_wii {

  glw_root_t gr;

  hts_thread_t threadid;

  int running;

  glw_loadable_texture_t *cursors[4];
  
  glw_renderer_t cursor_renderer;


  float cursor_x;
  float cursor_y;
  float cursor_a; /* Angle */
  float cursor_alpha;

} glw_wii_t;


/**
 *
 */
typedef struct krepeat {
  int held_frames;
} krepeat_t;


static krepeat_t k_left, k_right, k_up, k_down, k_b;

static void
wpad_btn(glw_wii_t *gwii, krepeat_t *kr, action_type_t ac, int hold)
{
  event_t *e;

  if(hold) {

    if(kr->held_frames == 0 ||
       (kr->held_frames > 30 && (kr->held_frames % 10 == 0))) {
      e = event_create_action(ac);
      ui_dispatch_event(e, NULL, &gwii->gr.gr_uii);
    }
    kr->held_frames++;
  } else {
    kr->held_frames = 0;
  }
}



 
static void 
countevs(int chan, const WPADData *data)
{

}


static void
wpad_every_frame(glw_wii_t *gwii)
{
  glw_pointer_event_t gpe;
  int res, btn;
  uint32_t type;
  WPADData *wd;

  static int a_held;

  WPAD_ReadPending(WPAD_CHAN_ALL, countevs);
  res = WPAD_Probe(0, &type);
  switch(res) {
  case WPAD_ERR_NO_CONTROLLER:
    break;
  case WPAD_ERR_NOT_READY:
    break;
  case WPAD_ERR_NONE:
    break;
  default:
    break;
  }

  if(res == WPAD_ERR_NONE) {

    wd = WPAD_Data(0);

    if(wd->ir.valid) {
      gwii->cursor_a = wd->ir.angle;
      gwii->cursor_x = 1.1 * (    wd->ir.x / 320 - 1);
      gwii->cursor_y = 1.1 * (1 - wd->ir.y / 240    );
      gwii->cursor_alpha = 1.0;
    } else if(gwii->cursor_alpha > 0.0) {
      gwii->cursor_alpha -= 0.02;
    }
      
    if(wd->btns_h & WPAD_BUTTON_HOME)
      exit(0);
  
    btn = wd->btns_h;
  } else {
    btn = 0;
  }

  /* Pointer -> GLW */

  glw_lock(&gwii->gr);

  gpe.x = gwii->cursor_x;
  gpe.y = gwii->cursor_y;

  if(btn & WPAD_BUTTON_A && a_held == 0) {
    
    gpe.type = GLW_POINTER_CLICK;

    glw_pointer_event(&gwii->gr, &gpe);
    a_held = 1;
  } else if(!(btn & WPAD_BUTTON_A) && a_held == 1) {
    
    gpe.type = GLW_POINTER_RELEASE;

    glw_pointer_event(&gwii->gr, &gpe);
    a_held = 0;
  } else {
    gpe.type = GLW_POINTER_MOTION;
    glw_pointer_event(&gwii->gr, &gpe);
  }

  glw_unlock(&gwii->gr);


  wpad_btn(gwii, &k_left,  ACTION_LEFT,  btn & WPAD_BUTTON_LEFT);
  wpad_btn(gwii, &k_right, ACTION_RIGHT, btn & WPAD_BUTTON_RIGHT);
  wpad_btn(gwii, &k_up,    ACTION_UP,    btn & WPAD_BUTTON_UP);
  wpad_btn(gwii, &k_down,  ACTION_DOWN,  btn & WPAD_BUTTON_DOWN);
  wpad_btn(gwii, &k_b,     ACTION_BACKSPACE,  btn & WPAD_BUTTON_B);
}



/**
 *
 */
static void
wpad_render(glw_wii_t *gwii)
{
  glw_rctx_t rc;
  int grab = gwii->gr.gr_pointer_grab != NULL;
  int i;

  for(i = 0; i < 4; i++)
    glw_tex_layout(&gwii->gr, gwii->cursors[i]);

  if(gwii->cursor_alpha < 0.01)
    return;

  memset(&rc, 0, sizeof(rc));

  rc.rc_alpha = 1.0;

  for(i = 0; i < 2; i++) {

    guMtxIdentity(rc.rc_be.gbr_model_matrix);
    guMtxTransApply(rc.rc_be.gbr_model_matrix,
		    rc.rc_be.gbr_model_matrix,
		    0, 0, -2.6);
    glw_Translatef(&rc, gwii->cursor_x, gwii->cursor_y, 0);
    glw_Scalef(&rc, 0.2, 0.2, 1.0);

    if(i == 0)
      glw_Translatef(&rc, 0.2, -0.2, 0);

    glw_Rotatef(&rc, gwii->cursor_a, 0, 0, -1.0);

    glw_render(&gwii->cursor_renderer, &rc, GLW_RENDER_MODE_QUADS,
	       GLW_RENDER_ATTRIBS_TEX,
	       &gwii->cursors[i + grab * 2]->glt_texture, 1, 1, 1,
	       gwii->cursor_alpha);
  }
}



/**
 *
 */
static void
glw_wii_loop(glw_wii_t *gwii)
{
  glw_rctx_t rc;
  void *gp_fifo;
  float yscale, w, h;
  uint32_t xfbHeight;
  int curframe;
  float rquad = 0.0f;
  GXColor background = {0,0,0, 0xff};

  GXRModeObj *rmode = wii_rmode;

  Mtx44 perspective;

  gp_fifo = memalign(32, DEFAULT_FIFO_SIZE);
  memset(gp_fifo, 0, DEFAULT_FIFO_SIZE);
  GX_Init(gp_fifo, DEFAULT_FIFO_SIZE);
  
  // clears the bg to color and clears the z buffer
  GX_SetCopyClear(background, 0x00ffffff);
 

  // Setup frame buffers

  GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);

  yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
  xfbHeight = GX_SetDispCopyYScale(yscale);

  GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
  GX_SetDispCopyDst(rmode->fbWidth, xfbHeight);
  GX_SetCopyFilter(rmode->aa, rmode->sample_pattern,
		   GX_TRUE, rmode->vfilter);
  GX_SetFieldMode(rmode->field_rendering,
		  rmode->viHeight == 2 * rmode->xfbHeight 
		  ? GX_ENABLE : GX_DISABLE);

  if(rmode->aa)
    GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
  else
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

  /* We are displaying from XFB 0 at the moment,
     So let GX start with XFB 1 */

  curframe = 1;

  GX_SetCullMode(GX_CULL_NONE);
  GX_CopyDisp(wii_xfb[curframe], GX_TRUE);
  GX_SetDispCopyGamma(GX_GM_1_0);

  // setup the vertex descriptor
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);


  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ,  GX_F32,  0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,  0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

  GX_InvalidateTexAll();


  GX_SetNumChans(1);
  GX_SetNumTexGens(0);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

  GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);

  GX_SetNumTexGens(1);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);


  // setup our projection matrix
  // this creates a perspective matrix with a view angle of 90,
  // and aspect ratio based on the display resolution
  w = rmode->viWidth;
  h = rmode->viHeight;
  guPerspective(perspective, 45, 1.0, 1.0F, 300.0F);
  GX_LoadProjectionMtx(perspective, GX_PERSPECTIVE);



  /* Setup cursor */
  WPAD_SetDataFormat(0, WPAD_FMT_BTNS_ACC_IR);
  WPAD_SetVRes(0, rmode->fbWidth, rmode->xfbHeight);

  glw_render_init(&gwii->cursor_renderer, 4, GLW_RENDER_ATTRIBS_TEX);

  glw_render_vtx_pos(&gwii->cursor_renderer, 0, -1.0, -1.0, 0.0);
  glw_render_vtx_pos(&gwii->cursor_renderer, 1,  1.0, -1.0, 0.0);
  glw_render_vtx_pos(&gwii->cursor_renderer, 2,  1.0,  1.0, 0.0);
  glw_render_vtx_pos(&gwii->cursor_renderer, 3, -1.0,  1.0, 0.0);

  glw_render_vtx_st(&gwii->cursor_renderer, 0, 0.0, 1.0);
  glw_render_vtx_st(&gwii->cursor_renderer, 1, 1.0, 1.0);
  glw_render_vtx_st(&gwii->cursor_renderer, 2, 1.0, 0.0);
  glw_render_vtx_st(&gwii->cursor_renderer, 3, 0.0, 0.0);  gwii->cursors[2]= glw_tex_create(&gwii->gr, "theme://wii/shadow_grab.png");


  gwii->cursors[0]= glw_tex_create(&gwii->gr, "theme://wii/shadow_point.png");
  gwii->cursors[1]= glw_tex_create(&gwii->gr, "theme://wii/generic_point.png");
  gwii->cursors[2]= glw_tex_create(&gwii->gr, "theme://wii/shadow_grab.png");
  gwii->cursors[3]= glw_tex_create(&gwii->gr, "theme://wii/generic_grab.png");

  gwii->running = 1;

  while(1) {

    wpad_every_frame(gwii);

    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);

    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, 
		    GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);

    glw_lock(&gwii->gr);

    glw_reaper0(&gwii->gr);


    memset(&rc, 0, sizeof(rc));
    
    guMtxIdentity(rc.rc_be.gbr_model_matrix);
    guMtxTransApply(rc.rc_be.gbr_model_matrix,
		    rc.rc_be.gbr_model_matrix,
		    0, 0, -2.6);
    
    rc.rc_size_x = 640 * 1.3333;
    rc.rc_size_y = 480;
    rc.rc_fullwindow = 1;

    glw_layout0(gwii->gr.gr_universe, &rc);

    rc.rc_alpha = 1.0f;
    glw_render0(gwii->gr.gr_universe, &rc);


    /* Render cursor */
    wpad_render(gwii);
    
    glw_unlock(&gwii->gr);

    GX_DrawDone();
    curframe ^= 1;
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(wii_xfb[curframe], GX_TRUE);

    VIDEO_SetNextFramebuffer(wii_xfb[curframe]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    rquad-=0.15f;
  }
}


/**
 *
 */
static int
glw_wii_start(ui_t *ui, int argc, char *argv[], int primary)
{
  const char *theme_path = SHOWTIME_GLW_DEFAULT_THEME_URL;
  glw_wii_t *gwii = calloc(1, sizeof(glw_wii_t));

  /* Parse options */
  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "--theme") && argc > 1) {
      theme_path = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else
      break;
  }

  if(glw_init(&gwii->gr, 14, theme_path, ui, primary)) {
    printf("GLW failed to init\n");
    sleep(3);
    exit(0);
  }

  glw_wii_loop(gwii);
  return 0;
}

/**
 *
 */
ui_t glw_ui = {
  .ui_title = "glw",
  .ui_start = glw_wii_start,
  .ui_dispatch_event = glw_dispatch_event,
  .ui_flags = UI_MAINTHREAD,
};
