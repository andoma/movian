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
#include <limits.h>
#include <errno.h>
#include <wchar.h>

#include "glw.h"
#include "glw_video_common.h"

#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/XF86keysym.h>

#include "main.h"
#include "ui/linux/x11_common.h"
#include "ui/linux/nvidia.h"
#include "settings.h"
#include "navigator.h"

#include "glw_settings.h"
#if ENABLE_VALGRIND
#include <valgrind/callgrind.h>
#endif

#if ENABLE_VDPAU
#include "video/vdpau.h"
#endif

#include "arch/linux/linux.h"

#include "glw_rec.h"

typedef struct glw_x11 {

  glw_root_t gr;

  int running;
  hts_thread_t thread;

  Display *display;
  int screen;
  int screen_width;
  int screen_height;
  int root;
  XVisualInfo *xvi;
  Window win;
  GLXContext glxctx;
  Cursor blank_cursor;

  int cursor_hidden;

  int is_fullscreen;
  int want_fullscreen;

  setting_t *settings_mouse_btn;
  int map_mouse_wheel_to_keys;

  Colormap colormap;
  const char *displayname_real;
  const char *displayname_title;

  PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI;

  int fullwindow;
  int req_width;
  int req_height;
  int fixed_window_size;
  int no_screensaver;
  int64_t hide_cursor_at;

  XIM im;
  XIC ic;
  Status status;

  int working_vsync;

  struct x11_screensaver_state *sss;

  Atom atom_deletewindow;

  int wm_flags;
#define GX11_WM_DETECTED       0x1 // A window manager is present
#define GX11_WM_CAN_FULLSCREEN 0x2 // WM can fullscreen us

  void *nvidia;

  int vdpau_preempted;


  // Available video modes (see: ui/ui.h)
  int vmodes;

} glw_x11_t;


/**
 *
 */
static void
build_blank_cursor(glw_x11_t *gx11)
{
  char cursorNoneBits[32];
  XColor dontCare;
  Pixmap cursorNonePixmap;

  memset(cursorNoneBits, 0, sizeof( cursorNoneBits ));
  memset(&dontCare, 0, sizeof( dontCare ));
  cursorNonePixmap =
    XCreateBitmapFromData(gx11->display, gx11->root,
			  cursorNoneBits, 16, 16);

  gx11->blank_cursor = XCreatePixmapCursor(gx11->display,
					   cursorNonePixmap, cursorNonePixmap,
					   &dontCare, &dontCare, 0, 0);

  XFreePixmap(gx11->display, cursorNonePixmap);
}


/**
 *
 */
static void
hide_cursor(glw_x11_t *gx11)
{
  glw_pointer_event_t gpe = {0};

  if(gx11->cursor_hidden)
    return;

  gx11->cursor_hidden = 1;
  XDefineCursor(gx11->display, gx11->win, gx11->blank_cursor);

  gpe.type = GLW_POINTER_GONE;
  glw_lock(&gx11->gr);
  glw_pointer_event(&gx11->gr, &gpe);
  glw_unlock(&gx11->gr);
}


/**
 *
 */
static void
autohide_cursor(glw_x11_t *gx11)
{
  if(!gx11->fullwindow)
    return;

  if(gx11->cursor_hidden)
    return;

  if(gx11->gr.gr_time_usec > gx11->hide_cursor_at) {
    hide_cursor(gx11);
  }
}


/**
 *
 */
static void
show_cursor(glw_x11_t *gx11)
{
  gx11->hide_cursor_at = gx11->gr.gr_time_usec + GLW_CURSOR_AUTOHIDE_TIME;
  if(!gx11->cursor_hidden)
    return;

  gx11->cursor_hidden = 0;
  XUndefineCursor(gx11->display, gx11->win);
}

/**
 *
 */
static void
fullscreen_grab(glw_x11_t *gx11)
{
  XSync(gx11->display, False);

  while( GrabSuccess !=
	 XGrabPointer(gx11->display, gx11->win,
		      True,
		      ButtonPressMask | ButtonReleaseMask | ButtonMotionMask
		      | PointerMotionMask,
		      GrabModeAsync, GrabModeAsync,
		      gx11->win, None, CurrentTime))
    usleep(100);

  XSetInputFocus(gx11->display, gx11->win, RevertToNone, CurrentTime);
  XWarpPointer(gx11->display, None, gx11->root,
	       0, 0, 0, 0,
	       gx11->gr.gr_width / 2, gx11->gr.gr_height / 2);
  XGrabKeyboard(gx11->display,  gx11->win, False,
		GrabModeAsync, GrabModeAsync, CurrentTime);

}

/**
 *
 */
static int
check_vsync(glw_x11_t *gx11)
{
  int i;

  int64_t c;

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  glXSwapBuffers(gx11->display, gx11->win);
  c = arch_get_ts();
  for(i = 0; i < 5; i++) {
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glXSwapBuffers(gx11->display, gx11->win);
  }
  c = arch_get_ts() - c;

  return c > 25000; // Probably working
}


