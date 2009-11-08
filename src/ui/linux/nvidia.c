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
#include <string.h>

#include <X11/Xlib.h>

#include <NVCtrl/NVCtrl.h>
#include <NVCtrl/NVCtrlLib.h>

#include "nvidia.h"
#include "showtime.h"



typedef struct nvctrl_data {
  Display *dpy;
  int scr;
  int dev;
  
  char *modelines;
  char *metamodes;

  int meta_720p50;
  int meta_720p60;

} nvctrl_data_t;





static const char *
display_device_name(int mask)
{
  switch (mask) {
  case (1 <<  0): return "CRT-0"; break;
  case (1 <<  1): return "CRT-1"; break;
  case (1 <<  2): return "CRT-2"; break;
  case (1 <<  3): return "CRT-3"; break;
  case (1 <<  4): return "CRT-4"; break;
  case (1 <<  5): return "CRT-5"; break;
  case (1 <<  6): return "CRT-6"; break;
  case (1 <<  7): return "CRT-7"; break;

  case (1 <<  8): return "TV-0"; break;
  case (1 <<  9): return "TV-1"; break;
  case (1 << 10): return "TV-2"; break;
  case (1 << 11): return "TV-3"; break;
  case (1 << 12): return "TV-4"; break;
  case (1 << 13): return "TV-5"; break;
  case (1 << 14): return "TV-6"; break;
  case (1 << 15): return "TV-7"; break;

  case (1 << 16): return "DFP-0"; break;
  case (1 << 17): return "DFP-1"; break;
  case (1 << 18): return "DFP-2"; break;
  case (1 << 19): return "DFP-3"; break;
  case (1 << 20): return "DFP-4"; break;
  case (1 << 21): return "DFP-5"; break;
  case (1 << 22): return "DFP-6"; break;
  case (1 << 23): return "DFP-7"; break;
  default: return "Unknown";
  }
}



static int
have_modeline(const char *allmodes, const char *mode)
{
  int j;
  const char *start;
  start = allmodes;
  for(j = 0; *start; j++) {
    if(allmodes[j] == '\0') {
      if(strstr(start, mode))
	return 1;
      start = &allmodes[j+1];
    }
  }
  return 0;
}


static int
find_metamode(const char *in, const char *mode)
{
  int j;
  const char *s = in, *s2;
  for(j = 0; *s; j++) {
    if(in[j] == '\0') {

      if((s2 = strstr(s, " :: ")) != NULL) {
	if((s2 = strstr(s2 + 4, ": ")) != NULL) {
	  s2 += 2;
	  if(!strncmp(s2, mode, strlen(mode)) &&
	     s2[strlen(mode)] == ' ') {
	    /* Ok, correct line */
	    if((s2 = strstr(s, "id=")) != NULL) {
	      s2 += 3;
	      return atoi(s2);
	    }
	  }
	}
      }
      s = in + j + 1;
    }
  }
  return 0;
}




static int
add_mode(nvctrl_data_t *nvd, int width, int height, int rr)
{
  char name[64];
  char buf[256];
  char *result = NULL;
  int metaid;
  const char *s;
  snprintf(name, sizeof(name), "showtime_%dx%d_%d_0", width, height, rr);

  if(!have_modeline(nvd->modelines, name)) {

    snprintf(buf, sizeof(buf), "width=%d, height=%d, refreshrate=%d",
	     width, height, rr);

    if(!XNVCTRLStringOperation(nvd->dpy, NV_CTRL_TARGET_TYPE_X_SCREEN, 
			       nvd->scr, 
			       0, NV_CTRL_STRING_OPERATION_GTF_MODELINE, 
			       buf, &result))
      result = NULL;
    
    if(result == NULL) {
      TRACE(TRACE_ERROR, "nVidia", "Unable to generate modeline for %s",
	    buf);
      return 0;
    }

    snprintf(buf, sizeof(buf), "\"%s\" %s", name, result);
    XFree(result);
    
    TRACE(TRACE_DEBUG, "nVidia", "Adding modeline: %s", buf);
  
    if(!XNVCTRLSetStringAttribute(nvd->dpy, nvd->scr, nvd->dev,
				  NV_CTRL_STRING_ADD_MODELINE, buf)) {
      TRACE(TRACE_ERROR, "nVidia", "Unable to add modeline: %s", buf);
      return 0;
    }
  }

  snprintf(buf, sizeof(buf), "%s: %s", display_device_name(nvd->dev), name);

  metaid = find_metamode(nvd->metamodes, name);
  if(metaid > 0) {
    TRACE(TRACE_DEBUG, "nVidia", "%s maps to meta id %d", name, metaid);
    return metaid;
  }

  TRACE(TRACE_DEBUG, "nVidia", "Adding metamode: %s", buf);
  
  if(!XNVCTRLStringOperation(nvd->dpy, NV_CTRL_TARGET_TYPE_X_SCREEN, nvd->scr,
			     0, NV_CTRL_STRING_OPERATION_ADD_METAMODE,
			     buf, &result)) {
    TRACE(TRACE_ERROR, "nVidia", "Unable to add metamode: %s", buf);
    return 0;
  }
  
  if((s = strstr(result, "id=")) != NULL)
    metaid = atoi(s + 3);
  else
    metaid = 0;

  XFree(result);

  return metaid;
}




