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

#include <unistd.h>

#include "showtime.h"
#include "arch/arch.h"
#include "misc/str.h"
#include "navigator.h"

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_audio.h"
#include "ppapi/c/ppb_audio_config.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_console.h"
#include "ppapi/c/ppb_file_system.h"
#include "ppapi/c/ppb_file_ref.h"
#include "ppapi/c/ppb_file_io.h"
#include "ppapi/c/ppb_fullscreen.h"
#include "ppapi/c/ppb_host_resolver.h"
#include "ppapi/c/ppb_input_event.h"
#include "ppapi/c/ppb_instance.h"
#include "ppapi/c/ppb_message_loop.h"
#include "ppapi/c/ppb_graphics_3d.h"
#include "ppapi/c/ppb_tcp_socket.h"
#include "ppapi/c/ppb_udp_socket.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_video_decoder.h"
#include "ppapi/c/ppb_view.h"
#include "ppapi/c/ppp.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/ppp_input_event.h"
#include "ppapi/c/ppp_messaging.h"

#include "ui/glw/glw.h"
#include "ppapi/gles2/gl2ext_ppapi.h"

PPB_GetInterface get_browser_interface;

const PPB_Console *ppb_console;
const PPB_Var *ppb_var;
const PPB_Core *ppb_core;
const PPB_View *ppb_view;
const PPB_Instance *ppb_instance;
const PPB_Graphics3D *ppb_graphics3d;
const PPB_InputEvent *ppb_inputevent;
const PPB_KeyboardInputEvent *ppb_keyboardinputevent;
const PPB_HostResolver *ppb_hostresolver;
const PPB_NetAddress *ppb_netaddress;
const PPB_TCPSocket *ppb_tcpsocket;
const PPB_UDPSocket *ppb_udpsocket;
const PPB_MessageLoop *ppb_messageloop;
const PPB_FileSystem *ppb_filesystem;
const PPB_FileRef *ppb_fileref;
const PPB_FileIO *ppb_fileio;
const PPB_AudioConfig *ppb_audioconfig;
const PPB_Audio *ppb_audio;
const PPB_Fullscreen *ppb_fullscreen;
const PPB_VideoDecoder *ppb_videodecoder;

PP_Instance g_Instance;
PP_Resource g_persistent_fs;
PP_Resource g_cache_fs;


typedef struct nacl_glw_root {
  glw_root_t gr;

} nacl_glw_root_t;

PP_Resource nacl_3d_context;

static nacl_glw_root_t *uiroot;

static void mainloop(nacl_glw_root_t *ngr);

/**
 *
 */
int
arch_stop_req(void)
{
  return 0;
}

/**
 *
 */
void
arch_exit(void)
{
  exit(0);
}



#define CORE_INITIALIZED   0x1

static int is_hidden;
static int initialized;

/**
 *
 */
static void
init_ui(void *data, int flags)
{
  nacl_glw_root_t *ngr = data;

  initialized |= flags;

  if(!(initialized & CORE_INITIALIZED))
    return;

  if(ngr->gr.gr_width == 0 || ngr->gr.gr_height == 0)
    return;

  if(nacl_3d_context) {
    ppb_graphics3d->ResizeBuffers(nacl_3d_context,
                                  ngr->gr.gr_width,
                                  ngr->gr.gr_height);
    return;
  }

  const int32_t attrib_list[] = {
    PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
    PP_GRAPHICS3DATTRIB_WIDTH,  ngr->gr.gr_width,
    PP_GRAPHICS3DATTRIB_HEIGHT, ngr->gr.gr_height,
    PP_GRAPHICS3DATTRIB_NONE
  };

  nacl_3d_context = ppb_graphics3d->Create(g_Instance, 0, attrib_list);

  if(!ppb_instance->BindGraphics(g_Instance, nacl_3d_context)) {
    TRACE(TRACE_DEBUG, "NACL", "Unable to bind 3d context");
    glSetCurrentContextPPAPI(0);
    return;
  }

  glSetCurrentContextPPAPI(nacl_3d_context);
  TRACE(TRACE_DEBUG, "NACL", "Current 3d context set");

  glw_opengl_init_context(&ngr->gr);
  glClearColor(0,0,0,0);

  mainloop(ngr);
}



/**
 *
 */
static void
ui_courier_poll(void *aux, int val)
{
  glw_root_t *gr = aux;
  glw_lock(gr);
  prop_courier_poll(gr->gr_courier);
  glw_unlock(gr);
}


/**
 *
 */
static void
ui_courier_notify(void *opaque)
{
  ppb_core->CallOnMainThread(0, (const struct PP_CompletionCallback) {
     ui_courier_poll, opaque}, 0);
}


/**
 *
 */