/**
 *
 */
static int
window_open(glw_x11_t *gx11, int fullscreen)
{
  XSetWindowAttributes winAttr;
  unsigned long mask;
  XTextProperty text;
  char buf[60];
  int fevent, x, y, w, h;

  winAttr.event_mask = KeyPressMask | KeyReleaseMask | StructureNotifyMask |
    ButtonPressMask | ButtonReleaseMask |
    PointerMotionMask | ButtonMotionMask | EnterWindowMask | LeaveWindowMask |
    ExposureMask;

  winAttr.background_pixmap = None;
  winAttr.background_pixel  = 0;
  winAttr.border_pixel      = 0;

  winAttr.colormap = gx11->colormap =
    XCreateColormap(gx11->display, gx11->root,
		    gx11->xvi->visual, AllocNone);

  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  if(fullscreen) {

    x = 0;
    y = 0;
    w = gx11->screen_width;
    h = gx11->screen_height;

    winAttr.override_redirect = True;
    mask |= CWOverrideRedirect;

  } else {

    x = gx11->screen_width  / 4;
    y = gx11->screen_height / 4;
    w = gx11->req_width  ?: 1280;
    h = gx11->req_height ?: 720;
  }

  gx11->win =
    XCreateWindow(gx11->display,
		  gx11->root,
		  x, y, w, h,
		  0,
		  gx11->xvi->depth, InputOutput,
		  gx11->xvi->visual, mask, &winAttr
		  );

  gx11->gr.gr_width  = w;
  gx11->gr.gr_height = h;

  gx11->glxctx = glXCreateContext(gx11->display, gx11->xvi, NULL, 1);

  if(gx11->glxctx == NULL) {
    TRACE(TRACE_ERROR, "GLW", "Unable to create GLX context on \"%s\"\n",
	    gx11->displayname_real);
    return 1;
  }


  glXMakeCurrent(gx11->display, gx11->win, gx11->glxctx);

  XMapWindow(gx11->display, gx11->win);

  /* Set window title */
  snprintf(buf, sizeof(buf), APPNAMEUSER);

  text.value = (unsigned char *)buf;
  text.encoding = XA_STRING;
  text.format = 8;
  text.nitems = strlen(buf);

  XSetWMName(gx11->display, gx11->win, &text);

  /* Create the window deletion atom */
  XSetWMProtocols(gx11->display, gx11->win, &gx11->atom_deletewindow, 1);

  if(fullscreen)
    fullscreen_grab(gx11);

  glw_opengl_init_context(&gx11->gr);

  if(gx11->glXSwapIntervalSGI != NULL)
    gx11->glXSwapIntervalSGI(1);

  gx11->working_vsync = check_vsync(gx11);

  if(!gx11->working_vsync) {

    if(strstr((const char *)glGetString(GL_VENDOR) ?: "", "NVIDIA")) {
      TRACE(TRACE_ERROR, "GLW",
	    "OpenGL on \"%s\" does not sync to vertical blank.\n"
	    "This is required for OpenGL interface to\n"
	    "function property. Please fix this.\n",
	    gx11->displayname_real);
      return 1;
    }

    TRACE(TRACE_INFO, "GLW",
	  "OpenGL driver does not provide adequate vertical sync "
	  "capabilities. Using soft timers");
  }

#if ENABLE_VDPAU
  vdpau_dev_t *vd = gx11->gr.gr_be.gbr_vdpau_dev;
  if(vd != NULL) {
    gx11->gr.gr_be.gbr_glVDPAUInitNV((const GLvoid *)(uintptr_t)vd->vd_dev,
                                     vd->vd_getproc);
  }
#endif

  hide_cursor(gx11);

  /* X Input method init */
  if(gx11->im != NULL) {
    gx11->ic = XCreateIC(gx11->im,
			 XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
			 XNClientWindow, gx11->win,
			 NULL);
    XGetICValues(gx11->ic, XNFilterEvents, &fevent, NULL);

    XSelectInput(gx11->display, gx11->win, fevent | winAttr.event_mask);
  } else {
    TRACE(TRACE_INFO, "GLW", "Unable to enable keyboard input compositioning");
  }
  return 0;
}

/**
 * Undo what window_open() does
 */
static void
window_close(glw_x11_t *gx11)
{
  if(gx11->ic != NULL) {
    XDestroyIC(gx11->ic);
    gx11->ic = NULL;
  }

  show_cursor(gx11);
  XDestroyWindow(gx11->display, gx11->win);
  glXDestroyContext(gx11->display, gx11->glxctx);
  XFreeColormap(gx11->display, gx11->colormap);
}


/**
 *
 */
static void
window_shutdown(glw_x11_t *gx11)
{
  glw_video_reset(&gx11->gr);

  glFlush();
  XSync(gx11->display, False);

  if(gx11->is_fullscreen) {
    XUngrabPointer(gx11->display, CurrentTime);
    XUngrabKeyboard(gx11->display, CurrentTime);
  }
  glw_lock(&gx11->gr);
  glw_flush(&gx11->gr);
  glw_unlock(&gx11->gr);
  window_close(gx11);
}


