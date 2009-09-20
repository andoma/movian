/**
 *  nVidia specific code
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

#include <stdio.h>

#include <X11/Xlib.h>

#include <NVCtrl/NVCtrl.h>
#include <NVCtrl/NVCtrlLib.h>

#include "nvidia.h"
#include "showtime.h"



static void
add_mode(Display *dpy, int scr, int dev, int refreshrate)
{
  int i;
  int width, height;
  char buf[256];
  char *result;

  if(!XNVCTRLQueryAttribute(dpy, scr, dev, NV_CTRL_FRONTEND_RESOLUTION, &i))
    return;

  width  = i >> 16;
  height = i & 0xffff;

  printf("%x\n", i);

  snprintf(buf, sizeof(buf), "width=%d, height=%d, refreshrate=%d",
	   width, height, refreshrate);

  if(!XNVCTRLStringOperation(dpy, NV_CTRL_TARGET_TYPE_X_SCREEN, scr, 
			     0, NV_CTRL_STRING_OPERATION_GTF_MODELINE, 
			     buf, &result))
    result = NULL;

  printf("Mode: %s\n", result);

  snprintf(buf, sizeof(buf), "\"%dx%d_%d_0\" %s", 
	   width, height, refreshrate, result);
  XFree(result);

  printf("ModeLine: %s\n", buf);
  
  if(!XNVCTRLSetStringAttribute(dpy, scr, dev, NV_CTRL_STRING_ADD_MODELINE,
				buf))
    printf("Unable to add mode\n");




}



void
nvidia_init(Display *dpy, int screen)
{
  int event_base, error_base, major, minor, mask;
  char *str;
  int i;

  if(!XNVCTRLQueryExtension(dpy, &event_base, &error_base) ||
     !XNVCTRLQueryVersion(dpy, &major, &minor) ||
     !XNVCTRLIsNvScreen(dpy, screen))
    return;
  
  if(XNVCTRLQueryStringAttribute(dpy, screen, 0, 
				 NV_CTRL_STRING_PRODUCT_NAME, &str)) {
    TRACE(TRACE_DEBUG, "nVidia", "GPU: %s", str);
    XFree(str);
  }

  if(!XNVCTRLQueryAttribute(dpy, screen, 0,
			    NV_CTRL_ASSOCIATED_DISPLAY_DEVICES,
			    &mask))
    return;

  i = __builtin_popcount(mask);

  if(i != 1) {
    TRACE(TRACE_ERROR, "nVidia", 
	  "%s associated displays with screen. GPU control disabled",
	  i ? "Multiple" : "No");
    return;
  }

  if(XNVCTRLQueryStringAttribute(dpy, screen, mask,
				 NV_CTRL_STRING_DISPLAY_DEVICE_NAME,
				 &str)) {
    TRACE(TRACE_DEBUG, "nVidia", "Display device: %s", str);
    XFree(str);
  }
  
  if(XNVCTRLQueryAttribute(dpy, screen, 0, NV_CTRL_GPU_CORE_TEMPERATURE, &i))
    TRACE(TRACE_DEBUG, "nVidia", "GPU Core Temp: %d°", i);

  if(XNVCTRLQueryAttribute(dpy, screen, mask, NV_CTRL_REFRESH_RATE, &i))
    TRACE(TRACE_DEBUG, "nVidia", 
	  "Current Refreshrate: %.2f Hz", (float)i / 100.0);

  return;

  /* */

  add_mode(dpy, screen, 1 << 16, 72);




}