static void *
init_thread(void *aux)
{
  g_persistent_fs = ppb_filesystem->Create(g_Instance,
                                           PP_FILESYSTEMTYPE_LOCALPERSISTENT);

  if(ppb_filesystem->Open(g_persistent_fs, 128 * 1024 * 1024,
                          PP_BlockUntilComplete())) {
    panic("Failed to open persistent filesystem");
  }

  g_cache_fs = ppb_filesystem->Create(g_Instance,
                                      PP_FILESYSTEMTYPE_LOCALTEMPORARY);

  if(ppb_filesystem->Open(g_cache_fs, 128 * 1024 * 1024,
                          PP_BlockUntilComplete())) {
    panic("Failed to open cache/temporary filesystem");
  }


  showtime_init();

  uiroot->gr.gr_prop_ui = prop_create_root("ui");
  uiroot->gr.gr_prop_nav = nav_spawn();

  prop_courier_t *pc = prop_courier_create_notify(ui_courier_notify, uiroot);

  if(glw_init3(&uiroot->gr, NULL, pc))
    return NULL;

  TRACE(TRACE_DEBUG, "GLW", "GLW %p created", uiroot);

  glw_lock(&uiroot->gr);
  glw_load_universe(&uiroot->gr);
  glw_unlock(&uiroot->gr);

  ppb_core->CallOnMainThread(0, (const struct PP_CompletionCallback) {
      &init_ui, uiroot}, CORE_INITIALIZED);

  return NULL;
}


/**
 *
 */
static PP_Bool
Instance_DidCreate(PP_Instance instance,  uint32_t argc,
                   const char *argn[], const char *argv[])
{
  g_Instance = instance;
  gconf.trace_level = TRACE_DEBUG;

  if(!glInitializePPAPI(get_browser_interface))
    panic("Unable to initialize GL PPAPI");

  ppb_inputevent->RequestInputEvents(instance,
                                     PP_INPUTEVENT_CLASS_MOUSE |
                                     PP_INPUTEVENT_CLASS_WHEEL);

  ppb_inputevent->RequestFilteringInputEvents(instance,
                                              PP_INPUTEVENT_CLASS_KEYBOARD);

  gconf.cache_path = strdup("cache:///cache");
  gconf.persistent_path = strdup("persistent:///persistent");

  uiroot = calloc(1, sizeof(nacl_glw_root_t));

  hts_thread_create_detached("init", init_thread, NULL, 0);
  return PP_TRUE;
}



/**
 *
 */
void
trace_arch(int level, const char *prefix, const char *str)
{
  struct PP_Var var_prefix = ppb_var->VarFromUtf8(prefix, strlen(prefix));
  struct PP_Var var_str    = ppb_var->VarFromUtf8(str, strlen(str));

  PP_LogLevel pplevel;

  switch(level) {
  case TRACE_EMERG:  pplevel = PP_LOGLEVEL_ERROR; break;
  case TRACE_ERROR:  pplevel = PP_LOGLEVEL_ERROR; break;
  default:           pplevel = PP_LOGLEVEL_LOG;   break;
  }

  ppb_console->LogWithSource(g_Instance, pplevel, var_prefix, var_str);
  ppb_var->Release(var_str);
  ppb_var->Release(var_prefix);
}


/**
 *
 */
void
panic(const char *fmt, ...)
{
  va_list ap;
  char buf[1024];
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  trace_arch(TRACE_EMERG, "Panic", buf);
  while(1) {
    sleep(100);
  }
}


/**
 *
 */
static void
Instance_DidDestroy(PP_Instance instance)
{
}




static void
swap_done(void *user_data, int32_t flags)
{
  mainloop(user_data);
}


/**
 *
 */
static void
mainloop(nacl_glw_root_t *ngr)
{
  glw_root_t *gr = &ngr->gr;
  int zmax = 0;
  glw_rctx_t rc;

  glw_lock(gr);
  glw_prepare_frame(gr, 0);

  int refresh = gr->gr_need_refresh;
  gr->gr_need_refresh = 0;

  if(refresh) {

    glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1, &zmax);
    glw_layout0(gr->gr_universe, &rc);

    if(refresh & GLW_REFRESH_FLAG_RENDER && !is_hidden) {
      glViewport(0, 0, gr->gr_width, gr->gr_height);
      glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
      glw_render0(gr->gr_universe, &rc);

    }
  }

  glw_unlock(gr);

  struct PP_CompletionCallback cc;
  cc.func = &swap_done;
  cc.user_data = ngr;

  if(refresh & GLW_REFRESH_FLAG_RENDER && !is_hidden) {
    glw_post_scene(gr);
    ppb_graphics3d->SwapBuffers(nacl_3d_context, cc);
  } else {
    ppb_core->CallOnMainThread(16, cc, 0);
  }
}