/**
 *
 */
static int
check_ext_string(const char *extensionsString, const char *extension)
{
  const char *pos = strstr(extensionsString, extension);
  return pos!=NULL && (pos==extensionsString || pos[-1]==' ') &&
    (pos[strlen(extension)]==' ' || pos[strlen(extension)]=='\0');
}




/**
 *
 */
static int
GLXExtensionSupported(Display *dpy, const char *extension)
{
  const char *s;

  s = glXQueryExtensionsString(dpy, DefaultScreen(dpy));
  if(s != NULL && check_ext_string(s, extension))
    return 1;

  s = glXGetClientString(dpy, GLX_EXTENSIONS);
  if(s != NULL && check_ext_string(s, extension))
    return 1;

  return 0;
}


/**
 * Try to figure out if we have a window manager and query some of its
 * capabilities
 */
static void
probe_wm(glw_x11_t *gx11)
{
  int i, format;
  Atom *items, type;
  unsigned long bytes_after, r, nitems;
  unsigned char *prop_return;
  int wm_window_id;
  char *wm_name;

  Atom NET_SUPPORTED =
    XInternAtom(gx11->display, "_NET_SUPPORTED", 0);
  Atom STATE_FS =
    XInternAtom(gx11->display, "_NET_WM_STATE_FULLSCREEN", 0);
  Atom NET_SUPPORTING_WM_CHECK =
    XInternAtom(gx11->display, "_NET_SUPPORTING_WM_CHECK", 0);
  Atom NET_WM_NAME =
    XInternAtom(gx11->display, "_NET_WM_NAME", 0);

  if(XGetWindowProperty(gx11->display, gx11->root, NET_SUPPORTING_WM_CHECK,
			0, 16384, False, AnyPropertyType, &type, &format, &r,
			&bytes_after, &prop_return) != Success ||
     r == 0 || prop_return == NULL) {
    TRACE(TRACE_INFO, "GLW",
	  "No window manager found (NET_SUPPORTING_WM_CHECK not set)");
    return;
  }


  wm_window_id = *(int *)prop_return;
  XFree(prop_return);
  prop_return = NULL;

  if(XGetWindowProperty(gx11->display, wm_window_id, NET_WM_NAME,
			0, 16384, False, AnyPropertyType, &type, &format, &r,
			&bytes_after, &prop_return) != Success ||
     r == 0 || prop_return == NULL) {
    TRACE(TRACE_INFO, "GLW",
	  "No window manager found (NET_WM_NAME not set on wm window)");
    return;
  }

  wm_name = mystrdupa((char *)prop_return);
  XFree(prop_return);
  prop_return = NULL;

  if(XGetWindowProperty(gx11->display, gx11->root, NET_SUPPORTED,
			0, 16384, False, AnyPropertyType, &type, &format,
			&nitems, &bytes_after, &prop_return) != Success ||
     r == 0 || prop_return == NULL) {
    TRACE(TRACE_INFO, "GLW",
	  "No window manager found (NET_SUPPORTING_WM_CHECK not set)");
    return;
  }

  items = (Atom *)prop_return;

  gx11->wm_flags |= GX11_WM_DETECTED;

  for(i = 0; i < nitems; i++) {
    if(items[i] == STATE_FS)
      gx11->wm_flags |= GX11_WM_CAN_FULLSCREEN;
  }

  TRACE(TRACE_DEBUG, "GLW", "Window manager (%s) detected%s",
	wm_name,
	gx11->wm_flags & GX11_WM_CAN_FULLSCREEN ? ", can fullscreen" : "");

  prop_set_string(prop_create(gx11->gr.gr_prop_ui, "windowmanager"),
		  wm_name);

  XFree(prop_return);
}

/**
 *
 */
static void
wm_set_fullscreen(glw_x11_t *gx11, int on)
{
  XEvent xev = {0};
  Atom STATE_FS = XInternAtom(gx11->display, "_NET_WM_STATE_FULLSCREEN", 0);
  Atom STATE = XInternAtom(gx11->display, "_NET_WM_STATE", 0);

  TRACE(TRACE_DEBUG, "GLW", "Fullscreen via windowmanager: %s",
	on ? "On" : "Off");

  xev.xclient.type = ClientMessage;
  xev.xclient.send_event = True;
  xev.xclient.message_type = STATE;
  xev.xclient.window = gx11->win;
  xev.xclient.format = 32;
  xev.xclient.data.l[0] = !!on;
  xev.xclient.data.l[1] = STATE_FS;

  XSendEvent(gx11->display, gx11->root, False,
	     SubstructureRedirectMask | SubstructureNotifyMask,
	     &xev);
}


/**
 *
 */
#if ENABLE_VDPAU
static void
vdpau_preempted(void *aux)
{
  glw_x11_t *gx11 = aux;
  gx11->vdpau_preempted = 1;
}
#endif

/**
 *
 */
