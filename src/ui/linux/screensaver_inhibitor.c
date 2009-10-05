/*
 *  Core for inhibiting screensaver on Linux
 *  Copyright (C) 2009 Andreas Öman
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

#include <X11/Xlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "showtime.h"
#include "screensaver_inhibitor.h"
#include "misc/callout.h"

struct inhibitor {
  Display *dpy;
  int timeout;
  callout_t callout;
  int failcnt;
};



static void
inhibit_xscreensaver(callout_t *c, void *aux)
{
  struct inhibitor *inh = aux;
  
  XResetScreenSaver(inh->dpy);
  callout_arm(&inh->callout, inhibit_xscreensaver, inh, inh->timeout);
}


static void
inhibit_gnomescreensaver(callout_t *c, void *aux)
{
  struct inhibitor *inh = aux;

  if(system("gnome-screensaver-command -p") == -1) {
    TRACE(TRACE_ERROR, "screensaver-inhibit", 
	  "gnome-screensaver-command -p failed: %s", strerror(errno));
    if (++inh->failcnt > 10) {
      TRACE(TRACE_ERROR, "screensaver-inhibit", 
	    "gnome-screensaver-command failed 10 times, giving up");
      return;
    }
  }
  callout_arm(&inh->callout, inhibit_gnomescreensaver, inh, inh->timeout);
}


void *
screensaver_inhibitor_init(const char *displayname)
{
  int r;
  int tmo, itvl, prefbl, alexp;
  Display *dpy;
  struct inhibitor *inh;

  if((dpy = XOpenDisplay(displayname)) == NULL) {
    TRACE(TRACE_ERROR, "screensaver-inhibit", "Unable to open display %s",
	  displayname);
    return NULL;
  }

  inh = calloc(1, sizeof(struct inhibitor));
  inh->dpy = dpy;

  /* Try xscreensaver */
  r = XGetScreenSaver(dpy, &tmo, &itvl, &prefbl, &alexp);
  if(r == 1 && tmo > 0) {

    inh->timeout = tmo / 2 + 5;
    
    callout_arm(&inh->callout, inhibit_xscreensaver, inh, inh->timeout);
    TRACE(TRACE_INFO, "screensaver-inhibit", 
	  "Using xscreensaver (interval %d) on %s", inh->timeout,
	  displayname);
    return inh;
  }
  
  /* Try gnome-screensaver */
  if(system("gnome-screensaver-command -p") == 0) {
    
    inh->timeout = 30;
    callout_arm(&inh->callout, inhibit_gnomescreensaver, inh, inh->timeout);
    TRACE(TRACE_INFO, "screensaver-inhibit", 
	  "Using gnome screensaver (interval %d) on %s", inh->timeout,
	  displayname);
    return inh;
  }
  
  free(inh);
  TRACE(TRACE_INFO, "screensaver-inhibit", 
	"No screensaver will be inhibited");
  return NULL;
}
