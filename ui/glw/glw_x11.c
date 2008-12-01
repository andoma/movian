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
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "glw.h"
#include "glw_video.h"

#include <GL/glx.h>
#include <GL/glu.h>
#include <X11/Xatom.h>

#include "showtime.h"
#include "hid/keymapper.h"
#include "settings.h"

typedef struct glw_x11 {

  glw_root_t gr;

  hts_thread_t threadid;

  int running;

  glw_t *universe;

  Display *display;
  int screen;
  int screen_width;
  int screen_height;
  int root;
  XVisualInfo *xvi;
  Window win;
  GLXContext glxctx;
  float aspect_ratio;

  int is_fullscreen;
  int want_fullscreen;

  Colormap colormap;
  const char *displayname;
  int coords[2][4];
  int do_videosync;
  struct {
    enum {
      X11SS_NONE,
      X11SS_XSCREENSAVER,
      X11SS_GNOME,
    }     mode;
    int   interval;
    pid_t pid;
  } screensaver;
  Atom deletewindow;

  PFNGLXGETVIDEOSYNCSGIPROC glXGetVideoSyncSGI;
  PFNGLXWAITVIDEOSYNCSGIPROC glXWaitVideoSyncSGI;

} glw_x11_t;

static void update_gpu_info(void);




/**
 * Save display settings
 */
static void
display_settings_save(glw_x11_t *gx11)
{
  htsmsg_t *m = htsmsg_create();

  htsmsg_add_u32(m, "fullscreen", gx11->want_fullscreen);
  
  hts_settings_save(m, "display");
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
}


/**
 * Add a settings pane with relevant settings
 */
static void
display_settings_init(glw_x11_t *gx11)
{
  prop_t *r;

  htsmsg_t *settings = hts_settings_load("display");

  r = settings_add_dir(NULL, "display", "Display settings");
  
  settings_add_bool(r, "fullscreen",
		    "Fullscreen mode", 0, settings,
		    display_set_mode, gx11);

  htsmsg_destroy(settings);
}


/**
 *
 */