/**
 *
 */
static void
Instance_DidChangeView(PP_Instance instance, PP_Resource view)
{
  struct PP_Rect rect;
  ppb_view->GetRect(view, &rect);

  uiroot->gr.gr_width  = rect.size.width;
  uiroot->gr.gr_height = rect.size.height;

  init_ui(uiroot, 0);
}

/**
 *
 */
static void
Instance_DidChangeFocus(PP_Instance instance, PP_Bool has_focus)
{
}


/**
 *
 */
static PP_Bool
Instance_HandleDocumentLoad(PP_Instance instance, PP_Resource url_loader)
{
  return PP_FALSE;
}



#define KC_LEFT  37
#define KC_UP    38
#define KC_RIGHT 39
#define KC_DOWN  40
#define KC_ESC   27

#define KC_F1    112
#define KC_F11   122
#define KC_F12   123

#define KC_HOME    36
#define KC_END     35
#define KC_PAGE_UP   33
#define KC_PAGE_DOWN 34
#define KC_ENTER   13
#define KC_TAB     9
#define KC_BACKSPACE 8

#define MOD_SHIFT 0x1
#define MOD_CTRL  0x2
#define MOD_ALT   0x4



static const struct {
  int code;
  int modifier;
  int action1;
  int action2;
  int action3;
} keysym2action[] = {

  { KC_LEFT,         0,           ACTION_LEFT },
  { KC_RIGHT,        0,           ACTION_RIGHT },
  { KC_UP,           0,           ACTION_UP },
  { KC_DOWN,         0,           ACTION_DOWN },

  { KC_LEFT,         MOD_SHIFT,   ACTION_MOVE_LEFT },
  { KC_RIGHT,        MOD_SHIFT,   ACTION_MOVE_RIGHT },
  { KC_UP,           MOD_SHIFT,   ACTION_MOVE_UP },
  { KC_DOWN,         MOD_SHIFT,   ACTION_MOVE_DOWN },

  { KC_TAB,          0,           ACTION_FOCUS_NEXT },
  { KC_TAB,          MOD_SHIFT,   ACTION_FOCUS_PREV },

  { KC_ESC,          0,           ACTION_CANCEL, ACTION_NAV_BACK},
  { KC_ENTER,        0,           ACTION_ACTIVATE, ACTION_ENTER},
  { KC_BACKSPACE,    0,           ACTION_BS, ACTION_NAV_BACK},

  { KC_LEFT,         MOD_ALT,    ACTION_NAV_BACK},
  { KC_RIGHT,        MOD_ALT,    ACTION_NAV_FWD},

  { KC_LEFT,         MOD_SHIFT | MOD_CTRL,   ACTION_SKIP_BACKWARD},
  { KC_RIGHT,        MOD_SHIFT | MOD_CTRL,   ACTION_SKIP_FORWARD},

  { KC_PAGE_UP,      0,            ACTION_PAGE_UP, ACTION_PREV_CHANNEL},
  { KC_PAGE_DOWN,    0,            ACTION_PAGE_DOWN, ACTION_NEXT_CHANNEL},

  { KC_HOME,         0,           ACTION_TOP},
  { KC_END,          0,           ACTION_BOTTOM},
};


/**
 *
 */
