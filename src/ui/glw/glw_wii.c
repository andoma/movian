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

#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>

#include <ogc/lwp_queue.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>

#include "showtime.h"
#include "ui/ui.h"
#include "settings.h"
#include "ui/glw/glw.h"
#include "prop/prop.h"
#include "glw_texture.h"
#include "notifications.h"

#define DEFAULT_FIFO_SIZE	(256*1024)

extern void *wii_xfb[2];
extern GXRModeObj wii_vmode;

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

  int wide;

} glw_wii_t;


/**
 *
 */
static const struct {
  int KS;
  int modifier;
  int action1;
  int action2;
  int action3;
} keysym2action[] = {
  
  { KS_Left,         0,           ACTION_LEFT,        ACTION_SEEK_BACKWARD },
  { KS_Right,        0,           ACTION_RIGHT,       ACTION_SEEK_FORWARD },
  { KS_Up,           0,           ACTION_UP },
  { KS_Down,         0,           ACTION_DOWN },
  { KS_Prior,        0,           ACTION_PAGE_UP,     ACTION_NEXT_CHANNEL },
  { KS_Next,         0,           ACTION_PAGE_DOWN,   ACTION_PREV_CHANNEL },
  { KS_Home,         0,           ACTION_TOP },
  { KS_End,          0,           ACTION_BOTTOM },

  { KS_Left,         MOD_ANYMETA,    ACTION_NAV_BACK },
  { KS_Right,        MOD_ANYMETA,    ACTION_NAV_FWD },

  { '+',             MOD_ANYCONTROL, ACTION_ZOOM_UI_INCR },
  { KS_KP_Add,       MOD_ANYCONTROL, ACTION_ZOOM_UI_INCR },
  { '-',             MOD_ANYCONTROL, ACTION_ZOOM_UI_DECR },
  { KS_KP_Subtract,  MOD_ANYCONTROL, ACTION_ZOOM_UI_DECR },

  { KS_f1,                   0,   ACTION_MENU },

  { KS_F1,                   MOD_ANYSHIFT,   ACTION_SKIP_BACKWARD },
  { KS_F2,                   MOD_ANYSHIFT,   ACTION_PLAYPAUSE },
  { KS_F3,                   MOD_ANYSHIFT,   ACTION_SKIP_FORWARD },
  { KS_F4,                   MOD_ANYSHIFT,   ACTION_STOP },

  { KS_F5,                   MOD_ANYSHIFT,   ACTION_VOLUME_DOWN },
  { KS_F6,                   MOD_ANYSHIFT,   ACTION_VOLUME_MUTE_TOGGLE },
  { KS_F7,                   MOD_ANYSHIFT,   ACTION_VOLUME_UP },
};



/**
 * Keyboard input
 */
static void
process_keyboard_event(glw_wii_t *gwii, keyboard_event *ke)
{
  event_t *e = NULL;
  int i;
  action_type_t av[10];

  if(0)TRACE(TRACE_DEBUG, "WiiKeyboard", "%d 0x%x 0x%x 0x%x",
	ke->type, ke->modifiers, ke->keycode, ke->symbol);

  if(ke->type == KEYBOARD_CONNECTED) {

    notify_add(NOTIFY_INFO, NULL, 5, "USB keyboard connected");
    return;
  } else if(ke->type == KEYBOARD_DISCONNECTED) {
    notify_add(NOTIFY_WARNING, NULL, 5, "USB keyboard disconnected");
    return;
  }

  if(ke->type != KEYBOARD_PRESSED)
    return;

  switch(ke->symbol) {
    /* Static key mappings, these cannot be changed */
  case KS_BackSpace:
    e = event_create_action_multi((const action_type_t[]){
	ACTION_BS, ACTION_NAV_BACK}, 2);
    break;
  case KS_Return:  e = event_create_action(ACTION_ENTER);     break;
  case KS_Escape:  e = event_create_action(ACTION_QUIT);      break;
  case KS_Tab:     e = event_create_action(ACTION_FOCUS_NEXT);break;
    /* Always send 1 char ASCII */
  default:
    if(ke->symbol < 32 || ke->symbol > 127)
      break;
    
    e = event_create_int(EVENT_UNICODE, ke->symbol);
    break;
  }

  if(e == NULL) {

    for(i = 0; i < sizeof(keysym2action) / sizeof(*keysym2action); i++) {
      
      if(keysym2action[i].KS == ke->symbol &&
	 ((keysym2action[i].modifier == 0 && ke->modifiers == 0) ||
	  (keysym2action[i].modifier & ke->modifiers) == ke->modifiers)) {
	
	av[0] = keysym2action[i].action1;
	av[1] = keysym2action[i].action2;
	av[2] = keysym2action[i].action3;

	if(keysym2action[i].action3 != ACTION_NONE)
	  e = event_create_action_multi(av, 3);
	if(keysym2action[i].action2 != ACTION_NONE)
	  e = event_create_action_multi(av, 2);
	else
	  e = event_create_action_multi(av, 1);
	break;
      }
    }
  }

  if(e != NULL)
   glw_dispatch_event(&gwii->gr.gr_uii, e);
}