static Cursor
blank_cursor(glw_x11_t *gx11)
{
  Cursor blank = None;
  char cursorNoneBits[32];
  XColor dontCare;
  Pixmap cursorNonePixmap;

  memset(cursorNoneBits, 0, sizeof( cursorNoneBits ));
  memset(&dontCare, 0, sizeof( dontCare ));
  cursorNonePixmap =
    XCreateBitmapFromData(gx11->display, gx11->root,
			  cursorNoneBits, 16, 16);

  blank = XCreatePixmapCursor(gx11->display,
			      cursorNonePixmap, cursorNonePixmap,
			      &dontCare, &dontCare, 0, 0);

  XFreePixmap(gx11->display, cursorNonePixmap);

  return blank;
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
static void
window_open(glw_x11_t *gx11)
{
  XSetWindowAttributes winAttr;
  unsigned long mask;
  int fullscreen = gx11->want_fullscreen;
  XTextProperty text;
  extern char *htsversion;
  char buf[60];

  winAttr.event_mask        = KeyPressMask | StructureNotifyMask;
  winAttr.background_pixmap = None;
  winAttr.background_pixel  = 0;
  winAttr.border_pixel      = 0;

  winAttr.colormap = gx11->colormap = 
    XCreateColormap(gx11->display, gx11->root,
		    gx11->xvi->visual, AllocNone);
  
  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  gx11->coords[0][0] = gx11->screen_width  / 4;
  gx11->coords[0][1] = gx11->screen_height / 4;
  gx11->coords[0][2] = gx11->screen_width  * 3 / 4;
  gx11->coords[0][3] = gx11->screen_height * 3 / 4;

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

  gx11->glxctx = glXCreateContext(gx11->display, gx11->xvi, NULL, 1);

  if(gx11->glxctx == NULL) {
    fprintf(stderr, "Unable to create GLX context on \"%s\"\n",
	    gx11->displayname);
    exit(1);
  }


  glXMakeCurrent(gx11->display, gx11->win, gx11->glxctx);

  XMapWindow(gx11->display, gx11->win);

  /* Make an empty / blank cursor */

  XDefineCursor(gx11->display, gx11->win, blank_cursor(gx11));


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

  if(getenv("__GL_SYNC_TO_VBLANK") == 0) {
    gx11->do_videosync = 1;
    fprintf(stderr, 
	    "Display: Using 'glXWaitVideoSyncSGI' for vertical sync\n");
  } else {
    fprintf(stderr, 
	    "Display: Using '__GL_SYNC_TO_VBLANK' for vertical sync\n");
  }

  gx11->is_fullscreen = gx11->want_fullscreen;

  update_gpu_info();

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  /* Load fragment shaders */
  glw_video_global_init(&gx11->gr);

}

/**
 * Undo what window_open() does
 */
static void
window_close(glw_x11_t *gx11)
{
  XUndefineCursor(gx11->display, gx11->win);
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
  window_open(gx11);
  display_settings_save(gx11);
}


/**
 *
 */
static int
GLXExtensionSupported(Display *dpy, const char *extension)
{
    const char *extensionsString, *pos;

    extensionsString = glXQueryExtensionsString(dpy, DefaultScreen(dpy));
    if(extensionsString == NULL)
      return 0;

    pos = strstr(extensionsString, extension);

    return pos!=NULL && (pos==extensionsString || pos[-1]==' ') &&
        (pos[strlen(extension)]==' ' || pos[strlen(extension)]=='\0');
}



/**
 * This thread keeps the screensaver from starting up.
 */
static void *
screensaver_inhibitor(void *aux)
{
  glw_x11_t *gx11 = aux;

  while (gx11->screensaver.interval > 0) {
    sleep(gx11->screensaver.interval);
    switch (gx11->screensaver.mode)
      {
      case X11SS_XSCREENSAVER:
	XResetScreenSaver(gx11->display);
	break;
	
      case X11SS_GNOME:
	if (system("gnome-screensaver-command -p") == -1) {
	  static int failcnt = 0;
	  printf("gnome-screensaver-command -p failed: %s", strerror(errno));
	  if (++failcnt > 10) {
	    printf("giving up screensaver inhibitor\n");
	    gx11->screensaver.mode = X11SS_NONE;
	    gx11->screensaver.interval = 0;
	  }
	}
	break;
	
      default:
	break;
      }
  }

  return NULL;
}


/**
 * Try to figure out which screensaver to inhibit
 */
static int
screensaver_query(glw_x11_t *gx11)
{
  int r;
  int tmo, itvl, prefbl, alexp;

  /* Try xscreensaver */
  r = XGetScreenSaver(gx11->display, &tmo, &itvl, &prefbl, &alexp);
  if (r == 1 && tmo > 0) {
    gx11->screensaver.mode = X11SS_XSCREENSAVER;
    gx11->screensaver.interval = tmo / 2 + 5;
    printf("Using xscreensaver (interval %i)\n",
	   gx11->screensaver.interval);
    return 0;
  }

  /* Try gnome-screensaver */
  if (system("gnome-screensaver-command -p") == 0) {
    gx11->screensaver.mode = X11SS_GNOME;
    gx11->screensaver.interval = 30;
    printf("Using gnome screensaver (interval %i)\n",
	   gx11->screensaver.interval);
    return 0;
  }
  
  gx11->screensaver.mode = X11SS_NONE;
  gx11->screensaver.interval = 0;
  printf("No screensaver will be inhibited\n");
  return -1;
}


static void
screensaver_inhibitor_init(glw_x11_t *gx11)
{
  hts_thread_t tid;

  if(screensaver_query(gx11) == -1)
    return;

  hts_thread_create_detached(&tid, screensaver_inhibitor, gx11);
}



/**
 *
 */
static void
glw_x11_init(glw_x11_t *gx11)
{
  int attribs[10];
  int na = 0;

#if 0
  prop_display = prop_create(prop_get_global(), "display");
  prop_gpu     = prop_create(prop_get_global(), "gpu");

  prop_display_refreshrate = 
    prop_create(prop_display, "refreshrate");
#endif

  gx11->displayname = getenv("DISPLAY");

  display_settings_init(gx11);

  if((gx11->display = XOpenDisplay(gx11->displayname)) == NULL) {
    fprintf(stderr, "Unable to open X display \"%s\"\n", gx11->displayname);
    exit(1);
  }

  if(!glXQueryExtension(gx11->display, NULL, NULL)) {
    fprintf(stderr, "OpenGL GLX extension not supported by display \"%s\"\n",
	    gx11->displayname);
    exit(1);
  }

  if(!GLXExtensionSupported(gx11->display, "GLX_SGI_video_sync")) {
    fprintf(stderr,
	    "OpenGL GLX extension GLX_SGI_video_sync is not supported "
	    "by display \"%s\"\n",
	    gx11->displayname);
    exit(1);
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
    fprintf(stderr, "Unable to find an adequate Visual on \"%s\"\n",
	    gx11->displayname);
    exit(1);
  }

  gx11->glXGetVideoSyncSGI = (PFNGLXGETVIDEOSYNCSGIPROC)
    glXGetProcAddress((const GLubyte*)"glXGetVideoSyncSGI");
  gx11->glXWaitVideoSyncSGI = (PFNGLXWAITVIDEOSYNCSGIPROC)
    glXGetProcAddress((const GLubyte*)"glXWaitVideoSyncSGI");

  screensaver_inhibitor_init(gx11);

  window_open(gx11);
}


/**
 *
 */
static void
gl_keypress(glw_x11_t *gx11, XEvent *event)
{
  XComposeStatus composestatus;
  char str[16], c;
  KeySym keysym;
  int len;
  char buf[32];
  event_t *e = NULL;
  int r;

  len = XLookupString(&event->xkey, str, sizeof(str), &keysym, &composestatus);


  if(len == 1) {
    c = str[0];
    switch(c) {
      /* Static key mappings, these cannot be changed */
    case 8:          e = event_create_simple(EVENT_BACKSPACE); break;
    case 13:         e = event_create_simple(EVENT_ENTER);     break;
    case 27:         e = event_create_simple(EVENT_KEY_CLOSE); break;
      /* Always send 1 char ASCII */
    default:
      e = event_create_unicode(c);
      break;
    }
  } else if((event->xkey.state & 0xf) == 0) {
    switch(keysym) {
    case XK_Left:    e = event_create_simple(EVENT_LEFT);  break;
    case XK_Right:   e = event_create_simple(EVENT_RIGHT); break;
    case XK_Up:      e = event_create_simple(EVENT_UP);    break;
    case XK_Down:    e = event_create_simple(EVENT_DOWN);  break;
    }
  }

  if(e == NULL) {

    /* Construct a string representing the key */
    if(keysym != NoSymbol) {
      snprintf(buf, sizeof(buf),
	       "x11 %s%s%s- %s",
	       event->xkey.state & ShiftMask   ? "- Shift " : "",
	       event->xkey.state & Mod1Mask    ? "- Alt "   : "",
	       event->xkey.state & ControlMask ? "- Ctrl "  : "",
	       XKeysymToString(keysym));
    } else {
      snprintf(buf, sizeof(buf),
	       "x11 - raw - 0x%x", event->xkey.keycode);
    }
    e = keymapper_resolve(NULL, buf);

    if(e == NULL)
      return;
  }

  glw_lock();
  r = glw_signal0(gx11->universe, GLW_SIGNAL_EVENT, e);
  glw_unlock();

  if(r == 0) {
    /* Not consumed, drop it into the main event dispatcher */
    event_post(e);
  } else {
    event_unref(e);
  }
}

/**
 *
 */
static void
update_gpu_info(void)
{
#if 0
  prop_set_string(prop_create(prop_gpu, "vendor"),
		      (const char *)glGetString(GL_VENDOR));

  prop_set_string(prop_create(prop_gpu, "name"),
		      (const char *)glGetString(GL_RENDERER));

  prop_set_string(prop_create(prop_gpu, "driver"),
		      (const char *)glGetString(GL_VERSION));
#endif
}




static int
intcmp(const void *A, const void *B)
{
  const int *a = A;
  const int *b = B;
  return *a - *b;
}


#define FRAME_DURATION_SAMPLES 31 /* should be an odd number */

static int
update_timings(void)
{
  struct timeval tv;
  static int64_t lastts, firstsample;
  static int deltaarray[FRAME_DURATION_SAMPLES];
  static int deltaptr;
  static int lastframedur;
  int d, r = 0;
  
  gettimeofday(&tv, NULL);
  wallclock = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
  walltime = tv.tv_sec;
  
  if(lastts != 0) {
    d = wallclock - lastts;
    if(deltaptr == 0)
      firstsample = wallclock;

    deltaarray[deltaptr++] = d;

    if(deltaptr == FRAME_DURATION_SAMPLES) {
      qsort(deltaarray, deltaptr, sizeof(int), intcmp);
      d = deltaarray[FRAME_DURATION_SAMPLES / 2];
      
      if(lastframedur == 0) {
	lastframedur = d;
      } else {
	lastframedur = (d + lastframedur) / 2;
      }
      frame_duration = lastframedur;
      r = 1;
      deltaptr = 0;

      //      glw_set_framerate(1000000.0 / (float)frame_duration);
    }
  }
  lastts = wallclock;
  return r;
}



/**
 * Master scene rendering
 */
static void 
layout_draw(glw_x11_t *gx11, float aspect)
{
  glw_rctx_t rc;

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  //  fullscreen_fader = GLW_LP(16, fullscreen_fader, fullscreen);
  //  prop_set_float(prop_fullscreen, fullscreen_fader);
  
  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;
  rc.rc_focused = 1;
  rc.rc_fullscreen = 0;
  glw_layout0(gx11->universe, &rc);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  gluLookAt(0, 0, 3.4,
	    0, 0, 1,
	    0, 1, 0);

  rc.rc_alpha = 1.0f;
  glw_render0(gx11->universe, &rc);
}


/**
 *
 */
static int
layout_event_handler(glw_t *w, void *opaque, glw_signal_t sig, void *extra)
{
  event_t *e = extra;

  if(sig != GLW_SIGNAL_EVENT_BUBBLE)
    return 0;

  event_post(e);
  return 1;
}




/**
 *
 */
static void
glw_sysglue_mainloop(glw_x11_t *gx11)
{
  XEvent event;
  int w, h;
  unsigned int retraceCount = 0;

  gx11->glXGetVideoSyncSGI(&retraceCount);

  while(gx11->running) {
    if(gx11->is_fullscreen != gx11->want_fullscreen) {
      glw_lock();
      window_change_displaymode(gx11);
      glw_unlock();
    }

    if(frame_duration != 0) {

      while(XPending(gx11->display)) {
	XNextEvent(gx11->display, &event);
      
	switch(event.type) {
	case KeyPress:
	  gl_keypress(gx11, &event);
	  break;

	case ConfigureNotify:
	  w = event.xconfigure.width;
	  h = event.xconfigure.height;
	  glViewport(0, 0, w, h);
	  gx11->aspect_ratio = (float)w / (float)h;
	  break;


        case ClientMessage:
	  if((Atom)event.xclient.data.l[0] == gx11->deletewindow) {
	    /* Window manager wants us to close */
	    ui_exit_showtime();
	  }
	  break;

	default:
	  break;
	}
      }
    }
    glw_lock();
    glw_reaper0(&gx11->gr);
    layout_draw(gx11, gx11->aspect_ratio);
    glw_unlock();


    glFlush();

    if(gx11->do_videosync)
      gx11->glXWaitVideoSyncSGI(2, (retraceCount+1)%2, &retraceCount);

    glXSwapBuffers(gx11->display, gx11->win);

    update_timings();
  }
  window_shutdown(gx11);
}



/**
 *
 */
static void *
glw_x11_thread(void *aux)
{
  glw_x11_t *gx11 = aux;

  glw_x11_init(gx11);

  if(glw_init(&gx11->gr))
    return NULL;

  gx11->running = 1;

  
  gx11->universe = glw_model_create(&gx11->gr,
				    "theme://universe.model", NULL, 0, NULL);

  glw_set_i(gx11->universe,
	    GLW_ATTRIB_SIGNAL_HANDLER, layout_event_handler, gx11, 1000,
	    NULL);

  glw_sysglue_mainloop(gx11);

  return NULL;
}




/**
 *
 */
uii_t *
glw_start(const char *arg)
{
  glw_x11_t *gx11 = calloc(1, sizeof(glw_x11_t));

  hts_thread_create(&gx11->threadid, glw_x11_thread, gx11);
  return &gx11->gr.gr_uii;
}