static int
handle_keydown(nacl_glw_root_t *ngr, PP_Resource input_event)
{
  glw_root_t *gr = &ngr->gr;

  action_type_t av[10];
  uint32_t code = ppb_keyboardinputevent->GetKeyCode(input_event);
  uint32_t mod  = ppb_inputevent->GetModifiers(input_event) & 0xf;
  event_t *e = NULL;

  if(code == KC_F11) {
    int fs = ppb_fullscreen->IsFullscreen(g_Instance);
    ppb_fullscreen->SetFullscreen(g_Instance, !fs);
    return 1;
  }

  for(int i = 0; i < sizeof(keysym2action) / sizeof(*keysym2action); i++) {

    if(keysym2action[i].code == code &&
       keysym2action[i].modifier == mod) {

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

  if(e == NULL && code >= KC_F1 && code <= KC_F12)
    e = event_from_Fkey(code - KC_F1 + 1, mod & 1);


  if(e != NULL) {
    glw_lock(gr);
    glw_inject_event(gr, e);
    glw_unlock(gr);
    return 1;
  }
  return 0;
}


/**
 *
 */
static int
handle_char(nacl_glw_root_t *ngr, PP_Resource input_event)
{
  glw_root_t *gr = &ngr->gr;
  event_t *e  = NULL;
  struct PP_Var v = ppb_keyboardinputevent->GetCharacterText(input_event);
  uint32_t len;
  const char *s = ppb_var->VarToUtf8(v, &len);
  if(s != NULL) {
    char *x = alloca(len + 1);
    memcpy(x, s, len);
    x[len] = 0;
    const char *X = x;
    uint32_t uc = utf8_get(&X);
    if(uc && uc != 0xfffd)
      e = event_create_int(EVENT_UNICODE, uc);

  }

  ppb_var->Release(v);

  if(e != NULL) {
    glw_lock(gr);
    glw_inject_event(gr, e);
    glw_unlock(gr);
    return 1;
  }
  return 0;
}


/**
 *
 */
static PP_Bool
Input_HandleInputEvent(PP_Instance instance, PP_Resource input_event)
{
  PP_InputEvent_Type type = ppb_inputevent->GetType(input_event);
  nacl_glw_root_t *ngr = uiroot;

  switch(type) {
  case PP_INPUTEVENT_TYPE_KEYDOWN:
    return handle_keydown(ngr, input_event);
  case PP_INPUTEVENT_TYPE_CHAR:
    return handle_char(ngr, input_event);

  default:
    break;
  }
  return PP_FALSE;
}


/**
 *
 */
static void
Messaging_HandleMessage(PP_Instance instance, struct PP_Var v)
{
  unsigned int len;

  const char *s = ppb_var->VarToUtf8(v, &len);

  if(s == NULL)
    return;

  // zero terminate the string
  char *x = alloca(len + 1);
  memcpy(x, s, len);
  x[len] = 0;

  if(!strcmp(x, "hidden")) {
    is_hidden = 1;
  } else if(!strcmp(x, "visible")) {
    is_hidden = 0;
  } else {
    TRACE(TRACE_DEBUG, "NACL", "Got unmapped event %s from browser", x);
  }
}


/**
 *
 */
PP_EXPORT int32_t
PPP_InitializeModule(PP_Module a_module_id, PPB_GetInterface get_browser)
{
  get_browser_interface = get_browser;

  ppb_console            = get_browser(PPB_CONSOLE_INTERFACE);
  ppb_var                = get_browser(PPB_VAR_INTERFACE);
  ppb_core               = get_browser(PPB_CORE_INTERFACE);
  ppb_view               = get_browser(PPB_VIEW_INTERFACE);
  ppb_graphics3d         = get_browser(PPB_GRAPHICS_3D_INTERFACE);
  ppb_instance           = get_browser(PPB_INSTANCE_INTERFACE);
  ppb_inputevent         = get_browser(PPB_INPUT_EVENT_INTERFACE);
  ppb_keyboardinputevent = get_browser(PPB_KEYBOARD_INPUT_EVENT_INTERFACE);
  ppb_hostresolver       = get_browser(PPB_HOSTRESOLVER_INTERFACE);
  ppb_netaddress         = get_browser(PPB_NETADDRESS_INTERFACE);
  ppb_tcpsocket          = get_browser(PPB_TCPSOCKET_INTERFACE);
  ppb_udpsocket          = get_browser(PPB_UDPSOCKET_INTERFACE);
  ppb_messageloop        = get_browser(PPB_MESSAGELOOP_INTERFACE);
  ppb_filesystem         = get_browser(PPB_FILESYSTEM_INTERFACE);
  ppb_fileref            = get_browser(PPB_FILEREF_INTERFACE);
  ppb_fileio             = get_browser(PPB_FILEIO_INTERFACE);
  ppb_audio              = get_browser(PPB_AUDIO_INTERFACE);
  ppb_audioconfig        = get_browser(PPB_AUDIO_CONFIG_INTERFACE);
  ppb_fullscreen         = get_browser(PPB_FULLSCREEN_INTERFACE);
  ppb_videodecoder       = get_browser(PPB_VIDEODECODER_INTERFACE);
  return PP_OK;
}


/**
 *
 */
PP_EXPORT const void *
PPP_GetInterface(const char* interface_name)
{
  if(!strcmp(interface_name, PPP_INSTANCE_INTERFACE)) {
    static PPP_Instance instance_interface = {
      &Instance_DidCreate,
      &Instance_DidDestroy,
      &Instance_DidChangeView,
      &Instance_DidChangeFocus,
      &Instance_HandleDocumentLoad,
    };
    return &instance_interface;
  }

  if(!strcmp(interface_name, PPP_INPUT_EVENT_INTERFACE)) {
    static PPP_InputEvent input_event_interface = {
      &Input_HandleInputEvent,
    };
    return &input_event_interface;
  }


  if(!strcmp(interface_name, PPP_MESSAGING_INTERFACE)) {
    static PPP_Messaging messaging_interface = {
      &Messaging_HandleMessage,
    };
    return &messaging_interface;
  }

  return NULL;
}

/**
 *
 */
PP_EXPORT void
PPP_ShutdownModule()
{
}
