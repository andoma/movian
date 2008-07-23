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

#include <libhts/hts_strtab.h>

#include <libglw/glw.h>
#include <GL/glx.h>

#include "showtime.h"
#include "hid/keymapper.h"
#include "layout/layout.h"
#include "display.h"
#include "video/video_decoder.h"

static struct {
  Display *display;
  int screen;
  int screen_width;
  int screen_height;
  int root;
  XVisualInfo *xvi;
  Window win;
  GLXContext glxctx;
  float aspect_ratio;
  int current_displaymode;
  Colormap colormap;
  const char *displayname;
  int coords[2][4];
  int do_videosync;
  int update_gfx_info;
} x11state;


static struct display_settings {
  enum {
    DISPLAYMODE_WINDOWED = 0,
    DISPLAYMODE_FULLSCREEN,
  } displaymode;

} display_settings;

static struct strtab displaymodetab[] = {
  { "windowed",           DISPLAYMODE_WINDOWED },
  { "fullscreen",         DISPLAYMODE_FULLSCREEN },
};

static glw_t *display_settings_model;

PFNGLXGETVIDEOSYNCSGIPROC _glXGetVideoSyncSGI;
PFNGLXWAITVIDEOSYNCSGIPROC _glXWaitVideoSyncSGI;

/**
 * Load display settings
 */
static void
display_settings_load(void)
{
  htsmsg_t *settings;
  const char *v;

  if((settings = hts_settings_load("display")) == NULL)
    return;

  if((v = htsmsg_get_str(settings, "displaymode")) != NULL)
    display_settings.displaymode = str2val(v, displaymodetab);

  htsmsg_destroy(settings);
}



/**
 * Save display settings
 */
static void
display_settings_save(void)
{
  htsmsg_t *m = htsmsg_create();

  htsmsg_add_str(m, "displaymode", val2str(display_settings.displaymode,
					   displaymodetab));
  
  hts_settings_save(m, "display");
  htsmsg_destroy(m);
}


/**
 * Switch displaymode, we just set a variable and let mainloop switch
 * later on
 */

static void
display_set_mode(void *opaque, void *opaque2, int value)
{
  display_settings.displaymode = value;
}

/**
 * Add a settings pane with relevant settings
 */
void
display_settings_init(appi_t *ai, glw_t *m)
{
  glw_t *icon = 
    glw_model_create("theme://settings/display/display-icon.model", NULL);
  glw_t *tab  = 
    glw_model_create("theme://settings/display/x11/display-x11.model", NULL);

  glw_t *w;

  glw_add_tab(m, "settings_list", icon, "settings_deck", tab);

  display_settings_model = tab;

  if((w = glw_find_by_id(tab, "displaymode", 0)) != NULL) {
    glw_selection_add_text_option(w, "Windowed", display_set_mode, NULL, NULL,
				  DISPLAYMODE_WINDOWED,
				  display_settings.displaymode ==
				  DISPLAYMODE_WINDOWED);

    glw_selection_add_text_option(w, "Fullscreen", display_set_mode, NULL, NULL,
				  DISPLAYMODE_FULLSCREEN,
				  display_settings.displaymode ==
				  DISPLAYMODE_FULLSCREEN);
  }
  x11state.update_gfx_info = 1;
}


/**
 *
 */
static Cursor
blank_cursor(void)
{
  Cursor blank = None;
  char cursorNoneBits[32];
  XColor dontCare;
  Pixmap cursorNonePixmap;

  memset(cursorNoneBits, 0, sizeof( cursorNoneBits ));
  memset(&dontCare, 0, sizeof( dontCare ));
  cursorNonePixmap =
    XCreateBitmapFromData(x11state.display, x11state.root,
			  cursorNoneBits, 16, 16);

  blank = XCreatePixmapCursor(x11state.display,
			      cursorNonePixmap, cursorNonePixmap,
			      &dontCare, &dontCare, 0, 0);

  XFreePixmap(x11state.display, cursorNonePixmap);

  return blank;
}

/**
 *
 */
static void
fullscreen_grab(void)
{
  XSync(x11state.display, False);
    
  while( GrabSuccess !=
	 XGrabPointer(x11state.display, x11state.win,
		      True,
		      ButtonPressMask | ButtonReleaseMask | ButtonMotionMask
		      | PointerMotionMask,
		      GrabModeAsync, GrabModeAsync,
		      x11state.win, None, CurrentTime))
    usleep(100);

  XSetInputFocus(x11state.display, x11state.win, RevertToNone, CurrentTime);
  XWarpPointer(x11state.display, None, x11state.root,
	       0, 0, 0, 0,
	       x11state.coords[0][2] / 2, x11state.coords[0][3] / 2);
  XGrabKeyboard(x11state.display,  x11state.win, False,
		GrabModeAsync, GrabModeAsync, CurrentTime);

}

