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

#include "glw.h"
#include "glw_video.h"

#include <GL/glx.h>
#include <GL/glu.h>
#include <X11/Xatom.h>

#include "showtime.h"
#include "ui/keymapper.h"
#include "ui/linux/screensaver_inhibitor.h"
#include "settings.h"

typedef struct glw_x11 {

  glw_root_t gr;

  hts_thread_t threadid;

  int running;

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
  const char *displayname_real;
  const char *displayname_title;

  char *config_name;

  int coords[2][4];
  Atom deletewindow;

  PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI;

  int window_width;
  int window_height;

  int is_pointer_enabled;
  int want_pointer_enabled;

  int frame_duration;

  prop_t *prop_display;
  prop_t *prop_gpu;

} glw_x11_t;

static const keymap_defmap_t glw_default_keymap[] = {
  { EVENT_PLAYPAUSE, "x11 - F2"},ShowtimeGLView
  { EVENT_NONE, NULL},
};





static void update_gpu_info(glw_x11_t *gx11);




/**
 * Save display settings
 */
static void
display_settings_save(glw_x11_t *gx11)
{
  htsmsg_t *m = htsmsg_create_map();

  htsmsg_add_u32(m, "fullscreen", gx11->want_fullscreen);
  htsmsg_add_u32(m, "pointer",    gx11->want_pointer_enabled);
  
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
}



/**
 * Switch pointer on/off
 */
static void
display_set_pointer(void *opaque, int value)
{
  glw_x11_t *gx11 = opaque;
  gx11->want_pointer_enabled = value;
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
  
  settings_add_bool(r, "fullscreen",
		    "Fullscreen mode", 0, settings,
		    display_set_mode, gx11,
		    SETTINGS_INITIAL_UPDATE);

  settings_add_bool(r, "pointer",
		    "Mouse pointer", 1, settings,
		    display_set_pointer, gx11,
		    SETTINGS_INITIAL_UPDATE);

  htsmsg_destroy(settings);

  gx11->gr.gr_uii.uii_km =
    keymapper_create(r, gx11->config_name, "Keymap", glw_default_keymap);
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

  winAttr.event_mask        = KeyPressMask | StructureNotifyMask |
    ButtonPressMask | ButtonReleaseMask |
    PointerMotionMask | ButtonMotionMask;

  winAttr.background_pixmap = None;
  winAttr.background_pixel  = 0;
  winAttr.border_pixel      = 0;

  winAttr.colormap = gx11->colormap = 
    XCreateColormap(gx11->display, gx11->root,
		    gx11->xvi->visual, AllocNone);
  
  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  gx11->coords[0][0] = gx11->screen_width  / 4;
  gx11->coords[0][1] = gx11->screen_height / 4;
  gx11->coords[0][2] = 640; //gx11->screen_width  * 3 / 4;
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

  gx11->window_width  = gx11->coords[fullscreen][2];
  gx11->window_height = gx11->coords[fullscreen][3];

  gx11->glxctx = glXCreateContext(gx11->display, gx11->xvi, NULL, 1);

  if(gx11->glxctx == NULL) {
    fprintf(stderr, "Unable to create GLX context on \"%s\"\n",
	    gx11->displayname_real);
    exit(1);
  }


  glXMakeCurrent(gx11->display, gx11->win, gx11->glxctx);

  XMapWindow(gx11->display, gx11->win);

  /* Make an empty / blank cursor */

  if(gx11->want_pointer_enabled == 0)
    XDefineCursor(gx11->display, gx11->win, blank_cursor(gx11));

  gx11->is_pointer_enabled = gx11->want_pointer_enabled;

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

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glEnable(GL_TEXTURE_2D);

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
static void
glw_x11_init(glw_x11_t *gx11)
{
  int attribs[10];
  int na = 0;

  gx11->prop_display = prop_create(gx11->gr.gr_uii.uii_prop, "display");
  gx11->prop_gpu     = prop_create(gx11->gr.gr_uii.uii_prop, "gpu");

  display_settings_init(gx11);

  if((gx11->display = XOpenDisplay(gx11->displayname_real)) == NULL) {
    fprintf(stderr, "Unable to open X display \"%s\"\n",
	    gx11->displayname_real);
    exit(1);
  }

  if(!glXQueryExtension(gx11->display, NULL, NULL)) {
    fprintf(stderr, "OpenGL GLX extension not supported by display \"%s\"\n",
	    gx11->displayname_real);
    exit(1);
  }

  if(!GLXExtensionSupported(gx11->display, "GLX_SGI_swap_control")) {
    fprintf(stderr,
	    "OpenGL GLX extension GLX_SGI_swap_control is not supported "
	    "by display \"%s\"\n",
	    gx11->displayname_real);
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
	    gx11->displayname_real);
    exit(1);
  }

  gx11->glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)
    glXGetProcAddress((const GLubyte*)"glXSwapIntervalSGI");

  screensaver_inhibitor_init(gx11->displayname_real);

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


  len = XLookupString(&event->xkey, str, sizeof(str), &keysym, &composestatus);

  buf[0] = 0;

  if(len == 1) {
    c = str[0];
    switch(c) {
      /* Static key mappings, these cannot be changed */
    case 8:          e = event_create_simple(EVENT_BACKSPACE); break;
    case 13:         e = event_create_simple(EVENT_ENTER);     break;
    case 27:         e = event_create_simple(EVENT_CLOSE);     break;
    case 9:          e = event_create_simple(EVENT_FOCUS_NEXT);break;
      /* Always send 1 char ASCII */
    default:
      if(c < 32 || c == 127)
	break;

      buf[0] = c;
      buf[1] = 0;
      e = event_create_unicode(c);
      ui_dispatch_event(e, buf, &gx11->gr.gr_uii);
      return;
    }
  } else if((event->xkey.state & 0xf) == 0) {
    switch(keysym) {
    case XK_Left:    e = event_create_simple(EVENT_LEFT);  break;
    case XK_Right:   e = event_create_simple(EVENT_RIGHT); break;
    case XK_Up:      e = event_create_simple(EVENT_UP);    break;
    case XK_Down:    e = event_create_simple(EVENT_DOWN);  break;
    }
  } else if(keysym == XK_ISO_Left_Tab) {
    e = event_create_simple(EVENT_FOCUS_PREV);
  }

  if(e != NULL) {
    ui_dispatch_event(e, NULL, &gx11->gr.gr_uii);
    return;
  }

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

  ui_dispatch_event(e, buf, &gx11->gr.gr_uii);
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




