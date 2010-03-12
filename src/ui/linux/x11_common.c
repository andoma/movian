/**
 *  X11 common code
 *  Copyright (C) 2010 Andreas Öman
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

#include "config.h"
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#ifdef CONFIG_LIBXSS
#include <X11/extensions/scrnsaver.h>
#endif

#include "misc/callout.h"
#include "showtime.h"

#include "x11_common.h"

struct x11_screensaver_state {
  Display *dpy;
  callout_t callout;
};


/**
 *
 */
static void
reset_screensaver(callout_t *c, void *aux)
{
  struct x11_screensaver_state *s = aux;

  XResetScreenSaver(s->dpy);
  callout_arm(&s->callout, reset_screensaver, s, 30);
}


/**
 *
 */
struct x11_screensaver_state *
x11_screensaver_suspend(Display *dpy)
{
  struct x11_screensaver_state *s;

  s = calloc(1, sizeof(struct x11_screensaver_state));
  s->dpy = dpy;
  callout_arm(&s->callout, reset_screensaver, s, 1);

#ifdef CONFIG_LIBXSS
  XScreenSaverSuspend(dpy, 1);
  TRACE(TRACE_DEBUG, "X11", "Suspending screensaver");
#endif
  return s;
}


/**
 *
 */
void
x11_screensaver_resume(struct x11_screensaver_state *s)
{
#ifdef CONFIG_LIBXSS
  TRACE(TRACE_DEBUG, "X11", "Resuming screensaver");
  XScreenSaverSuspend(s->dpy, 0);
#endif

  callout_disarm(&s->callout);
  free(s);
}