static int
glw_x11_init(glw_x11_t *gx11)
{
  int attribs[10];
  int na = 0;

  int use_locales = XSupportsLocale() && XSetLocaleModifiers("@im=none") != NULL;

  if((gx11->display = XOpenDisplay(gx11->displayname_real)) == NULL) {
    TRACE(TRACE_ERROR, "GLW", "Unable to open X display \"%s\"\n",
          gx11->displayname_real);
    return 1;
  }

  if(!glXQueryExtension(gx11->display, NULL, NULL)) {
    TRACE(TRACE_ERROR, "GLW",
	  "OpenGL GLX extension not supported by display \"%s\"\n",
          gx11->displayname_real);
    return 1;
  }


  gx11->screen        = DefaultScreen(gx11->display);
  gx11->screen_width  = DisplayWidth(gx11->display, gx11->screen);
  gx11->screen_height = DisplayHeight(gx11->display, gx11->screen);
  gx11->root          = RootWindow(gx11->display, gx11->screen);

  attribs[na++] = GLX_RGBA;
  attribs[na++] = GLX_RED_SIZE;
  attribs[na++] = 1;
  attribs[na++] = GLX_GREEN_SIZE;
  attribs[na++] = 1;
  attribs[na++] = GLX_BLUE_SIZE;
  attribs[na++] = 1;
  attribs[na++] = GLX_DOUBLEBUFFER;
  attribs[na++] = None;

  gx11->xvi = glXChooseVisual(gx11->display, gx11->screen, attribs);

  if(gx11->xvi == NULL) {
    TRACE(TRACE_ERROR, "GLW", "Unable to find an adequate Visual on \"%s\"\n",
          gx11->displayname_real);
    return 1;
  }

  if(GLXExtensionSupported(gx11->display, "GLX_SGI_swap_control")) {
    TRACE(TRACE_DEBUG, "GLW", "GLX_SGI_swap_control extension is present");
    gx11->glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)
      glXGetProcAddress((const GLubyte*)"glXSwapIntervalSGI");
  }

  build_blank_cursor(gx11);

  if(use_locales)
    gx11->im = XOpenIM(gx11->display, NULL, NULL, NULL);


  gx11->atom_deletewindow =
    XInternAtom(gx11->display, "WM_DELETE_WINDOW", 0);

#if ENABLE_VDPAU

  gx11->gr.gr_be.gbr_glVDPAUInitNV =
    (PFNGLVDPAUINITNVPROC)
    glXGetProcAddress((const GLubyte*)"glVDPAUInitNV");

  if(gx11->gr.gr_be.gbr_glVDPAUInitNV != NULL) {

    vdpau_dev_t *vd;
    vd = vdpau_init_x11(gx11->display, gx11->screen, vdpau_preempted, gx11);
    gx11->gr.gr_be.gbr_vdpau_dev = vd;

    gx11->gr.gr_be.gbr_glVDPAUUnregisterSurfaceNV =
      (PFNGLVDPAUUNREGISTERSURFACENVPROC)
      glXGetProcAddress((const GLubyte*)"glVDPAUUnregisterSurfaceNV");

    gx11->gr.gr_be.gbr_glVDPAUUnmapSurfacesNV =
      (PFNGLVDPAUUNMAPSURFACESNVPROC)
      glXGetProcAddress((const GLubyte*)"glVDPAUUnmapSurfacesNV");

    gx11->gr.gr_be.gbr_glVDPAURegisterOutputSurfaceNV =
      (PFNGLVDPAUREGISTEROUTPUTSURFACENVPROC)
      glXGetProcAddress((const GLubyte*)"glVDPAURegisterOutputSurfaceNV");

    gx11->gr.gr_be.gbr_glVDPAUMapSurfacesNV =
      (PFNGLVDPAUMAPSURFACESNVPROC)
      glXGetProcAddress((const GLubyte*)"glVDPAUMapSurfacesNV");
  }
#endif

  probe_wm(gx11);


  gx11->is_fullscreen = gx11->want_fullscreen;

  int fs = 0;
  if(gx11->wm_flags == 0) {
    fs = 1; // No window manager, open in fullscreen mode
  } else {
    /* If window manager cannot do fullscreen, ask window to open
       in fullscreen mode */
    fs = gx11->want_fullscreen && !(gx11->wm_flags & GX11_WM_CAN_FULLSCREEN);
  }

  if(window_open(gx11, fs))
    return -1;

  // Fullscreen via window manager
  if(gx11->want_fullscreen && !fs)
    wm_set_fullscreen(gx11, 1);

  return 0;
}



/**
 *
 */
static void
window_change_fullscreen(glw_x11_t *gx11)
{
  if(gx11->wm_flags & GX11_WM_CAN_FULLSCREEN) {

    wm_set_fullscreen(gx11, gx11->want_fullscreen);
    gx11->is_fullscreen = gx11->want_fullscreen;

  } else {

    window_shutdown(gx11);
    if(window_open(gx11, gx11->want_fullscreen))
      exit(1);
  }
  glw_set_fullscreen(&gx11->gr, gx11->is_fullscreen);
}