/**
 *
 */
static void
window_open(void)
{
  XSetWindowAttributes winAttr;
  unsigned long mask;
  int fullscreen = display_settings.displaymode == DISPLAYMODE_FULLSCREEN;
  const GLubyte *glvendor;

  winAttr.event_mask        = KeyPressMask | StructureNotifyMask;
  winAttr.background_pixmap = None;
  winAttr.background_pixel  = 0;
  winAttr.border_pixel      = 0;

  winAttr.colormap = x11state.colormap = 
    XCreateColormap(x11state.display, x11state.root,
		    x11state.xvi->visual, AllocNone);
  
  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  x11state.coords[0][0] = x11state.screen_width  / 4;
  x11state.coords[0][1] = x11state.screen_height / 4;
  x11state.coords[0][2] = x11state.screen_width  * 3 / 4;
  x11state.coords[0][3] = x11state.screen_height * 3 / 4;

  x11state.coords[1][0] = 0;
  x11state.coords[1][1] = 0;
  x11state.coords[1][2] = x11state.screen_width;
  x11state.coords[1][3] = x11state.screen_height;

  if(fullscreen) {

    winAttr.override_redirect = True;
    mask |= CWOverrideRedirect;
  }

  x11state.aspect_ratio =
    (float)x11state.coords[fullscreen][2] / 
    (float)x11state.coords[fullscreen][3];

  x11state.win = 
    XCreateWindow(x11state.display,
		  x11state.root,
		  x11state.coords[fullscreen][0],
		  x11state.coords[fullscreen][1],
		  x11state.coords[fullscreen][2],
		  x11state.coords[fullscreen][3],
		  0,
		  x11state.xvi->depth, InputOutput,
		  x11state.xvi->visual, mask, &winAttr
		  );

  x11state.glxctx = glXCreateContext(x11state.display, x11state.xvi, NULL, 1);

  if(x11state.glxctx == NULL) {
    fprintf(stderr, "Unable to create GLX context on \"%s\"\n",
	    x11state.displayname);
    exit(1);
  }


  glXMakeCurrent(x11state.display, x11state.win, x11state.glxctx);

  XMapWindow(x11state.display, x11state.win);

  /* Make an empty / blank cursor */

  XDefineCursor(x11state.display, x11state.win, blank_cursor());


  if(fullscreen)
    fullscreen_grab();

  glvendor = glGetString(GL_VENDOR); 
  if(strcmp((char *)glvendor, "NVIDIA Corporation")) {
    /* Can't rely on __GL_SYNC_TO_VBLANK, use other methods */
    x11state.do_videosync = 1;
    fprintf(stderr, 
	    "Display: Using 'glXWaitVideoSyncSGI' for vertical sync\n");
  } else {
    fprintf(stderr, 
	    "Display: Using '__GL_SYNC_TO_VBLANK' for vertical sync\n");
  }

  x11state.current_displaymode = display_settings.displaymode;

  glw_init_gl();

}

/**
 * Undo what window_open() does
 */
static void
window_close(void)
{
  XUndefineCursor(x11state.display, x11state.win);
  XDestroyWindow(x11state.display, x11state.win);
  glXDestroyContext(x11state.display, x11state.glxctx);
  XFreeColormap(x11state.display, x11state.colormap);
}


/**
 *
 */
static void
window_change_displaymode(void)
{
  glFlush();
  XSync(x11state.display, False);

  if(x11state.current_displaymode == DISPLAYMODE_FULLSCREEN) {
    XUngrabPointer(x11state.display, CurrentTime);
    XUngrabKeyboard(x11state.display, CurrentTime);
  }
  glw_flush();
  window_close();
  window_open();
  display_settings_save();
  vd_init(); /* Reload fragment shaders */
}


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
 *
 */
