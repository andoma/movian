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
#include "hid/input.h"
#include "hid/keymapper.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"
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

/**
 * Load display settings
 */
static void
display_settings_load(void)
{
  char path[PATH_MAX];
  struct config_head cl;
  const char *v;

  snprintf(path, sizeof(path), "%s/display", settingsdir);

  TAILQ_INIT(&cl);

  if(config_read_file0(path, &cl) == -1)
    return;

  if((v = config_get_str_sub(&cl, "displaymode", NULL)) != NULL)
    display_settings.displaymode = str2val(v, displaymodetab);

  config_free0(&cl);
}



/**
 * Save display settings
 */
static void
display_settings_save(void)
{
  char path[PATH_MAX];
  FILE *fp;

  snprintf(path, sizeof(path), "%s/display", settingsdir);

  if((fp = fopen(path, "w+")) == NULL)
    return;

  fprintf(fp, "displaymode = %s\n", val2str(display_settings.displaymode,
					    displaymodetab));
  fclose(fp);
}




/**
 * Add a settings pane with relevant settings
 */
void
display_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic)
{
  struct layout_form_entry_list lfelist;
  glw_t *t;

  TAILQ_INIT(&lfelist);

  t = layout_form_add_tab(m,
			  "settings_list",     "settings/display-icon",
			  "settings_container","settings/display-tab");
  
  if(t == NULL)
    return;

  layout_form_add_option(t, "displaymodes", "Windowed", 
			 DISPLAYMODE_WINDOWED);
  layout_form_add_option(t, "displaymodes", "Fullscreen",
			 DISPLAYMODE_FULLSCREEN);

  LFE_ADD_OPTION(&lfelist, "displaymodes", &display_settings.displaymode);

  layout_form_initialize(&lfelist, m, gfs, ic, 0);
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
  inputevent_t ie;

  len = XLookupString(&event->xkey, str, sizeof(str), &keysym, &composestatus);


  if(len == 1) {
    c = str[0];
    switch(c) {
      /* Static key mappings, these cannot be changed */
    case 8:          input_key_down(INPUT_KEY_BACK);          return;
    case 9:          input_key_down(INPUT_KEY_TASK_DOSWITCH); return;
    case 13:         input_key_down(INPUT_KEY_ENTER);         return;
    case 27:         input_key_down(INPUT_KEY_CLOSE);         return;
      /* Always send 1 char ASCII */
    default:
      input_key_down(c);
      break;
    }
  } else if((event->xkey.state & 0xf) == 0) {
    switch(keysym) {
    case XK_Left:    input_key_down(INPUT_KEY_LEFT);          return;
    case XK_Right:   input_key_down(INPUT_KEY_RIGHT);         return;
    case XK_Up:      input_key_down(INPUT_KEY_UP);            return;
    case XK_Down:    input_key_down(INPUT_KEY_DOWN);          return;
    }
  }
  ie.type = INPUT_KEYDESC;

  /* Construct a string representing the key */
  if(keysym != NoSymbol) {
    snprintf(ie.u.keydesc, sizeof(ie.u.keydesc),
	     "x11 %s%s%s- %s",
	     event->xkey.state & ShiftMask   ? "- Shift " : "",
	     event->xkey.state & Mod1Mask    ? "- Alt "   : "",
	     event->xkey.state & ControlMask ? "- Ctrl "  : "",
	     XKeysymToString(keysym));
  } else {
    snprintf(ie.u.keydesc, sizeof(ie.u.keydesc),
	     "x11 - raw - 0x%x", event->xkey.keycode);
  }

  /* Pass it to the mapper */

  keymapper_resolve(&ie);
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

  glXGetVideoSyncSGI(&retraceCount);

  while(1) {

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
      glXWaitVideoSyncSGI(2, (retraceCount+1)%2, &retraceCount);

    glXSwapBuffers(x11state.display, x11state.win);
    gl_update_timings();
    glw_reaper();
  }
}