/**
 *
 */
typedef struct krepeat {
  int held_frames;
} krepeat_t;


static krepeat_t k_left, k_right, k_up, k_down;
static krepeat_t k_b, k_2, k_home, k_plus, k_minus;

static void
wpad_btn(glw_wii_t *gwii, krepeat_t *kr, int pressed, action_type_t ac)
{
  event_t *e;

  if(ac == ACTION_NONE)
    return;

  if(pressed) {

    if(kr->held_frames == 0 ||
       (kr->held_frames > 30 && (kr->held_frames % 3 == 0))) {
      e = event_create_action(ac);
      glw_dispatch_event(&gwii->gr.gr_uii, e);
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
  int ir = 0;

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
      ir = 1;
    } else if(gwii->cursor_alpha > 0.0) {
      gwii->cursor_alpha -= 0.02;
    }
      
  
    btn = wd->btns_h;
  } else {
    btn = 0;
  }

  /* Pointer -> GLW */

  glw_lock(&gwii->gr);

  gpe.x = gwii->cursor_x;
  gpe.y = gwii->cursor_y;

  if(btn & WPAD_BUTTON_A && a_held == 0) {
    
    gpe.type = GLW_POINTER_LEFT_PRESS;

    glw_pointer_event(&gwii->gr, &gpe);
    a_held = 1;
  } else if(!(btn & WPAD_BUTTON_A) && a_held == 1) {
    
    gpe.type = GLW_POINTER_LEFT_RELEASE;

    glw_pointer_event(&gwii->gr, &gpe);
    a_held = 0;
  } else if(ir) {
    gpe.type = GLW_POINTER_MOTION_UPDATE;
    glw_pointer_event(&gwii->gr, &gpe);
  }

  wpad_btn(gwii, &k_left,  btn & WPAD_BUTTON_LEFT,
	   ir ? ACTION_LEFT    : ACTION_DOWN);
  wpad_btn(gwii, &k_right, btn & WPAD_BUTTON_RIGHT,
	   ir ? ACTION_RIGHT   : ACTION_UP);
  wpad_btn(gwii, &k_up,    btn & WPAD_BUTTON_UP,
	   ir ? ACTION_UP      : ACTION_LEFT);
  wpad_btn(gwii, &k_down,  btn & WPAD_BUTTON_DOWN,
	   ir ? ACTION_DOWN    : ACTION_RIGHT);
  
  wpad_btn(gwii, &k_b,     btn & WPAD_BUTTON_B,
	   ACTION_NAV_BACK);

  wpad_btn(gwii, &k_2,     btn & WPAD_BUTTON_2,
	   ir ? ACTION_NONE    : ACTION_ACTIVATE);

  wpad_btn(gwii, &k_home,  btn & WPAD_BUTTON_HOME,
	   ACTION_MENU);

  wpad_btn(gwii, &k_plus,  btn & WPAD_BUTTON_PLUS,
	   ACTION_VOLUME_UP);

  wpad_btn(gwii, &k_minus, btn & WPAD_BUTTON_MINUS,
	   ACTION_VOLUME_DOWN);

  glw_unlock(&gwii->gr);
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
		    0, 0, -1 / tan(45 * M_PI / 360));
    glw_Translatef(&rc, gwii->cursor_x, gwii->cursor_y, 0);
    glw_Scalef(&rc, 0.1, 0.1, 1.0);

    if(i == 0)
      glw_Translatef(&rc, 0.1, -0.1, 0);

    glw_Rotatef(&rc, gwii->cursor_a, 0, 0, -1.0);

    glw_render(&gwii->cursor_renderer, &gwii->gr, &rc, GLW_RENDER_MODE_QUADS,
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
  keyboard_event event;
  glw_rctx_t rc;
  void *gp_fifo;
  float yscale;
  uint32_t xfbHeight;
  int curframe;
  GXColor background = {0,0,0, 0xff};
  int resetted = 0;

  Mtx44 perspective;

  gp_fifo = memalign(32, DEFAULT_FIFO_SIZE);
  memset(gp_fifo, 0, DEFAULT_FIFO_SIZE);
  GX_Init(gp_fifo, DEFAULT_FIFO_SIZE);
  
  // clears the bg to color and clears the z buffer
  GX_SetCopyClear(background, 0x00ffffff);
 

  // Setup frame buffers

  GX_SetViewport(0, 0, wii_vmode.fbWidth, wii_vmode.efbHeight, 0, 1);

  yscale = GX_GetYScaleFactor(wii_vmode.efbHeight, wii_vmode.xfbHeight);
  xfbHeight = GX_SetDispCopyYScale(yscale);

  GX_SetScissor(0, 0, wii_vmode.fbWidth, wii_vmode.efbHeight);
  GX_SetDispCopySrc(0, 0, wii_vmode.fbWidth, wii_vmode.efbHeight);
  GX_SetDispCopyDst(wii_vmode.fbWidth, xfbHeight);
  GX_SetCopyFilter(wii_vmode.aa, wii_vmode.sample_pattern,
		   GX_TRUE, wii_vmode.vfilter);
  GX_SetFieldMode(wii_vmode.field_rendering,
		  wii_vmode.viHeight == 2 * wii_vmode.xfbHeight 
		  ? GX_ENABLE : GX_DISABLE);

  if(wii_vmode.aa)
    GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
  else
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

  /* We are displaying from XFB 0 at the moment,
     So let GX start with XFB 1 */

  curframe = 1;

  GX_SetCullMode(GX_CULL_FRONT);
  GX_CopyDisp(wii_xfb[curframe], GX_TRUE);
  GX_SetDispCopyGamma(GX_GM_1_0);

  // setup the vertex descriptor
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);


  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ,  GX_F32,  0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,  0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST,   GX_F32,  0);
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
  guPerspective(perspective, 45, 1.0, 1.0F, 300.0F);
  GX_LoadProjectionMtx(perspective, GX_PERSPECTIVE);



  /* Setup cursor */
  WPAD_SetDataFormat(0, WPAD_FMT_BTNS_ACC_IR);
  WPAD_SetVRes(0, wii_vmode.fbWidth, wii_vmode.xfbHeight);

  glw_render_init(&gwii->cursor_renderer, 4, GLW_RENDER_ATTRIBS_TEX);

  glw_render_vtx_pos(&gwii->cursor_renderer, 0, -1.0, -1.0, 0.0);
  glw_render_vtx_pos(&gwii->cursor_renderer, 1,  1.0, -1.0, 0.0);
  glw_render_vtx_pos(&gwii->cursor_renderer, 2,  1.0,  1.0, 0.0);
  glw_render_vtx_pos(&gwii->cursor_renderer, 3, -1.0,  1.0, 0.0);

  glw_render_vtx_st(&gwii->cursor_renderer, 0, 0.0, 1.0);
  glw_render_vtx_st(&gwii->cursor_renderer, 1, 1.0, 1.0);
  glw_render_vtx_st(&gwii->cursor_renderer, 2, 1.0, 0.0);
  glw_render_vtx_st(&gwii->cursor_renderer, 3, 0.0, 0.0);


  gwii->cursors[0] =
    glw_tex_create(&gwii->gr, "skin://wii/shadow_point.png", 0, -1, -1);
  gwii->cursors[1] =
    glw_tex_create(&gwii->gr, "skin://wii/generic_point.png", 0, -1, -1);
  gwii->cursors[2] =
    glw_tex_create(&gwii->gr, "skin://wii/shadow_grab.png", 0, -1, -1);
  gwii->cursors[3] =
    glw_tex_create(&gwii->gr, "skin://wii/generic_grab.png", 0, -1, -1);

  gwii->running = 1;

  KEYBOARD_Init(NULL);

  gwii->gr.gr_width  = wii_vmode.viWidth;
  gwii->gr.gr_height = wii_vmode.viHeight;

  GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GX_SetColorUpdate(GX_TRUE);

  while(1) {

    wpad_every_frame(gwii);

    GX_SetViewport(0, 0, wii_vmode.fbWidth, wii_vmode.efbHeight, 0, 1);

    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, 
		    GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);

    glw_lock(&gwii->gr);

    if(KEYBOARD_GetEvent(&event))
      process_keyboard_event(gwii, &event);

    glw_prepare_frame(&gwii->gr);

    glw_rctx_init(&rc, 
		  gwii->gr.gr_width * (gwii->wide ? 1.3333 : 1),
		  gwii->gr.gr_height);
		  
    guMtxIdentity(rc.rc_be.gbr_model_matrix);
    guMtxTransApply(rc.rc_be.gbr_model_matrix,
		    rc.rc_be.gbr_model_matrix,
		    0, 0, -1 / tan(45 * M_PI / 360));
    
    glw_layout0(gwii->gr.gr_universe, &rc);
    glw_render0(gwii->gr.gr_universe, &rc);


    /* Render cursor */
    wpad_render(gwii);
    
    glw_unlock(&gwii->gr);

    GX_DrawDone();
    curframe ^= 1;
    GX_CopyDisp(wii_xfb[curframe], GX_TRUE);

    VIDEO_SetNextFramebuffer(wii_xfb[curframe]);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    if(SYS_ResetButtonDown() && !resetted) {
      resetted = 1;
      showtime_shutdown(0);
    }
  }
}


