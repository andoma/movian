/*
 *  Code for using X11 as system glue
 *  Copyright (C) 2007 Andreas Öman
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
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <wchar.h>

#include "glw.h"
#include "glw_video_opengl.h"

#include <GL/glx.h>
#include <GL/glu.h>
#include <X11/Xatom.h>
#include <X11/XF86keysym.h>

#include "showtime.h"
#include "ui/linux/screensaver_inhibitor.h"
#include "ui/linux/nvidia.h"
#include "settings.h"
#include "ipc/ipc.h"

typedef struct glw_x11 {

  glw_root_t gr;

  hts_thread_t threadid;

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

  float aspect_ratio;

  int is_fullscreen;
  int want_fullscreen;

  int map_mouse_wheel_to_keys;

  Colormap colormap;
  const char *displayname_real;
  const char *displayname_title;

  char *config_name;

  int coords[2][4];
  Atom deletewindow;

  PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI;

  int font_size;
  int want_font_size;
  setting_t *font_size_setting;


  prop_t *prop_display;
  prop_t *prop_gpu;

  setting_t *fullscreen_setting;

  int fullwindow;
  int autohide_counter;
  
  XIM im;
  XIC ic;
  Status status;

  int saveconf;

  int working_vsync;
  int force_no_vsync;
} glw_x11_t;

#define AUTOHIDE_TIMEOUT 100 // XXX: in frames.. bad

static void glw_x11_dispatch_event(uii_t *uii, event_t *e);




static void update_gpu_info(glw_x11_t *gx11);


/**
 * Save display settings
 */
static void
display_settings_save(glw_x11_t *gx11)
{
  htsmsg_t *m = htsmsg_create_map();

  htsmsg_add_u32(m, "fullscreen", gx11->want_fullscreen);
  htsmsg_add_u32(m, "fontsize",   gx11->want_font_size);
  htsmsg_add_u32(m, "map_mouse_wheel_to_keys", gx11->map_mouse_wheel_to_keys);

  htsmsg_store_save(m, "displays/%s", gx11->config_name);
  htsmsg_destroy(m);
}


/**
 * Switch displaymode, we just set a variable and let mainloop switch
 * later on
 */
static void
display_set_mode(void *opaque, int value)
{
  glw_x11_t *gx11 = opaque;
  gx11->want_fullscreen = value;
  gx11->saveconf = 1;
}


/**
 * Use can remap mousewheel to up/down key actions
 */
static void
display_set_map_mouse_wheel_to_keys(void *opaque, int value)
{
  glw_x11_t *gx11 = opaque;
  gx11->map_mouse_wheel_to_keys = value;
  gx11->saveconf = 1;
}


/**
 * Change fontsize
 */
static void
display_set_fontsize(void *opaque, int value)
{
  glw_x11_t *gx11 = opaque;
  gx11->want_font_size = value;
  gx11->saveconf = 1;
}


/**
 * Add a settings pane with relevant settings
 */
static void
display_settings_init(glw_x11_t *gx11)
{
  prop_t *r;
  char title[256];
  htsmsg_t *settings = htsmsg_store_load("displays/%s", gx11->config_name);

  if(gx11->displayname_title) {
    snprintf(title, sizeof(title), "Display settings for GLW/X11 on screen %s",
	     gx11->displayname_title);
  } else {
    snprintf(title, sizeof(title), "Display settings for GLW/X11");
  }

  r = settings_add_dir(NULL, "display", title, "display");
  
  gx11->fullscreen_setting = settings_add_bool(r, "fullscreen",
					       "Fullscreen mode", 0, settings,
					       display_set_mode, gx11,
					       SETTINGS_INITIAL_UPDATE);

  settings_add_bool(r, "map_mouse_wheel_to_keys",
		    "Map mouse wheel to up/down", 0, settings,
		    display_set_map_mouse_wheel_to_keys, gx11,
		    SETTINGS_INITIAL_UPDATE);

  gx11->font_size_setting = 
    settings_add_int(r, "fontsize",
		     "Font size", 20, settings, 14, 40, 1,
		     display_set_fontsize, gx11,
		     SETTINGS_INITIAL_UPDATE, "px");

  gx11->saveconf = 0; // Don't need to save, cause we just loaded

  htsmsg_destroy(settings);
}


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
  glw_pointer_event_t gpe;

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
  if(gx11->cursor_hidden)
    return;

  if(gx11->autohide_counter == 0)
    hide_cursor(gx11);
  else
    gx11->autohide_counter--;
}
  