static void
add_modes(nvctrl_data_t *nvd)
{
  int i;
  int width, height;

  if(!XNVCTRLQueryAttribute(nvd->dpy, nvd->scr, nvd->dev,
			    NV_CTRL_FRONTEND_RESOLUTION, &i))
    return;

  width  = i >> 16;
  height = i & 0xffff;

  TRACE(TRACE_DEBUG, "nVidia", "Frontend resolution: %d x %d",
	width, height);

  if(!XNVCTRLQueryAttribute(nvd->dpy, nvd->scr, nvd->dev,
			    NV_CTRL_BACKEND_RESOLUTION, &i))
    return;

  width  = i >> 16;
  height = i & 0xffff;

  TRACE(TRACE_DEBUG, "nVidia", "Backend resolution: %d x %d",
	width, height);

  if(!XNVCTRLQueryAttribute(nvd->dpy, nvd->scr, nvd->dev,
			    NV_CTRL_FLATPANEL_NATIVE_RESOLUTION, &i))
    return;

  width  = i >> 16;
  height = i & 0xffff;

  TRACE(TRACE_DEBUG, "nVidia", "DFP native resolution: %d x %d",
	width, height);

  if(!XNVCTRLQueryAttribute(nvd->dpy, nvd->scr, nvd->dev,
			    NV_CTRL_GPU_SCALING_ACTIVE, &i))
    return;
  TRACE(TRACE_DEBUG, "nVidia", "GPU scaling: %d", i);

  if(!XNVCTRLQueryAttribute(nvd->dpy, nvd->scr, nvd->dev,
			    NV_CTRL_GPU_SCALING_ACTIVE, &i))
    return;
  TRACE(TRACE_DEBUG, "nVidia", "DFP scaling: %d", i);


  // Get list of modelines

  if(!XNVCTRLQueryBinaryData(nvd->dpy, nvd->scr, nvd->dev,
			     NV_CTRL_BINARY_DATA_MODELINES,
			     (void *) &nvd->modelines, &i)) {
    TRACE(TRACE_INFO, "nVidia", "Unable to query modelines");
    return;
  }



  // Get list of metamodes

  if(!XNVCTRLQueryBinaryData(nvd->dpy, nvd->scr, nvd->dev,
			     NV_CTRL_BINARY_DATA_METAMODES,
			     (void *) &nvd->metamodes, &i)) {
    TRACE(TRACE_INFO, "nVidia", "Unable to query metamodes");
    return;
  }

  nvd->meta_720p50 = add_mode(nvd, 1280, 720, 50);
  nvd->meta_720p60 = add_mode(nvd, 1280, 720, 60);
}






void
nvidia_init(Display *dpy, int screen)
{
  nvctrl_data_t *nvd;
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

  nvd = calloc(1, sizeof(nvctrl_data_t));

  nvd->dpy = dpy;
  nvd->scr = screen;
  nvd->dev = mask;

  add_modes(nvd);


  XNVCTRLSetAttribute(dpy, screen, mask,
		      NV_CTRL_GPU_SCALING, 0x20003);
}
