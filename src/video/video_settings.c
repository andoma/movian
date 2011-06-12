#include <stdio.h>
#include "htsmsg/htsmsg_store.h"
#include "settings.h"
#include "video_settings.h"

struct prop *subtitle_settings_dir;

struct subtitle_settings subtitle_settings;
struct video_settings video_settings;

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

void
video_settings_init(void)
{
  htsmsg_t *store;
  prop_t *s;
  setting_t *x;

  s = settings_add_dir(NULL, "Video playback", "video", NULL,
		       "Video acceleration and display behaviour");

  if((store = htsmsg_store_load("videoplayback")) == NULL)
    store = htsmsg_create_map();
#if ENABLE_VDPAU 
  settings_create_bool(s, "vdpau", "Enable VDPAU", 1,
		       store, set_vdpau, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");
#endif

  settings_create_bool(s, "stretch_horizontal",
		       "Stretch video to widescreen", 0,
		       store, set_stretch_horizontal, NULL, 
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"videoplayback");


  s = settings_add_dir(NULL, "Subtitles", "subtitle", NULL,
		       "Generic settings for video subtitles");
  subtitle_settings_dir = s;

  if((store = htsmsg_store_load("subtitles")) == NULL)
    store = htsmsg_create_map();

  settings_create_bool(s, "alwaysselect", "Always select a subtitle", 1, 
		       store, set_subtitle_always_select, NULL,
		       SETTINGS_INITIAL_UPDATE,  NULL,
		       settings_generic_save_settings, 
		       (void *)"subtitles");
  
  settings_create_int(s, "scale", "Subtitle size scaling",
		      100, store, 5, 300, 5, set_subtitle_scale, NULL,
		      SETTINGS_INITIAL_UPDATE, "%", NULL,
		      settings_generic_save_settings, 
		      (void *)"subtitles");

  x = settings_create_multiopt(s, "alignment", "Subtitle alignment",
			       set_subtitle_alignment, NULL);

  settings_multiopt_add_opt(x, "0", "Center", 1);
  settings_multiopt_add_opt(x, "1", "Left", 0);
  settings_multiopt_add_opt(x, "2", "Right", 0);
  settings_multiopt_add_opt(x, "3", "Auto", 0);

  settings_multiopt_initiate(x, store, settings_generic_save_settings, 
			     (void *)"subtitles");
}