/**
 *
 */
static void
show_cursor(glw_x11_t *gx11)
{
  if(!gx11->cursor_hidden)
    return;

  gx11->autohide_counter = AUTOHIDE_TIMEOUT;
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
	       gx11->coords[0][2] / 2, gx11->coords[0][3] / 2);
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
  c = showtime_get_ts();
  for(i = 0; i < 5; i++) {
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glXSwapBuffers(gx11->display, gx11->win);
  }
  c = showtime_get_ts() - c;

  return c > 25000; // Probably working
}


/**
 *
 */
static int
window_open(glw_x11_t *gx11)
{
  XSetWindowAttributes winAttr;
  unsigned long mask;
  int fullscreen = gx11->want_fullscreen;
  XTextProperty text;
  extern char *htsversion;
  char buf[60];
  int fevent;

  winAttr.event_mask = KeyPressMask | StructureNotifyMask |
    ButtonPressMask | ButtonReleaseMask |
    PointerMotionMask | ButtonMotionMask | EnterWindowMask;

  winAttr.background_pixmap = None;
  winAttr.background_pixel  = 0;
  winAttr.border_pixel      = 0;

  winAttr.colormap = gx11->colormap = 
    XCreateColormap(gx11->display, gx11->root,
		    gx11->xvi->visual, AllocNone);
  
  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  gx11->coords[0][0] = gx11->screen_width  / 4;
  gx11->coords[0][1] = gx11->screen_height / 4;
  gx11->coords[0][2] = 853; //gx11->screen_width  * 3 / 4;
  gx11->coords[0][3] = 480; //gx11->screen_height * 3 / 4;

  gx11->coords[1][0] = 0;
  gx11->coords[1][1] = 0;
  gx11->coords[1][2] = gx11->screen_width;
  gx11->coords[1][3] = gx11->screen_height;

  if(fullscreen) {

    winAttr.override_redirect = True;
    mask |= CWOverrideRedirect;
  }

  gx11->aspect_ratio =
    (float)gx11->coords[fullscreen][2] / 
    (float)gx11->coords[fullscreen][3];

  gx11->win = 
    XCreateWindow(gx11->display,
		  gx11->root,
		  gx11->coords[fullscreen][0],
		  gx11->coords[fullscreen][1],
		  gx11->coords[fullscreen][2],
		  gx11->coords[fullscreen][3],
		  0,
		  gx11->xvi->depth, InputOutput,
		  gx11->xvi->visual, mask, &winAttr
		  );

  gx11->gr.gr_width  = gx11->coords[fullscreen][2];
  gx11->gr.gr_height = gx11->coords[fullscreen][3];

  gx11->glxctx = glXCreateContext(gx11->display, gx11->xvi, NULL, 1);

  if(gx11->glxctx == NULL) {
    TRACE(TRACE_ERROR, "GLW", "Unable to create GLX context on \"%s\"\n",
	    gx11->displayname_real);
    return 1;
  }


  glXMakeCurrent(gx11->display, gx11->win, gx11->glxctx);

  XMapWindow(gx11->display, gx11->win);

  /* Set window title */
  snprintf(buf, sizeof(buf), "HTS Showtime %s", htsversion);

  text.value = (unsigned char *)buf;
  text.encoding = XA_STRING;
  text.format = 8;
  text.nitems = strlen(buf);
  
  XSetWMName(gx11->display, gx11->win, &text);

  /* Create the window deletion atom */
  gx11->deletewindow = XInternAtom(gx11->display, "WM_DELETE_WINDOW",
				      0);

  XSetWMProtocols(gx11->display, gx11->win, &gx11->deletewindow, 1);

  if(fullscreen)
    fullscreen_grab(gx11);

  gx11->is_fullscreen = gx11->want_fullscreen;

  update_gpu_info(gx11);

  glw_opengl_init_context(&gx11->gr);

  if(gx11->glXSwapIntervalSGI != NULL && gx11->force_no_vsync ==0)
    gx11->glXSwapIntervalSGI(1);

  gx11->working_vsync = check_vsync(gx11);

  if(!gx11->working_vsync) {

    if(strstr((const char *)glGetString(GL_VENDOR) ?: "", "NVIDIA")) {
      TRACE(TRACE_ERROR, "GLW", 
	    "OpenGL on \"%s\" does not sync to vertical blank.\n"
	    "This is required for Showtime's OpenGL interface to\n"
	    "function property. Please fix this.\n",
	    gx11->displayname_real);
      return 1;
    }

    TRACE(TRACE_INFO, "GLW", 
	  "OpenGL driver does not provide adequate vertical sync "
	  "capabilities. Using soft timers");
  }

  hide_cursor(gx11);
  
  /* X Input method init */
  if(gx11->im != NULL) {
    gx11->ic = XCreateIC(gx11->im,
			 XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
			 XNClientWindow, gx11->win,
			 NULL);
    XGetICValues(gx11->ic, XNFilterEvents, &fevent, NULL);

    XSelectInput(gx11->display, gx11->win, fevent | winAttr.event_mask);
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
  glw_video_global_flush(&gx11->gr);

  glFlush();
  XSync(gx11->display, False);

  if(gx11->is_fullscreen) {
    XUngrabPointer(gx11->display, CurrentTime);
    XUngrabKeyboard(gx11->display, CurrentTime);
  }
  glw_flush0(&gx11->gr);
  window_close(gx11);
}


/**
 *
 */
static void
window_change_displaymode(glw_x11_t *gx11)
{
  window_shutdown(gx11);
  if(window_open(gx11))
    exit(1);
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
 *
 */
static int
glw_x11_init(glw_x11_t *gx11)
{
  int attribs[10];
  int na = 0;
  
  if(!XSupportsLocale()) {
    TRACE(TRACE_ERROR, "GLW", "XSupportsLocale returned false");
    return 1;
  }

  if(XSetLocaleModifiers("") == NULL) {
    TRACE(TRACE_ERROR, "GLW", "XSetLocaleModifiers returned NULL");
    return 1;
  }

  gx11->prop_display = prop_create(gx11->gr.gr_uii.uii_prop, "display");
  gx11->prop_gpu     = prop_create(gx11->gr.gr_uii.uii_prop, "gpu");

  display_settings_init(gx11);

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

  screensaver_inhibitor_init(gx11->displayname_real);

  build_blank_cursor(gx11);

  gx11->im = XOpenIM(gx11->display, NULL, NULL, NULL);

#ifdef CONFIG_NVCTRL
  nvidia_init(gx11->display, gx11->screen);
#endif

  return window_open(gx11);
}


/**
 *
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
  { XK_Page_Up,      0,           ACTION_PAGE_UP,     ACTION_CHANNEL_PREV },
  { XK_Page_Down,    0,           ACTION_PAGE_DOWN,   ACTION_CHANNEL_NEXT },
  { XK_Home,         0,           ACTION_TOP },
  { XK_End,          0,           ACTION_BOTTOM },

  { XK_ISO_Left_Tab, ShiftMask,   ACTION_FOCUS_PREV },
  
  { XK_Left,         Mod1Mask,    ACTION_NAV_BACK },
  { XK_Right,        Mod1Mask,    ACTION_NAV_FWD },

  { '+',             ControlMask, ACTION_ZOOM_UI_INCR },
  { XK_KP_Add,       ControlMask, ACTION_ZOOM_UI_INCR },
  { '-',             ControlMask, ACTION_ZOOM_UI_DECR },
  { XK_KP_Subtract,  ControlMask, ACTION_ZOOM_UI_DECR },

  { XK_F11,          0,           ACTION_FULLSCREEN_TOGGLE },
  
  { XF86XK_AudioLowerVolume, 0,   ACTION_VOLUME_DOWN },
  { XF86XK_AudioRaiseVolume, 0,   ACTION_VOLUME_UP },
  { XF86XK_AudioMute,        0,   ACTION_VOLUME_MUTE_TOGGLE },

  { XF86XK_Back,             0,   ACTION_NAV_BACK },
  { XF86XK_Forward,          0,   ACTION_NAV_FWD },
  { XF86XK_AudioPlay,        0,   ACTION_PLAYPAUSE },
  { XF86XK_AudioStop,        0,   ACTION_STOP },
  { XF86XK_AudioPrev,        0,   ACTION_PREV_TRACK },
  { XF86XK_AudioNext,        0,   ACTION_NEXT_TRACK },
  { XF86XK_Eject,            0,   ACTION_EJECT },
  { XF86XK_AudioMedia,       0,   ACTION_MENU },
  { XK_Menu,                 0,   ACTION_MENU },
  { XK_F1,                   0,   ACTION_MENU },
  
  { XK_F1,                   ShiftMask,   ACTION_PREV_TRACK },
  { XK_F2,                   ShiftMask,   ACTION_PLAYPAUSE },
  { XK_F3,                   ShiftMask,   ACTION_NEXT_TRACK },
  { XK_F4,                   ShiftMask,   ACTION_STOP },

  { XK_F5,                   ShiftMask,   ACTION_VOLUME_DOWN },
  { XK_F6,                   ShiftMask,   ACTION_VOLUME_MUTE_TOGGLE },
  { XK_F7,                   ShiftMask,   ACTION_VOLUME_UP },

  { XK_F1,                   ControlMask,   ACTION_SEEK_BACKWARD },
  { XK_F3,                   ControlMask,   ACTION_SEEK_FORWARD },
  
  { XF86XK_Sleep,            0,           ACTION_SLEEP },

};



/**
 *
 */
static void
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
    while((n = mbrtowc(&wc, s, len, &ps)) > 0) {
      strncpy(buf, s, n);
      buf[n] = '\0';
      e = event_create_unicode(wc);

      glw_x11_dispatch_event(&gx11->gr.gr_uii, e);
      s += n;
      len -= n;
    }
    return;
  } else if((state & ~ShiftMask) == 0 && len == 1) {
    c = str[0];
    switch(c) {
      /* Static key mappings, these cannot be changed */
    case 8:          
      e = event_create_action_multi(
				    (const action_type_t[]){
				      ACTION_BS, ACTION_NAV_BACK}, 2);
      break;
    case 13:         e = event_create_action(ACTION_ENTER);     break;
    case 27:         e = event_create_action(ACTION_CLOSE);     break;
    case 9:          e = event_create_action(ACTION_FOCUS_NEXT);break;
      /* Always send 1 char ASCII */
    default:
      if(c < 32 || c == 127)
	break;

      e = event_create_unicode(c);
      break;
    }
  }

  if(e == NULL) {

    for(i = 0; i < sizeof(keysym2action) / sizeof(*keysym2action); i++) {
      
      if(keysym2action[i].XK == keysym &&
	 keysym2action[i].modifier == state) {
	
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
    glw_x11_dispatch_event(&gx11->gr.gr_uii, e);

}

/**
 *
 */
static void
update_gpu_info(glw_x11_t *gx11)
{
  prop_t *gpu = gx11->prop_gpu;
  prop_set_string(prop_create(gpu, "vendor"),
		      (const char *)glGetString(GL_VENDOR));

  prop_set_string(prop_create(gpu, "name"),
		      (const char *)glGetString(GL_RENDERER));

  prop_set_string(prop_create(gpu, "driver"),
		      (const char *)glGetString(GL_VERSION));
}


/**
 * Master scene rendering
 */
static void 
layout_draw(glw_x11_t *gx11, float aspect)
{
  glw_rctx_t rc;

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  memset(&rc, 0, sizeof(rc));
  rc.rc_size_x = gx11->gr.gr_width;
  rc.rc_size_y = gx11->gr.gr_height;
  glw_layout0(gx11->gr.gr_universe, &rc);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  gluLookAt(0, 0, 1 / tan(45 * M_PI / 360),
	    0, 0, 1,
	    0, 1, 0);

  rc.rc_alpha = 1.0f;
  glw_render0(gx11->gr.gr_universe, &rc);
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
  glw_pointer_event_t gpe;

  struct timespec tp;
  int64_t start;
  int frame;

  clock_gettime(CLOCK_MONOTONIC, &tp);
  start = (int64_t)tp.tv_sec * 1000000LL + tp.tv_nsec / 1000;

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","fullwindow"),
		 PROP_TAG_CALLBACK_INT, glw_x11_in_fullwindow, gx11,
		 PROP_TAG_ROOT, gx11->gr.gr_uii.uii_prop,
		 NULL);

  while(1) {

    if(gx11->fullwindow)
      autohide_cursor(gx11);

    if(gx11->is_fullscreen != gx11->want_fullscreen) {
      glw_lock(&gx11->gr);
      window_change_displaymode(gx11);
      glw_unlock(&gx11->gr);
    }

    if(gx11->font_size != gx11->want_font_size) {

      gx11->font_size = gx11->want_font_size;
      glw_lock(&gx11->gr);
      glw_font_change_size(&gx11->gr, gx11->font_size);
      glw_unlock(&gx11->gr);
    }

    if(gx11->saveconf) {
      gx11->saveconf = 0;
      display_settings_save(gx11);
    }

    while(XPending(gx11->display)) {
      XNextEvent(gx11->display, &event);

      if(XFilterEvent(&event, gx11->win))
	continue;
      
      switch(event.type) {
      case EnterNotify:
	// This is _a_bit_ ugly
#ifdef CONFIG_DBUS
	mpkeys_grab();
#endif
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
	gl_keypress(gx11, &event);
	break;

      case ConfigureNotify:
	w = event.xconfigure.width;
	h = event.xconfigure.height;
	glViewport(0, 0, w, h);
	gx11->aspect_ratio = (float)w / (float)h;
	gx11->gr.gr_width  = w;
	gx11->gr.gr_height = h;
	break;


      case ClientMessage:
	if((Atom)event.xclient.data.l[0] == gx11->deletewindow) {
	  /* Window manager wants us to close */
	  showtime_shutdown(0);
	}
	break;
	  
      case MotionNotify:
	show_cursor(gx11);

	gpe.x =  (2.0 * event.xmotion.x / gx11->gr.gr_width ) - 1;
	gpe.y = -(2.0 * event.xmotion.y / gx11->gr.gr_height) + 1;
	gpe.type = GLW_POINTER_MOTION;

	glw_lock(&gx11->gr);
	glw_pointer_event(&gx11->gr, &gpe);
	glw_unlock(&gx11->gr);
	break;
	  
      case ButtonRelease:
	if(event.xbutton.button == 1) {
	  gpe.x =  (2.0 * event.xmotion.x / gx11->gr.gr_width ) - 1;
	  gpe.y = -(2.0 * event.xmotion.y / gx11->gr.gr_height) + 1;
	  gpe.type = GLW_POINTER_RELEASE;
	  glw_lock(&gx11->gr);
	  glw_pointer_event(&gx11->gr, &gpe);
	  glw_unlock(&gx11->gr);
	}
	break;

      case ButtonPress:
	gpe.x =  (2.0 * event.xmotion.x / gx11->gr.gr_width ) - 1;
	gpe.y = -(2.0 * event.xmotion.y / gx11->gr.gr_height) + 1;


	switch(event.xbutton.button) {
	case 1:
	  /* Left click */
	  gpe.type = GLW_POINTER_CLICK;
	  break;
	case 4:
	  /* Scroll up */
	  if(gx11->map_mouse_wheel_to_keys) {
	    glw_x11_dispatch_event(&gx11->gr.gr_uii,
				   event_create_action(ACTION_UP));
	    goto noevent;
	  } else {
	    gpe.type = GLW_POINTER_SCROLL;
	    gpe.delta_y = -0.2;
	  }
	  break;
	case 5:
	  /* Scroll down */
	  if(gx11->map_mouse_wheel_to_keys) {
	    glw_x11_dispatch_event(&gx11->gr.gr_uii,
				   event_create_action(ACTION_DOWN));
	    goto noevent;
	    
	  } else {
	    gpe.type = GLW_POINTER_SCROLL;
	    gpe.delta_y = 0.2;
	  }
	  break;

	default:
	  goto noevent;
	}
	glw_lock(&gx11->gr);
	glw_pointer_event(&gx11->gr, &gpe);
	glw_unlock(&gx11->gr);
      noevent:
	break;

      default:
	break;
      }
    }
    glw_lock(&gx11->gr);
    glw_prepare_frame(&gx11->gr);
    layout_draw(gx11, gx11->aspect_ratio);
    glw_unlock(&gx11->gr);

    frame++;

    if(!gx11->working_vsync) {
      int64_t deadline = frame * 1000000LL / 60 + start;
      struct timespec req;
      req.tv_sec  =  deadline / 1000000;
      req.tv_nsec = (deadline % 1000000) * 1000;

      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL);
    }
    glXSwapBuffers(gx11->display, gx11->win);
  }
  window_shutdown(gx11);
}



/**
 *
 */
static int
glw_x11_start(ui_t *ui, int argc, char *argv[], int primary)
{
  glw_x11_t *gx11 = calloc(1, sizeof(glw_x11_t));
  char confname[256];
  const char *theme_path = SHOWTIME_GLW_DEFAULT_THEME_URL;

  gx11->displayname_real = getenv("DISPLAY");
  snprintf(confname, sizeof(confname), "glw/x11/default");
  gx11->displayname_title  = NULL;

  /* Parse options */

  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "--display") && argc > 1) {
      gx11->displayname_real = argv[1];
      snprintf(confname, sizeof(confname), "glw/x11/%s", argv[1]);
      gx11->displayname_title  = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--theme") && argc > 1) {
      theme_path = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "--force-no-vsync")) {
      gx11->force_no_vsync = 1;
      argc -= 1; argv += 1;
      continue;
    } else {
      break;
    }
  }

  // This may aid some vsync problems with nVidia drivers
  if(!gx11->force_no_vsync)
    setenv("__GL_SYNC_TO_VBLANK", "1", 1);


  gx11->config_name = strdup(confname);

  gx11->want_font_size = 20;

  if(glw_x11_init(gx11))
     return 1;

  if(glw_init(&gx11->gr, gx11->want_font_size, theme_path, ui, primary))
    return 1;

  glw_x11_mainloop(gx11);

  return 0;
}