static int
intcmp(const void *A, const void *B)
{
  const int *a = A;
  const int *b = B;
  return *a - *b;
}


#define FRAME_DURATION_SAMPLES 31 /* should be an odd number */

static int
update_timings(glw_x11_t *gx11)
{
  static int64_t lastts, firstsample;
  static int deltaarray[FRAME_DURATION_SAMPLES];
  static int deltaptr;
  static int lastframedur;
  int64_t wallclock = showtime_get_ts();
  int d, r = 0;
  
  
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
      gx11->frame_duration = lastframedur;
      r = 1;
      deltaptr = 0;
      
      // printf("framerate = %f\n", 1000000.0 / (float)gx11->frame_duration);
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
  rc.rc_size_x = gx11->window_width;
  rc.rc_size_y = gx11->window_height;
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
glw_x11_compute_font_size(glw_x11_t *gx11)
{
  float s = (gx11->window_height - 480.0) / 22.0 + 14.0;
  if(s < 14)
    s = 14;
  glw_font_change_size(&gx11->gr, s);
}




/**
 *
 */
static void
glw_sysglue_mainloop(glw_x11_t *gx11)
{
  XEvent event;
  int w, h;
  glw_pointer_event_t gpe;
  int update_font_size_thres = 1;

  gx11->glXSwapIntervalSGI(1);

  while(gx11->running) {
    if(gx11->is_fullscreen != gx11->want_fullscreen) {
      glw_lock(&gx11->gr);
      window_change_displaymode(gx11);
      glw_unlock(&gx11->gr);
    }

    if(gx11->is_pointer_enabled != gx11->want_pointer_enabled) {

      if(gx11->want_pointer_enabled) {
	XUndefineCursor(gx11->display, gx11->win);
      } else {
	XDefineCursor(gx11->display, gx11->win, blank_cursor(gx11));
      }
      gx11->is_pointer_enabled = gx11->want_pointer_enabled;
      display_settings_save(gx11);
    }

    if(gx11->frame_duration != 0) {

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
	  gx11->window_width  = w;
	  gx11->window_height = h;
	  update_font_size_thres = 10;
	  break;


        case ClientMessage:
	  if((Atom)event.xclient.data.l[0] == gx11->deletewindow) {
	    /* Window manager wants us to close */
	    ui_exit_showtime(0);
	  }
	  break;
	  
	case MotionNotify:
	  if(!gx11->is_pointer_enabled)
	    break;

	  gpe.x =  (2.0 * event.xmotion.x / gx11->window_width ) - 1;
	  gpe.y = -(2.0 * event.xmotion.y / gx11->window_height) + 1;
	  gpe.type = GLW_POINTER_MOTION;

	  glw_lock(&gx11->gr);
	  glw_pointer_event(&gx11->gr, &gpe);
	  glw_unlock(&gx11->gr);
	  break;
	  
	case ButtonRelease:
	  if(event.xbutton.button == 1) {
	    gpe.x =  (2.0 * event.xmotion.x / gx11->window_width ) - 1;
	    gpe.y = -(2.0 * event.xmotion.y / gx11->window_height) + 1;
	    gpe.type = GLW_POINTER_RELEASE;
	    glw_lock(&gx11->gr);
	    glw_pointer_event(&gx11->gr, &gpe);
	    glw_unlock(&gx11->gr);
	  }
	  break;

	case ButtonPress:
	  if(!gx11->is_pointer_enabled)
	    break;

	  gpe.x =  (2.0 * event.xmotion.x / gx11->window_width ) - 1;
	  gpe.y = -(2.0 * event.xmotion.y / gx11->window_height) + 1;

	  glw_lock(&gx11->gr);

	  switch(event.xbutton.button) {
	  case 1:
	    /* Left click */
	    gpe.type = GLW_POINTER_CLICK;
	    break;
	  case 4:
	    /* Scroll up */
	    gpe.type = GLW_POINTER_SCROLL;
	    gpe.delta_y = -0.2;
	    break;
	  case 5:
	    /* Scroll down */
	    gpe.type = GLW_POINTER_SCROLL;
	    gpe.delta_y = 0.2;
              
	    break;

	  default:
	    goto noevent;
	  }
	  glw_pointer_event(&gx11->gr, &gpe);
	noevent:
	  glw_unlock(&gx11->gr);
	  break;

	default:
	  break;
	}
      }
    }
    glw_lock(&gx11->gr);

    if(update_font_size_thres > 0) {
      update_font_size_thres--;

      if(update_font_size_thres == 0)
	glw_x11_compute_font_size(gx11);
    }

    glw_reaper0(&gx11->gr);
    layout_draw(gx11, gx11->aspect_ratio);
    glw_unlock(&gx11->gr);

    glXSwapBuffers(gx11->display, gx11->win);
    update_timings(gx11);
  }
  window_shutdown(gx11);
}



/**
 *
 */
static int
glw_x11_start(ui_t *ui, int argc, char *argv[])
{
  glw_x11_t *gx11 = calloc(1, sizeof(glw_x11_t));
  char confname[256];
  const char *theme_path = SHOWTIME_DEFAULT_THEME_URL;

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
    } else
      break;
  }

  gx11->config_name = strdup(confname);

  glw_x11_init(gx11);

  if(glw_init(&gx11->gr, 40, theme_path, ui))
    return 1;

  gx11->running = 1;
  glw_sysglue_mainloop(gx11);

  return 0;
}


/**
 *
 */
ui_t glw_x11_ui = {
  .ui_title = "glw_x11",
  .ui_start = glw_x11_start,
  .ui_dispatch_event = glw_dispatch_event,
};