/**
 * Widescreen mode
 */
static void
gwii_set_widescreen(void *opaque, int value)
{
  glw_wii_t *gwii = opaque;
  gwii->wide = value;
}


/**
 *
 */
static int
glw_wii_start(ui_t *ui, int argc, char *argv[], int primary)
{
  const char *skin_path = SHOWTIME_GLW_DEFAULT_SKIN_URL;
  glw_wii_t *gwii = calloc(1, sizeof(glw_wii_t));
 
  /* Parse options */
  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "--skin") && argc > 1) {
      skin_path = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else
      break;
  }

  gwii->gr.gr_normalized_texture_coords = 1;

  glw_root_t *gr = &gwii->gr;

  if(glw_init(gr, skin_path, ui, primary, "wii", NULL)) {
    printf("GLW failed to init\n");
    sleep(3);
    exit(0);
  }

  settings_create_bool(gr->gr_settings, "widescreen",
		       "Widescreen", CONF_GetAspectRatio() == 1, NULL,
		       gwii_set_widescreen, gwii,
		       SETTINGS_INITIAL_UPDATE, gr->gr_courier,
		       NULL, NULL);

  prop_t *def = prop_create(gwii->gr.gr_uii.uii_prop, "defaults");
  prop_set_int(prop_create(def, "underscan_h"), 13);
  prop_set_int(prop_create(def, "underscan_v"), 13);

  glw_load_universe(&gwii->gr);

  gwii->gr.gr_frameduration = 1000000 / 50;

  glw_set_fullscreen(&gwii->gr, 1); // Always fullscreen

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
