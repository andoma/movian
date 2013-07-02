/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2011 Andreas Öman
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
#include "showtime.h"
#include "htsmsg/htsmsg_store.h"
#include "settings.h"
#include "video_settings.h"
#include "misc/str.h"


struct video_settings video_settings;



#if ENABLE_VDPAU 
static void
set_vdpau(void *opaque, int on)
{
  video_settings.vdpau = on;
}

static void
set_vdpau_deinterlace(void *opaque, const char *str)
{
  video_settings.vdpau_deinterlace = atoi(str);
}

static void
set_vdpau_deinterlace_resolution_limit(void *opaque, const char *str)
{
  video_settings.vdpau_deinterlace_resolution_limit = atoi(str);
}
#endif


#if ENABLE_VDA
static void
set_vda(void *opaque, int on)
{
  video_settings.vda = on;
}
#endif


static void
set_stretch_horizontal(void *opaque, int on)
{
  video_settings.stretch_horizontal = on;
}

static void
set_stretch_fullscreen(void *opaque, int on)
{
  video_settings.stretch_fullscreen = on;
}

static void
set_vzoom(void *opaque, int v)
{
  video_settings.vzoom = v;
}

static void
set_video_resumemode(void *opaque, const char *str)
{
  video_settings.resume_mode = atoi(str);
}

static void
set_played_threshold(void *opaque, int v)
{
  video_settings.played_threshold = v;
}

static void
set_continuous_playback(void *opaque, int v)
{
  video_settings.continuous_playback = v;
}


void
video_settings_init(void)
{
  htsmsg_t *store;
  prop_t *s;
  setting_t *x;

  s = settings_add_dir(NULL, _p("Video playback"), "video", NULL,
		       _p("Video acceleration and display behaviour"),
		       NULL);

  if((store = htsmsg_store_load("videoplayback")) == NULL)
    store = htsmsg_create_map();
#if ENABLE_VDPAU 
  settings_create_bool(s, "vdpau", _p("Enable VDPAU"), 1,
		       store, set_vdpau, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");

  x = settings_create_multiopt(s, "vdpau_deinterlace",
			       _p("Preferred VDPAU deinterlacer method"), 0);

  settings_multiopt_add_opt(x, "2", _p("Temporal/Spatial"), 1);
  settings_multiopt_add_opt(x, "1", _p("Temporal"), 0);
  settings_multiopt_add_opt(x, "0", _p("Off"), 0);

  settings_multiopt_initiate(x, set_vdpau_deinterlace, NULL, NULL, 
			     store, settings_generic_save_settings,
                             (void *)"videoplayback");

  x = settings_create_multiopt(s, "vdpau_deinterlace_resolution_limit",
			       _p("Maximum resolution for deinterlacer"), 0);
  settings_multiopt_add_opt(x, "576", _p("576"), 0);
  settings_multiopt_add_opt(x, "720", _p("720"), 0);
  settings_multiopt_add_opt(x, "1080", _p("1080"), 0);
  settings_multiopt_add_opt(x, "0", _p("No limit"), 1);

  settings_multiopt_initiate(x, set_vdpau_deinterlace_resolution_limit, NULL, NULL, 
			     store, settings_generic_save_settings,
                             (void *)"videoplayback");

#endif

#if ENABLE_VDA
  settings_create_bool(s, "vda", _p("Enable VDA"), 1,
		       store, set_vda, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");
#endif

  settings_create_bool(s, "stretch_horizontal",
		       _p("Stretch video to widescreen"), 0,
		       store, set_stretch_horizontal, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");

  settings_create_bool(s, "stretch_fullscreen",
		       _p("Stretch video to fullscreen"), 0,
		       store, set_stretch_fullscreen, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");

  settings_create_int(s, "vzoom",
		      _p("Video zoom"), 100, store, 50, 200,
		      1, set_vzoom, NULL,
		      SETTINGS_INITIAL_UPDATE,
		      "%", NULL,
		      settings_generic_save_settings, 
		      (void *)"videoplayback");
  
  video_settings.resume_mode = 1;
  x = settings_create_multiopt(s, "resumemode",
			       _p("Resume video playback"), 0);

  settings_multiopt_add_opt(x, "1", _p("Yes"), 1);
  settings_multiopt_add_opt(x, "0", _p("No"), 0);

  settings_multiopt_initiate(x, set_video_resumemode, NULL, NULL,
			     store, settings_generic_save_settings, 
			     (void *)"videoplayback");

  settings_create_int(s, "played_threshold",
		      _p("Count video as played when reaching"),
		      90, store, 1, 100,
		      1, set_played_threshold, NULL,
		      SETTINGS_INITIAL_UPDATE,
		      "%", NULL,
		      settings_generic_save_settings, 
		      (void *)"videoplayback");

  settings_create_bool(s, "continuous_playback",
		       _p("Automatically play next video in list"), 0,
		       store, set_continuous_playback, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");


}