/**
 * The defs are found in /usr/include/X11/keysymdef.h
 */
static const struct {
  int XK;
  int modifier;
  int action1;
  int action2;
  int action3;
} keysym2action[] = {

  { XK_Left,         0,           ACTION_LEFT },
  { XK_Right,        0,           ACTION_RIGHT },
  { XK_Up,           0,           ACTION_UP },
  { XK_Down,         0,           ACTION_DOWN },

  { XK_Left,         ShiftMask,   ACTION_MOVE_LEFT },
  { XK_Right,        ShiftMask,   ACTION_MOVE_RIGHT },
  { XK_Up,           ShiftMask,   ACTION_MOVE_UP },
  { XK_Down,         ShiftMask,   ACTION_MOVE_DOWN },

  { XK_ISO_Left_Tab, ShiftMask,   ACTION_FOCUS_PREV },

  { XK_Left,         Mod1Mask,    ACTION_NAV_BACK},
  { XK_Right,        Mod1Mask,    ACTION_NAV_FWD},

  { XK_Left,         ShiftMask | ControlMask,   ACTION_SKIP_BACKWARD},
  { XK_Right,        ShiftMask | ControlMask,   ACTION_SKIP_FORWARD},

  { XK_Prior,        0,            ACTION_PAGE_UP,   ACTION_PREV_CHANNEL, ACTION_SKIP_BACKWARD},
  { XK_Next,         0,            ACTION_PAGE_DOWN, ACTION_NEXT_CHANNEL, ACTION_SKIP_FORWARD},

  { XK_Home,         0,           ACTION_TOP},
  { XK_End,          0,           ACTION_BOTTOM},

  { XK_plus,         ControlMask, ACTION_ZOOM_UI_INCR},
  { XK_minus,        ControlMask, ACTION_ZOOM_UI_DECR},
  { XK_0,            ControlMask, ACTION_ZOOM_UI_RESET},

  { XK_F4,           Mod1Mask,    ACTION_QUIT},
  { XK_F12,          Mod1Mask,    ACTION_RECORD_UI},

  { XF86XK_AudioPause,    0,    ACTION_PLAYPAUSE},
  { XF86XK_AudioPlay,     0,    ACTION_PLAYPAUSE},
  { XF86XK_AudioStop,     0,    ACTION_STOP},
  { XF86XK_Eject,         0,    ACTION_EJECT},
  { XF86XK_AudioRecord,   0,    ACTION_RECORD},

  { XF86XK_AudioNext,     0,    ACTION_SKIP_FORWARD},
  { XF86XK_AudioPrev,     0,    ACTION_SKIP_BACKWARD},

  { XF86XK_AudioLowerVolume, 0, ACTION_VOLUME_DOWN },
  { XF86XK_AudioRaiseVolume, 0, ACTION_VOLUME_UP },
  { XF86XK_AudioMute,        0, ACTION_VOLUME_MUTE_TOGGLE },

  { XF86XK_Standby,          0, ACTION_STANDBY},
};



/**
 *
 */
