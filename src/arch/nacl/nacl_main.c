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
#include <unistd.h>
#include <stdlib.h>

#include "main.h"
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
#include "ppapi/c/ppb_messaging.h"
#include "ppapi/c/ppb_network_monitor.h"
#include "ppapi/c/ppb_network_list.h"
#include "ppapi/c/ppb_graphics_3d.h"
#include "ppapi/c/ppb_tcp_socket.h"
#include "ppapi/c/ppb_udp_socket.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_var_dictionary.h"
#include "ppapi/c/ppb_var_array_buffer.h"
#include "ppapi/c/ppb_video_decoder.h"
#include "ppapi/c/ppb_view.h"

#include "ppapi/c/ppp.h"
#include "ppapi/c/ppp_instance.h"
#include "ppapi/c/ppp_input_event.h"
#include "ppapi/c/ppp_messaging.h"

#include "ui/glw/glw.h"
#include "ppapi/gles2/gl2ext_ppapi.h"

#include "nacl_dnd.h"
#include "nacl.h"

#include "media/media.h"

PPB_GetInterface get_browser_interface;

const PPB_Console *ppb_console;
const PPB_Var *ppb_var;
const PPB_VarDictionary *ppb_vardict;
const PPB_VarArrayBuffer *ppb_vararraybuf;
const PPB_Core *ppb_core;
const PPB_View *ppb_view;
const PPB_Instance *ppb_instance;
const PPB_Graphics3D *ppb_graphics3d;
const PPB_InputEvent *ppb_inputevent;
const PPB_KeyboardInputEvent *ppb_keyboardinputevent;
const PPB_MouseInputEvent *ppb_mouseinputevent;
const PPB_HostResolver *ppb_hostresolver;
const PPB_NetAddress *ppb_netaddress;
const PPB_NetworkMonitor *ppb_networkmonitor;
const PPB_NetworkList *ppb_networklist;
const PPB_TCPSocket *ppb_tcpsocket;
const PPB_UDPSocket *ppb_udpsocket;
const PPB_MessageLoop *ppb_messageloop;
const PPB_Messaging *ppb_messaging;
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


static hts_mutex_t nacl_jsrpc_mutex;
static hts_cond_t nacl_jsrpc_cond;
static int nacl_jsrpc_counter;

typedef struct nacl_jsrpc {
  LIST_ENTRY(nacl_jsrpc) nj_link;
  int nj_reqid;
  struct PP_Var nj_result;
  
} nacl_jsrpc_t;

