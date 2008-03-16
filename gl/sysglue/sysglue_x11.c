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

#include <libglw/glw.h>
#include <GL/glx.h>

#include "showtime.h"
#include "hid/input.h"
#include "layout/layout.h"
#include "sysglue.h"

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
  int fullscreen;
} x11state;



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


void
gl_sysglue_init(int argc, char **argv)
{
  char *displayname = getenv("DISPLAY");
  int attribs[10];
  int na = 0;
  XSetWindowAttributes winAttr;
  unsigned long mask;
  int x, y, w, h;
  const char *fullscreen = config_get_str("fullscreen", NULL);

  if(fullscreen)
    x11state.fullscreen = 1;

  if((x11state.display = XOpenDisplay(displayname)) == NULL) {
    fprintf(stderr, "Unable to open X display \"%s\"\n", displayname);
    exit(1);
  }

  if(!glXQueryExtension(x11state.display, NULL, NULL)) {
    fprintf(stderr, "OpenGL GLX extension not supported by display \"%s\"\n",
	    displayname);
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
	    displayname);
    exit(1);
  }

  winAttr.event_mask        = KeyPressMask | StructureNotifyMask;
  winAttr.background_pixmap = None;
  winAttr.background_pixel  = 0;
  winAttr.border_pixel      = 0;

  winAttr.colormap = XCreateColormap(x11state.display, x11state.root,
				     x11state.xvi->visual, AllocNone);
  
  mask = CWBackPixmap | CWBorderPixel | CWColormap | CWEventMask;

  if(x11state.fullscreen) {
    w = x11state.screen_width;
    h = x11state.screen_height;
    x = 0;
    y = 0;
    winAttr.override_redirect = True;
    mask |= CWOverrideRedirect;

  } else {
    w = x11state.screen_width  * 3 / 4;
    h = x11state.screen_height * 3 / 4;
    x = x11state.screen_width  / 4;
    y = x11state.screen_height / 4;
  }

  x11state.aspect_ratio = (float)w / (float)h;

  x11state.win = 
    XCreateWindow(x11state.display,
		  x11state.root,
		  x, y, w, h, 0,
		  x11state.xvi->depth, InputOutput,
		  x11state.xvi->visual, mask, &winAttr
		  );

  x11state.glxctx = glXCreateContext(x11state.display, x11state.xvi, NULL, 1);

  if(x11state.glxctx == NULL) {
    fprintf(stderr, "Unable to create GLX context on \"%s\"\n",
	    displayname);
    exit(1);
  }


  glXMakeCurrent(x11state.display, x11state.win, x11state.glxctx);

  XMapWindow(x11state.display, x11state.win);

  /* Make an empty / blank cursor */

  XDefineCursor(x11state.display, x11state.win, blank_cursor());


  if(x11state.fullscreen) {
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
		 0, 0, 0, 0, x / 2, y / 2);
    XGrabKeyboard(x11state.display,  x11state.win, False,
		  GrabModeAsync, GrabModeAsync, CurrentTime);
  }
  gl_common_init();
}


static void
gl_keypress(XEvent *event)
{
  XComposeStatus composestatus;
  char str[16];
  KeySym keysym;
  int len;
  input_key_t ikey;

  len = XLookupString(&event->xkey, str, sizeof(str), &keysym, &composestatus);
  ikey = 0;

  if(len > 0) {
    ikey  = str[0];

    switch(ikey) {
    case 8:     input_key_down(INPUT_KEY_BACK);       break;
    case 9:     input_key_down(INPUT_KEY_TASK_DOSWITCH); break;
    case 13:    input_key_down(INPUT_KEY_ENTER);      break;
    case 27:    input_key_down(INPUT_KEY_CLOSE);      break;
    case ' ':   input_key_down(INPUT_KEY_SELECT);     break;
    case '+':   input_key_down(INPUT_KEY_INCR);       break;
    case '-':   input_key_down(INPUT_KEY_DECR);       break;
    default:
      break;
    }

  } else {
    switch(keysym) {
    case XK_Left:      ikey = INPUT_KEY_LEFT;             break;
    case XK_Right:     ikey = INPUT_KEY_RIGHT;            break;
    case XK_Up:        ikey = INPUT_KEY_UP;               break;
    case XK_Down:      ikey = INPUT_KEY_DOWN;             break;
    case XK_Page_Up:   ikey = INPUT_KEY_PREV;             break;
    case XK_Page_Down: ikey = INPUT_KEY_NEXT;             break;
    case XK_End:       ikey = INPUT_KEY_STOP;             break;
    case XK_F1:        ikey = INPUT_KEY_MENU;             break;
    case XK_F2:        ikey = INPUT_KEY_PLAYPAUSE;        break;

    case XK_F9:        ikey = INPUT_KEY_SWITCH_VIEW;      break;
    case XK_F10:       ikey = INPUT_KEY_POWER;            break;
    case XK_F11:       ikey = INPUT_KEY_SEEK_BACKWARD;    break;
    case XK_F12:       ikey = INPUT_KEY_SEEK_FORWARD;     break;
      
    case 0:
      switch(event->xkey.keycode) {
      case 176:     ikey = INPUT_KEY_VOLUME_UP;     break;
      case 174:     ikey = INPUT_KEY_VOLUME_DOWN;   break;
      case 160:     ikey = INPUT_KEY_VOLUME_MUTE;   break;
      default:
	break;
      }
    }
  }
  
  if(ikey)
    input_key_down(ikey);
}





void
gl_sysglue_mainloop(void)
{
  XEvent event;
  int w, h;

  while(1) {

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

      layout_draw(x11state.aspect_ratio);
    }
    glFlush();
    glXSwapBuffers(x11state.display, x11state.win);
    gl_update_timings();
    glw_reaper();
  }
}