static int
gl_keypress(glw_x11_t *gx11, XEvent *event)
{
  char str[16], c;
  KeySym keysym;
  int len;
  char buf[32];
  event_t *e = NULL;
  wchar_t wc;
  mbstate_t ps = {0};
  int n, i;
  char *s;
  action_type_t av[10];

  int state = event->xkey.state & (ShiftMask | ControlMask | Mod1Mask);

  XComposeStatus composestatus;

  if(gx11->ic != NULL) {
    len = Xutf8LookupString(gx11->ic,(XKeyPressedEvent*)event,
			    str, sizeof(str),
			    &keysym, &gx11->status);
  } else {
    len = XLookupString(&event->xkey, str, sizeof(str),
			&keysym, &composestatus);
  }

  if(len > 1) {
    buf[0] = 0;
    s = str;
    int something = 0;
    while((n = mbrtowc(&wc, s, len, &ps)) > 0) {
      strncpy(buf, s, n);
      buf[n] = '\0';
      e = event_create_int(EVENT_UNICODE, wc);

      glw_inject_event(&gx11->gr, e);
      s += n;
      len -= n;
      something = 1;
    }
    return something;
  } else if((state & ~ShiftMask) == 0 && len == 1) {
    c = str[0];
    switch(c) {
      /* Static key mappings, these cannot be changed */
    case 8:
      e = event_create_action_multi(
				    (const action_type_t[]){
				      ACTION_BS, ACTION_NAV_BACK}, 2);
      break;
    case 13:
      e = event_create_action_multi(
				    (const action_type_t[]){
				      ACTION_ACTIVATE, ACTION_ENTER}, 2);
      break;
    case 9:          e = event_create_action(ACTION_FOCUS_NEXT);
      break;
    case 27:
      e = event_create_action_multi(
				    (const action_type_t[]){
				      ACTION_CANCEL, ACTION_NAV_BACK}, 2);
      break;
    case 127:        e = event_create_action(ACTION_DELETE);
      break;

      /* Always send 1 char ASCII */
    default:
      if(c < 32 || c == 127)
	break;

      e = event_create_int(EVENT_UNICODE, c);
      break;
    }
  }

#if ENABLE_VALGRIND
  if(keysym == XK_F1 && state == Mod1Mask) {
    CALLGRIND_START_INSTRUMENTATION;
    printf("Callgrind started\n");
    return 0;
  }
  if(keysym == XK_F2 && state == Mod1Mask) {
    CALLGRIND_STOP_INSTRUMENTATION;
    printf("Callgrind stopped\n");
    return 0;
  }
#endif

  if(e == NULL) {

    for(i = 0; i < sizeof(keysym2action) / sizeof(*keysym2action); i++) {

      if(keysym2action[i].XK == keysym &&
	 keysym2action[i].modifier == state) {

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
  }

  if(e == NULL && keysym >= XK_F1 && keysym <= XK_F12)
    e = event_from_Fkey(keysym - XK_F1 + 1,
			event->xkey.state & ShiftMask ? 1 : 0);

  if(e == NULL
     && keysym != XK_Shift_L   && keysym != XK_Shift_R
     && keysym != XK_Control_L && keysym != XK_Control_R
     && keysym != XK_Caps_Lock && keysym != XK_Shift_Lock
     && keysym != XK_Meta_L    && keysym != XK_Meta_R
     && keysym != XK_Alt_L     && keysym != XK_Alt_R) {

    /* Construct a string representing the key */
    if(keysym != NoSymbol) {
      const char *sym = XKeysymToString(keysym);

      snprintf(buf, sizeof(buf),
	       "%s%s%s%s",
	       event->xkey.state & ShiftMask   ? "Shift+" : "",
	       event->xkey.state & Mod1Mask    ? "Alt+"   : "",
	       event->xkey.state & ControlMask ? "Ctrl+"  : "",
	       sym);

    } else {
      snprintf(buf, sizeof(buf),
	       "X11+0x%x", event->xkey.keycode);
    }
    e = event_create_str(EVENT_KEYDESC, buf);
  }
  if(e != NULL) {
    e->e_flags |= EVENT_KEYPRESS;
    glw_inject_event(&gx11->gr, e);
    return 1;
  }
  return 0;
}

/**
 *
 */
static void
update_gpu_info(glw_x11_t *gx11)
{
  prop_t *gpu = prop_create(gx11->gr.gr_prop_ui, "gpu");
  prop_set_string(prop_create(gpu, "vendor"),
		      (const char *)glGetString(GL_VENDOR));

  prop_set_string(prop_create(gpu, "name"),
		      (const char *)glGetString(GL_RENDERER));

  prop_set_string(prop_create(gpu, "driver"),
		      (const char *)glGetString(GL_VERSION));
}

/**
 *
 */
static void
glw_x11_in_fullwindow(void *opaque, int v)
{
  glw_x11_t *gx11 = opaque;
  gx11->fullwindow = v;
}


/**
 *
 */
static void
glw_x11_mainloop(glw_x11_t *gx11)
{
  XEvent event;
  int w, h;
  glw_pointer_event_t gpe = {0};
  struct timespec tp;
  int64_t start;
  int frame = 0;
  int pending_screensaver_kill = 0;

  clock_gettime(CLOCK_MONOTONIC, &tp);
  start = (int64_t)tp.tv_sec * 1000000LL + tp.tv_nsec / 1000;

  prop_sub_t *fwsub =
    prop_subscribe(0,
		   PROP_TAG_NAME("ui","fullwindow"),
		   PROP_TAG_COURIER, gx11->gr.gr_courier,
		   PROP_TAG_CALLBACK_INT, glw_x11_in_fullwindow, gx11,
		   PROP_TAG_ROOT, gx11->gr.gr_prop_ui,
		   NULL);

  if(!gx11->wm_flags || gx11->no_screensaver)
    // No window manager, disable system screen saver right away
    gx11->sss = x11_screensaver_suspend(gx11->display);

  glw_set_fullscreen(&gx11->gr, gx11->is_fullscreen);


  while(gx11->running) {

    autohide_cursor(gx11);

    if(gx11->wm_flags && !gx11->no_screensaver) {

      if(gx11->fullwindow && gx11->sss == NULL)
	gx11->sss = x11_screensaver_suspend(gx11->display);

      if(!gx11->fullwindow && gx11->sss != NULL) {
	x11_screensaver_resume(gx11->sss);
	gx11->sss = NULL;
      }
    }

    if(gx11->is_fullscreen != gx11->want_fullscreen)
      window_change_fullscreen(gx11);

    while(XPending(gx11->display)) {
      XNextEvent(gx11->display, &event);

      if(XFilterEvent(&event, gx11->win))
	continue;

      switch(event.type) {
      case Expose:
	glw_lock(&gx11->gr);
        glw_need_refresh(&gx11->gr, 0);
	glw_unlock(&gx11->gr);
	break;

      case EnterNotify:
	break;

      case LeaveNotify:
	gpe.type = GLW_POINTER_GONE;
	glw_lock(&gx11->gr);
	glw_pointer_event(&gx11->gr, &gpe);
	glw_unlock(&gx11->gr);
	break;

      case FocusIn:
	if(gx11->ic != NULL)
	  XSetICFocus(gx11->ic);
	break;
      case FocusOut:
	if(gx11->ic != NULL)
	  XUnsetICFocus(gx11->ic);
	break;
      case KeyPress:
	hide_cursor(gx11);
	pending_screensaver_kill = !gl_keypress(gx11, &event);
	break;

      case KeyRelease:
	if(pending_screensaver_kill)
	  glw_kill_screensaver(&gx11->gr);
	break;

      case ConfigureNotify:
	if(gx11->fixed_window_size)
	  break;

	w = event.xconfigure.width;
	h = event.xconfigure.height;
	glViewport(0, 0, w, h);
	glw_lock(&gx11->gr);
	gx11->gr.gr_width  = w;
	gx11->gr.gr_height = h;
	glw_unlock(&gx11->gr);
	break;


      case ClientMessage:
	if((Atom)event.xclient.data.l[0] == gx11->atom_deletewindow) {
	  /* Window manager wants us to close */
	  app_shutdown(0);
	}
	break;

      case MotionNotify:
	show_cursor(gx11);

        gpe.x =  (2.0 * event.xmotion.x / gx11->gr.gr_width ) - 1;
        gpe.y = -(2.0 * event.xmotion.y / gx11->gr.gr_height) + 1;
        gpe.ts = event.xmotion.time * 1000LL;
        gpe.type = GLW_POINTER_MOTION_UPDATE;
        glw_lock(&gx11->gr);
        glw_pointer_event(&gx11->gr, &gpe);
        glw_unlock(&gx11->gr);
	break;

      case ButtonRelease:
	gpe.x =  (2.0 * event.xmotion.x / gx11->gr.gr_width ) - 1;
	gpe.y = -(2.0 * event.xmotion.y / gx11->gr.gr_height) + 1;
        gpe.ts = event.xmotion.time * 1000LL;

	switch(event.xbutton.button) {
	case 1:
          gpe.type = GLW_POINTER_LEFT_RELEASE;
	  break;
	case 3:
	  gpe.type = GLW_POINTER_RIGHT_RELEASE;
	  break;
	default:
	  continue;
	}

	glw_lock(&gx11->gr);
	glw_pointer_event(&gx11->gr, &gpe);
	glw_unlock(&gx11->gr);
	break;

      case ButtonPress:
	gpe.x =  (2.0 * event.xmotion.x / gx11->gr.gr_width ) - 1;
	gpe.y = -(2.0 * event.xmotion.y / gx11->gr.gr_height) + 1;
        gpe.ts = event.xmotion.time * 1000LL;

	switch(event.xbutton.button) {
	case 1:
	  /* Left click */
          gpe.type = GLW_POINTER_LEFT_PRESS;
	  break;
	case 2:
	  glw_inject_event(&gx11->gr, event_create_action(ACTION_MENU));
	  continue;
	case 3:
	  /* Right click */
	  gpe.type = GLW_POINTER_RIGHT_PRESS;
	  break;
	case 4:
	  /* Scroll up */
	  if(gx11->map_mouse_wheel_to_keys) {
	    glw_inject_event(&gx11->gr, event_create_action(ACTION_UP));
	    continue;
	  } else {
	    gpe.type = GLW_POINTER_SCROLL;
	    gpe.delta_y = -0.2;
	  }
	  break;
	case 5:
	  /* Scroll down */
	  if(gx11->map_mouse_wheel_to_keys) {
	    glw_inject_event(&gx11->gr, event_create_action(ACTION_DOWN));
	    continue;
	  } else {
	    gpe.type = GLW_POINTER_SCROLL;
	    gpe.delta_y = 0.2;
	  }
	  break;

	case 7:
	  glw_inject_event(&gx11->gr, event_create_action(ACTION_MENU));
	  continue;
	case 8:
	  glw_inject_event(&gx11->gr, event_create_action(ACTION_NAV_BACK));
	  continue;
	case 9:
	  glw_inject_event(&gx11->gr, event_create_action(ACTION_NAV_FWD));
	  continue;
	default:
	  continue;
	}
	glw_lock(&gx11->gr);
	glw_pointer_event(&gx11->gr, &gpe);
	glw_unlock(&gx11->gr);
	break;

      default:
	break;
      }
    }

    glw_root_t *gr = &gx11->gr;

    glw_lock(gr);

    int flags = 0;

    if(gx11->vdpau_preempted) {
#if ENABLE_VDPAU
      if(!vdpau_reinit_x11(gr->gr_be.gbr_vdpau_dev)) {
	TRACE(TRACE_DEBUG, "VDPAU", "X11: VDPAU Reinitialized");
	gx11->vdpau_preempted = 0;
	flags |= GLW_REINITIALIZE_VDPAU;
      }
#endif
    }

    if(!gx11->is_fullscreen)
      gr->gr_screensaver_reset_at = gr->gr_frame_start;

    glw_prepare_frame(gr, flags);
    int refresh = gr->gr_need_refresh;
    gr->gr_need_refresh = 0;

    if(refresh) {
      glw_rctx_t rc;
      int zmax = 0;
      glw_rctx_init(&rc, gr->gr_width, gx11->gr.gr_height, 1, &zmax);

      glw_layout0(gr->gr_universe, &rc);

      if(refresh & GLW_REFRESH_FLAG_RENDER) {
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glw_render0(gr->gr_universe, &rc);
      }
    }
    glw_unlock(gr);

    if(refresh & GLW_REFRESH_FLAG_RENDER) {
      glw_post_scene(gr);

      if(!gx11->working_vsync) {
	int64_t deadline = frame * 1000000LL / 60 + start;
	struct timespec req;
	req.tv_sec  =  deadline / 1000000;
	req.tv_nsec = (deadline % 1000000) * 1000;

	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL);
      }
      glXSwapBuffers(gx11->display, gx11->win);
    } else {
      usleep(16666);
    }

    frame++;

#ifdef CONFIG_NVCTRL
    if(gx11->nvidia != NULL)
      nvidia_frame(gx11->nvidia);
#endif
  }

  if(gx11->sss != NULL)
    x11_screensaver_resume(gx11->sss);

  prop_unsubscribe(fwsub);

  window_shutdown(gx11);
  XFlush(gx11->display);
  XSync(gx11->display, True);
}



/**
 *
 */
static void
eventsink(void *opaque, prop_event_t event, ...)
{
  glw_x11_t *gx11 = opaque;
  va_list ap;
  event_t *e;
  va_start(ap, event);

  switch(event) {
  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(event_is_action(e, ACTION_FULLSCREEN_TOGGLE))
      gx11->want_fullscreen = !gx11->want_fullscreen;
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void *
glw_x11_thread(void *aux)
{
  glw_x11_t *gx11 = aux;
  glw_root_t *gr = &gx11->gr;

  gx11->displayname_real = getenv("DISPLAY");

  // This may aid some vsync problems with nVidia drivers
  setenv("__GL_SYNC_TO_VBLANK", "1", 1);

  if(glw_x11_init(gx11))
     return NULL;

  if(glw_init(gr))
    return NULL;

#ifdef CONFIG_NVCTRL
  gx11->nvidia = nvidia_init(gx11->display, gx11->screen,
			     gx11->gr.gr_uii.uii_prop, &gx11->vmodes,
			     gr->gr_settings, gr->gr_settings_instance,
			     gr->gr_courier);
#endif

  gx11->settings_mouse_btn =
    setting_create(SETTING_BOOL, glw_settings.gs_settings,
                   SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Map mouse wheel to up/down")),
                   SETTING_HTSMSG("map_mouse_wheel_to_keys",
                                  glw_settings.gs_settings_store, "glw"),
                   SETTING_COURIER(gr->gr_courier),
                   SETTING_WRITE_BOOL(&gx11->map_mouse_wheel_to_keys),
                   NULL);

  prop_sub_t *evsub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK, eventsink, gx11,
		   PROP_TAG_NAME("ui", "eventSink"),
		   PROP_TAG_ROOT, gr->gr_prop_ui,
		   PROP_TAG_COURIER, gr->gr_courier,
		   NULL);


  gx11->want_fullscreen |= !!gconf.fullscreen;

  update_gpu_info(gx11);
  glw_lock(gr);
  glw_load_universe(gr);
  glw_unlock(gr);

  glw_x11_mainloop(gx11);

  glw_lock(gr);
  glw_unload_universe(gr);
  glw_unlock(gr);
  glw_reap(gr);
  glw_reap(gr);

  setting_destroy(gx11->settings_mouse_btn);

  prop_unsubscribe(evsub);

  glw_fini(gr);
  return NULL;
}



/**
 *
 */
static void *
glw_x11_start(struct prop *nav)
{
  glw_x11_t *gx11 = calloc(1, sizeof(glw_x11_t));

  gx11->gr.gr_prop_ui = prop_create_root("ui");
  gx11->gr.gr_prop_nav = nav ?: nav_spawn();
  gx11->running = 1;

  hts_thread_create_joinable("glw", &gx11->thread,
			     glw_x11_thread, gx11, 0);

  return gx11;
}

/**
 *
 */
static prop_t *
glw_x11_stop(void *aux)
{
  glw_x11_t *gx11 = aux;
  glw_root_t *gr = &gx11->gr;
  prop_t *nav = gr->gr_prop_nav;
  gx11->running = 0;
  hts_thread_join(&gx11->thread);
  prop_destroy(gr->gr_prop_ui);
  free(gx11);
  return nav;
}



const linux_ui_t ui_glw = {
  .start = glw_x11_start,
  .stop  = glw_x11_stop,
};
