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
#include "misc/string.h"


struct prop *subtitle_settings_dir;

struct subtitle_settings subtitle_settings;
struct video_settings video_settings;

static int
parse_bgr(const char *str)
{
  int bgr;
  if(*str == '#')
    str++;

  if(strlen(str) == 6) {

    bgr  = hexnibble(str[0]) << 4;
    bgr |= hexnibble(str[1]);
    bgr |= hexnibble(str[2]) << 12;
    bgr |= hexnibble(str[3]) << 8;
    bgr |= hexnibble(str[4]) << 20;
    bgr |= hexnibble(str[5]) << 16;
    return bgr;
  }

  return 0;
}



static void
set_subtitle_always_select(void *opaque, int v)
{
  subtitle_settings.always_select = v;
}

static void
set_subtitle_scale(void *opaque, int v)
{
  subtitle_settings.scaling = v;
}

static void
set_subtitle_alignment(void *opaque, const char *str)
{
  subtitle_settings.alignment = atoi(str);
}

static void
set_subtitle_align_on_video(void *opaque, int v)
{
  subtitle_settings.align_on_video = v;
}

static void
set_subtitle_style_override(void *opaque, int v)
{
  subtitle_settings.style_override = v;
}

static void
set_subtitle_color(void *opaque, const char *str)
{
  subtitle_settings.color = parse_bgr(str);
}

static void
set_subtitle_shadow_color(void *opaque, const char *str)
{
  subtitle_settings.shadow_color = parse_bgr(str);
}

static void
set_subtitle_shadow_size(void *opaque, int v)
{
  subtitle_settings.shadow_displacement = v;
}

static void
set_subtitle_outline_color(void *opaque, const char *str)
{
  subtitle_settings.outline_color = parse_bgr(str);
}

static void
set_subtitle_outline_size(void *opaque, int v)
{
  subtitle_settings.outline_size = v;
}




#if ENABLE_VDPAU 
static void
set_vdpau(void *opaque, int on)
{
  video_settings.vdpau = on;
}
#endif


static void
set_stretch_horizontal(void *opaque, int on)
{
  video_settings.stretch_horizontal = on;
}

static void
set_vzoom(void *opaque, int v)
{
  video_settings.vzoom = v;
}

#if ENABLE_PS3_VDEC
static void
set_force_42(void *opaque, int v)
{
  video_settings.force_42 = v;
}
#endif

void
video_settings_init(void)
{
  htsmsg_t *store;
  prop_t *s;
  setting_t *x;

  s = settings_add_dir(NULL, _p("Video playback"), "video", NULL,
		       _p("Video acceleration and display behaviour"));

  if((store = htsmsg_store_load("videoplayback")) == NULL)
    store = htsmsg_create_map();
#if ENABLE_VDPAU 
  settings_create_bool(s, "vdpau", _p("Enable VDPAU"), 1,
		       store, set_vdpau, NULL, 
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

  settings_create_int(s, "vzoom",
		      _p("Video zoom"), 100, NULL, 50, 200,
		      1, set_vzoom, NULL,
		      SETTINGS_INITIAL_UPDATE,
		      "%", NULL,
		      settings_generic_save_settings, 
		      (void *)"videoplayback");


#if ENABLE_PS3_VDEC
  settings_create_bool(s, "force42", _p("Force Level 4.2 for h264 content"), 0,
		       store, set_force_42, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");
#endif


  s = settings_add_dir(NULL, _p("Subtitles"), "subtitle", NULL,
		       _p("Generic settings for video subtitles"));
  subtitle_settings_dir = s;

  if((store = htsmsg_store_load("subtitles")) == NULL)
    store = htsmsg_create_map();

  settings_create_bool(s, "alwaysselect",
		       _p("Always try to select a subtitle"), 1, 
		       store, set_subtitle_always_select, NULL,
		       SETTINGS_INITIAL_UPDATE,  NULL,
		       settings_generic_save_settings, 
		       (void *)"subtitles");
  
  settings_create_int(s, "scale", _p("Subtitle size"),
		      100, store, 30, 500, 5, set_subtitle_scale, NULL,
		      SETTINGS_INITIAL_UPDATE, "%", NULL,
		      settings_generic_save_settings, 
		      (void *)"subtitles");

  settings_create_bool(s, "subonvideoframe",
		       _p("Force subtitles to reside on video frame"), 0, 
		       store, set_subtitle_align_on_video, NULL,
		       SETTINGS_INITIAL_UPDATE,  NULL,
		       settings_generic_save_settings, 
		       (void *)"subtitles");

  x = settings_create_multiopt(s, "align", _p("Subtitle position"),
			       set_subtitle_alignment, NULL);

  settings_multiopt_add_opt(x, "2", _p("Center"), 1);
  settings_multiopt_add_opt(x, "1", _p("Left"), 0);
  settings_multiopt_add_opt(x, "3", _p("Right"), 0);
  settings_multiopt_add_opt(x, "0", _p("Auto"), 0);

  settings_multiopt_initiate(x, store, settings_generic_save_settings, 
			     (void *)"subtitles");

  settings_create_divider(s, _p("Subtitle styling"));

  settings_create_string(s, "color", _p("Color"), "FFFFFF", 
			 store, set_subtitle_color, NULL,
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"subtitles");

  settings_create_string(s, "shadowcolor", _p("Shadow color"),
			 "000000", 
			 store, set_subtitle_shadow_color, NULL,
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"subtitles");

  settings_create_int(s, "shadowcolorsize", _p("Shadow offset"),
		      2, store, 0, 10, 1, set_subtitle_shadow_size, NULL,
		      SETTINGS_INITIAL_UPDATE, "px", NULL,
		      settings_generic_save_settings, 
		      (void *)"subtitles");

  settings_create_string(s, "outlinecolor", _p("Outline color"),
			 "000000", 
			 store, set_subtitle_outline_color, NULL,
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"subtitles");

  settings_create_int(s, "shadowoutlinesize", _p("Outline size"),
		      1, store, 0, 4, 1, set_subtitle_outline_size, NULL,
		      SETTINGS_INITIAL_UPDATE, "px", NULL,
		      settings_generic_save_settings, 
		      (void *)"subtitles");

  settings_create_bool(s, "styleoverride",
		       _p("Ignore embedded styling"), 0, 
		       store, set_subtitle_style_override, NULL,
		       SETTINGS_INITIAL_UPDATE,  NULL,
		       settings_generic_save_settings, 
		       (void *)"subtitles");
}