/**
 *
 */
static void
glw_x11_dispatch_event(uii_t *uii, event_t *e)
{
  glw_x11_t *gx11 = (glw_x11_t *)uii;

  /* Take care of any X11 specific events first */
  
  if(event_is_action(e, ACTION_FULLSCREEN_TOGGLE)) {
    settings_toggle_bool(gx11->fullscreen_setting);
  } else if(event_is_action(e, ACTION_ZOOM_UI_INCR)) {

    settings_set_int(gx11->font_size_setting, gx11->want_font_size + 1);

  } else if(event_is_action(e, ACTION_ZOOM_UI_DECR)) {

    settings_set_int(gx11->font_size_setting, gx11->want_font_size - 1);

  } else {
    /* Pass it on to GLW */
    glw_dispatch_event(uii, e);
    return;

  }
  event_unref(e);
}


/**
 *
 */
static void
glw_x11_stop(uii_t *uii)
{
  glw_x11_t *gx11 = (glw_x11_t *)uii;
  // Prevent it from running any more, Perhaps a bit ugly, but we're gonna
  // exit() very soon, so who cares.
  glw_lock(&gx11->gr);
}


/**
 *
 */
ui_t glw_ui = {
  .ui_title = "glw",
  .ui_start = glw_x11_start,
  .ui_dispatch_event = glw_x11_dispatch_event,
  .ui_stop = glw_x11_stop,
};