static LIST_HEAD(, nacl_jsrpc) nacl_jsrpcs;


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
send_running(void)
{
  struct PP_Var req = ppb_vardict->Create();
  nacl_dict_set_str(req, "msgtype", "running");
  ppb_messaging->PostMessage(g_Instance, req);
  ppb_var->Release(req);
}


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

  send_running();

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


  main_init();

  uiroot->gr.gr_prop_ui = prop_create_root("ui");
  uiroot->gr.gr_prop_nav = nav_spawn();

  prop_courier_t *pc = prop_courier_create_notify(ui_courier_notify, uiroot);

  if(glw_init4(&uiroot->gr, NULL, pc, 0))
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

  hts_mutex_init(&nacl_jsrpc_mutex);
  hts_cond_init(&nacl_jsrpc_cond, &nacl_jsrpc_mutex);

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

  { KC_PAGE_UP,      0, ACTION_PAGE_UP,   ACTION_PREV_CHANNEL, ACTION_SKIP_BACKWARD},
  { KC_PAGE_DOWN,    0, ACTION_PAGE_DOWN, ACTION_NEXT_CHANNEL, ACTION_SKIP_FORWARD},

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
      else if(keysym2action[i].action2 != ACTION_NONE)
        e = event_create_action_multi(av, 2);
      else
        e = event_create_action_multi(av, 1);
      break;
    }
  }

  if(e == NULL && code >= KC_F1 && code <= KC_F12)
    e = event_from_Fkey(code - KC_F1 + 1, mod & 1);


  if(e != NULL) {
    e->e_flags |= EVENT_KEYPRESS;
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
    if(uc > 31 && uc != 0xfffd)
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
handle_mouse_event(nacl_glw_root_t *ngr, PP_Resource mouse_event,
                   PP_InputEvent_Type type)
{
  glw_pointer_event_t gpe = {0};

  struct PP_Point pos = ppb_mouseinputevent->GetPosition(mouse_event);

  gpe.x =  (2.0 * pos.x / ngr->gr.gr_width ) - 1;
  gpe.y = -(2.0 * pos.y / ngr->gr.gr_height) + 1;

  PP_InputEvent_MouseButton ppbtn =
    ppb_mouseinputevent->GetButton(mouse_event);

  switch(type) {
  case PP_INPUTEVENT_TYPE_MOUSEDOWN:

    switch(ppbtn) {
    default:
      return PP_FALSE;
    case PP_INPUTEVENT_MOUSEBUTTON_LEFT:
      gpe.type = GLW_POINTER_LEFT_PRESS;
      break;
    case PP_INPUTEVENT_MOUSEBUTTON_RIGHT:
      gpe.type = GLW_POINTER_RIGHT_PRESS;
      break;
    }
    break;

  case PP_INPUTEVENT_TYPE_MOUSEUP:
    switch(ppbtn) {
    default:
      return PP_FALSE;
    case PP_INPUTEVENT_MOUSEBUTTON_LEFT:
      gpe.type = GLW_POINTER_LEFT_RELEASE;
      break;
    case PP_INPUTEVENT_MOUSEBUTTON_RIGHT:
      gpe.type = GLW_POINTER_RIGHT_RELEASE;
      break;
    }
    break;
  case PP_INPUTEVENT_TYPE_MOUSEMOVE:
  case PP_INPUTEVENT_TYPE_MOUSEENTER:
    gpe.type = GLW_POINTER_MOTION_UPDATE;
    break;

  case PP_INPUTEVENT_TYPE_MOUSELEAVE:
    gpe.type = GLW_POINTER_GONE;
    break;
  default:
    return PP_FALSE;
  }

  glw_lock(&ngr->gr);
  glw_pointer_event(&ngr->gr, &gpe);
  glw_unlock(&ngr->gr);
  return PP_TRUE;
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
  case PP_INPUTEVENT_TYPE_MOUSEDOWN:
  case PP_INPUTEVENT_TYPE_MOUSEUP:
  case PP_INPUTEVENT_TYPE_MOUSEMOVE:
  case PP_INPUTEVENT_TYPE_MOUSELEAVE:
  case PP_INPUTEVENT_TYPE_MOUSEENTER:
    return handle_mouse_event(ngr, input_event, type);

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
static struct PP_Var
nacl_jsrpc(struct PP_Var req)
{
  hts_mutex_lock(&nacl_jsrpc_mutex);
  int id = ++nacl_jsrpc_counter;

  nacl_dict_set_int(req, "reqid", id);

  nacl_jsrpc_t *nj = calloc(1, sizeof(nacl_jsrpc_t));
  nj->nj_reqid = id;
  LIST_INSERT_HEAD(&nacl_jsrpcs, nj, nj_link);

  ppb_messaging->PostMessage(g_Instance, req);
  ppb_var->Release(req);

  while(nj->nj_reqid != 0)
    hts_cond_wait(&nacl_jsrpc_cond, &nacl_jsrpc_mutex);

  LIST_REMOVE(nj, nj_link);

  hts_mutex_unlock(&nacl_jsrpc_mutex);

  struct PP_Var result = nj->nj_result;
  free(nj);

  return result;
}


/**
 *
 */
static void
jsrpc_handle_reply(struct PP_Var reply)
{
  nacl_jsrpc_t *nj;
  int reqid = nacl_var_dict_get_int64(reply, "reqid", 0);

  hts_mutex_lock(&nacl_jsrpc_mutex);
  LIST_FOREACH(nj, &nacl_jsrpcs, nj_link) {
    if(nj->nj_reqid == reqid)
      break;
  }

  nj->nj_result = reply;
  ppb_var->AddRef(nj->nj_result);
  nj->nj_reqid = 0;
  hts_cond_broadcast(&nacl_jsrpc_cond);
  hts_mutex_unlock(&nacl_jsrpc_mutex);
}


/**
 *
 */
void
nacl_fsinfo(uint64_t *size, uint64_t *avail, const char *fs)
{
  struct PP_Var request = ppb_vardict->Create();
  nacl_dict_set_str(request, "msgtype", "fsinfo");
  nacl_dict_set_str(request, "fs", fs);

  struct PP_Var result = nacl_jsrpc(request);

  int64_t s    = nacl_var_dict_get_int64(result, "size", 0);
  int64_t used = nacl_var_dict_get_int64(result, "used", 0);

  *size = s;
  *avail = s - used;
}


/**
 *
 */
void
nacl_dict_set_str(struct PP_Var var_dict, const char *key, const char *value)
{
  struct PP_Var var_key = ppb_var->VarFromUtf8(key, strlen(key));
  struct PP_Var var_value = ppb_var->VarFromUtf8(value, strlen(value));

  ppb_vardict->Set(var_dict, var_key, var_value);
  ppb_var->Release(var_key);
  ppb_var->Release(var_value);
}


/**
 *
 */
void
nacl_dict_set_int(struct PP_Var var_dict, const char *key, int i)
{
  struct PP_Var var_key = ppb_var->VarFromUtf8(key, strlen(key));
  struct PP_Var var_value = PP_MakeInt32(i);

  ppb_vardict->Set(var_dict, var_key, var_value);
  ppb_var->Release(var_key);
}


/**
 *
 */
void
nacl_dict_set_int64(struct PP_Var var_dict, const char *key, int64_t i)
{
  struct PP_Var var_key = ppb_var->VarFromUtf8(key, strlen(key));
  struct PP_Var var_value = PP_MakeDouble(i);

  ppb_vardict->Set(var_dict, var_key, var_value);
  ppb_var->Release(var_key);
}


/**
 *
 */
rstr_t *
nacl_var_dict_get_str(struct PP_Var dict, const char *key)
{
  rstr_t *ret = NULL;
  unsigned int len;
  struct PP_Var var_key = ppb_var->VarFromUtf8(key, strlen(key));
  struct PP_Var r = ppb_vardict->Get(dict, var_key);
  ppb_var->Release(var_key);

  const char *s = ppb_var->VarToUtf8(r, &len);
  if(s != NULL)
    ret = rstr_allocl(s, len);
  ppb_var->Release(r);
  return ret;
}


/**
 *
 */
int64_t
nacl_var_dict_get_int64(struct PP_Var dict, const char *key, int64_t def)
{
  struct PP_Var var_key = ppb_var->VarFromUtf8(key, strlen(key));
  struct PP_Var r = ppb_vardict->Get(dict, var_key);
  ppb_var->Release(var_key);

  switch(r.type) {
  case PP_VARTYPE_INT32:
    return r.value.as_int;
  case PP_VARTYPE_DOUBLE:
    return r.value.as_double;
  default:
    return def;
  }
}


/**
 *
 */
static void
Messaging_HandleMessage(PP_Instance instance, struct PP_Var v)
{
  rstr_t *type = nacl_var_dict_get_str(v, "msgtype");
  if(type == NULL)
    return;

  const char *t = rstr_get(type);
  if(!strcmp(t, "hidden")) {
    is_hidden = 1;
    media_global_hold(1, MP_HOLD_OS);
  } else if(!strcmp(t, "visible")) {
    is_hidden = 0;
    media_global_hold(0, MP_HOLD_OS);
  } else if(!strcmp(t, "openurl")) {
    rstr_t *url = nacl_var_dict_get_str(v, "url");
    if(url != NULL)
      event_dispatch(event_create_openurl(rstr_get(url)));
    rstr_release(url);

  } else if(!strcmp(t, "dndopenreply")) {
    nacl_dnd_open_reply(v);
  } else if(!strcmp(t, "dndreadreply")) {
    nacl_dnd_read_reply(v);
  } else if(!strcmp(t, "rpcreply")) {
    jsrpc_handle_reply(v);

  } else {
    TRACE(TRACE_DEBUG, "NACL", "Got unmapped event %s from browser", t);
  }

  rstr_release(type);

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
  ppb_vardict            = get_browser(PPB_VAR_DICTIONARY_INTERFACE);
  ppb_vararraybuf        = get_browser(PPB_VAR_ARRAY_BUFFER_INTERFACE);
  ppb_core               = get_browser(PPB_CORE_INTERFACE);
  ppb_view               = get_browser(PPB_VIEW_INTERFACE);
  ppb_graphics3d         = get_browser(PPB_GRAPHICS_3D_INTERFACE);
  ppb_instance           = get_browser(PPB_INSTANCE_INTERFACE);
  ppb_inputevent         = get_browser(PPB_INPUT_EVENT_INTERFACE);
  ppb_keyboardinputevent = get_browser(PPB_KEYBOARD_INPUT_EVENT_INTERFACE);
  ppb_mouseinputevent    = get_browser(PPB_MOUSE_INPUT_EVENT_INTERFACE);
  ppb_hostresolver       = get_browser(PPB_HOSTRESOLVER_INTERFACE);
  ppb_netaddress         = get_browser(PPB_NETADDRESS_INTERFACE);
  ppb_networkmonitor     = get_browser(PPB_NETWORKMONITOR_INTERFACE);
  ppb_networklist        = get_browser(PPB_NETWORKLIST_INTERFACE);
  ppb_tcpsocket          = get_browser(PPB_TCPSOCKET_INTERFACE);
  ppb_udpsocket          = get_browser(PPB_UDPSOCKET_INTERFACE);
  ppb_messageloop        = get_browser(PPB_MESSAGELOOP_INTERFACE);
  ppb_messaging          = get_browser(PPB_MESSAGING_INTERFACE);
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