void
gl_sysglue_init(int argc, char **argv)
{
  int attribs[10];
  int na = 0;

  x11state.displayname = getenv("DISPLAY");

  display_settings_load();

  if((x11state.display = XOpenDisplay(x11state.displayname)) == NULL) {
    fprintf(stderr, "Unable to open X display \"%s\"\n", x11state.displayname);
    exit(1);
  }

  if(!glXQueryExtension(x11state.display, NULL, NULL)) {
    fprintf(stderr, "OpenGL GLX extension not supported by display \"%s\"\n",
	    x11state.displayname);
    exit(1);
  }

  if(!GLXExtensionSupported(x11state.display, "GLX_SGI_video_sync")) {
    fprintf(stderr,
	    "OpenGL GLX extension GLX_SGI_video_sync is not supported "
	    "by display \"%s\"\n",
	    x11state.displayname);
    exit(1);
  }

  x11state.screen        = DefaultScreen(x11state.display);
  x11state.screen_width  = DisplayWidth(x11state.display, x11state.screen);
  x11state.screen_height = DisplayHeight(x11state.display, x11state.screen);
  x11state.root          = RootWindow(x11state.display, x11state.screen);
 
  attribs[na++] = GLX_RGBA;
  attribs[na++] = GLX_RED_SIZE;
  attribs[na++] = 1;
  attribs[na++] = GLX_GREEN_SIZE;
  attribs[na++] = 1;
  attribs[na++] = GLX_BLUE_SIZE; 
  attribs[na++] = 1;
  attribs[na++] = GLX_DOUBLEBUFFER;
  attribs[na++] = None;
  
  x11state.xvi = glXChooseVisual(x11state.display, x11state.screen, attribs);

  if(x11state.xvi == NULL) {
    fprintf(stderr, "Unable to find an adequate Visual on \"%s\"\n",
	    x11state.displayname);
    exit(1);
  }

  _glXGetVideoSyncSGI = (PFNGLXGETVIDEOSYNCSGIPROC)
    glXGetProcAddress((const GLubyte*)"glXGetVideoSyncSGI");
  _glXWaitVideoSyncSGI = (PFNGLXWAITVIDEOSYNCSGIPROC)
    glXGetProcAddress((const GLubyte*)"glXWaitVideoSyncSGI");

  window_open();
}


/**
 *
 */



/**
 *
 */
static void
gl_keypress(XEvent *event)
{
  XComposeStatus composestatus;
  char str[16], c;
  KeySym keysym;
  int len;
  char buf[32];

  len = XLookupString(&event->xkey, str, sizeof(str), &keysym, &composestatus);


  if(len == 1) {
    c = str[0];
    switch(c) {
      /* Static key mappings, these cannot be changed */
    case 8:          event_post_simple(GEV_BACKSPACE);           return;
    case 9:          event_post_simple(EVENT_KEY_TASK_DOSWITCH); return;
    case 13:         event_post_simple(GEV_ENTER);               return;
    case 27:         event_post_simple(EVENT_KEY_CLOSE);         return;
      /* Always send 1 char ASCII */
    default:
      event_post(glw_event_create_unicode(c));
      break;
    }
  } else if((event->xkey.state & 0xf) == 0) {
    switch(keysym) {
    case XK_Left:    event_post_simple(GEV_LEFT);          return;
    case XK_Right:   event_post_simple(GEV_RIGHT);         return;
    case XK_Up:      event_post_simple(GEV_UP);            return;
    case XK_Down:    event_post_simple(GEV_DOWN);          return;
    }
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

  /* Pass it to the mapper */

  keymapper_resolve(buf);
}

/**
 *
 */
static void
update_gfx_info(void)
{
  const GLubyte *s;

  s = glGetString(GL_VENDOR);
  if(!strcmp((char *)s, "NVIDIA Corporation")) {
    glw_set_source(display_settings_model, "gpuicon", 
		   "theme://images/nvidia.png");
  } else if(!strcmp((char *)s, "ATI Technologies Inc.")) {
    glw_set_source(display_settings_model, "gpuicon", 
		   "theme://images/ati.png");
  }

  glw_set_caption(display_settings_model, "gpuvendor", (char *)s);

  s = glGetString(GL_RENDERER);
  glw_set_caption(display_settings_model, "gpuname", (char *)s);

  s = glGetString(GL_VERSION);
  glw_set_caption(display_settings_model, "gpudriver", (char *)s);
}


/**
 *
 */
void
gl_sysglue_mainloop(void)
{
  XEvent event;
  int w, h;
  unsigned int retraceCount = 0, prev;

  _glXGetVideoSyncSGI(&retraceCount);

  while(1) {

    if(x11state.update_gfx_info) {
      update_gfx_info();
      x11state.update_gfx_info = 0;
    }

    if(x11state.current_displaymode != display_settings.displaymode)
      window_change_displaymode();

    if(frame_duration != 0) {

      while(XPending(x11state.display)) {
	XNextEvent(x11state.display, &event);
      
	switch(event.type) {
	case KeyPress:
	  gl_keypress(&event);
	  break;

	case ConfigureNotify:
	  w = event.xconfigure.width;
	  h = event.xconfigure.height;
	  glViewport(0, 0, w, h);
	  x11state.aspect_ratio = (float)w / (float)h;
	  break;

	default:
	  break;
	}
      }
    }
    glFlush();

    layout_draw(x11state.aspect_ratio);

    prev = retraceCount;

    if(x11state.do_videosync)
      _glXWaitVideoSyncSGI(2, (retraceCount+1)%2, &retraceCount);

    glXSwapBuffers(x11state.display, x11state.win);
    if(gl_update_timings()) {
      char tmp[30];
      snprintf(tmp, sizeof(tmp), "%.2f Hz", (float)1000000. / frame_duration);
      glw_set_caption(display_settings_model, "refreshrate", tmp);
    }
    glw_reaper();
  }
}

